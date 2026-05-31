use std::collections::HashMap;
use std::sync::Arc;

use arrow::array::{Array, ArrayRef, Int32Builder, Int64Builder, StringArray};
use arrow::datatypes::{DataType, Schema, SchemaRef};
use arrow_array::{RecordBatch, UInt32Array};
use arrow_select::take::take;
use datafusion::logical_expr::JoinType;
use itertools::Itertools;
use tracing::*;

use crate::federated_optimizer::{JoinSelectionResult, JoinStrategy};

#[derive(Debug, thiserror::Error)]
pub enum JoinError {
    #[error("Schema mismatch: {0}")]
    SchemaMismatch(String),
    #[error("Key not found: {0}")]
    KeyNotFound(String),
    #[error("Execution error: {0}")]
    Execution(String),
    #[error("Type mismatch: {0}")]
    TypeMismatch(String),
}

pub type Result<T> = std::result::Result<T, JoinError>;

pub struct JoinExecutor {
    pub strategy: JoinStrategy,
    pub join_type: JoinType,
    pub left_keys: Vec<String>,
    pub right_keys: Vec<String>,
    pub selection: JoinSelectionResult,
}

impl JoinExecutor {
    pub fn new(
        strategy: JoinStrategy,
        join_type: JoinType,
        left_keys: Vec<String>,
        right_keys: Vec<String>,
        selection: JoinSelectionResult,
    ) -> Self {
        Self { strategy, join_type, left_keys, right_keys, selection }
    }

    pub fn execute(
        &self,
        left: &[RecordBatch],
        right: &[RecordBatch],
    ) -> Result<Vec<RecordBatch>> {
        debug!(
            strategy = ?self.strategy,
            join_type = ?self.join_type,
            left_rows = left.iter().map(|b| b.num_rows()).sum::<usize>(),
            right_rows = right.iter().map(|b| b.num_rows()).sum::<usize>(),
            "executing join"
        );

        match self.strategy {
            JoinStrategy::HashJoin => self.hash_join(left, right),
            JoinStrategy::SortMergeJoin => self.sort_merge_join(left, right),
            JoinStrategy::NestedLoopJoin => self.nested_loop_join(left, right),
        }
    }

    fn merge_schemas(&self, left: SchemaRef, right: SchemaRef) -> Result<SchemaRef> {
        let mut fields: Vec<_> = left.fields().iter().cloned().collect();
        for f in right.fields() {
            let name = f.name().clone();
            if left.field_with_name(&name).is_ok() {
                fields.push(arrow::datatypes::Field::new(format!("{}_right", name), f.data_type().clone(), f.is_nullable()));
            } else {
                fields.push(f.as_ref().clone());
            }
        }
        Ok(Arc::new(Schema::new(fields)))
    }

    fn hash_join(&self, left: &[RecordBatch], right: &[RecordBatch]) -> Result<Vec<RecordBatch>> {
        let left_schema = left.first().ok_or_else(|| JoinError::SchemaMismatch("no left batches".into()))?.schema();
        let right_schema = right.first().ok_or_else(|| JoinError::SchemaMismatch("no right batches".into()))?.schema();
        let output_schema = self.merge_schemas(left_schema.clone(), right_schema.clone())?;

        let (build_batches, probe_batches, build_keys, probe_keys, swap) = if self.selection.build_side == 0 {
            (left, right, &self.left_keys, &self.right_keys, false)
        } else {
            (right, left, &self.right_keys, &self.left_keys, true)
        };

        let hash_table = self.build_hash_table(build_batches, build_keys)?;

        let mut left_indices = Vec::new();
        let mut right_indices = Vec::new();

        for (probe_batch_idx, probe_batch) in probe_batches.iter().enumerate() {
            let probe_keys_arrays: Vec<ArrayRef> = probe_keys.iter()
                .map(|k| probe_batch.column_by_name(k)
                    .ok_or_else(|| JoinError::KeyNotFound(k.clone()))
                    .map(|a| a.clone()))
                .collect::<Result<Vec<_>>>()?;

            for row in 0..probe_batch.num_rows() {
                let key = self.build_key(&probe_keys_arrays, row)?;
                if let Some(build_rows) = hash_table.get(&key) {
                    for &(build_batch_idx, build_row) in build_rows {
                        if swap {
                            left_indices.push((build_batch_idx, build_row));
                            right_indices.push((probe_batch_idx, row));
                        } else {
                            left_indices.push((probe_batch_idx, row));
                            right_indices.push((build_batch_idx, build_row));
                        }
                    }
                }
            }
        }

        self.build_join_result(left, right, &left_indices, &right_indices, output_schema)
    }

