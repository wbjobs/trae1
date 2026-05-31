use std::fmt;

use serde::Serialize;

#[derive(Debug)]
pub enum AppError {
    Startup(String),
    Pool(String),
    Sql(rusqlite::Error),
    Query(String),
    RowLimitExceeded(usize),
}

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppError::Startup(s) => write!(f, "startup error: {s}"),
            AppError::Pool(s) => write!(f, "pool error: {s}"),
            AppError::Sql(e) => write!(f, "sql error: {e}"),
            AppError::Query(s) => write!(f, "query error: {s}"),
            AppError::RowLimitExceeded(n) => write!(
                f,
                "result row count {n} exceeds configured maximum"
            ),
        }
    }
}

impl std::error::Error for AppError {}

impl From<rusqlite::Error> for AppError {
    fn from(e: rusqlite::Error) -> Self {
        AppError::Sql(e)
    }
}

#[derive(Serialize)]
pub struct ErrorBody {
    pub error: String,
    pub code: u16,
}
