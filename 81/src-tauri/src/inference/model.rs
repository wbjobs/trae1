use std::time::Instant;

use ndarray::{Array2, Array4};
use ort::session::{Session, SessionBuilder};
use ort::GraphOptimizationLevel;
use parking_lot::Mutex;

use crate::audio::MfccExtractor;

pub const COMMAND_LABELS: [&str; 10] = [
    "开机",
    "关机",
    "调高音量",
    "调低音量",
    "下一首",
    "上一首",
    "暂停",
    "播放",
    "静音",
    "取消静音",
];

#[derive(Debug, Clone)]
pub struct CommandResult {
    pub label: String,
    pub label_index: usize,
    pub confidence: f32,
    pub all_confidences: Vec<(String, f32)>,
    pub inference_time_ms: u64,
    pub timestamp: Instant,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CommandLabel {
    PowerOn = 0,
    PowerOff = 1,
    VolumeUp = 2,
    VolumeDown = 3,
    NextTrack = 4,
    PreviousTrack = 5,
    Pause = 6,
    Play = 7,
    Mute = 8,
    Unmute = 9,
    Unknown = 10,
}

impl CommandLabel {
    pub fn from_index(idx: usize) -> Self {
        match idx {
            0 => CommandLabel::PowerOn,
            1 => CommandLabel::PowerOff,
            2 => CommandLabel::VolumeUp,
            3 => CommandLabel::VolumeDown,
            4 => CommandLabel::NextTrack,
            5 => CommandLabel::PreviousTrack,
            6 => CommandLabel::Pause,
            7 => CommandLabel::Play,
            8 => CommandLabel::Mute,
            9 => CommandLabel::Unmute,
            _ => CommandLabel::Unknown,
        }
    }

    pub fn to_str(&self) -> &str {
        match self {
            CommandLabel::PowerOn => "开机",
            CommandLabel::PowerOff => "关机",
            CommandLabel::VolumeUp => "调高音量",
            CommandLabel::VolumeDown => "调低音量",
            CommandLabel::NextTrack => "下一首",
            CommandLabel::PreviousTrack => "上一首",
            CommandLabel::Pause => "暂停",
            CommandLabel::Play => "播放",
            CommandLabel::Mute => "静音",
            CommandLabel::Unmute => "取消静音",
            CommandLabel::Unknown => "未知",
        }
    }

    pub fn to_smart_home_action(&self) -> &str {
        match self {
            CommandLabel::PowerOn => "power_on",
            CommandLabel::PowerOff => "power_off",
            CommandLabel::VolumeUp => "volume_up",
            CommandLabel::VolumeDown => "volume_down",
            CommandLabel::NextTrack => "next_track",
            CommandLabel::PreviousTrack => "previous_track",
            CommandLabel::Pause => "pause",
            CommandLabel::Play => "play",
            CommandLabel::Mute => "mute",
            CommandLabel::Unmute => "unmute",
            CommandLabel::Unknown => "unknown",
        }
    }
}

pub struct CommandModel {
    session: Mutex<Session>,
    mfcc_extractor: MfccExtractor,
    confidence_threshold: f32,
}

impl CommandModel {
    pub fn new(model_path: &str, confidence_threshold: f32) -> Result<Self, String> {
        let environment = ort::init();

        let session = SessionBuilder::new(&environment)
            .map_err(|e| format!("创建会话构建器失败: {}", e))?
            .with_model_from_file(model_path)
            .map_err(|e| format!("加载模型失败: {} - {}", model_path, e))?
            .with_optimization_level(GraphOptimizationLevel::Level3)
            .map_err(|e| format!("设置优化级别失败: {}", e))?
            .with_intra_threads(2)
            .map_err(|e| format!("设置线程数失败: {}", e))?
            .commit()
            .map_err(|e| format!("提交会话失败: {}", e))?;

        let mfcc_config = crate::audio::MfccConfig::default();
        let mfcc_extractor = MfccExtractor::new(mfcc_config);

        log::info!("命令词模型加载成功: {}", model_path);

        Ok(Self {
            session: Mutex::new(session),
            mfcc_extractor,
            confidence_threshold,
        })
    }

    pub fn set_confidence_threshold(&mut self, threshold: f32) {
        self.confidence_threshold = threshold;
    }

    pub fn confidence_threshold(&self) -> f32 {
        self.confidence_threshold
    }

    pub fn infer(&self, samples: &[f32]) -> Result<CommandResult, String> {
        let start = Instant::now();

        let mfcc_features = self.mfcc_extractor.extract_mfcc(samples);

        let mut flat_features = Vec::with_capacity(1 * 98 * 13 * 1);
        for frame in &mfcc_features {
            for &val in frame {
                flat_features.push(val);
            }
        }

        while flat_features.len() < 1 * 98 * 13 * 1 {
            flat_features.push(0.0);
        }

        let input_array =
            Array4::from_shape_vec((1, 98, 13, 1), flat_features)
                .map_err(|e| format!("构建输入张量失败: {}", e))?;

        let session = self.session.lock();
        let output: Vec<Array2<f32>> = session
            .run(ort::inputs![input_array])
            .map_err(|e| format!("模型推理失败: {}", e))?;

        let logits = output.first().ok_or_else(|| "模型无输出".to_string())?;
        let logits_vec: Vec<f32> = logits.iter().cloned().collect();

        let max_val = logits_vec
            .iter()
            .cloned()
            .fold(f32::NEG_INFINITY, f32::max);
        let exp_vals: Vec<f32> = logits_vec.iter().map(|&x| (x - max_val).exp()).collect();
        let sum: f32 = exp_vals.iter().sum();
        let probabilities: Vec<f32> = exp_vals.iter().map(|&x| x / sum).collect();

        let mut best_idx = 0;
        let mut best_prob = 0.0f32;
        for (i, &p) in probabilities.iter().enumerate() {
            if p > best_prob {
                best_prob = p;
                best_idx = i;
            }
        }

        let inference_time = start.elapsed().as_millis() as u64;

        let label_idx = if best_prob >= self.confidence_threshold {
            best_idx.min(9)
        } else {
            10
        };

        let label = if label_idx < COMMAND_LABELS.len() {
            COMMAND_LABELS[label_idx].to_string()
        } else {
            "未知".to_string()
        };

        let all_confidences: Vec<(String, f32)> = COMMAND_LABELS
            .iter()
            .enumerate()
            .map(|(i, &l)| (l.to_string(), probabilities[i]))
            .collect();

        Ok(CommandResult {
            label,
            label_index: label_idx,
            confidence: best_prob,
            all_confidences,
            inference_time_ms: inference_time,
            timestamp: start,
        })
    }
}