    fn build_hash_table(
        &self,
        batches: &[RecordBatch],
        keys: &[String],
    ) -> Result<HashMap<Vec<u8>, Vec<(usize, usize)>>> {
        let mut hash_table: HashMap<Vec<u8>, Vec<(usize, usize)>> = HashMap::new();

        for (batch_idx, batch) in batches.iter().enumerate() {
            let key_arrays: Vec<ArrayRef> = keys.iter()
                .map(|k| batch.column_by_name(k)
                    .ok_or_else(|| JoinError::KeyNotFound(k.clone()))
                    .map(|a| a.clone()))
                .collect::<Result<Vec<_>>>()?;

            for row in 0..batch.num_rows() {
                let key = self.build_key(&key_arrays, row)?;
                hash_table.entry(key).or_default().push((batch_idx, row));
            }
        }

        Ok(hash_table)
    }

    fn build_key(&self, arrays: &[ArrayRef], row: usize) -> Result<Vec<u8>> {
        let mut key = Vec::new();
        for arr in arrays {
            if arr.is_null(row) {
                key.extend_from_slice(&[0xFF]);
                continue;
            }
            key.extend_from_slice(&[0x00]);
            match arr.data_type() {
                DataType::Int32 => {
                    let v = arrow::array::as_primitive_array::<arrow::datatypes::Int32Type>(arr).value(row);
                    key.extend_from_slice(&v.to_le_bytes());
                }
                DataType::Int64 => {
                    let v = arrow::array::as_primitive_array::<arrow::datatypes::Int64Type>(arr).value(row);
                    key.extend_from_slice(&v.to_le_bytes());
                }
                DataType::Utf8 => {
                    let v = arrow::array::as_string_array(arr).value(row);
                    key.extend_from_slice(v.as_bytes());
                }
                DataType::Float64 => {
                    let v = arrow::array::as_primitive_array::<arrow::datatypes::Float64Type>(arr).value(row);
                    key.extend_from_slice(&v.to_le_bytes());
                }
                _ => {
                    let s = format!("{:?}", arr);
                    key.extend_from_slice(s.as_bytes());
                }
            }
        }
        Ok(key)
    }

