use std::collections::VecDeque;

use serde::{Deserialize, Serialize};

use crate::inference::CommandResult;

#[derive(Debug, Clone)]
pub struct VotingConfig {
    pub min_frames: usize,
    pub max_frames: usize,
    pub min_consensus: f32,
    pub min_avg_confidence: f32,
    pub timeout_ms: u64,
}

impl Default for VotingConfig {
    fn default() -> Self {
        Self {
            min_frames: 3,
            max_frames: 5,
            min_consensus: 0.6,
            min_avg_confidence: 0.7,
            timeout_ms: 3000,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VotingResult {
    pub accepted: bool,
    pub final_label: String,
    pub final_label_index: usize,
    pub avg_confidence: f32,
    pub max_confidence: f32,
    pub consensus_ratio: f32,
    pub frames_voted: usize,
    pub all_votes: Vec<(String, f32)>,
}

struct FrameVote {
    label_index: usize,
    label: String,
    confidence: f32,
    timestamp: std::time::Instant,
}

pub struct MultiFrameVoter {
    config: VotingConfig,
    votes: VecDeque<FrameVote>,
    last_triggered: std::time::Instant,
    trigger_cooldown_ms: u64,
}

impl MultiFrameVoter {
    pub fn new(config: VotingConfig) -> Self {
        Self {
            config,
            votes: VecDeque::with_capacity(config.max_frames),
            last_triggered: std::time::Instant::now()
                .checked_sub(std::time::Duration::from_secs(10))
                .unwrap_or_else(std::time::Instant::now),
            trigger_cooldown_ms: 500,
        }
    }

    pub fn add_vote(&mut self, result: &CommandResult) -> VotingResult {
        let now = std::time::Instant::now();

        self.votes.retain(|v| {
            now.duration_since(v.timestamp).as_millis() as u64 <= self.config.timeout_ms
        });

        self.votes.push_back(FrameVote {
            label_index: result.label_index,
            label: result.label.clone(),
            confidence: result.confidence,
            timestamp: now,
        });

        while self.votes.len() > self.config.max_frames {
            self.votes.pop_front();
        }

        self.evaluate()
    }

    fn evaluate(&self) -> VotingResult {
        if self.votes.len() < self.config.min_frames {
            return VotingResult {
                accepted: false,
                final_label: String::new(),
                final_label_index: 10,
                avg_confidence: 0.0,
                max_confidence: 0.0,
                consensus_ratio: 0.0,
                frames_voted: self.votes.len(),
                all_votes: self.get_all_votes(),
            };
        }

        let mut label_counts: std::collections::HashMap<usize, (usize, f32)> =
            std::collections::HashMap::new();

        for vote in &self.votes {
            let entry = label_counts.entry(vote.label_index).or_insert((0, 0.0));
            entry.0 += 1;
            entry.1 += vote.confidence;
        }

        let mut best_label = 10;
        let mut best_count = 0;
        let mut best_conf_sum = 0.0;

        for (&label, &(count, conf_sum)) in &label_counts {
            if count > best_count || (count == best_count && conf_sum > best_conf_sum) {
                best_label = label;
                best_count = count;
                best_conf_sum = conf_sum;
            }
        }

        let consensus_ratio = best_count as f32 / self.votes.len() as f32;
        let avg_confidence = if best_count > 0 {
            best_conf_sum / best_count as f32
        } else {
            0.0
        };

        let max_confidence = self
            .votes
            .iter()
            .filter(|v| v.label_index == best_label)
            .map(|v| v.confidence)
            .fold(0.0f32, f32::max);

        let accepted = consensus_ratio >= self.config.min_consensus
            && avg_confidence >= self.config.min_avg_confidence;

        let final_label = if accepted {
            self.votes
                .iter()
                .find(|v| v.label_index == best_label)
                .map(|v| v.label.clone())
                .unwrap_or_default()
        } else {
            String::new()
        };

        VotingResult {
            accepted,
            final_label,
            final_label_index: if accepted { best_label } else { 10 },
            avg_confidence,
            max_confidence,
            consensus_ratio,
            frames_voted: self.votes.len(),
            all_votes: self.get_all_votes(),
        }
    }

    fn get_all_votes(&self) -> Vec<(String, f32)> {
        self.votes
            .iter()
            .map(|v| (v.label.clone(), v.confidence))
            .collect()
    }

    pub fn reset(&mut self) {
        self.votes.clear();
    }

    pub fn set_config(&mut self, config: VotingConfig) {
        self.config = config;
        self.votes.shrink_to(self.config.max_frames);
    }

    pub fn config(&self) -> &VotingConfig {
        &self.config
    }

    pub fn can_trigger(&self) -> bool {
        self.last_triggered.elapsed().as_millis() as u64 >= self.trigger_cooldown_ms
    }

    pub fn mark_triggered(&mut self) {
        self.last_triggered = std::time::Instant::now();
        self.votes.clear();
    }

    pub fn set_cooldown(&mut self, cooldown_ms: u64) {
        self.trigger_cooldown_ms = cooldown_ms;
    }
}

impl Clone for MultiFrameVoter {
    fn clone(&self) -> Self {
        Self::new(self.config.clone())
    }
}
