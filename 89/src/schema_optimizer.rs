//! Schema optimizer: auto-detect dictionary encoding candidates and apply compression.
//!
//! For datasets with string columns (e.g. 10M rows × 100-byte avg strings),
//! default UTF8 arrays can use >20GB of memory due to per-value overhead
//! (offsets, validity, heap allocations). By automatically converting
//! low-cardinality string columns (unique < 10% of total rows) to
//! DictionaryArray, we can reduce memory by 70%+ while preserving zero-copy
//! semantics through the Arrow ecosystem.

use std::collections::HashSet;
use std::sync::Arc;

use arrow::array::{
    Array, ArrayRef,
    cast::AsArray,
};
use arrow::compute::cast;
use arrow::datatypes::{
    DataType, Field, Schema, SchemaRef,
};
use arrow::record_batch::RecordBatch;
use serde::{Deserialize, Serialize};
use tracing::{info, warn};

use crate::proto::columnar::gateway::{
    ColumnOptimization, SchemaOptimization,
};

const DEFAULT_CARDINALITY_THRESHOLD: f64 = 0.10; // 10%
const MIN_ROWS_FOR_OPTIMIZATION: usize = 10_000;
const MIN_STRING_LENGTH_FOR_STATS: usize = 8;

/// Per-column statistics gathered during optimization scan.
#[derive(Debug, Clone)]
pub struct ColumnStats {
    pub name: String,
    pub data_type: DataType,
    pub row_count: usize,
    pub null_count: usize,
    /// For string/binary columns only
    pub unique_count: Option<usize>,
    pub avg_str_len: Option<f64>,
    pub total_data_bytes: Option<usize>,
}

/// Result of optimization: new batches + optional suggestion report.
#[derive(Debug)]
pub struct OptimizedBatches {
    pub schema: SchemaRef,
    pub batches: Vec<RecordBatch>,
    pub suggestions: Option<SchemaOptimization>,
}

/// Optimizer configuration.
#[derive(Debug, Clone)]
pub struct OptimizerConfig {
    /// If true, actually apply dictionary encoding to the data.
    pub apply_dictionary: bool,
    /// If true, generate optimization suggestions in the response.
    pub generate_suggestions: bool,
    /// Unique values / total rows below this threshold → dictionary candidate.
    pub cardinality_threshold: f64,
    /// Compression codec to use for Arrow IPC (LZ4 if available).
    pub compression_codec: Option<arrow_ipc::CompressionCodec>,
}

impl Default for OptimizerConfig {
    fn default() -> Self {
        Self {
            apply_dictionary: false,
            generate_suggestions: false,
            cardinality_threshold: DEFAULT_CARDINALITY_THRESHOLD,
            compression_codec: Some(arrow_ipc::CompressionCodec::LZ4_FRAME),
        }
    }
}

/// Estimate total memory usage in bytes for an array (best effort).
pub fn estimate_array_mem_bytes(arr: &dyn Array) -> usize {
    let mut total = 0;
    let data = arr.to_data();

    // Buffers
    for buf in data.buffers() {
        total += buf.len();
    }
    // Children (nested types, dictionaries)
    for child in data.child_data() {
        total += estimate_array_mem_bytes(&child.into());
    }
    // Null bitmap (if any)
    if let Some(bm) = data.nulls() {
        total += bm.buffer().len();
    }
    total
}

/// Gather statistics for a single column across all batches.
pub fn gather_stats(name: &str, data_type: &DataType, batches: &[RecordBatch], col_idx: usize) -> ColumnStats {
    let row_count: usize = batches.iter().map(|b| b.num_rows()).sum();
    let null_count: usize = batches.iter().map(|b| b.column(col_idx).null_count()).sum();

    let mut unique_count = None;
    let mut avg_str_len = None;
    let mut total_data_bytes = None;

    match data_type {
        DataType::Utf8 | DataType::LargeUtf8 => {
            let mut uniq = HashSet::<&str>::new();
            let mut total_chars = 0usize;
            let mut non_null = 0usize;
            let mut bytes = 0usize;

            for b in batches {
                let arr = b.column(col_idx);
                bytes += estimate_array_mem_bytes(arr.as_ref());
                let s = arr.as_string::<i32>();
                for opt_v in s.iter() {
                    if let Some(v) = opt_v {
                        uniq.insert(v);
                        total_chars += v.len();
                        non_null += 1;
                    }
                }
            }
            unique_count = Some(uniq.len());
            if non_null > 0 {
                avg_str_len = Some(total_chars as f64 / non_null as f64);
            }
            total_data_bytes = Some(bytes);
        }
        DataType::Binary | DataType::LargeBinary => {
            let mut bytes = 0usize;
            for b in batches {
                bytes += estimate_array_mem_bytes(b.column(col_idx).as_ref());
            }
            total_data_bytes = Some(bytes);
        }
        _ => {
            let mut bytes = 0usize;
            for b in batches {
                bytes += estimate_array_mem_bytes(b.column(col_idx).as_ref());
            }
            total_data_bytes = Some(bytes);
        }
    }

    ColumnStats {
        name: name.to_string(),
        data_type: data_type.clone(),
        row_count,
        null_count,
        unique_count,
        avg_str_len,
        total_data_bytes,
    }
}

