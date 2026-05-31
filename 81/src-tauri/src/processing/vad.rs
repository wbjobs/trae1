use std::f32::consts::PI;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VadConfig {
    pub sample_rate: u32,
    pub frame_duration_ms: u64,
    pub energy_threshold: f32,
    pub spectral_flatness_threshold: f32,
    pub hangover_ms: u64,
    pub aggressiveness: u8,
}

impl Default for VadConfig {
    fn default() -> Self {
        Self {
            sample_rate: 16000,
            frame_duration_ms: 20,
            energy_threshold: 0.01,
            spectral_flatness_threshold: 0.4,
            hangover_ms: 200,
            aggressiveness: 2,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VadState {
    Silence = 0,
    Voice = 1,
    Transition = 2,
}

pub struct VoiceActivityDetector {
    config: VadConfig,
    state: VadState,
    last_voice_time: std::time::Instant,
    noise_floor: f32,
    noise_update_alpha: f32,
    frame_size: usize,
    is_initialized: bool,
    init_frames: usize,
    consecutive_silence: usize,
    consecutive_voice: usize,
}

impl VoiceActivityDetector {
    pub fn new(config: VadConfig) -> Self {
        let frame_size =
            (config.sample_rate as u64 * config.frame_duration_ms / 1000) as usize;

        Self {
            config,
            state: VadState::Silence,
            last_voice_time: std::time::Instant::now()
                .checked_sub(std::time::Duration::from_secs(10))
                .unwrap_or_else(std::time::Instant::now),
            noise_floor: 1e-6,
            noise_update_alpha: 0.995,
            frame_size,
            is_initialized: false,
            init_frames: 0,
            consecutive_silence: 0,
            consecutive_voice: 0,
        }
    }

    fn compute_rms_energy(&self, samples: &[f32]) -> f32 {
        if samples.is_empty() {
            return 0.0;
        }
        let sum_sq: f32 = samples.iter().map(|&x| x * x).sum();
        (sum_sq / samples.len() as f32).sqrt()
    }

    fn compute_spectral_flatness(&self, samples: &[f32]) -> f32 {
        if samples.is_empty() {
            return 0.0;
        }

        let mut sum_log = 0.0f32;
        let mut sum_linear = 0.0f32;
        let n = samples.len();

        for i in 0..n {
            let mag = samples[i].abs() + 1e-10;
            sum_log += mag.ln();
            sum_linear += mag;
        }

        let geom_mean = (sum_log / n as f32).exp();
        let arith_mean = sum_linear / n as f32;

        if arith_mean < 1e-10 {
            1.0
        } else {
            (geom_mean / arith_mean).clamp(0.0, 1.0)
        }
    }

    fn compute_zero_crossing_rate(&self, samples: &[f32]) -> f32 {
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

    pub fn process(&mut self, samples: &[f32]) -> VadState {
        let energy = self.compute_rms_energy(samples);
        let spectral_flatness = self.compute_spectral_flatness(samples);
        let zcr = self.compute_zero_crossing_rate(samples);

        if !self.is_initialized {
            self.noise_floor = (self.noise_floor * self.init_frames as f32 + energy)
                / (self.init_frames + 1) as f32;
            self.init_frames += 1;

            if self.init_frames >= 50 {
                self.is_initialized = true;
                let mut config = self.config.clone();
                config.energy_threshold = self.noise_floor * 2.0;
                self.config = config;
            }

            return VadState::Silence;
        }

        if energy < self.noise_floor * 1.5 {
            self.noise_floor = self.noise_update_alpha * self.noise_floor
                + (1.0 - self.noise_update_alpha) * energy;
        }

        let adaptive_threshold = match self.config.aggressiveness {
            0 => self.noise_floor * 1.5,
            1 => self.noise_floor * 2.0,
            2 => self.noise_floor * 3.0,
            3 => self.noise_floor * 4.0,
            _ => self.noise_floor * 2.5,
        };

        let energy_score = if energy > adaptive_threshold { 1.0 } else { 0.0 };

        let sf_score = if spectral_flatness < self.config.spectral_flatness_threshold {
            1.0
        } else {
            0.0
        };

        let zcr_score = if zcr > 0.05 && zcr < 0.5 { 1.0 } else { 0.0 };

        let combined_score = (energy_score * 0.5 + sf_score * 0.3 + zcr_score * 0.2);
        let is_voice = combined_score >= 0.5;

        if is_voice {
            self.consecutive_voice += 1;
            self.consecutive_silence = 0;
            self.last_voice_time = std::time::Instant::now();

            if self.consecutive_voice >= 2 || self.state == VadState::Voice {
                self.state = VadState::Voice;
            } else {
                self.state = VadState::Transition;
            }
        } else {
            self.consecutive_silence += 1;

            let hangover_frames = (self.config.hangover_ms / self.config.frame_duration_ms) as usize;

            if self.consecutive_silence >= hangover_frames {
                self.consecutive_voice = 0;
                self.state = VadState::Silence;
            } else if self.state == VadState::Voice {
                self.state = VadState::Transition;
            }
        }

        self.state
    }

    pub fn is_voice(&self) -> bool {
        self.state == VadState::Voice || self.state == VadState::Transition
    }

    pub fn reset(&mut self) {
        self.state = VadState::Silence;
        self.is_initialized = false;
        self.init_frames = 0;
        self.consecutive_silence = 0;
        self.consecutive_voice = 0;
        self.noise_floor = 1e-6;
    }

    pub fn set_aggressiveness(&mut self, level: u8) {
        self.config.aggressiveness = level.clamp(0, 3);
    }

    pub fn noise_floor(&self) -> f32 {
        self.noise_floor
    }

    pub fn snr_estimate(&self, signal_energy: f32) -> f32 {
        if self.noise_floor < 1e-10 {
            return 30.0;
        }
        10.0 * (signal_energy / self.noise_floor).log10()
    }
}

impl Clone for VoiceActivityDetector {
    fn clone(&self) -> Self {
        Self::new(self.config.clone())
    }
}