    fn sort_merge_join(&self, left: &[RecordBatch], right: &[RecordBatch]) -> Result<Vec<RecordBatch>> {
        let left_schema = left.first().ok_or_else(|| JoinError::SchemaMismatch("no left batches".into()))?.schema();
        let right_schema = right.first().ok_or_else(|| JoinError::SchemaMismatch("no right batches".into()))?.schema();
        let output_schema = self.merge_schemas(left_schema.clone(), right_schema.clone())?;

        let left_combined = concat_batches(left)?;
        let right_combined = concat_batches(right)?;

        let left_sorted = sort_batch_by_keys(&left_combined, &self.left_keys, true)?;
        let right_sorted = sort_batch_by_keys(&right_combined, &self.right_keys, true)?;

        let mut left_indices = Vec::new();
        let mut right_indices = Vec::new();

        let left_keys_arrays: Vec<ArrayRef> = self.left_keys.iter()
            .map(|k| left_sorted.column_by_name(k).ok_or_else(|| JoinError::KeyNotFound(k.clone())).map(|a| a.clone()))
            .collect::<Result<Vec<_>>>()?;

        let right_keys_arrays: Vec<ArrayRef> = self.right_keys.iter()
            .map(|k| right_sorted.column_by_name(k).ok_or_else(|| JoinError::KeyNotFound(k.clone())).map(|a| a.clone()))
            .collect::<Result<Vec<_>>>()?;

        let mut i = 0;
        let mut j = 0;
        let n = left_sorted.num_rows();
        let m = right_sorted.num_rows();

        while i < n && j < m {
            let cmp = self.compare_keys(&left_keys_arrays, i, &right_keys_arrays, j)?;
            match cmp {
                std::cmp::Ordering::Less => i += 1,
                std::cmp::Ordering::Greater => j += 1,
                std::cmp::Ordering::Equal => {
                    let mut j_start = j;
                    while j_start > 0 && self.compare_keys(&right_keys_arrays, j_start - 1, &left_keys_arrays, i)? == std::cmp::Ordering::Equal {
                        j_start -= 1;
                    }

                    let mut j_end = j;
                    while j_end < m && self.compare_keys(&right_keys_arrays, j_end, &left_keys_arrays, i)? == std::cmp::Ordering::Equal {
                        j_end += 1;
                    }

                    for k in j_start..j_end {
                        left_indices.push((0, i));
                        right_indices.push((0, k));
                    }

                    i += 1;
                    j = j_end;
                }
            }
        }

        self.build_join_result(&[left_sorted], &[right_sorted], &left_indices, &right_indices, output_schema)
    }

    fn compare_keys(
        &self,
        left_arrays: &[ArrayRef],
        left_row: usize,
        right_arrays: &[ArrayRef],
        right_row: usize,
    ) -> Result<std::cmp::Ordering> {
        for ((l_arr, r_arr), (lk, rk)) in left_arrays.iter().zip(right_arrays.iter()).zip(self.left_keys.iter().zip(self.right_keys.iter())) {
            let l_null = l_arr.is_null(left_row);
            let r_null = r_arr.is_null(right_row);
            if l_null && r_null {
                continue;
            }
            if l_null {
                return Ok(std::cmp::Ordering::Less);
            }
            if r_null {
                return Ok(std::cmp::Ordering::Greater);
            }

            match (l_arr.data_type(), r_arr.data_type()) {
                (DataType::Int64, DataType::Int64) => {
                    let l = arrow::array::as_primitive_array::<arrow::datatypes::Int64Type>(l_arr).value(left_row);
                    let r = arrow::array::as_primitive_array::<arrow::datatypes::Int64Type>(r_arr).value(right_row);
                    match l.cmp(&r) {
                        std::cmp::Ordering::Equal => continue,
                        ord => return Ok(ord),
                    }
                }
                (DataType::Utf8, DataType::Utf8) => {
                    let l = arrow::array::as_string_array(l_arr).value(left_row);
                    let r = arrow::array::as_string_array(r_arr).value(right_row);
                    match l.cmp(r) {
                        std::cmp::Ordering::Equal => continue,
                        ord => return Ok(ord),
                    }
                }
                _ => {
                    let l = format!("{:?}", l_arr);
                    let r = format!("{:?}", r_arr);
                    match l.cmp(&r) {
                        std::cmp::Ordering::Equal => continue,
                        ord => return Ok(ord),
                    }
                }
            }
        }
        Ok(std::cmp::Ordering::Equal)
    }

    fn nested_loop_join(&self, left: &[RecordBatch], right: &[RecordBatch]) -> Result<Vec<RecordBatch>> {
        let left_schema = left.first().ok_or_else(|| JoinError::SchemaMismatch("no left batches".into()))?.schema();
        let right_schema = right.first().ok_or_else(|| JoinError::SchemaMismatch("no right batches".into()))?.schema();
        let output_schema = self.merge_schemas(left_schema.clone(), right_schema.clone())?;

        let mut left_indices = Vec::new();
        let mut right_indices = Vec::new();

        for (li, lb) in left.iter().enumerate() {
            for lr in 0..lb.num_rows() {
                for (ri, rb) in right.iter().enumerate() {
                    for rr in 0..rb.num_rows() {
                        left_indices.push((li, lr));
                        right_indices.push((ri, rr));
                    }
                }
            }
        }

        self.build_join_result(left, right, &left_indices, &right_indices, output_schema)
    }

