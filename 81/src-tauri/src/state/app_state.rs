use std::sync::Arc;

use parking_lot::Mutex;
use serde::{Deserialize, Serialize};

use crate::inference::{CommandResult, HotwordResult};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RecognitionHistory {
    pub label: String,
    pub confidence: f32,
    pub inference_time_ms: u64,
    pub timestamp: u64,
    pub sent_to_smart_home: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppStatus {
    pub is_recording: bool,
    pub is_awake: bool,
    pub smart_home_connected: bool,
    pub current_label: String,
    pub current_confidence: f32,
    pub last_inference_time_ms: u64,
    pub history_count: usize,
}

pub struct AppState {
    pub is_recording: Mutex<bool>,
    pub is_awake: Mutex<bool>,
    pub smart_home_connected: Mutex<bool>,
    pub current_result: Mutex<Option<CommandResult>>,
    pub last_hotword: Mutex<Option<HotwordResult>>,
    pub history: Mutex<Vec<RecognitionHistory>>,
    pub max_history: usize,
    pub model_loaded: Mutex<bool>,
    pub audio_device: Mutex<String>,
}

impl AppState {
    pub fn new(max_history: usize) -> Self {
        Self {
            is_recording: Mutex::new(false),
            is_awake: Mutex::new(false),
            smart_home_connected: Mutex::new(false),
            current_result: Mutex::new(None),
            last_hotword: Mutex::new(None),
            history: Mutex::new(Vec::new()),
            max_history,
            model_loaded: Mutex::new(false),
            audio_device: Mutex::new(String::new()),
        }
    }

    pub fn set_recording(&self, recording: bool) {
        *self.is_recording.lock() = recording;
    }

    pub fn set_awake(&self, awake: bool) {
        *self.is_awake.lock() = awake;
    }

    pub fn set_smart_home_connected(&self, connected: bool) {
        *self.smart_home_connected.lock() = connected;
    }

    pub fn set_current_result(&self, result: CommandResult) {
        *self.current_result.lock() = Some(result);
    }

    pub fn set_last_hotword(&self, result: HotwordResult) {
        *self.last_hotword.lock() = Some(result);
    }

    pub fn set_model_loaded(&self, loaded: bool) {
        *self.model_loaded.lock() = loaded;
    }

    pub fn set_audio_device(&self, device: String) {
        *self.audio_device.lock() = device;
    }

    pub fn add_history(&self, entry: RecognitionHistory) {
        let mut history = self.history.lock();
        history.push(entry);
        if history.len() > self.max_history {
            let overflow = history.len() - self.max_history;
            history.drain(0..overflow);
        }
    }

    pub fn clear_history(&self) {
        self.history.lock().clear();
    }

    pub fn get_status(&self) -> AppStatus {
        let result = self.current_result.lock();
        let (current_label, current_confidence, last_inference_time) = match result.as_ref() {
            Some(r) => (
                r.label.clone(),
                r.confidence,
                r.inference_time_ms,
            ),
            None => ("--".to_string(), 0.0, 0),
        };

        AppStatus {
            is_recording: *self.is_recording.lock(),
            is_awake: *self.is_awake.lock(),
            smart_home_connected: *self.smart_home_connected.lock(),
            current_label,
            current_confidence,
            last_inference_time_ms: last_inference_time,
            history_count: self.history.lock().len(),
        }
    }

    pub fn get_history(&self, limit: usize) -> Vec<RecognitionHistory> {
        let history = self.history.lock();
        let start = if history.len() > limit {
            history.len() - limit
        } else {
            0
        };
        history[start..].to_vec()
    }
}

pub type SharedAppState = Arc<AppState>;
