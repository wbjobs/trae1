use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use std::collections::VecDeque;
use std::sync::Arc;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Feedback {
    pub fingerprint_hash: String,
    pub predicted_class: u32,
    pub predicted_confidence: f32,
    pub actual_class: Option<u32>,
    pub is_correct: Option<bool>,
    pub timestamp: u64,
    pub client_addr: String,
}

#[derive(Debug, Clone)]
pub struct FingerprintStats {
    pub total_predictions: usize,
    pub correct_predictions: usize,
    pub mispredictions: usize,
    pub unknown_predictions: usize,
}

pub struct OnlineLearning {
    feedback_buffer: RwLock<VecDeque<Feedback>>,
    max_buffer_size: usize,
    correction_threshold: f32,
    stats: RwLock<FingerprintStats>,
    last_update: RwLock<Instant>,
    update_interval: Duration,
}

impl OnlineLearning {
    pub fn new(max_buffer_size: usize, update_interval_secs: u64) -> Self {
        Self {
            feedback_buffer: RwLock::new(VecDeque::new()),
            max_buffer_size,
            correction_threshold: 0.15,
            stats: RwLock::new(FingerprintStats {
                total_predictions: 0,
                correct_predictions: 0,
                mispredictions: 0,
                unknown_predictions: 0,
            }),
            last_update: RwLock::new(Instant::now()),
            update_interval: Duration::from_secs(update_interval_secs),
        }
    }

    pub fn record_prediction(
        &self,
        fingerprint_hash: String,
        predicted_class: u32,
        predicted_confidence: f32,
        client_addr: String,
    ) {
        let feedback = Feedback {
            fingerprint_hash,
            predicted_class,
            predicted_confidence,
            actual_class: None,
            is_correct: None,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs(),
            client_addr,
        };

        let mut buffer = self.feedback_buffer.write();
        if buffer.len() >= self.max_buffer_size {
            buffer.pop_front();
        }
        buffer.push_back(feedback);

        self.update_stats_with_prediction(predicted_class, predicted_confidence);
    }

    pub fn submit_feedback(
        &self,
        fingerprint_hash: String,
        actual_class: u32,
        client_addr: String,
    ) -> Result<(), String> {
        let mut buffer = self.feedback_buffer.write();
        
        let feedback = buffer.iter_mut()
            .find(|f| f.fingerprint_hash == fingerprint_hash && f.client_addr == client_addr)
            .ok_or_else(|| "Feedback not found".to_string())?;
        
        feedback.actual_class = Some(actual_class);
        feedback.is_correct = Some(feedback.predicted_class == actual_class);

        drop(buffer);

        self.update_stats_with_feedback(
            feedback.predicted_class,
            feedback.predicted_confidence,
            actual_class,
        );

        if self.should_trigger_update() {
            self.trigger_model_update();
        }

        Ok(())
    }

    pub fn submit_correction(
        &self,
        fingerprint_hash: String,
        actual_class: u32,
    ) -> Result<(), String> {
        let mut buffer = self.feedback_buffer.write();
        
        if let Some(feedback) = buffer.iter_mut()
            .find(|f| f.fingerprint_hash == fingerprint_hash)
        {
            feedback.actual_class = Some(actual_class);
            feedback.is_correct = Some(feedback.predicted_class != actual_class);
            feedback.predicted_class = actual_class;
            
            let was_correct = feedback.is_correct.unwrap_or(false);
            drop(buffer);
            
            let mut stats = self.stats.write();
            if was_correct {
                stats.correct_predictions = stats.correct_predictions.saturating_sub(1);
                stats.mispredictions += 1;
            }
        }
        
        Ok(())
    }

    fn update_stats_with_prediction(&self, predicted_class: u32, confidence: f32) {
        let mut stats = self.stats.write();
        stats.total_predictions += 1;
        
        if predicted_class == 0 {
            stats.unknown_predictions += 1;
        }
    }

    fn update_stats_with_feedback(
        &self,
        predicted_class: u32,
        confidence: f32,
        actual_class: u32,
    ) {
        let mut stats = self.stats.write();
        stats.total_predictions += 1;
        
        if predicted_class == actual_class {
            stats.correct_predictions += 1;
        } else {
            stats.mispredictions += 1;
        }
    }

    pub fn should_trigger_update(&self) -> bool {
        let last_update = *self.last_update.read();
        last_update.elapsed() >= self.update_interval
    }

    pub fn trigger_model_update(&self) {
        let mut last_update = self.last_update.write();
        *last_update = Instant::now();
        
        let buffer = self.feedback_buffer.read();
        let recent_feedback: Vec<_> = buffer.iter()
            .filter(|f| f.actual_class.is_some())
            .cloned()
            .collect();
        
        if recent_feedback.len() > 10 {
            tracing::info!(
                "Online learning: collected {} feedback samples, triggering model update",
                recent_feedback.len()
            );
        }
    }

    pub fn get_stats(&self) -> FingerprintStats {
        self.stats.read().clone()
    }

    pub fn get_accuracy(&self) -> f32 {
        let stats = self.stats.read();
        if stats.total_predictions == 0 {
            return 0.0;
        }
        stats.correct_predictions as f32 / stats.total_predictions as f32
    }

    pub fn get_pending_feedback_count(&self) -> usize {
        self.feedback_buffer.read().iter()
            .filter(|f| f.actual_class.is_none())
            .count()
    }

    pub fn get_confirmed_feedback_count(&self) -> usize {
        self.feedback_buffer.read().iter()
            .filter(|f| f.actual_class.is_some())
            .count()
    }

    pub fn clear_old_feedback(&self, max_age_secs: u64) {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        let mut buffer = self.feedback_buffer.write();
        buffer.retain(|f| now - f.timestamp < max_age_secs);
    }
}

pub struct ModelUpdater {
    online_learning: Arc<OnlineLearning>,
}

impl ModelUpdater {
    pub fn new(online_learning: Arc<OnlineLearning>) -> Self {
        Self { online_learning }
    }

    pub fn compute_weight_updates(&self) -> Vec<WeightUpdate> {
        let buffer = self.online_learning.feedback_buffer.read();
        let confirmed: Vec<_> = buffer.iter()
            .filter(|f| f.actual_class.is_some())
            .collect();
        
        let mut updates = Vec::new();
        
        let mut class_corrections: HashMap<u32, Vec<&Feedback>> = HashMap::new();
        for feedback in confirmed {
            if let Some(actual) = feedback.actual_class {
                class_corrections.entry(actual).or_default().push(feedback);
            }
        }
        
        for (class_id, feedbacks) in class_corrections {
            let error_rate = feedbacks.iter()
                .filter(|f| f.is_correct == Some(false))
                .count() as f32 / feedbacks.len() as f32;
            
            if error_rate > 0.3 {
                updates.push(WeightUpdate {
                    class_id,
                    direction: UpdateDirection::IncreaseConfidence,
                    magnitude: error_rate * 0.1,
                    sample_count: feedbacks.len(),
                });
            }
        }
        
        updates
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WeightUpdate {
    pub class_id: u32,
    pub direction: UpdateDirection,
    pub magnitude: f32,
    pub sample_count: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum UpdateDirection {
    IncreaseConfidence,
    DecreaseConfidence,
}

use std::collections::HashMap;
