use std::f32::consts::PI;

use rustfft::num_complex::Complex;
use rustfft::num_traits::Zero;
use rustfft::{FftPlanner, Fft};
use std::sync::Arc;

#[derive(Debug, Clone)]
pub struct MfccConfig {
    pub sample_rate: u32,
    pub num_mfcc: usize,
    pub num_mel_bins: usize,
    pub frame_length_ms: u64,
    pub frame_step_ms: u64,
    pub fft_size: usize,
    pub pre_emphasis: f32,
    pub lower_frequency: f32,
    pub upper_frequency: Option<f32>,
    pub num_frames: usize,
}

impl Default for MfccConfig {
    fn default() -> Self {
        Self {
            sample_rate: 16_000,
            num_mfcc: 13,
            num_mel_bins: 40,
            frame_length_ms: 25,
            frame_step_ms: 10,
            fft_size: 512,
            pre_emphasis: 0.97,
            lower_frequency: 20.0,
            upper_frequency: None,
            num_frames: 98,
        }
    }
}

#[derive(Clone)]
pub struct MfccExtractor {
    config: MfccConfig,
    mel_filter_bank: Vec<Vec<f32>>,
    dct_matrix: Vec<Vec<f32>>,
    hamming_window: Vec<f32>,
    fft: Arc<dyn Fft<f32>>,
    fft_inverse: Arc<dyn Fft<f32>>,
}

fn hz_to_mel(hz: f32) -> f32 {
    2595.0 * (1.0 + hz / 700.0).log10()
}

fn mel_to_hz(mel: f32) -> f32 {
    700.0 * (10.0f32.powf(mel / 2595.0) - 1.0)
}

fn hamming_window(size: usize) -> Vec<f32> {
    (0..size)
        .map(|i| 0.54 - 0.46 * (2.0 * PI * i as f32 / (size as f32 - 1.0)).cos())
        .collect()
}

fn create_mel_filter_bank(
    num_bins: usize,
    fft_size: usize,
    sample_rate: u32,
    low_freq: f32,
    high_freq: f32,
) -> Vec<Vec<f32>> {
    let num_fft_bins = fft_size / 2 + 1;
    let mel_low = hz_to_mel(low_freq);
    let mel_high = hz_to_mel(high_freq);

    let mel_points: Vec<f32> = (0..num_bins + 2)
        .map(|i| mel_low + (mel_high - mel_low) * i as f32 / (num_bins + 1) as f32)
        .collect();

    let bin_points: Vec<usize> = mel_points
        .iter()
        .map(|&m| {
            let hz = mel_to_hz(m);
            ((fft_size + 1) as f32 * hz / sample_rate as f32).floor() as usize
        })
        .collect();

    let mut filter_bank = vec![vec![0.0f32; num_fft_bins]; num_bins];

    for i in 0..num_bins {
        let f_left = bin_points[i];
        let f_center = bin_points[i + 1];
        let f_right = bin_points[i + 2];

        for j in f_left..f_center {
            if j < num_fft_bins {
                filter_bank[i][j] = (j as f32 - f_left as f32) / (f_center as f32 - f_left as f32);
            }
        }
        for j in f_center..f_right {
            if j < num_fft_bins {
                filter_bank[i][j] = (f_right as f32 - j as f32) / (f_right as f32 - f_center as f32);
            }
        }
    }

    filter_bank
}

fn create_dct_matrix(num_mfcc: usize, num_bins: usize) -> Vec<Vec<f32>> {
    let mut dct = vec![vec![0.0f32; num_bins]; num_mfcc];
    let n = num_bins as f32;

    for k in 0..num_mfcc {
        for i in 0..num_bins {
            let angle = PI * k as f32 * (2 * i + 1) as f32 / (2.0 * n);
            dct[k][i] = angle.cos();
        }
    }

    dct
}

impl MfccExtractor {
    pub fn new(config: MfccConfig) -> Self {
        let high_freq = config
            .upper_frequency
            .unwrap_or(config.sample_rate as f32 / 2.0);

        let mel_filter_bank = create_mel_filter_bank(
            config.num_mel_bins,
            config.fft_size,
            config.sample_rate,
            config.lower_frequency,
            high_freq,
        );

        let dct_matrix = create_dct_matrix(config.num_mfcc, config.num_mel_bins);

        let frame_length_samples =
            (config.sample_rate as u64 * config.frame_length_ms / 1000) as usize;
        let hamming_window = hamming_window(frame_length_samples);

        let mut planner = FftPlanner::<f32>::new();
        let fft = planner.plan_fft_forward(config.fft_size);
        let fft_inverse = planner.plan_fft_inverse(config.fft_size);

        Self {
            config,
            mel_filter_bank,
            dct_matrix,
            hamming_window,
            fft,
            fft_inverse,
        }
    }

    pub fn config(&self) -> &MfccConfig {
        &self.config
    }

    fn pre_emphasis(&self, signal: &[f32]) -> Vec<f32> {
        let mut emphasized = Vec::with_capacity(signal.len());
        emphasized.push(signal[0]);
        for i in 1..signal.len() {
            emphasized.push(signal[i] - self.config.pre_emphasis * signal[i - 1]);
        }
        emphasized
    }

