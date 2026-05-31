pub mod model;
pub mod hotword;

pub use model::{CommandModel, CommandResult, CommandLabel};
pub use hotword::{HotwordDetector, HotwordResult};
