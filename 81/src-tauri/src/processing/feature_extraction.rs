use std::f32::consts::PI;

use serde::{Deserialize, Serialize};

use crate::audio::MFCCExtractor;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FeatureVector {
    pub features: Vec<f32>,
    pub num_frames: usize,
    pub feature_dim: usize,
    pub mean: f32,
    pub std: f32,
    pub energy: Vec<f32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PhonemeFeatures {
    pub mfcc_sequence: Vec<Vec<f32>>,
    pub delta_mfcc: Vec<Vec<f32>>,
    pub delta_delta_mfcc: Vec<Vec<f32>>,
    pub energy_profile: Vec<f32>,
    pub zcr_profile: Vec<f32>,
}

pub struct FeatureExtractor {
    mfcc_extractor: MFCCExtractor,
    sample_rate: u32,
    num_mfcc_coeffs: usize,
    use_deltas: bool,
    use_energy: bool,
}

impl FeatureExtractor {
    pub fn new(sample_rate: u32, num_mfcc_coeffs: usize) -> Self {
        let mfcc_extractor = MFCCExtractor::new(sample_rate, num_mfcc_coeffs);
        Self {
            mfcc_extractor,
            sample_rate,
            num_mfcc_coeffs,
            use_deltas: true,
            use_energy: true,
        }
    }

    pub fn extract_mfcc_sequence(&mut self, samples: &[f32]) -> PhonemeFeatures {
        let (mfcc, energies) = self.mfcc_extractor.extract_with_energy(samples);

        let mfcc_norm = Self::normalize_frames(&mfcc);

        let delta_mfcc = if self.use_deltas {
            Self::compute_delta(&mfcc_norm, 2)
        } else {
            Vec::new()
        };

        let delta_delta_mfcc = if self.use_deltas && !delta_mfcc.is_empty() {
            Self::compute_delta(&delta_mfcc, 2)
        } else {
            Vec::new()
        };

        let zcr_profile = Self::compute_zcr_profile(samples, 512, 256);
        let energy_profile = if self.use_energy { energies.clone() } else { Vec::new() };

        PhonemeFeatures {
            mfcc_sequence: mfcc_norm,
            delta_mfcc,
            delta_delta_mfcc,
            energy_profile,
            zcr_profile,
        }
    }

    pub fn extract_feature_vector(&mut self, samples: &[f32]) -> FeatureVector {
        let features = self.mfcc_extractor.extract(samples);

        let num_frames = features.len();
        let feature_dim = if features.is_empty() { 0 } else { features[0].len() };

        let mut all_features = Vec::new();
        for frame in &features {
            all_features.extend_from_slice(frame);
        }

        let mean = if all_features.is_empty() {
            0.0
        } else {
            all_features.iter().sum::<f32>() / all_features.len() as f32
        };

        let variance = if all_features.is_empty() {
            0.0
        } else {
            all_features.iter().map(|&x| (x - mean).powi(2)).sum::<f32>()
                / all_features.len() as f32
        };
        let std = variance.sqrt();

        let energy: Vec<f32> = features
            .iter()
            .map(|frame| frame.iter().sum::<f32>() / frame.len() as f32)
            .collect();

        FeatureVector {
            features: all_features,
            num_frames,
            feature_dim,
            mean,
            std,
            energy,
        }
    }

    pub fn extract_aggregate_features(&mut self, samples: &[f32]) -> Vec<f32> {
        let mfcc = self.mfcc_extractor.extract(samples);
        if mfcc.is_empty() {
            return vec![0.0; self.num_mfcc_coeffs * 3];
        }

        let num_coeffs = mfcc[0].len();
        let mut agg_features = Vec::with_capacity(num_coeffs * 3);

        for i in 0..num_coeffs {
            let mut sum = 0.0;
            let mut max = f32::MIN;
            let mut min = f32::MAX;

            for frame in &mfcc {
                let val = frame[i];
                sum += val;
                max = max.max(val);
                min = min.min(val);
            }

            let mean = sum / mfcc.len() as f32;
            agg_features.push(mean);
            agg_features.push(max);
            agg_features.push(min);
        }

        Self::z_normalize(&mut agg_features);
        agg_features
    }

    pub fn compute_speech_quality_score(&self, samples: &[f32]) -> f32 {
        if samples.is_empty() {
            return 0.0;
        }

        let rms = Self::compute_rms(samples);
        let snr_estimate = self.estimate_snr(samples);
        let zcr = Self::compute_zcr(samples);

        let rms_score = if rms > 0.01 {
            (rms / 0.1).min(1.0)
        } else {
            0.0
        };

        let snr_score = (snr_estimate / 20.0).min(1.0).max(0.0);

        let zcr_score = if zcr > 0.02 && zcr < 0.5 {
            1.0
        } else {
            0.5
        };

        0.4 * rms_score + 0.4 * snr_score + 0.2 * zcr_score
    }

    fn normalize_frames(frames: &[Vec<f32>]) -> Vec<Vec<f32>> {
        if frames.is_empty() {
            return Vec::new();
        }

        let num_frames = frames.len();
        let num_coeffs = frames[0].len();
        let mut result = vec![vec![0.0; num_coeffs]; num_frames];

        for coeff in 0..num_coeffs {
            let mut sum = 0.0;
            for frame in frames {
                sum += frame[coeff];
            }
            let mean = sum / num_frames as f32;

            let mut variance = 0.0;
            for frame in frames {
                variance += (frame[coeff] - mean).powi(2);
            }
            variance /= num_frames as f32;
            let std = variance.sqrt().max(1e-10);

            for i in 0..num_frames {
                result[i][coeff] = (frames[i][coeff] - mean) / std;
            }
        }

        result
    }

    fn compute_delta(frames: &[Vec<f32>], window: usize) -> Vec<Vec<f32>> {
        if frames.len() < 2 * window + 1 {
            return frames.to_vec();
        }

        let num_frames = frames.len();
        let num_coeffs = frames[0].len();
        let mut delta = vec![vec![0.0; num_coeffs]; num_frames];

        let mut denom = 0.0;
        for k in 1..=window {
            denom += (k * k) as f32;
        }
        denom *= 2.0;

        for t in 0..num_frames {
            for c in 0..num_coeffs {
                let mut sum = 0.0;
                for k in 1..=window {
                    let past_idx = if t >= k { t - k } else { 0 };
                    let future_idx = if t + k < num_frames { t + k } else { num_frames - 1 };
                    sum += k as f32 * (frames[future_idx][c] - frames[past_idx][c]);
                }
                delta[t][c] = sum / denom;
            }
        }

        delta
    }

    fn compute_zcr_profile(samples: &[f32], frame_size: usize, hop_size: usize) -> Vec<f32> {
        if samples.len() < frame_size {
            return vec![0.0; 1];
        }

        let num_frames = (samples.len() - frame_size) / hop_size + 1;
        let mut zcr = Vec::with_capacity(num_frames);

        for i in 0..num_frames {
            let start = i * hop_size;
            let end = start + frame_size;
            let frame = &samples[start..end];
            zcr.push(Self::compute_zcr(frame));
        }

        zcr
    }

    fn compute_zcr(samples: &[f32]) -> f32 {
        if samples.len() < 2 {
            return 0.0;
        }

        let mut crossings = 0;
        for i in 1..samples.len() {
            if (samples[i] >= 0.0 && samples[i - 1] < 0.0)
                || (samples[i] < 0.0 && samples[i - 1] >= 0.0)
            {
                crossings += 1;
            }
        }

        crossings as f32 / samples.len() as f32
    }

    fn compute_rms(samples: &[f32]) -> f32 {
        if samples.is_empty() {
            return 0.0;
        }
        let sum_sq: f32 = samples.iter().map(|&x| x * x).sum();
        (sum_sq / samples.len() as f32).sqrt()
    }

    fn estimate_snr(&self, samples: &[f32]) -> f32 {
        if samples.len() < 512 {
            return 10.0;
        }

        let mut noise_estimate = 0.0;
        let noise_frames = 5;
        let noise_frame_size = samples.len() / noise_frames;

        for i in 0..noise_frames {
            let start = i * noise_frame_size;
            let end = start + noise_frame_size;
            let rms = Self::compute_rms(&samples[start..end]);
            if i == 0 || rms < noise_estimate {
                noise_estimate = rms;
            }
        }

        let signal_rms = Self::compute_rms(samples);
        noise_estimate = noise_estimate.max(1e-8);

        10.0 * (signal_rms / noise_estimate).log10()
    }

    fn z_normalize(v: &mut [f32]) {
        if v.is_empty() {
            return;
        }

        let mean = v.iter().sum::<f32>() / v.len() as f32;
        let variance: f32 = v.iter().map(|&x| (x - mean).powi(2)).sum::<f32>() / v.len() as f32;
        let std = variance.sqrt().max(1e-10);

        for x in v.iter_mut() {
            *x = (*x - mean) / std;
        }
    }

    pub fn check_pronunciation_consistency(
        &self,
        samples_list: &[Vec<f32>],
    ) -> (f32, Vec<f32>) {
        if samples_list.len() < 2 {
            return (1.0, vec![1.0; samples_list.len()]);
        }

        let features: Vec<Vec<f32>> = samples_list
            .iter()
            .map(|s| {
                let mut fe = FeatureExtractor::new(self.sample_rate, self.num_mfcc_coeffs);
                fe.extract_aggregate_features(s)
            })
            .collect();

        let mut pairwise_scores = Vec::new();
        for i in 0..features.len() {
            for j in i + 1..features.len() {
                let sim = Self::cosine_similarity(&features[i], &features[j]);
                pairwise_scores.push(sim);
            }
        }

        let avg_sim = if pairwise_scores.is_empty() {
            0.0
        } else {
            pairwise_scores.iter().sum::<f32>() / pairwise_scores.len() as f32
        };

        let individual_scores: Vec<f32> = features
            .iter()
            .enumerate()
            .map(|(idx, feat)| {
                let mut total = 0.0;
                let mut count = 0.0;
                for (j, other) in features.iter().enumerate() {
                    if j != idx {
                        total += Self::cosine_similarity(feat, other);
                        count += 1.0;
                    }
                }
                if count > 0.0 {
                    total / count
                } else {
                    1.0
                }
            })
            .collect();

        (avg_sim, individual_scores)
    }

    fn cosine_similarity(a: &[f32], b: &[f32]) -> f32 {
        let n = a.len().min(b.len());
        if n == 0 {
            return 0.0;
        }

        let mut dot_product = 0.0;
        let mut norm_a = 0.0;
        let mut norm_b = 0.0;

        for i in 0..n {
            dot_product += a[i] * b[i];
            norm_a += a[i] * a[i];
            norm_b += b[i] * b[i];
        }

        let denom = norm_a.sqrt() * norm_b.sqrt();
        if denom < 1e-10 {
            0.0
        } else {
            (dot_product / denom).clamp(-1.0, 1.0)
        }
    }
}

impl Clone for FeatureExtractor {
    fn clone(&self) -> Self {
        Self::new(self.sample_rate, self.num_mfcc_coeffs)
    }
}
