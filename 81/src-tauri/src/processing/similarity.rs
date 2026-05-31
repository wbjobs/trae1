use std::f32::consts::PI;

use serde::{Deserialize, Serialize};

use crate::processing::PhonemeFeatures;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SimilarityResult {
    pub cosine_similarity: f32,
    pub dtw_distance: f32,
    pub combined_score: f32,
    pub matched: bool,
    pub threshold: f32,
    pub details: SimilarityDetails,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SimilarityDetails {
    pub mfcc_similarity: f32,
    pub delta_similarity: f32,
    pub delta_delta_similarity: f32,
    pub energy_similarity: f32,
    pub zcr_similarity: f32,
}

pub struct SimilarityScorer {
    pub cosine_threshold: f32,
    pub dtw_threshold: f32,
    pub combined_threshold: f32,
    pub cosine_weight: f32,
    pub dtw_weight: f32,
    pub mfcc_weight: f32,
    pub delta_weight: f32,
    pub energy_weight: f32,
    pub zcr_weight: f32,
}

impl Default for SimilarityScorer {
    fn default() -> Self {
        Self {
            cosine_threshold: 0.75,
            dtw_threshold: 50.0,
            combined_threshold: 0.7,
            cosine_weight: 0.6,
            dtw_weight: 0.4,
            mfcc_weight: 0.5,
            delta_weight: 0.3,
            energy_weight: 0.1,
            zcr_weight: 0.1,
        }
    }
}

impl SimilarityScorer {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_thresholds(
        cosine_threshold: f32,
        dtw_threshold: f32,
        combined_threshold: f32,
    ) -> Self {
        Self {
            cosine_threshold,
            dtw_threshold,
            combined_threshold,
            ..Self::default()
        }
    }

    pub fn score_features(
        &self,
        template: &PhonemeFeatures,
        input: &PhonemeFeatures,
    ) -> SimilarityResult {
        let mfcc_sim = self.cosine_similarity_2d(&template.mfcc_sequence, &input.mfcc_sequence);
        let delta_sim = if !template.delta_mfcc.is_empty() && !input.delta_mfcc.is_empty() {
            self.cosine_similarity_2d(&template.delta_mfcc, &input.delta_mfcc)
        } else {
            0.0
        };
        let delta_delta_sim = if !template.delta_delta_mfcc.is_empty() && !input.delta_delta_mfcc.is_empty() {
            self.cosine_similarity_2d(&template.delta_delta_mfcc, &input.delta_delta_mfcc)
        } else {
            0.0
        };
        let energy_sim = if !template.energy_profile.is_empty() && !input.energy_profile.is_empty() {
            self.cosine_similarity_1d(&template.energy_profile, &input.energy_profile)
        } else {
            0.0
        };
        let zcr_sim = if !template.zcr_profile.is_empty() && !input.zcr_profile.is_empty() {
            self.cosine_similarity_1d(&template.zcr_profile, &input.zcr_profile)
        } else {
            0.0
        };

        let weighted_cosine = self.mfcc_weight * mfcc_sim
            + self.delta_weight * (delta_sim + delta_delta_sim) / 2.0
            + self.energy_weight * energy_sim
            + self.zcr_weight * zcr_sim;

        let dtw_dist = self.dtw_distance(&template.mfcc_sequence, &input.mfcc_sequence);
        let normalized_dtw = 1.0 - (dtw_dist / self.dtw_threshold).min(1.0);

        let combined_score =
            self.cosine_weight * weighted_cosine + self.dtw_weight * normalized_dtw;

        let matched = combined_score >= self.combined_threshold
            && weighted_cosine >= self.cosine_threshold;

        SimilarityResult {
            cosine_similarity: weighted_cosine,
            dtw_distance: dtw_dist,
            combined_score,
            matched,
            threshold: self.combined_threshold,
            details: SimilarityDetails {
                mfcc_similarity: mfcc_sim,
                delta_similarity: delta_sim,
                delta_delta_similarity: delta_delta_sim,
                energy_similarity: energy_sim,
                zcr_similarity: zcr_sim,
            },
        }
    }

    pub fn score_multiple_templates(
        &self,
        templates: &[PhonemeFeatures],
        input: &PhonemeFeatures,
    ) -> SimilarityResult {
        if templates.is_empty() {
            return SimilarityResult {
                cosine_similarity: 0.0,
                dtw_distance: f32::INFINITY,
                combined_score: 0.0,
                matched: false,
                threshold: self.combined_threshold,
                details: SimilarityDetails {
                    mfcc_similarity: 0.0,
                    delta_similarity: 0.0,
                    delta_delta_similarity: 0.0,
                    energy_similarity: 0.0,
                    zcr_similarity: 0.0,
                },
            };
        }

        let results: Vec<SimilarityResult> = templates
            .iter()
            .map(|t| self.score_features(t, input))
            .collect();

        let best_result = results
            .iter()
            .max_by(|a, b| a.combined_score.partial_cmp(&b.combined_score).unwrap_or(std::cmp::Ordering::Equal))
            .unwrap()
            .clone();

        let avg_cosine: f32 = results.iter().map(|r| r.cosine_similarity).sum::<f32>() / results.len() as f32;
        let avg_dtw: f32 = results.iter().map(|r| r.dtw_distance).sum::<f32>() / results.len() as f32;

        SimilarityResult {
            cosine_similarity: (best_result.cosine_similarity + avg_cosine) / 2.0,
            dtw_distance: (best_result.dtw_distance + avg_dtw) / 2.0,
            combined_score: best_result.combined_score,
            matched: best_result.matched,
            threshold: self.combined_threshold,
            details: best_result.details,
        }
    }

    fn dtw_distance(&self, seq1: &[Vec<f32>], seq2: &[Vec<f32>]) -> f32 {
        if seq1.is_empty() || seq2.is_empty() {
            return f32::INFINITY;
        }

        let n = seq1.len();
        let m = seq2.len();
        let dim = seq1[0].len();

        let mut cost_matrix = vec![vec![f32::INFINITY; m + 1]; n + 1];
        cost_matrix[0][0] = 0.0;

        for i in 1..=n {
            for j in 1..=m {
                let cost = self.euclidean_distance(&seq1[i - 1], &seq2[j - 1]);
                let min_prev = cost_matrix[i - 1][j]
                    .min(cost_matrix[i][j - 1])
                    .min(cost_matrix[i - 1][j - 1]);
                cost_matrix[i][j] = cost + min_prev;
            }
        }

        cost_matrix[n][m] / ((n + m) as f32 * dim as f32).sqrt()
    }

    fn dtw_distance_fast(&self, seq1: &[Vec<f32>], seq2: &[Vec<f32>], band: usize) -> f32 {
        if seq1.is_empty() || seq2.is_empty() {
            return f32::INFINITY;
        }

        let n = seq1.len();
        let m = seq2.len();
        let dim = seq1[0].len();

        let mut prev = vec![f32::INFINITY; m + 1];
        let mut curr = vec![f32::INFINITY; m + 1];
        prev[0] = 0.0;

        for i in 1..=n {
            curr[0] = f32::INFINITY;
            let j_start = 1.max(i.saturating_sub(band));
            let j_end = m.min(i + band);

            for j in j_start..=j_end {
                let cost = self.euclidean_distance(&seq1[i - 1], &seq2[j - 1]);
                let min_prev = prev[j]
                    .min(curr[j - 1])
                    .min(prev[j - 1]);
                curr[j] = cost + min_prev;
            }

            std::mem::swap(&mut prev, &mut curr);
            curr.fill(f32::INFINITY);
        }

        prev[m] / ((n + m) as f32 * dim as f32).sqrt()
    }

    fn euclidean_distance(&self, v1: &[f32], v2: &[f32]) -> f32 {
        let n = v1.len().min(v2.len());
        let mut sum = 0.0;
        for i in 0..n {
            let diff = v1[i] - v2[i];
            sum += diff * diff;
        }
        sum.sqrt()
    }

    fn cosine_similarity_1d(&self, v1: &[f32], v2: &[f32]) -> f32 {
        let n = v1.len().min(v2.len());
        if n == 0 {
            return 0.0;
        }

        let mut dot = 0.0;
        let mut norm1 = 0.0;
        let mut norm2 = 0.0;

        for i in 0..n {
            dot += v1[i] * v2[i];
            norm1 += v1[i] * v1[i];
            norm2 += v2[i] * v2[i];
        }

        let denom = norm1.sqrt() * norm2.sqrt();
        if denom < 1e-10 {
            0.0
        } else {
            (dot / denom).clamp(-1.0, 1.0)
        }
    }

    fn cosine_similarity_2d(&self, m1: &[Vec<f32>], m2: &[Vec<f32>]) -> f32 {
        if m1.is_empty() || m2.is_empty() {
            return 0.0;
        }

        let num_frames = m1.len().min(m2.len());
        let num_coeffs = m1[0].len().min(m2[0].len());

        let mut total_sim = 0.0;
        let mut valid_frames = 0;

        for i in 0..num_frames {
            let sim = self.cosine_similarity_1d(&m1[i][..num_coeffs], &m2[i][..num_coeffs]);
            if !sim.is_nan() {
                total_sim += sim;
                valid_frames += 1;
            }
        }

        if valid_frames == 0 {
            0.0
        } else {
            total_sim / valid_frames as f32
        }
    }

    pub fn set_combined_threshold(&mut self, threshold: f32) {
        self.combined_threshold = threshold.clamp(0.3, 0.95);
    }
}

impl Clone for SimilarityScorer {
    fn clone(&self) -> Self {
        Self {
            cosine_threshold: self.cosine_threshold,
            dtw_threshold: self.dtw_threshold,
            combined_threshold: self.combined_threshold,
            cosine_weight: self.cosine_weight,
            dtw_weight: self.dtw_weight,
            mfcc_weight: self.mfcc_weight,
            delta_weight: self.delta_weight,
            energy_weight: self.energy_weight,
            zcr_weight: self.zcr_weight,
        }
    }
}

pub fn softmax(scores: &[f32]) -> Vec<f32> {
    let max_val = scores.iter().cloned().fold(f32::NEG_INFINITY, f32::max);
    let exps: Vec<f32> = scores.iter().map(|&s| (s - max_val).exp()).collect();
    let sum: f32 = exps.iter().sum();
    exps.iter().map(|&e| e / sum).collect()
}
