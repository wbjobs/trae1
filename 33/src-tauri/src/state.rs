use std::collections::VecDeque;
use std::path::PathBuf;
use std::sync::Arc;

use parking_lot::Mutex;

use crate::gesture::TrajectoryPoint;
use crate::inference::InferenceEngine;

#[derive(Debug, Clone, Default)]
pub struct ModelStatus {
    pub loaded: bool,
    pub path: Option<PathBuf>,
    pub input_shape: Option<Vec<u64>>,
    pub labels: Vec<String>,
    pub error: Option<String>,
}

#[derive(Clone)]
pub struct AppState {
    pub engine: Arc<Mutex<Option<InferenceEngine>>>,
    pub last_port: Arc<Mutex<Option<u16>>>,
    pub status: Arc<Mutex<ModelStatus>>,
    pub trajectory: Arc<Mutex<VecDeque<TrajectoryPoint>>>,
}

impl AppState {
    pub fn new() -> Self {
        Self {
            engine: Arc::new(Mutex::new(None)),
            last_port: Arc::new(Mutex::new(None)),
            status: Arc::new(Mutex::new(ModelStatus::default())),
            trajectory: Arc::new(Mutex::new(VecDeque::with_capacity(500))),
        }
    }
}

impl Default for AppState {
    fn default() -> Self {
        Self::new()
    }
}

pub const TRAJECTORY_MAX_POINTS: usize = 500;

pub fn push_trajectory_point(state: &AppState, point: TrajectoryPoint) {
    let mut traj = state.trajectory.lock();
    traj.push_back(point);
    if traj.len() > TRAJECTORY_MAX_POINTS {
        traj.pop_front();
    }
}

pub fn get_trajectory(state: &AppState) -> Vec<TrajectoryPoint> {
    state.trajectory.lock().iter().cloned().collect()
}

pub fn clear_trajectory(state: &AppState) {
    state.trajectory.lock().clear();
}
