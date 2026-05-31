use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FunctionProfile {
    pub name: String,
    pub module: String,
    pub cpu_percent: f64,
    pub call_count: u64,
    pub avg_self_time_ns: u64,
    pub total_time_ns: u64,
    pub children: Vec<FunctionProfile>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StackFrame {
    pub function: String,
    pub module: String,
    pub offset: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StackSample {
    pub timestamp_ns: u64,
    pub tid: u32,
    pub frames: Vec<StackFrame>,
    pub weight: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProfileData {
    pub framework: String,
    pub scenario: String,
    pub sample_count: u64,
    pub sampling_freq_hz: u32,
    pub duration_secs: u64,
    pub samples: Vec<StackSample>,
    pub functions: HashMap<String, FunctionProfile>,
    pub top_functions: Vec<FunctionProfile>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HotSpotAnalysis {
    pub framework: String,
    pub scenario: String,
    pub top_10_functions: Vec<FunctionProfile>,
    pub category_breakdown: HashMap<String, f64>,
    pub memory_alloc_functions: Vec<FunctionProfile>,
    pub json_serialization_functions: Vec<FunctionProfile>,
    pub io_functions: Vec<FunctionProfile>,
    pub total_samples: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProfileDiff {
    pub framework_a: String,
    pub framework_b: String,
    pub scenario: String,
    pub function_diffs: Vec<FunctionDiff>,
    pub category_diffs: HashMap<String, f64>,
    pub bottlenecks_a: Vec<FunctionProfile>,
    pub bottlenecks_b: Vec<FunctionProfile>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FunctionDiff {
    pub function: String,
    pub cpu_percent_a: f64,
    pub cpu_percent_b: f64,
    pub diff_percent: f64,
    pub call_count_a: u64,
    pub call_count_b: u64,
    pub significance: f64,
}

pub fn categorize_function(name: &str) -> &'static str {
    let lower = name.to_lowercase();
    
    if lower.contains("json") || lower.contains("serde") || lower.contains("serialize") {
        "JSON Serialization"
    } else if lower.contains("alloc") || lower.contains("malloc") || lower.contains("free") 
           || lower.contains("box") || lower.contains("vec") || lower.contains("string") {
        "Memory Allocation"
    } else if lower.contains("socket") || lower.contains("read") || lower.contains("write")
           || lower.contains("send") || lower.contains("recv") || lower.contains("accept")
           || lower.contains("poll") || lower.contains("io") {
        "I/O"
    } else if lower.contains("hash") || lower.contains("hashmap") || lower.contains("lookup") {
        "Hashing/Lookup"
    } else if lower.contains("futures") || lower.contains("task") || lower.contains("wake")
           || lower.contains("poll") || lower.contains("await") {
        "Async Runtime"
    } else if lower.contains("sqlite") || lower.contains("sql") || lower.contains("query")
           || lower.contains("db") || lower.contains("database") {
        "Database"
    } else if lower.contains("tera") || lower.contains("template") || lower.contains("render") {
        "Template Rendering"
    } else if lower.contains("tls") || lower.contains("ssl") || lower.contains("crypto")
           || lower.contains("encrypt") || lower.contains("hash") {
        "Cryptography"
    } else if lower.contains("axum") || lower.contains("actix") || lower.contains("rocket")
           || lower.contains("warp") || lower.contains("tide") || lower.contains("hyper") {
        "Framework"
    } else if lower.contains("std::") || lower.contains("core::") || lower.contains("alloc::") {
        "Standard Library"
    } else {
        "Other"
    }
}

pub fn is_memory_alloc_function(name: &str) -> bool {
    let lower = name.to_lowercase();
    lower.contains("alloc") || lower.contains("malloc") || lower.contains("calloc")
        || lower.contains("realloc") || lower.contains("free") || lower.contains("box::new")
        || lower.contains("vec::with_capacity") || lower.contains("string::new")
}

pub fn is_json_function(name: &str) -> bool {
    let lower = name.to_lowercase();
    lower.contains("json") || lower.contains("serde_json") || lower.contains("serialize")
        || lower.contains("deserialize") || lower.contains("to_json") || lower.contains("from_json")
}

pub fn is_io_function(name: &str) -> bool {
    let lower = name.to_lowercase();
    lower.contains("read") || lower.contains("write") || lower.contains("send")
        || lower.contains("recv") || lower.contains("accept") || lower.contains("connect")
        || lower.contains("socket") || lower.contains("flush") || lower.contains("poll")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_categorize_json_function() {
        assert_eq!(categorize_function("serde_json::to_string"), "JSON Serialization");
        assert_eq!(categorize_function("serde::Serialize::serialize"), "JSON Serialization");
    }

    #[test]
    fn test_categorize_memory_function() {
        assert_eq!(categorize_function("alloc::alloc::alloc"), "Memory Allocation");
        assert_eq!(categorize_function("alloc::vec::Vec::with_capacity"), "Memory Allocation");
    }

    #[test]
    fn test_categorize_io_function() {
        assert_eq!(categorize_function("std::io::Read::read"), "I/O");
        assert_eq!(categorize_function("tokio::net::TcpStream::poll_read"), "I/O");
    }

    #[test]
    fn test_categorize_framework_function() {
        assert_eq!(categorize_function("axum::handler::call"), "Framework");
        assert_eq!(categorize_function("actix_web::router::route"), "Framework");
    }

    #[test]
    fn test_is_memory_alloc_function() {
        assert!(is_memory_alloc_function("alloc::alloc::alloc"));
        assert!(is_memory_alloc_function("malloc"));
        assert!(!is_memory_alloc_function("serde_json::to_string"));
    }

    #[test]
    fn test_is_json_function() {
        assert!(is_json_function("serde_json::to_string"));
        assert!(is_json_function("serde::Serialize::serialize"));
        assert!(!is_json_function("alloc::alloc::alloc"));
    }

    #[test]
    fn test_is_io_function() {
        assert!(is_io_function("std::io::Read::read"));
        assert!(is_io_function("tokio::net::TcpStream::poll_read"));
        assert!(!is_io_function("serde_json::to_string"));
    }
}
