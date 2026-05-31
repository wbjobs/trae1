use std::f32::consts::PI;

use rustfft::num_complex::Complex;
use rustfft::FftPlanner;
use serde::{Deserialize, Serialize};

pub const NS_FRAME_SIZE: usize = 480;
pub const NS_FFT_SIZE: usize = 512;
pub const NS_OVERLAP: usize = 240;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NoiseProfile {
    pub noise_mean: Vec<f32>,
    pub noise_var: Vec<f32>,
    pub snr_estimate: f32,
    pub learned_frames: usize,
}

impl NoiseProfile {
    pub fn new(fft_size: usize) -> Self {
        let num_bins = fft_size / 2 + 1;
        Self {
            noise_mean: vec![0.0; num_bins],
            noise_var: vec![1e-6; num_bins],
            snr_estimate: 20.0,
            learned_frames: 0,
        }
    }

    pub fn is_learned(&self) -> bool {
        self.learned_frames >= 50
    }
}

pub struct WienerFilter {
    fft_size: usize,
    hop_size: usize,
    fft: std::sync::Arc<dyn rustfft::Fft<f32>>,
    ifft: std::sync::Arc<dyn rustfft::Fft<f32>>,
    window: Vec<f32>,
    prev_input: Vec<f32>,
    prev_output: Vec<f32>,
}

impl WienerFilter {
    pub fn new(fft_size: usize, hop_size: usize) -> Self {
        let mut planner = FftPlanner::<f32>::new();
        let fft = planner.plan_fft_forward(fft_size);
        let ifft = planner.plan_fft_inverse(fft_size);

        let window = Self::hann_window(fft_size);

        Self {
            fft_size,
            hop_size,
            fft,
            ifft,
            window,
            prev_input: vec![0.0; fft_size - hop_size],
            prev_output: vec![0.0; fft_size - hop_size],
        }
    }

    fn hann_window(size: usize) -> Vec<f32> {
        (0..size)
            .map(|i| 0.5 * (1.0 - (2.0 * PI * i as f32 / (size as f32 - 1.0)).cos()))
            .collect()
    }

    fn compute_spectrum(&mut self, frame: &[f32]) -> (Vec<Complex<f32>>, Vec<f32>) {
        let mut buffer: Vec<Complex<f32>> = frame
            .iter()
            .zip(self.window.iter())
            .map(|(&s, &w)| Complex::new(s * w, 0.0))
            .collect();

        while buffer.len() < self.fft_size {
            buffer.push(Complex::zero());
        }

        self.fft.process(&mut buffer);

        let magnitudes: Vec<f32> = buffer.iter().map(|c| c.norm()).collect();
        (buffer, magnitudes)
    }

    fn reconstruct_time(&mut self, spectrum: &[Complex<f32>]) -> Vec<f32> {
        let mut buffer = spectrum.to_vec();
        while buffer.len() < self.fft_size {
            buffer.push(Complex::zero());
        }

        self.ifft.process(&mut buffer);

        let norm = 1.0 / self.fft_size as f32;
        buffer.iter().map(|c| c.re * norm).collect()
    }

    fn apply_wiener_filter(
        spectrum: &[Complex<f32>],
        magnitudes: &[f32],
        noise_magnitudes: &[f32>,
        snr_prior: &[f32],
        alpha: f32,
    ) -> Vec<Complex<f32>> {
        let num_bins = spectrum.len().min(magnitudes.len()).min(noise_magnitudes.len());
        let mut filtered = Vec::with_capacity(spectrum.len());

        for i in 0..num_bins {
            let noisy_mag = magnitudes[i];
            let noise_mag = noise_magnitudes[i].max(1e-10);

            let snr_post = (noisy_mag * noisy_mag) / (noise_mag * noise_mag).max(1e-10);

            let snr_prior_val = alpha * snr_prior[i] + (1.0 - alpha) * (snr_post - 1.0).max(0.0);

            let gain = (snr_prior_val / (1.0 + snr_prior_val)).sqrt().min(1.0).max(0.0);

            filtered.push(spectrum[i].scale(gain));
        }

        for i in num_bins..spectrum.len() {
            filtered.push(spectrum[i].scale(0.01));
        }

        filtered
    }
}

pub struct NoiseSuppressor {
    wiener: WienerFilter,
    noise_profile: NoiseProfile,
    noise_magnitudes: Vec<f32>,
    noise_update_alpha: f32,
    snr_prior: Vec<f32>,
    snr_smooth_alpha: f32,
    is_learning: bool,
    min_gain: f32,
    max_attenuation_db: f32,
    sample_rate: u32,
    frame_buffer: Vec<f32>,
}

impl NoiseSuppressor {
    pub fn new(sample_rate: u32) -> Self {
        let fft_size = NS_FFT_SIZE;
        let hop_size = NS_OVERLAP;
        let num_bins = fft_size / 2 + 1;

        Self {
            wiener: WienerFilter::new(fft_size, hop_size),
            noise_profile: NoiseProfile::new(fft_size),
            noise_magnitudes: vec![1e-6; num_bins],
            noise_update_alpha: 0.95,
            snr_prior: vec![1.0; num_bins],
            snr_smooth_alpha: 0.98,
            is_learning: false,
            min_gain: 0.05,
            max_attenuation_db: 20.0,
            sample_rate,
            frame_buffer: Vec::with_capacity(NS_FRAME_SIZE * 2),
        }
    }

