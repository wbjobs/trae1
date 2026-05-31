use std::time::Instant;

use crate::audio::MfccExtractor;

#[derive(Debug, Clone)]
pub struct HotwordResult {
    pub detected: bool,
    pub confidence: f32,
    pub timestamp: Instant,
}

#[derive(Clone)]
pub struct HotwordDetector {
    mfcc_extractor: MfccExtractor,
    threshold: f32,
    energy_threshold: f32,
    min_voice_frames: usize,
    template_mfcc: Option<Vec<Vec<f32>>>,
}

impl HotwordDetector {
    pub fn new(threshold: f32) -> Self {
        let mfcc_config = crate::audio::MfccConfig {
            num_mfcc: 13,
            num_mel_bins: 40,
            frame_length_ms: 25,
            frame_step_ms: 10,
            fft_size: 512,
            pre_emphasis: 0.97,
            lower_frequency: 20.0,
            upper_frequency: None,
            num_frames: 147,
            sample_rate: 16_000,
        };
        let mfcc_extractor = MfccExtractor::new(mfcc_config);

        Self {
            mfcc_extractor,
            threshold,
            energy_threshold: 0.01,
            min_voice_frames: 20,
            template_mfcc: None,
        }
    }

    pub fn set_threshold(&mut self, threshold: f32) {
        self.threshold = threshold;
    }

    pub fn set_template(&mut self, template_samples: &[f32]) {
        let mfcc = self.mfcc_extractor.extract_mfcc(template_samples);
        self.template_mfcc = Some(mfcc);
        log::info!("热词模板已加载，包含 {} 帧", self.template_mfcc.as_ref().unwrap().len());
    }

    fn compute_rms_energy(&self, samples: &[f32]) -> f32 {
        let sum_squares: f32 = samples.iter().map(|&x| x * x).sum();
        (sum_squares / samples.len() as f32).sqrt()
    }

    fn count_voice_frames(&self, samples: &[f32]) -> usize {
        let frame_step = (16_000u64 * 10 / 1000) as usize;
        let frame_len = (16_000u64 * 25 / 1000) as usize;
        let mut count = 0;
        let mut i = 0;
        while i + frame_len <= samples.len() {
            let energy = self.compute_rms_energy(&samples[i..i + frame_len]);
            if energy > self.energy_threshold {
                count += 1;
            }
            i += frame_step;
        }
        count
    }

    fn cosine_similarity(a: &[f32], b: &[f32]) -> f32 {
        let min_len = a.len().min(b.len());
        let mut dot = 0.0f32;
        let mut norm_a = 0.0f32;
        let mut norm_b = 0.0f32;
        for i in 0..min_len {
            dot += a[i] * b[i];
            norm_a += a[i] * a[i];
            norm_b += b[i] * b[i];
        }
        let denom = norm_a.sqrt() * norm_b.sqrt();
        if denom < 1e-10 {
            0.0
        } else {
            dot / denom
        }
    }

    fn dtw_distance(template: &[Vec<f32>], input: &[Vec<f32>]) -> f32 {
        let n = template.len();
        let m = input.len();
        if n == 0 || m == 0 {
            return f32::MAX;
        }

        let mut dtw = vec![vec![f32::MAX; m + 1]; n + 1];
        dtw[0][0] = 0.0;

        for i in 1..=n {
            for j in 1..=m {
                let cost = Self::euclidean_distance(&template[i - 1], &input[j - 1]);
                dtw[i][j] = cost
                    + dtw[i - 1][j]
                        .min(dtw[i][j - 1])
                        .min(dtw[i - 1][j - 1]);
            }
        }

        dtw[n][m] / (n + m) as f32
    }

    fn euclidean_distance(a: &[f32], b: &[f32]) -> f32 {
        let min_len = a.len().min(b.len());
        let sum: f32 = (0..min_len).map(|i| (a[i] - b[i]).powi(2)).sum();
        sum.sqrt()
    }

    pub fn detect(&self, samples: &[f32]) -> HotwordResult {
        let timestamp = Instant::now();
        let rms = self.compute_rms_energy(samples);
        let voice_frames = self.count_voice_frames(samples);

        if rms < self.energy_threshold * 0.5 || voice_frames < self.min_voice_frames {
            return HotwordResult {
                detected: false,
                confidence: 0.0,
                timestamp,
            };
        }

        if let Some(ref template) = self.template_mfcc {
            let input_mfcc = self.mfcc_extractor.extract_mfcc(samples);
            let distance = Self::dtw_distance(template, &input_mfcc);

            let max_distance = 100.0;
            let confidence = (1.0 - (distance / max_distance).min(1.0)).max(0.0);

            HotwordResult {
                detected: confidence >= self.threshold,
                confidence,
                timestamp,
            }
        } else {
            let voice_ratio = voice_frames as f32 / 147.0;
            let confidence = (rms / 0.1).min(1.0) * voice_ratio.min(1.0);

            HotwordResult {
                detected: confidence >= self.threshold && rms > self.energy_threshold,
                confidence,
                timestamp,
            }
        }
    }

    pub fn threshold(&self) -> f32 {
        self.threshold
    }
}