/// Determine if a column is a good candidate for dictionary encoding.
pub fn is_dictionary_candidate(stats: &ColumnStats, config: &OptimizerConfig) -> bool {
    if stats.row_count < MIN_ROWS_FOR_OPTIMIZATION {
        return false;
    }
    match stats.data_type {
        DataType::Utf8 | DataType::LargeUtf8 | DataType::Binary | DataType::LargeBinary => {}
        _ => return false,
    }
    let uniq = match stats.unique_count {
        Some(u) => u,
        None => return false,
    };
    let ratio = uniq as f64 / stats.row_count as f64;
    ratio <= config.cardinality_threshold
}

/// Apply dictionary encoding to a column across all batches, returning new arrays.
///
/// Uses Int32 as the index type (supports up to 2B unique values).
pub fn dictionary_encode_column(
    batches: &[RecordBatch],
    col_idx: usize,
) -> Result<Vec<ArrayRef>, String> {
    let dict_type = DataType::Dictionary(
        Box::new(DataType::Int32),
        Box::new(DataType::Utf8),
    );

    let mut out = Vec::with_capacity(batches.len());
    for b in batches {
        let arr = b.column(col_idx);
        let dict_arr = cast(arr.as_ref(), &dict_type)
            .map_err(|e| format!("cast to dictionary: {e}"))?;
        out.push(dict_arr);
    }

    Ok(out)
}

