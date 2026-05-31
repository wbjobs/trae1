use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use std::time::Duration;

use arrow::datatypes::SchemaRef;
use arrow_array::RecordBatch;
use datafusion::execution::context::SessionContext;
use datafusion::logical_expr::{
    Aggregate, Expr, Filter, Join, JoinType, LogicalPlan, Projection,
    TableScan, Union,
};
use datafusion::optimizer::optimizer::Optimizer;
use datafusion::prelude::*;
use datafusion_common::tree_node::{TreeNode, TreeNodeVisitor, VisitRecursion};
use datafusion_common::Result as DFResult;
use futures::future::BoxFuture;
use futures::{FutureExt, StreamExt, TryStreamExt};
use serde::{Deserialize, Serialize};
use tokio::sync::Mutex;
use tokio::time::timeout;
use tracing::*;
use uuid::Uuid;

use crate::datasource::{Datasource, DatasourceError, DatasourceRegistry};
use crate::federated_optimizer::{
    FederatedQueryOptimizer, JoinSelectionResult, JoinStrategy, JoinStrategySelector,
    OptimizerConfig, QueryPlan,
};
use crate::join_executor::{JoinError, JoinExecutor};
use crate::query_log::{QueryHandle, QueryLogger};

#[derive(Debug, thiserror::Error)]
pub enum FederatedQueryError {
    #[error("Datasource error: {0}")]
    Datasource(#[from] DatasourceError),
    #[error("Join error: {0}")]
    Join(#[from] JoinError),
    #[error("Query error: {0}")]
    Query(String),
    #[error("Timeout after {0}s")]
    Timeout(u64),
    #[error("Schema error: {0}")]
    Schema(String),
    #[error("Parse error: {0}")]
    Parse(String),
    #[error("Optimization error: {0}")]
    Optimization(String),
    #[error("Execution error: {0}")]
    Execution(String),
}

pub type Result<T> = std::result::Result<T, FederatedQueryError>;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FederatedQueryResult {
    pub query_id: String,
    pub batches: Vec<RecordBatch>,
    pub execution_plan: Option<QueryPlan>,
    pub elapsed_ms: u64,
    pub rows_returned: u64,
    pub accessed_datasources: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct QueryTableRef {
    pub datasource: Arc<dyn Datasource>,
    pub table_name: String,
    pub alias: String,
}

pub struct FederatedQueryEngine {
    pub registry: DatasourceRegistry,
    pub optimizer: FederatedQueryOptimizer,
    pub logger: Arc<QueryLogger>,
    pub ctx: SessionContext,
}

impl FederatedQueryEngine {
    pub fn new(
        config: OptimizerConfig,
        registry: DatasourceRegistry,
        logger: Arc<QueryLogger>,
    ) -> Self {
        let optimizer = FederatedQueryOptimizer::new(config.clone(), registry.clone());
        Self {
            registry,
            optimizer,
            logger,
            ctx: SessionContext::new(),
        }
    }

    pub fn config(&self) -> &OptimizerConfig {
        &self.optimizer.config
    }

    pub async fn execute(
        &self,
        sql: &str,
        timeout_sec: Option<u64>,
        include_explain: bool,
    ) -> Result<FederatedQueryResult> {
        let query_handle = self.logger.start_query(sql);
        let query_id = query_handle.query_id.clone();

        let effective_timeout = timeout_sec.unwrap_or(self.optimizer.config.default_query_timeout_sec);

        let result = timeout(
            Duration::from_secs(effective_timeout),
            self.execute_inner(sql, include_explain, query_handle),
        ).await;

        match result {
            Ok(Ok(res)) => Ok(res),
            Ok(Err(e)) => {
                let query_handle = self.logger.start_query(sql);
                query_handle.finish_with_error(&e.to_string());
                Err(e)
            }
            Err(_) => {
                let query_handle = self.logger.start_query(sql);
                query_handle.finish_with_error(&format!("timeout after {effective_timeout}s"));
                Err(FederatedQueryError::Timeout(effective_timeout))
            }
        }
    }

    async fn execute_inner(
        &self,
        sql: &str,
        include_explain: bool,
        mut query_handle: QueryHandle,
    ) -> Result<FederatedQueryResult> {
        let start = std::time::Instant::now();
        let query_id = query_handle.query_id.clone();

        let plan = self.parse_and_analyze(sql, &mut query_handle).await?;

        let (optimized_plan, optimizations) = self.optimizer.optimize(plan)
            .map_err(|e| FederatedQueryError::Optimization(format!("optimize: {e}")))?;

        let execution_plan = if include_explain {
            let plan = self.optimizer.build_execution_plan(&optimized_plan, optimizations);
            query_handle.set_execution_plan(plan.to_json());
            Some(plan)
        } else {
            None
        };

        let batches = self.execute_plan(&optimized_plan, &mut query_handle).await?;

        let rows_returned: u64 = batches.iter().map(|b| b.num_rows() as u64).sum();
        query_handle.add_rows(rows_returned);

        let elapsed_ms = start.elapsed().as_millis() as u64;

        query_handle.finish(true);

        Ok(FederatedQueryResult {
            query_id,
            batches,
            execution_plan,
            elapsed_ms,
            rows_returned,
            accessed_datasources: query_handle.accessed_datasources.clone(),
        })
    }

    async fn parse_and_analyze(
        &self,
        sql: &str,
        query_handle: &mut QueryHandle,
    ) -> Result<LogicalPlan> {
        let mut state = self.ctx.state();
        let plan = self.ctx.state().create_logical_plan(sql).await
            .map_err(|e| FederatedQueryError::Parse(format!("parse SQL: {e}")))?;

        let mut table_refs = HashSet::new();
        plan.visit(&mut |p: &LogicalPlan| {
            if let LogicalPlan::TableScan(t) = p {
                let table_name = t.table_name.to_string();
                table_refs.insert(table_name);
            }
            Ok(VisitRecursion::Continue)
        }).map_err(|e| FederatedQueryError::Parse(format!("analyze plan: {e}")))?;

        for table_ref in &table_refs {
            let parts: Vec<&str> = table_ref.split('.').collect();
            if parts.len() >= 2 {
                let ds_name = parts[0];
                if let Some(ds) = self.registry.get(ds_name) {
                    query_handle.add_datasource(ds_name);
                }
            }
        }

        Ok(plan)
    }

    async fn execute_plan(
        &self,
        plan: &LogicalPlan,
        query_handle: &mut QueryHandle,
    ) -> Result<Vec<RecordBatch>> {
        match plan {
            LogicalPlan::TableScan(t) => {
                let table_name = t.table_name.to_string();
                let parts: Vec<&str> = table_name.split('.').collect();
                if parts.len() < 2 {
                    return Err(FederatedQueryError::Parse(format!(
                        "table name must be in format datasource.table, got: {table_name}"
                    )));
                }

                let ds_name = parts[0];
                let table = parts[1..].join(".");
                let ds = self.registry.get(ds_name)
                    .ok_or_else(|| FederatedQueryError::Datasource(DatasourceError::InvalidConfig(format!(
                        "unknown datasource: {ds_name}"
                    ))))?;

                query_handle.add_datasource(ds_name);

                let columns: Vec<String> = if let Some(proj) = &t.projection {
                    proj.iter()
                        .filter_map(|i| t.source.schema().fields().get(*i).map(|f| f.name().clone()))
                        .collect()
                } else {
                    Vec::new()
                };

                let mut predicates = Vec::new();
                for f in &t.filters {
                    predicates.push(f.clone());
                }

                let batches = ds.execute_query(&table, &columns, &predicates, t.fetch).await?;
                Ok(batches)
            }

            LogicalPlan::Projection(p) => {
                let input = self.execute_plan(&p.input, query_handle).await?;
                let schema = p.schema().as_ref().clone();
                let schema = Arc::new(schema);

                let mut out_batches = Vec::new();
                for batch in input {
                    let mut columns = Vec::new();
                    for expr in &p.expr {
                        let col = self.evaluate_expr(expr, &batch)?;
                        columns.push(col);
                    }
                    let b = RecordBatch::try_new(schema.clone(), columns)
                        .map_err(|e| FederatedQueryError::Execution(format!("build projection batch: {e}")))?;
                    out_batches.push(b);
                }
                Ok(out_batches)
            }

            LogicalPlan::Filter(f) => {
                let input = self.execute_plan(&f.input, query_handle).await?;
                self.apply_filter(&input, &f.predicate)
            }

            LogicalPlan::Join(j) => {
                let left_fut = self.execute_plan(&j.left, query_handle);
                let right_fut = self.execute_plan(&j.right, query_handle);
                let (left, right) = futures::join!(left_fut, right_fut);
                let left = left?;
                let right = right?;

                let left_rows: usize = left.iter().map(|b| b.num_rows()).sum();
                let right_rows: usize = right.iter().map(|b| b.num_rows()).sum();

                let left_keys: Vec<Expr> = j.on.iter().map(|(l, _, _)| l.clone()).collect();
                let right_keys: Vec<Expr> = j.on.iter().map(|(_, r, _)| r.clone()).collect();

                let selector = JoinStrategySelector::new(self.optimizer.config.clone());
                let selection = selector.select_join_strategy(
                    Some(left_rows),
                    Some(right_rows),
                    j.join_type,
                    &left_keys,
                    &right_keys,
                );

                let left_key_names: Vec<String> = left_keys.iter()
                    .filter_map(|e| {
                        if let Expr::Column(c) = e { Some(c.name.clone()) } else { None }
                    })
                    .collect();

                let right_key_names: Vec<String> = right_keys.iter()
                    .filter_map(|e| {
                        if let Expr::Column(c) = e { Some(c.name.clone()) } else { None }
                    })
                    .collect();

                debug!(
                    ?selection.strategy,
                    left_rows,
                    right_rows,
                    "join strategy selected"
                );

                let executor = JoinExecutor::new(
                    selection.strategy,
                    j.join_type,
                    left_key_names,
                    right_key_names,
                    selection,
                );

                let result = executor.execute(&left, &right)?;
                Ok(result)
            }

            LogicalPlan::Aggregate(a) => {
                let input = self.execute_plan(&a.input, query_handle).await?;
                self.execute_aggregate(&input, a)
            }

            LogicalPlan::Union(u) => {
                let mut all_batches = Vec::new();
                for input in &u.inputs {
                    let batches = self.execute_plan(input, query_handle).await?;
                    all_batches.extend(batches);
                }
                Ok(all_batches)
            }

            LogicalPlan::Sort(s) => {
                let input = self.execute_plan(&s.input, query_handle).await?;
                let combined = arrow::compute::concat_batches(&input[0].schema(), &input)
                    .map_err(|e| FederatedQueryError::Execution(format!("concat for sort: {e}")))?;

                use arrow::compute::{SortColumn, SortOptions, lexsort_to_indices};
                use arrow_select::take::take;

                let sort_columns: Vec<SortColumn> = s.expr.iter()
                    .filter_map(|e| {
                        if let Expr::Sort(sc) = e {
                            let name = if let Expr::Column(c) = sc.expr.as_ref() {
                                c.name.clone()
                            } else {
                                return None;
                            };
                            let col = combined.column_by_name(&name)?;
                            Some(SortColumn {
                                values: col.clone(),
                                options: Some(SortOptions {
                                    descending: !sc.asc,
                                    nulls_first: sc.nulls_first,
                                }),
                            })
                        } else {
                            None
                        }
                    })
                    .collect();

                if sort_columns.is_empty() {
                    return Ok(input);
                }

                let indices = lexsort_to_indices(&sort_columns, s.fetch)
                    .map_err(|e| FederatedQueryError::Execution(format!("sort: {e}")))?;

                let mut arrays = Vec::new();
                for col in combined.columns() {
                    let taken = take(col, &indices, None)
                        .map_err(|e| FederatedQueryError::Execution(format!("take sorted: {e}")))?;
                    arrays.push(taken);
                }

                let sorted = RecordBatch::try_new(combined.schema(), arrays)
                    .map_err(|e| FederatedQueryError::Execution(format!("build sorted: {e}")))?;

                Ok(vec![sorted])
            }

            LogicalPlan::Limit(l) => {
                let input = self.execute_plan(&l.input, query_handle).await?;
                let combined = arrow::compute::concat_batches(&input[0].schema(), &input)
                    .map_err(|e| FederatedQueryError::Execution(format!("concat for limit: {e}")))?;

                let total_rows = combined.num_rows();
                let skip = l.skip.unwrap_or(0);
                let fetch = l.fetch.unwrap_or(total_rows);
                let end = (skip + fetch).min(total_rows);

                if skip >= total_rows {
                    return Ok(vec![RecordBatch::new_empty(combined.schema())]);
                }

                let mut arrays = Vec::new();
                for col in combined.columns() {
                    let sliced = col.slice(skip, end - skip);
                    arrays.push(sliced);
                }

                let limited = RecordBatch::try_new(combined.schema(), arrays)
                    .map_err(|e| FederatedQueryError::Execution(format!("build limited: {e}")))?;

                Ok(vec![limited])
            }

            LogicalPlan::EmptyRelation(_) => {
                Ok(Vec::new())
            }

            _ => {
                let df = self.ctx.execute_logical_plan(plan.clone()).await
                    .map_err(|e| FederatedQueryError::Execution(format!("execute: {e}")))?;
                let batches = df.collect().await
                    .map_err(|e| FederatedQueryError::Execution(format!("collect: {e}")))?;
                Ok(batches)
            }
        }
    }

    fn evaluate_expr(&self, expr: &Expr, batch: &RecordBatch) -> Result<arrow_array::ArrayRef> {
        match expr {
            Expr::Column(c) => {
                let col = batch.column_by_name(&c.name)
                    .ok_or_else(|| FederatedQueryError::Schema(format!("column not found: {}", c.name)))?;
                Ok(col.clone())
            }
            Expr::Alias(a) => self.evaluate_expr(&a.expr, batch),
            Expr::Literal(l) => {
                Ok(l.to_array_of_size(batch.num_rows()))
            }
            Expr::BinaryExpr(b) => {
                use datafusion::logical_expr::Operator;
                use arrow::compute::*;

                let left = self.evaluate_expr(&b.left, batch)?;
                let right = self.evaluate_expr(&b.right, batch)?;

                let result = match b.op {
                    Operator::Plus => add(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("add: {e}")))?,
                    Operator::Minus => subtract(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("sub: {e}")))?,
                    Operator::Multiply => multiply(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("mul: {e}")))?,
                    Operator::Divide => divide(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("div: {e}")))?,
                    _ => {
                        let values: Vec<Option<String>> = (0..batch.num_rows()).map(|_| None).collect();
                        Arc::new(arrow_array::StringArray::from(values)) as arrow_array::ArrayRef
                    }
                };
                Ok(result)
            }
            _ => {
                Err(FederatedQueryError::Execution(format!("unsupported expression: {expr:?}")))
            }
        }
    }

    fn apply_filter(&self, batches: &[RecordBatch], predicate: &Expr) -> Result<Vec<RecordBatch>> {
        use datafusion::logical_expr::Operator;
        use arrow::compute::*;

        let mut out = Vec::new();
        for batch in batches {
            let mask = self.evaluate_filter(predicate, batch)?;
            let filtered = filter_record_batch(batch, &mask)
                .map_err(|e| FederatedQueryError::Execution(format!("filter: {e}")))?;
            out.push(filtered);
        }
        Ok(out)
    }

    fn evaluate_filter(&self, expr: &Expr, batch: &RecordBatch) -> Result<arrow_array::BooleanArray> {
        use datafusion::logical_expr::Operator;
        use arrow::compute::*;
        use arrow::datatypes::DataType;

        match expr {
            Expr::BinaryExpr(b) => {
                let left = self.evaluate_expr(&b.left, batch)?;
                let right = self.evaluate_expr(&b.right, batch)?;

                let result = match b.op {
                    Operator::Eq => eq(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("eq: {e}")))?,
                    Operator::NotEq => neq(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("neq: {e}")))?,
                    Operator::Lt => lt(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("lt: {e}")))?,
                    Operator::LtEq => lt_eq(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("lte: {e}")))?,
                    Operator::Gt => gt(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("gt: {e}")))?,
                    Operator::GtEq => gt_eq(&left, &right).map_err(|e| FederatedQueryError::Execution(format!("gte: {e}")))?,
                    Operator::And => {
                        let l = self.evaluate_filter(&b.left, batch)?;
                        let r = self.evaluate_filter(&b.right, batch)?;
                        and(&l, &r).map_err(|e| FederatedQueryError::Execution(format!("and: {e}")))?
                    }
                    Operator::Or => {
                        let l = self.evaluate_filter(&b.left, batch)?;
                        let r = self.evaluate_filter(&b.right, batch)?;
                        or(&l, &r).map_err(|e| FederatedQueryError::Execution(format!("or: {e}")))?
                    }
                    _ => {
                        return Err(FederatedQueryError::Execution(format!("unsupported filter op: {:?}", b.op)));
                    }
                };
                Ok(result)
            }
            Expr::IsNull(c) => {
                let arr = self.evaluate_expr(c, batch)?;
                Ok(is_null(&arr).map_err(|e| FederatedQueryError::Execution(format!("is_null: {e}")))?)
            }
            Expr::IsNotNull(c) => {
                let arr = self.evaluate_expr(c, batch)?;
                Ok(is_not_null(&arr).map_err(|e| FederatedQueryError::Execution(format!("is_not_null: {e}")))?)
            }
            Expr::InList(i) => {
                let arr = self.evaluate_expr(&i.expr, batch)?;
                let mut result = arrow_array::BooleanArray::from(vec![false; batch.num_rows()]);
                for value in &i.list {
                    let val_arr = self.evaluate_expr(value, batch)?;
                    let eq_mask = eq(&arr, &val_arr).map_err(|e| FederatedQueryError::Execution(format!("in_list eq: {e}")))?;
                    result = or(&result, &eq_mask).map_err(|e| FederatedQueryError::Execution(format!("in_list or: {e}")))?;
                }
                if i.negated {
                    result = not(&result).map_err(|e| FederatedQueryError::Execution(format!("in_list not: {e}")))?;
                }
                Ok(result)
            }
            Expr::Literal(l) => {
                if let Some(b) = l.value().downcast_ref::<bool>() {
                    Ok(arrow_array::BooleanArray::from(vec![*b; batch.num_rows()]))
                } else {
                    Err(FederatedQueryError::Execution(format!("non-boolean literal in filter")))
                }
            }
            _ => {
                Err(FederatedQueryError::Execution(format!("unsupported filter: {expr:?}")))
            }
        }
    }

    fn execute_aggregate(
        &self,
        batches: &[RecordBatch],
        agg: &Aggregate,
    ) -> Result<Vec<RecordBatch>> {
        use datafusion::logical_expr::AggregateFunc;
        use arrow::compute::*;
        use arrow::datatypes::{DataType, Field, Schema};

        if batches.is_empty() {
            return Ok(Vec::new());
        }

        let schema = batches[0].schema();

        if agg.group_expr.is_empty() {
            let mut arrays = Vec::new();
            let mut fields = Vec::new();

            for expr in &agg.aggr_expr {
                if let Expr::AggregateFunction(a) = expr {
                    let func = &a.func;
                    let col_expr = &a.args[0];
                    let col_name = if let Expr::Column(c) = col_expr {
                        c.name.clone()
                    } else {
                        return Err(FederatedQueryError::Execution("aggregate arg must be column".into()));
                    };

                    let col = batches[0].column_by_name(&col_name)
                        .ok_or_else(|| FederatedQueryError::Schema(format!("column not found: {col_name}")))?;

                    let combined = concat(&[col.as_ref()])
                        .map_err(|e| FederatedQueryError::Execution(format!("concat: {e}")))?;

                    let (result_arr, field_name) = match func {
                        AggregateFunc::Count => {
                            let n = combined.len() - combined.null_count();
                            let arr = arrow_array::Int64Array::from(vec![n as i64]);
                            (Arc::new(arr) as arrow_array::ArrayRef, format!("COUNT({col_name})"))
                        }
                        AggregateFunc::Sum => {
                            match combined.data_type() {
                                DataType::Int64 => {
                                    let arr = arrow::array::as_primitive_array::<arrow::datatypes::Int64Type>(&combined);
                                    let sum: i64 = arr.iter().filter_map(|v| v).sum();
                                    (Arc::new(arrow_array::Int64Array::from(vec![sum])) as _, format!("SUM({col_name})"))
                                }
                                DataType::Float64 => {
                                    let arr = arrow::array::as_primitive_array::<arrow::datatypes::Float64Type>(&combined);
                                    let sum: f64 = arr.iter().filter_map(|v| v).sum();
                                    (Arc::new(arrow_array::Float64Array::from(vec![sum])) as _, format!("SUM({col_name})"))
                                }
                                _ => return Err(FederatedQueryError::Execution(format!("unsupported sum type: {:?}", combined.data_type()))),
                            }
                        }
                        AggregateFunc::Avg => {
                            match combined.data_type() {
                                DataType::Int64 => {
                                    let arr = arrow::array::as_primitive_array::<arrow::datatypes::Int64Type>(&combined);
                                    let values: Vec<i64> = arr.iter().filter_map(|v| v).collect();
                                    let avg = if values.is_empty() { 0.0 } else { values.iter().sum::<i64>() as f64 / values.len() as f64 };
                                    (Arc::new(arrow_array::Float64Array::from(vec![avg])) as _, format!("AVG({col_name})"))
                                }
                                DataType::Float64 => {
                                    let arr = arrow::array::as_primitive_array::<arrow::datatypes::Float64Type>(&combined);
                                    let values: Vec<f64> = arr.iter().filter_map(|v| v).collect();
                                    let avg = if values.is_empty() { 0.0 } else { values.iter().sum::<f64>() / values.len() as f64 };
                                    (Arc::new(arrow_array::Float64Array::from(vec![avg])) as _, format!("AVG({col_name})"))
                                }
                                _ => return Err(FederatedQueryError::Execution(format!("unsupported avg type: {:?}", combined.data_type()))),
                            }
                        }
                        AggregateFunc::Min => {
                            let min_val = min(&combined).map_err(|e| FederatedQueryError::Execution(format!("min: {e}")))?;
                            (min_val, format!("MIN({col_name})"))
                        }
                        AggregateFunc::Max => {
                            let max_val = max(&combined).map_err(|e| FederatedQueryError::Execution(format!("max: {e}")))?;
                            (max_val, format!("MAX({col_name})"))
                        }
                        _ => return Err(FederatedQueryError::Execution(format!("unsupported aggregate: {:?}", func))),
                    };

                    fields.push(Field::new(field_name, result_arr.data_type().clone(), true));
                    arrays.push(result_arr);
                }
            }

            let schema = Arc::new(Schema::new(fields));
            let batch = RecordBatch::try_new(schema, arrays)
                .map_err(|e| FederatedQueryError::Execution(format!("build agg batch: {e}")))?;
            return Ok(vec![batch]);
        }

        Err(FederatedQueryError::Execution("group by aggregate not yet supported".into()))
    }

    pub fn explain(&self, sql: &str, verbose: bool) -> Result<QueryPlan> {
        let plan = futures::executor::block_on(async {
            self.ctx.state().create_logical_plan(sql).await
        }).map_err(|e| FederatedQueryError::Parse(format!("parse: {e}")))?;

        let (optimized, optimizations) = self.optimizer.optimize(plan)
            .map_err(|e| FederatedQueryError::Optimization(format!("optimize: {e}")))?;

        Ok(self.optimizer.build_execution_plan(&optimized, optimizations))
    }
}

fn filter_record_batch(
    batch: &RecordBatch,
    mask: &arrow_array::BooleanArray,
) -> arrow::error::Result<RecordBatch> {
    use arrow_select::filter::filter_record_batch;
    filter_record_batch(batch, mask)
}