    fn pad_signal(&self, signal: &[f32]) -> Vec<f32> {
        let frame_length_samples =
            (self.config.sample_rate as u64 * self.config.frame_length_ms / 1000) as usize;
        let mut padded = signal.to_vec();
        while padded.len() < frame_length_samples {
            padded.push(0.0);
        }
        padded
    }

    fn compute_spectrum(&self, frame: &[f32]) -> Vec<f32> {
        let mut buffer: Vec<Complex<f32>> = frame
            .iter()
            .take(self.config.fft_size)
            .map(|&x| Complex::new(x, 0.0))
            .collect();

        while buffer.len() < self.config.fft_size {
            buffer.push(Complex::zero());
        }

        self.fft.process(&mut buffer);

        let num_bins = self.config.fft_size / 2 + 1;
        buffer.iter().take(num_bins).map(|c| c.norm()).collect()
    }

    fn apply_mel_filter_bank(&self, spectrum: &[f32]) -> Vec<f32> {
        let mut energies = vec![0.0f32; self.config.num_mel_bins];
        for (i, filter) in self.mel_filter_bank.iter().enumerate() {
            let mut energy = 0.0;
            for (j, &weight) in filter.iter().enumerate() {
                if j < spectrum.len() {
                    energy += spectrum[j] * weight;
                }
            }
            energies[i] = energy.max(1e-10);
        }
        energies.iter().map(|&e| e.ln()).collect()
    }

    fn apply_dct(&self, log_mel_energies: &[f32]) -> Vec<f32> {
        let mut mfcc = vec![0.0f32; self.config.num_mfcc];
        for k in 0..self.config.num_mfcc {
            let mut sum = 0.0;
            for i in 0..self.config.num_mel_bins {
                sum += log_mel_energies[i] * self.dct_matrix[k][i];
            }
            mfcc[k] = sum;
        }
        mfcc
    }

    pub fn extract_mfcc(&self, signal: &[f32]) -> Vec<Vec<f32>> {
        let signal = self.pre_emphasis(signal);
        let signal = self.pad_signal(&signal);

        let frame_length_samples =
            (self.config.sample_rate as u64 * self.config.frame_length_ms / 1000) as usize;
        let frame_step_samples =
            (self.config.sample_rate as u64 * self.config.frame_step_ms / 1000) as usize;

        let num_frames = if signal.len() >= frame_length_samples {
            ((signal.len() - frame_length_samples) / frame_step_samples) + 1
        } else {
            1
        };

        let num_frames = num_frames.min(self.config.num_frames);

        let mut mfcc_frames = Vec::with_capacity(num_frames);

        for i in 0..num_frames {
            let start = i * frame_step_samples;
            let end = (start + frame_length_samples).min(signal.len());

            let mut frame: Vec<f32> = signal[start..end].to_vec();
            while frame.len() < frame_length_samples {
                frame.push(0.0);
            }

            for (j, w) in self.hamming_window.iter().enumerate() {
                if j < frame.len() {
                    frame[j] *= *w;
                }
            }

            let spectrum = self.compute_spectrum(&frame);
            let log_mel = self.apply_mel_filter_bank(&spectrum);
            let mfcc = self.apply_dct(&log_mel);

            mfcc_frames.push(mfcc);
        }

        while mfcc_frames.len() < self.config.num_frames {
            mfcc_frames.push(vec![0.0f32; self.config.num_mfcc]);
        }

        mfcc_frames
    }

    pub fn extract_mfcc_flat(&self, signal: &[f32]) -> Vec<f32> {
        let mfcc_frames = self.extract_mfcc(signal);
        let mut flat = Vec::with_capacity(mfcc_frames.len() * self.config.num_mfcc);
        for frame in mfcc_frames {
            flat.extend_from_slice(&frame);
        }
        flat
    }

    pub fn compute_delta(&self, mfcc: &[Vec<f32>], n: usize) -> Vec<Vec<f32>> {
        let num_frames = mfcc.len();
        let num_coeffs = mfcc[0].len();
        let mut delta = vec![vec![0.0f32; num_coeffs]; num_frames];

        let denom: f32 = (1..=n).map(|i| (i * i) as f32).sum::<f32>() * 2.0;

        for t in 0..num_frames {
            for c in 0..num_coeffs {
                let mut sum = 0.0;
                for i in 1..=n {
                    let prev = if t as i32 - i as i32 >= 0 {
                        mfcc[t - i][c]
                    } else {
                        mfcc[0][c]
                    };
                    let next = if t + i < num_frames {
                        mfcc[t + i][c]
                    } else {
                        mfcc[num_frames - 1][c]
                    };
                    sum += i as f32 * (next - prev);
                }
                delta[t][c] = sum / denom;
            }
        }

        delta
    }

    pub fn extract_mfcc_with_deltas(&self, signal: &[f32]) -> Vec<Vec<f32>> {
        let mfcc = self.extract_mfcc(signal);
        let delta = self.compute_delta(&mfcc, 2);
        let delta_delta = self.compute_delta(&delta, 2);

        let num_frames = mfcc.len();
        let num_coeffs = mfcc[0].len();

        let mut result = vec![vec![0.0f32; num_coeffs * 3]; num_frames];
        for t in 0..num_frames {
            for c in 0..num_coeffs {
                result[t][c] = mfcc[t][c];
                result[t][c + num_coeffs] = delta[t][c];
                result[t][c + num_coeffs * 2] = delta_delta[t][c];
            }
        }
        result
    }
}