    fn build_join_result(
        &self,
        left: &[RecordBatch],
        right: &[RecordBatch],
        left_indices: &[(usize, usize)],
        right_indices: &[(usize, usize)],
        output_schema: SchemaRef,
    ) -> Result<Vec<RecordBatch>> {
        if left_indices.is_empty() {
            return Ok(vec![RecordBatch::new_empty(output_schema)]);
        }

        let left_schema = left[0].schema();
        let right_schema = right[0].schema();

        let mut left_arrays: Vec<ArrayRef> = Vec::with_capacity(left_schema.fields().len());
        for (field_idx, field) in left_schema.fields().iter().enumerate() {
            let mut values_indices = Vec::with_capacity(left_indices.len());
            for &(batch_idx, row_idx) in left_indices {
                values_indices.push(row_idx as u32);
            }
            let indices = UInt32Array::from(values_indices);
            let col = left[left_indices[0].0].column(field_idx).clone();
            let taken = take(&col, &indices, None)
                .map_err(|e| JoinError::Execution(format!("take column {}: {e}", field.name())))?;
            left_arrays.push(taken);
        }

        let mut right_arrays: Vec<ArrayRef> = Vec::with_capacity(right_schema.fields().len());
        for (field_idx, field) in right_schema.fields().iter().enumerate() {
            let mut values_indices = Vec::with_capacity(right_indices.len());
            for &(batch_idx, row_idx) in right_indices {
                values_indices.push(row_idx as u32);
            }
            let indices = UInt32Array::from(values_indices);
            let col = right[right_indices[0].0].column(field_idx).clone();
            let taken = take(&col, &indices, None)
                .map_err(|e| JoinError::Execution(format!("take column {}: {e}", field.name())))?;
            right_arrays.push(taken);
        }

        let mut all_arrays = left_arrays;
        all_arrays.extend(right_arrays);

        let batch = RecordBatch::try_new(output_schema.clone(), all_arrays)
            .map_err(|e| JoinError::Execution(format!("build result batch: {e}")))?;

        Ok(vec![batch])
    }
}

fn concat_batches(batches: &[RecordBatch]) -> Result<RecordBatch> {
    if batches.is_empty() {
        return Err(JoinError::SchemaMismatch("no batches to concat".into()));
    }
    let schema = batches[0].schema();
    arrow::compute::concat_batches(&schema, batches)
        .map_err(|e| JoinError::Execution(format!("concat batches: {e}")))
}

fn sort_batch_by_keys(batch: &RecordBatch, keys: &[String], ascending: bool) -> Result<RecordBatch> {
    use arrow::compute::{SortColumn, SortOptions, lexsort_to_indices};

    let sort_columns: Vec<SortColumn> = keys.iter()
        .map(|k| {
            let col = batch.column_by_name(k).ok_or_else(|| JoinError::KeyNotFound(k.clone()))?;
            Ok(SortColumn {
                values: col.clone(),
                options: Some(SortOptions {
                    descending: !ascending,
                    nulls_first: true,
                }),
            })
        })
        .collect::<Result<Vec<_>>>()?;

    let indices = lexsort_to_indices(&sort_columns, None)
        .map_err(|e| JoinError::Execution(format!("sort: {e}")))?;

    let mut new_arrays = Vec::with_capacity(batch.num_columns());
    for col in batch.columns() {
        let taken = take(col, &indices, None)
            .map_err(|e| JoinError::Execution(format!("take sorted: {e}")))?;
        new_arrays.push(taken);
    }

    RecordBatch::try_new(batch.schema(), new_arrays)
        .map_err(|e| JoinError::Execution(format!("build sorted batch: {e}")))
}