    pub fn start_learning(&mut self) {
        self.is_learning = true;
        self.noise_profile = NoiseProfile::new(self.wiener.fft_size);
        log::info!("开始学习环境噪声特征...");
    }

    pub fn stop_learning(&mut self) {
        self.is_learning = false;
        if self.noise_profile.learned_frames > 0 {
            log::info!(
                "噪声学习完成，共处理 {} 帧，估计 SNR: {:.1} dB",
                self.noise_profile.learned_frames,
                self.noise_profile.snr_estimate
            );
        }
    }

    pub fn is_learning(&self) -> bool {
        self.is_learning
    }

    pub fn get_noise_profile(&self) -> &NoiseProfile {
        &self.noise_profile
    }

    pub fn load_noise_profile(&mut self, profile: NoiseProfile) {
        self.noise_profile = profile;
        self.noise_magnitudes = self.noise_profile.noise_mean.clone();
        log::info!("已加载噪声配置文件");
    }

    pub fn estimate_snr(&self, signal: &[f32]) -> f32 {
        if signal.is_empty() {
            return 0.0;
        }

        let signal_power: f32 = signal.iter().map(|&x| x * x).sum::<f32>() / signal.len() as f32;
        let noise_power: f32 = self.noise_magnitudes.iter().map(|&m| m * m).sum::<f32>()
            / self.noise_magnitudes.len() as f32;

        if noise_power < 1e-10 {
            return 30.0;
        }

        10.0 * (signal_power / noise_power).log10()
    }

    pub fn set_noise_update_rate(&mut self, alpha: f32) {
        self.noise_update_alpha = alpha.clamp(0.8, 0.999);
    }

    fn update_noise_estimate(&mut self, magnitudes: &[f32]) {
        let num_bins = magnitudes.len().min(self.noise_magnitudes.len());

        for i in 0..num_bins {
            self.noise_magnitudes[i] = self.noise_update_alpha * self.noise_magnitudes[i]
                + (1.0 - self.noise_update_alpha) * magnitudes[i];
        }

        if self.is_learning {
            for i in 0..num_bins {
                let old_mean = self.noise_profile.noise_mean[i];
                let delta = magnitudes[i] - old_mean;
                self.noise_profile.noise_mean[i] = old_mean
                    + delta / (self.noise_profile.learned_frames + 1) as f32;
                self.noise_profile.noise_var[i] = self.noise_profile.noise_var[i]
                    + delta * (magnitudes[i] - self.noise_profile.noise_mean[i]);
            }
            self.noise_profile.learned_frames += 1;

            if self.noise_profile.learned_frames >= 100 {
                self.stop_learning();
            }
        }
    }

    pub fn process_frame(&mut self, samples: &[f32]) -> Vec<f32> {
        self.frame_buffer.extend_from_slice(samples);

        let mut output = Vec::with_capacity(samples.len());
        let fft_size = self.wiener.fft_size;
        let hop_size = self.wiener.hop_size;

        while self.frame_buffer.len() >= fft_size {
            let frame: Vec<f32> = self.frame_buffer.drain(0..fft_size).collect();

            let (spectrum, magnitudes) = self.wiener.compute_spectrum(&frame);

            if self.is_learning || !self.noise_profile.is_learned() {
                self.update_noise_estimate(&magnitudes);
            } else {
                self.update_noise_estimate(&magnitudes);
            }

            let filtered_spectrum = WienerFilter::apply_wiener_filter(
                &spectrum,
                &magnitudes,
                &self.noise_magnitudes,
                &self.snr_prior,
                self.snr_smooth_alpha,
            );

            let time_domain = self.wiener.reconstruct_time(&filtered_spectrum);

            output.extend_from_slice(&time_domain[0..hop_size]);
        }

        output
    }

    pub fn process(&mut self, samples: &[f32]) -> Vec<f32> {
        let mut output = Vec::with_capacity(samples.len());
        let mut pos = 0;

        while pos < samples.len() {
            let chunk_size = (samples.len() - pos).min(NS_FRAME_SIZE);
            let chunk = &samples[pos..pos + chunk_size];
            let processed = self.process_frame(chunk);
            output.extend_from_slice(&processed);
            pos += chunk_size;
        }

        while output.len() < samples.len() {
            output.push(0.0);
        }
        output.truncate(samples.len());

        output
    }

    pub fn get_dynamic_confidence_threshold(&self, base_threshold: f32, signal: &[f32]) -> f32 {
        let snr = self.estimate_snr(signal);

        let threshold_adjustment = if snr < 0.0 {
            0.15
        } else if snr < 5.0 {
            0.10
        } else if snr < 10.0 {
            0.05
        } else if snr < 15.0 {
            0.02
        } else {
            0.0
        };

        (base_threshold + threshold_adjustment).min(0.95).max(0.3)
    }

    pub fn set_max_attenuation(&mut self, db: f32) {
        self.max_attenuation_db = db.clamp(6.0, 40.0);
        self.min_gain = 10.0f32.powf(-self.max_attenuation_db / 20.0);
    }
}

impl Clone for NoiseSuppressor {
    fn clone(&self) -> Self {
        Self::new(self.sample_rate)
    }
}
