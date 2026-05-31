pub mod proto {
    tonic::include_proto!("columnar.gateway");
}

pub mod datasource;
pub mod federated_engine;
pub mod federated_optimizer;
pub mod join_executor;
pub mod query_log;
pub mod schema_optimizer;
pub mod service;
pub mod storage;

pub use datasource::{Datasource, DatasourceError, DatasourceRegistry, create_datasource};
pub use federated_engine::{FederatedQueryEngine, FederatedQueryError, FederatedQueryResult};
pub use federated_optimizer::{
    ColumnPruningRule, FederatedQueryOptimizer, JoinSelectionResult, JoinStrategy,
    JoinStrategySelector, OptimizerConfig, PlanNode, PredicatePushdownRule, QueryPlan,
};
pub use join_executor::{JoinError, JoinExecutor};
pub use query_log::{QueryHandle, QueryLogEntry, QueryLogger};
pub use schema_optimizer::{OptimizedBatches, OptimizerConfig as SchemaOptimizerConfig, optimize_schema};
pub use service::ColumnarGatewayService;
pub use storage::{ArrowStorage, StorageError, CompressionCodecRef, create_compression_codec};
