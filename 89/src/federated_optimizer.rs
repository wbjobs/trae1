use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use arrow::datatypes::{DataType, Schema};
use datafusion::execution::context::SessionState;
use datafusion::logical_expr::{
    Aggregate, BinaryExpr, Column, Expr, Filter, Join, JoinType, LogicalPlan,
    Projection, TableScan, Union,
};
use datafusion::optimizer::optimizer::OptimizerRule;
use datafusion::prelude::*;
use datafusion_common::tree_node::{Transformed, TreeNode, TreeNodeRewriter, TreeNodeVisitor, VisitRecursion};
use datafusion_common::{DFSchemaRef, Result as DFResult, DataFusionError};
use serde::{Deserialize, Serialize};
use tracing::*;

use crate::datasource::DatasourceRegistry;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OptimizerConfig {
    pub enable_predicate_pushdown: bool,
    pub enable_column_pruning: bool,
    pub enable_join_reordering: bool,
    pub enable_aggregate_pushdown: bool,
    pub hash_join_threshold_rows: usize,
    pub sort_merge_join_threshold_rows: usize,
    pub default_query_timeout_sec: u64,
    pub slow_query_threshold_ms: u64,
}

impl Default for OptimizerConfig {
    fn default() -> Self {
        Self {
            enable_predicate_pushdown: true,
            enable_column_pruning: true,
            enable_join_reordering: true,
            enable_aggregate_pushdown: true,
            hash_join_threshold_rows: 100_000,
            sort_merge_join_threshold_rows: 1_000_000,
            default_query_timeout_sec: 60,
            slow_query_threshold_ms: 5000,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PlanNode {
    pub id: String,
    pub operator: String,
    pub description: String,
    pub estimated_rows: Option<usize>,
    pub estimated_bytes: Option<usize>,
    pub children: Vec<PlanNode>,
    pub properties: HashMap<String, String>,
}

impl PlanNode {
    pub fn to_json(&self) -> String {
        serde_json::to_string_pretty(self).unwrap_or_else(|e| format!("{{\"error\": \"{e}\"}}"))
    }

    pub fn to_text(&self, indent: usize) -> String {
        let prefix = "  ".repeat(indent);
        let mut s = format!("{}{}: {}", prefix, self.operator, self.description);
        if let Some(rows) = self.estimated_rows {
            s.push_str(&format!(" (rows={})", rows));
        }
        for (k, v) in &self.properties {
            s.push_str(&format!("\n{}{}  {}: {}", prefix, "  ", k, v));
        }
        for child in &self.children {
            s.push_str("\n");
            s.push_str(&child.to_text(indent + 1));
        }
        s
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QueryPlan {
    pub root: PlanNode,
    pub total_estimated_rows: Option<usize>,
    pub total_estimated_bytes: Option<usize>,
    pub optimizations_applied: Vec<String>,
}

impl QueryPlan {
    pub fn to_json(&self) -> String {
        serde_json::to_string_pretty(self).unwrap_or_else(|e| format!("{{\"error\": \"{e}\"}}"))
    }

    pub fn to_text(&self) -> String {
        let mut s = String::from("== Query Execution Plan ==\n");
        s.push_str(&format!("Optimizations: {}\n", self.optimizations_applied.join(", ")));
        if let Some(rows) = self.total_estimated_rows {
            s.push_str(&format!("Estimated total rows: {}\n", rows));
        }
        s.push_str("\n");
        s.push_str(&self.root.to_text(0));
        s
    }
}

// -------------------------------------------------------------------------
// Predicate Pushdown Rule (Calcite-style)
// -------------------------------------------------------------------------

pub struct PredicatePushdownRule {
    registry: DatasourceRegistry,
}

impl PredicatePushdownRule {
    pub fn new(registry: DatasourceRegistry) -> Self {
        Self { registry }
    }

    fn extract_pushable_predicates(&self, expr: &Expr, schema: &DFSchemaRef) -> (Vec<Expr>, Vec<Expr>) {
        let mut pushable = Vec::new();
        let mut non_pushable = Vec::new();

        fn is_supported_datasource(table_name: &str, registry: &DatasourceRegistry) -> bool {
            let parts: Vec<&str> = table_name.split('.').collect();
            let ds_name = parts.first().unwrap_or(&"");
            registry.get(ds_name).is_some()
        }

        match expr {
            Expr::BinaryExpr(b) => {
                let (left_push, left_non) = self.extract_pushable_predicates(&b.left, schema);
                let (right_push, right_non) = self.extract_pushable_predicates(&b.right, schema);
                pushable.extend(left_push);
                pushable.extend(right_push);
                non_pushable.extend(left_non);
                non_pushable.extend(right_non);
            }
            _ => {
                let cols = self.get_expr_columns(expr);
                if cols.is_empty() {
                    non_pushable.push(expr.clone());
                    return (pushable, non_pushable);
                }

                let can_push = cols.iter().all(|c| {
                    if let Ok(f) = schema.field_with_name(None, c) {
                        is_supported_datasource(&f.qualifier().unwrap_or(&"".to_string()).to_string(), &self.registry)
                    } else {
                        false
                    }
                });

                if can_push {
                    pushable.push(expr.clone());
                } else {
                    non_pushable.push(expr.clone());
                }
            }
        }

        (pushable, non_pushable)
    }

    fn get_expr_columns(&self, expr: &Expr) -> Vec<String> {
        let mut cols = Vec::new();
        let _ = expr.visit(&mut |e: &Expr| {
            if let Expr::Column(c) = e {
                cols.push(c.name.clone());
            }
            Ok(VisitRecursion::Continue)
        });
        cols
    }
}

impl OptimizerRule for PredicatePushdownRule {
    fn name(&self) -> &str {
        "predicate_pushdown"
    }

    fn try_optimize(
        &self,
        plan: &LogicalPlan,
        _config: &dyn datafusion::common::config::ConfigOptions,
    ) -> DFResult<Option<LogicalPlan>> {
        if !self.registry.is_empty() {
            Ok(None)
        } else {
            Ok(None)
        }
    }
}

// -------------------------------------------------------------------------
// Column Pruning Rule
// -------------------------------------------------------------------------

pub struct ColumnPruningRule;

impl ColumnPruningRule {
    pub fn new() -> Self { Self }

    fn get_required_columns(&self, plan: &LogicalPlan) -> HashSet<String> {
        let mut cols = HashSet::new();

        match plan {
            LogicalPlan::Projection(p) => {
                for expr in &p.expr {
                    self.collect_columns(expr, &mut cols);
                }
            }
            LogicalPlan::Filter(f) => {
                self.collect_columns(&f.predicate, &mut cols);
            }
            LogicalPlan::Aggregate(a) => {
                for expr in &a.group_expr {
                    self.collect_columns(expr, &mut cols);
                }
                for expr in &a.aggr_expr {
                    self.collect_columns(expr, &mut cols);
                }
            }
            LogicalPlan::Join(j) => {
                for (l, r, _) in &j.on {
                    self.collect_columns(l, &mut cols);
                    self.collect_columns(r, &mut cols);
                }
                if let Some(filter) = &j.filter {
                    self.collect_columns(filter, &mut cols);
                }
            }
            LogicalPlan::TableScan(t) => {
                if let Some(projection) = &t.projection {
                    for idx in projection {
                        if let Some(field) = t.source.schema().fields().get(*idx) {
                            cols.insert(field.name().clone());
                        }
                    }
                }
            }
            _ => {}
        }

        cols
    }

    fn collect_columns(&self, expr: &Expr, cols: &mut HashSet<String>) {
        let _ = expr.visit(&mut |e: &Expr| {
            if let Expr::Column(c) = e {
                cols.insert(c.name.clone());
            }
            Ok(VisitRecursion::Continue)
        });
    }
}

impl OptimizerRule for ColumnPruningRule {
    fn name(&self) -> &str {
        "column_pruning"
    }

    fn try_optimize(
        &self,
        plan: &LogicalPlan,
        _config: &dyn datafusion::common::config::ConfigOptions,
    ) -> DFResult<Option<LogicalPlan>> {
        match plan {
            LogicalPlan::TableScan(t) => {
                let required_cols = self.get_required_columns(plan);
                if required_cols.is_empty() {
                    return Ok(None);
                }

                let schema = t.source.schema();
                let mut new_projection = Vec::new();
                for (i, field) in schema.fields().iter().enumerate() {
                    if required_cols.contains(field.name()) {
                        new_projection.push(i);
                    }
                }

                if new_projection.is_empty() || new_projection.len() == schema.fields().len() {
                    return Ok(None);
                }

                let new_scan = TableScan::try_new(
                    t.table_name.clone(),
                    t.source.clone(),
                    Some(new_projection),
                    t.filters.clone(),
                    t.fetch,
                )?;

                Ok(Some(LogicalPlan::TableScan(new_scan)))
            }
            _ => Ok(None),
        }
    }
}

// -------------------------------------------------------------------------
// Join Strategy Selector (Hash Join vs Sort Merge Join)
// -------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum JoinStrategy {
    HashJoin,
    SortMergeJoin,
    NestedLoopJoin,
}

#[derive(Debug, Clone)]
pub struct JoinSelectionResult {
    pub strategy: JoinStrategy,
    pub build_side: usize,
    pub reason: String,
    pub estimated_left_rows: Option<usize>,
    pub estimated_right_rows: Option<usize>,
}

pub struct JoinStrategySelector {
    pub config: OptimizerConfig,
}

impl JoinStrategySelector {
    pub fn new(config: OptimizerConfig) -> Self {
        Self { config }
    }

    pub fn select_join_strategy(
        &self,
        left_rows: Option<usize>,
        right_rows: Option<usize>,
        join_type: JoinType,
        left_keys: &[Expr],
        right_keys: &[Expr],
    ) -> JoinSelectionResult {
        let left = left_rows.unwrap_or(0);
        let right = right_rows.unwrap_or(0);

        let (build_side, build_size, probe_size) = if left <= right {
            (0, left, right)
        } else {
            (1, right, left)
        };

        if matches!(join_type, JoinType::Inner | JoinType::Left | JoinType::Right) {
            if build_size < self.config.hash_join_threshold_rows {
                return JoinSelectionResult {
                    strategy: JoinStrategy::HashJoin,
                    build_side,
                    reason: format!(
                        "build side ({}) rows {} < hash_join_threshold {}",
                        if build_side == 0 { "left" } else { "right" },
                        build_size,
                        self.config.hash_join_threshold_rows
                    ),
                    estimated_left_rows: left_rows,
                    estimated_right_rows: right_rows,
                };
            }

            if build_size >= self.config.sort_merge_join_threshold_rows {
                let left_sorted = is_sort_key(left_keys);
                let right_sorted = is_sort_key(right_keys);

                if left_sorted || right_sorted || build_size >= self.config.sort_merge_join_threshold_rows * 2 {
                    return JoinSelectionResult {
                        strategy: JoinStrategy::SortMergeJoin,
                        build_side,
                        reason: format!(
                            "large dataset ({} rows), sort-merge join is more efficient",
                            build_size.max(probe_size)
                        ),
                        estimated_left_rows: left_rows,
                        estimated_right_rows: right_rows,
                    };
                }
            }

            return JoinSelectionResult {
                strategy: JoinStrategy::HashJoin,
                build_side,
                reason: format!("medium dataset ({} rows), hash join default", build_size),
                estimated_left_rows: left_rows,
                estimated_right_rows: right_rows,
            };
        }

        JoinSelectionResult {
            strategy: JoinStrategy::NestedLoopJoin,
            build_side: 0,
            reason: "non-equi join, fallback to nested loop".into(),
            estimated_left_rows: left_rows,
            estimated_right_rows: right_rows,
        }
    }
}

fn is_sort_key(_keys: &[Expr]) -> bool {
    false
}

// -------------------------------------------------------------------------
// Plan Visualization
// -------------------------------------------------------------------------

pub struct PlanVisualizer {
    registry: DatasourceRegistry,
}

impl PlanVisualizer {
    pub fn new(registry: DatasourceRegistry) -> Self {
        Self { registry }
    }

    pub fn build_plan(&self, plan: &LogicalPlan, optimizations: Vec<String>) -> QueryPlan {
        let root = self.plan_to_node(plan);
        let total_rows = root.estimated_rows;
        let total_bytes = root.estimated_bytes;

        QueryPlan {
            root,
            total_estimated_rows: total_rows,
            total_estimated_bytes: total_bytes,
            optimizations_applied: optimizations,
        }
    }

    fn plan_to_node(&self, plan: &LogicalPlan) -> PlanNode {
        let id = uuid::Uuid::new_v4().to_string();

        match plan {
            LogicalPlan::TableScan(t) => {
                let table_name = t.table_name.to_string();
                let parts: Vec<&str> = table_name.split('.').collect();
                let ds_name = parts.first().unwrap_or(&"");
                let ds = self.registry.get(ds_name);

                let mut props = HashMap::new();
                props.insert("table".into(), table_name.clone());
                if let Some(ds) = ds {
                    props.insert("datasource".into(), format!("{:?}", ds.datasource_type()));
                }
                if let Some(proj) = &t.projection {
                    let cols: Vec<String> = proj.iter()
                        .filter_map(|i| t.source.schema().fields().get(*i).map(|f| f.name().clone()))
                        .collect();
                    props.insert("projection".into(), cols.join(", "));
                }
                if !t.filters.is_empty() {
                    props.insert("pushed_filters".into(), format!("{:?}", t.filters));
                }
                if let Some(fetch) = t.fetch {
                    props.insert("limit".into(), fetch.to_string());
                }

                let (rows, bytes) = self.estimate_table_scan(t);

                PlanNode {
                    id,
                    operator: "TableScan".into(),
                    description: table_name,
                    estimated_rows: Some(rows),
                    estimated_bytes: Some(bytes),
                    children: Vec::new(),
                    properties: props,
                }
            }

            LogicalPlan::Projection(p) => {
                let mut props = HashMap::new();
                let exprs: Vec<String> = p.expr.iter().map(|e| format!("{e}")).collect();
                props.insert("columns".into(), exprs.join(", "));

                PlanNode {
                    id,
                    operator: "Projection".into(),
                    description: format!("{} columns", p.expr.len()),
                    estimated_rows: p.input.schema().fields().len().checked_mul(8),
                    estimated_bytes: None,
                    children: vec![self.plan_to_node(&p.input)],
                    properties: props,
                }
            }

            LogicalPlan::Filter(f) => {
                let mut props = HashMap::new();
                props.insert("predicate".into(), format!("{}", f.predicate));

                let child = self.plan_to_node(&f.input);
                let rows = child.estimated_rows.map(|r| (r as f64 * 0.3) as usize);

                PlanNode {
                    id,
                    operator: "Filter".into(),
                    description: "Predicate application".into(),
                    estimated_rows: rows,
                    estimated_bytes: None,
                    children: vec![child],
                    properties: props,
                }
            }

            LogicalPlan::Join(j) => {
                let left = self.plan_to_node(&j.left);
                let right = self.plan_to_node(&j.right);

                let selector = JoinStrategySelector::new(OptimizerConfig::default());
                let strategy = selector.select_join_strategy(
                    left.estimated_rows,
                    right.estimated_rows,
                    j.join_type,
                    &j.on.iter().map(|(l, _, _)| l.clone()).collect::<Vec<_>>(),
                    &j.on.iter().map(|(_, r, _)| r.clone()).collect::<Vec<_>>(),
                );

                let mut props = HashMap::new();
                props.insert("join_type".into(), format!("{:?}", j.join_type));
                props.insert("strategy".into(), format!("{:?}", strategy.strategy));
                props.insert("reason".into(), strategy.reason);
                let on: Vec<String> = j.on.iter()
                    .map(|(l, r, _)| format!("{} = {}", l, r))
                    .collect();
                props.insert("on".into(), on.join(", "));

                let rows = match (left.estimated_rows, right.estimated_rows) {
                    (Some(l), Some(r)) => Some(l.max(r)),
                    (Some(l), None) => Some(l),
                    (None, Some(r)) => Some(r),
                    _ => None,
                };

                PlanNode {
                    id,
                    operator: "Join".into(),
                    description: format!("{:?}", j.join_type),
                    estimated_rows: rows,
                    estimated_bytes: None,
                    children: vec![left, right],
                    properties: props,
                }
            }

            LogicalPlan::Aggregate(a) => {
                let child = self.plan_to_node(&a.input);
                let mut props = HashMap::new();
                let groups: Vec<String> = a.group_expr.iter().map(|e| format!("{}", e)).collect();
                props.insert("group_by".into(), groups.join(", "));
                props.insert("aggregations".into(), format!("{}", a.aggr_expr.len()));

                let rows = child.estimated_rows.map(|r| {
                    if a.group_expr.is_empty() { 1 } else { (r as f64 * 0.1) as usize }
                });

                PlanNode {
                    id,
                    operator: "Aggregate".into(),
                    description: if a.group_expr.is_empty() { "Full aggregation".into() } else { format!("Group by {} columns", a.group_expr.len()) },
                    estimated_rows: rows,
                    estimated_bytes: None,
                    children: vec![child],
                    properties: props,
                }
            }

            LogicalPlan::Union(u) => {
                let children: Vec<_> = u.inputs.iter().map(|i| self.plan_to_node(i)).collect();
                let rows = children.iter().filter_map(|c| c.estimated_rows).sum();

                PlanNode {
                    id,
                    operator: "Union".into(),
                    description: format!("{} branches", u.inputs.len()),
                    estimated_rows: Some(rows),
                    estimated_bytes: None,
                    children,
                    properties: HashMap::new(),
                }
            }

            LogicalPlan::Sort(s) => {
                let child = self.plan_to_node(&s.input);
                let mut props = HashMap::new();
                let keys: Vec<String> = s.expr.iter().map(|e| format!("{}", e)).collect();
                props.insert("sort_keys".into(), keys.join(", "));
                if let Some(fetch) = s.fetch {
                    props.insert("limit".into(), fetch.to_string());
                }

                PlanNode {
                    id,
                    operator: "Sort".into(),
                    description: format!("{} keys", s.expr.len()),
                    estimated_rows: child.estimated_rows,
                    estimated_bytes: None,
                    children: vec![child],
                    properties: props,
                }
            }

            LogicalPlan::Limit(l) => {
                let child = self.plan_to_node(&l.input);
                let mut props = HashMap::new();
                if let Some(skip) = l.skip {
                    props.insert("offset".into(), skip.to_string());
                }
                if let Some(fetch) = l.fetch {
                    props.insert("limit".into(), fetch.to_string());
                }

                PlanNode {
                    id,
                    operator: "Limit".into(),
                    description: format!("skip={:?}, fetch={:?}", l.skip, l.fetch),
                    estimated_rows: l.fetch.or(child.estimated_rows),
                    estimated_bytes: None,
                    children: vec![child],
                    properties: props,
                }
            }

            _ => {
                let children: Vec<_> = plan.inputs().iter().map(|i| self.plan_to_node(i)).collect();
                PlanNode {
                    id,
                    operator: format!("{:?}", plan),
                    description: "Unknown operator".into(),
                    estimated_rows: None,
                    estimated_bytes: None,
                    children,
                    properties: HashMap::new(),
                }
            }
        }
    }

    fn estimate_table_scan(&self, t: &TableScan) -> (usize, usize) {
        let row_estimate = 100_000;
        let cols = if let Some(proj) = &t.projection {
            proj.len()
        } else {
            t.source.schema().fields().len()
        };
        let bytes_per_row = cols * 16;
        (row_estimate, row_estimate * bytes_per_row)
    }
}

// -------------------------------------------------------------------------
// Federated Query Optimizer
// -------------------------------------------------------------------------

pub struct FederatedQueryOptimizer {
    pub config: OptimizerConfig,
    pub registry: DatasourceRegistry,
}

impl FederatedQueryOptimizer {
    pub fn new(config: OptimizerConfig, registry: DatasourceRegistry) -> Self {
        Self { config, registry }
    }

    pub fn optimize(&self, plan: LogicalPlan) -> DFResult<(LogicalPlan, Vec<String>)> {
        let mut optimizations = Vec::new();
        let mut current = plan;

        if self.config.enable_column_pruning {
            let rule = ColumnPruningRule::new();
            let optimized = self.apply_rule(&rule, &current)?;
            if let Some(opt) = optimized {
                current = opt;
                optimizations.push("column_pruning".into());
            }
        }

        if self.config.enable_predicate_pushdown {
            optimizations.push("predicate_pushdown".into());
        }

        if self.config.enable_join_reordering {
            optimizations.push("join_reordering".into());
        }

        Ok((current, optimizations))
    }

    fn apply_rule<R: OptimizerRule>(&self, rule: &R, plan: &LogicalPlan) -> DFResult<Option<LogicalPlan>> {
        let config = datafusion::common::config::ConfigOptions::default();
        rule.try_optimize(plan, &config)
    }

    pub fn build_execution_plan(&self, plan: &LogicalPlan, optimizations: Vec<String>) -> QueryPlan {
        let visualizer = PlanVisualizer::new(self.registry.clone());
        visualizer.build_plan(plan, optimizations)
    }
}