/// Run optimizer on a set of record batches:
/// 1. Gather per-column statistics.
/// 2. Detect dictionary encoding candidates (low-cardinality strings).
/// 3. Apply dictionary encoding if enabled.
/// 4. Generate optimization suggestions if enabled.
pub fn optimize(
    batches: Vec<RecordBatch>,
    config: &OptimizerConfig,
) -> Result<OptimizedBatches, String> {
    if batches.is_empty() {
        return Ok(OptimizedBatches {
            schema: Arc::new(Schema::empty()),
            batches,
            suggestions: if config.generate_suggestions {
                Some(SchemaOptimization::default())
            } else {
                None
            },
        });
    }

    let orig_schema = batches[0].schema();
    let n_cols = orig_schema.fields().len();
    let row_count: usize = batches.iter().map(|b| b.num_rows()).sum();

    // Gather stats for all columns
    let stats: Vec<ColumnStats> = (0..n_cols)
        .map(|i| {
            let field = orig_schema.field(i);
            gather_stats(field.name(), field.data_type(), &batches, i)
        })
        .collect();

    // Decide which columns to encode
    let candidates: Vec<bool> = stats
        .iter()
        .map(|s| is_dictionary_candidate(s, config))
        .collect();

    let mut new_fields: Vec<Field> = Vec::with_capacity(n_cols);
    let mut per_col_new_arrays: Vec<Option<Vec<ArrayRef>>> = Vec::with_capacity(n_cols);

    for (i, field) in orig_schema.fields().iter().enumerate() {
        if candidates[i] && config.apply_dictionary {
            info!(
                "Column '{}' (Utf8, {} rows, {} unique, ratio={:.3}) will be dictionary-encoded",
                field.name(),
                stats[i].row_count,
                stats[i].unique_count.unwrap_or(0),
                stats[i].unique_count.unwrap_or(0) as f64 / stats[i].row_count.max(1) as f64
            );
            let new_arrs = dictionary_encode_column(&batches, i)?;
            let dict_type = DataType::Dictionary(
                Box::new(DataType::Int32),
                Box::new(DataType::Utf8),
            );
            new_fields.push(Field::new(field.name(), dict_type, field.is_nullable()));
            per_col_new_arrays.push(Some(new_arrs));
        } else {
            new_fields.push(field.as_ref().clone());
            per_col_new_arrays.push(None);
        }
    }

    let new_schema: SchemaRef = Arc::new(Schema::new(new_fields));

    // Build optimized batches
    let mut optimized_batches = Vec::with_capacity(batches.len());
    for (batch_idx, _b) in batches.iter().enumerate() {
        let mut columns: Vec<ArrayRef> = Vec::with_capacity(n_cols);
        for col_idx in 0..n_cols {
            if let Some(new_arrs) = &per_col_new_arrays[col_idx] {
                columns.push(new_arrs[batch_idx].clone());
            } else {
                columns.push(batches[batch_idx].column(col_idx).clone());
            }
        }
        let batch = RecordBatch::try_new(new_schema.clone(), columns)
            .map_err(|e| format!("record batch: {e}"))?;
        optimized_batches.push(batch);
    }

    // Generate suggestions if requested
    let suggestions = if config.generate_suggestions {
        let mut total_orig = 0i64;
        let mut total_opt = 0i64;
        let mut col_opts = Vec::with_capacity(n_cols);

        for (i, field) in orig_schema.fields().iter().enumerate() {
            let st = &stats[i];
            let orig_bytes = st.total_data_bytes.unwrap_or(0) as i64;
            total_orig += orig_bytes;

            let (encoding, optimized_bytes, opt_type_str) = if candidates[i] {
                // Estimate dict size: keys (4 bytes each) + values (unique strings)
                let uniq = st.unique_count.unwrap_or(0);
                let avg = st.avg_str_len.unwrap_or(0.0);
                let est = (row_count * 4 + uniq * avg as usize) as i64;
                let opt_bytes = if config.apply_dictionary {
                    // Use actual measurement after encoding
                    let mut sum = 0i64;
                    if let Some(new_arrs) = &per_col_new_arrays[i] {
                        for a in new_arrs {
                            sum += estimate_array_mem_bytes(a.as_ref()) as i64;
                        }
                    }
                    if sum > 0 { sum } else { est }
                } else {
                    est
                };
                ("dictionary".to_string(), opt_bytes, "Dictionary<Int32, Utf8>".to_string())
            } else {
                ("none".to_string(), orig_bytes, format!("{:?}", field.data_type()))
            };

            let saving_ratio = if orig_bytes > 0 {
                (orig_bytes - optimized_bytes) as f64 / orig_bytes as f64
            } else {
                0.0
            };
            total_opt += optimized_bytes;

            col_opts.push(ColumnOptimization {
                column_name: field.name().clone(),
                original_type: format!("{:?}", field.data_type()),
                optimized_type: opt_type_str,
                encoding,
                memory_saving_ratio: saving_ratio.max(0.0),
                original_bytes: orig_bytes,
                optimized_bytes,
            });
        }

        let total_saving = if total_orig > 0 {
            (total_orig - total_opt) as f64 / total_orig as f64
        } else {
            0.0
        };

        Some(SchemaOptimization {
            columns: col_opts,
            compression: config
                .compression_codec
                .as_ref()
                .map(|c| match c {
                    arrow_ipc::CompressionCodec::LZ4_FRAME => "lz4".to_string(),
                    arrow_ipc::CompressionCodec::ZSTD => "zstd".to_string(),
                    #[allow(unreachable_patterns)]
                    _ => "none".to_string(),
                })
                .unwrap_or_else(|| "none".to_string()),
            total_original_bytes: total_orig,
            total_optimized_bytes: total_opt,
            total_saving_ratio: total_saving.max(0.0),
        })
    } else {
        None
    };

    if let Some(opt) = &suggestions {
        info!(
            "Schema optimization: total {} → {} bytes (saved {:.1}%)",
            opt.total_original_bytes,
            opt.total_optimized_bytes,
            opt.total_saving_ratio * 100.0
        );
    }

    Ok(OptimizedBatches {
        schema: new_schema,
        batches: optimized_batches,
        suggestions,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use arrow::array::StringArray;
    use arrow::datatypes::{DataType, Field, Schema};

    #[test]
    fn test_dict_encoding_basic() {
        let schema = Arc::new(Schema::new(vec![Field::new(
            "name",
            DataType::Utf8,
            false,
        )]));
        let values: Vec<&str> = (0..100_000)
            .map(|i| match i % 5 {
                0 => "alice",
                1 => "bob",
                2 => "charlie",
                3 => "david",
                _ => "eve",
            })
            .collect();
        let arr = Arc::new(StringArray::from(values)) as ArrayRef;
        let batch = RecordBatch::try_new(schema.clone(), vec![arr]).unwrap();

        let mut cfg = OptimizerConfig::default();
        cfg.apply_dictionary = true;
        cfg.generate_suggestions = true;

        let opt = optimize(vec![batch], &cfg).unwrap();
        let col = opt.batches[0].column(0);
        assert!(matches!(col.data_type(), DataType::Dictionary(_, _)));
        assert_eq!(col.len(), 100_000);

        let sugg = opt.suggestions.unwrap();
        assert_eq!(sugg.columns.len(), 1);
        assert!(sugg.columns[0].memory_saving_ratio > 0.7); // >70% saving
    }
}
