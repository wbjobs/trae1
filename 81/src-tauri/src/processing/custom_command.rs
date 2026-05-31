use std::path::PathBuf;
use std::sync::Arc;

use parking_lot::Mutex;
use serde::{Deserialize, Serialize};

use crate::processing::{FeatureExtractor, PhonemeFeatures, SimilarityScorer, SimilarityResult};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CustomCommand {
    pub id: String,
    pub name: String,
    pub action: String,
    pub description: String,
    pub created_at: u64,
    pub updated_at: u64,
    pub is_active: bool,
    pub templates: Vec<PhonemeFeatures>,
    pub aggregate_features: Vec<f32>,
    pub sample_count: usize,
    pub avg_quality_score: f32,
    pub match_threshold: f32,
    pub trigger_count: u32,
    pub lora_trained: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum EnrollmentStatus {
    Idle,
    Recording(usize),
    Processing,
    CheckingQuality,
    Completed,
    Failed(String),
    Cancelled,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EnrollmentConfig {
    pub required_samples: usize,
    pub min_quality_score: f32,
    pub min_consistency_score: f32,
    pub sample_duration_ms: u64,
    pub silence_timeout_ms: u64,
    pub max_snr_db: f32,
    pub max_amplitude: f32,
}

impl Default for EnrollmentConfig {
    fn default() -> Self {
        Self {
            required_samples: 3,
            min_quality_score: 0.6,
            min_consistency_score: 0.7,
            sample_duration_ms: 2000,
            silence_timeout_ms: 1000,
            max_snr_db: 5.0,
            max_amplitude: 0.95,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EnrollmentGuide {
    pub step: usize,
    pub total_steps: usize,
    pub message: String,
    pub hint: String,
    pub progress: f32,
    pub can_continue: bool,
}

pub struct EnrollmentSession {
    pub command_name: String,
    pub command_action: String,
    pub command_description: String,
    pub samples: Vec<Vec<f32>>,
    pub sample_quality: Vec<f32>,
    pub status: EnrollmentStatus,
    pub config: EnrollmentConfig,
    pub feature_extractor: Arc<Mutex<FeatureExtractor>>,
    pub guide: EnrollmentGuide,
    pub started_at: std::time::Instant,
}

impl EnrollmentSession {
    pub fn new(
        command_name: String,
        command_action: String,
        command_description: String,
        config: EnrollmentConfig,
    ) -> Self {
        Self {
            command_name,
            command_action,
            command_description,
            samples: Vec::with_capacity(config.required_samples),
            sample_quality: Vec::with_capacity(config.required_samples),
            status: EnrollmentStatus::Idle,
            config,
            feature_extractor: Arc::new(Mutex::new(FeatureExtractor::new(16_000, 13))),
            guide: EnrollmentGuide {
                step: 0,
                total_steps: config.required_samples + 2,
                message: "准备开始录制".to_string(),
                hint: "请确保环境安静，发音清晰".to_string(),
                progress: 0.0,
                can_continue: true,
            },
            started_at: std::time::Instant::now(),
        }
    }

    pub fn start(&mut self) -> EnrollmentGuide {
        self.status = EnrollmentStatus::Recording(1);
        self.guide.step = 1;
        self.guide.message = format!("第 1 / {} 次录制", self.config.required_samples);
        self.guide.hint = "请清晰地说出命令词，语速适中".to_string();
        self.guide.progress = 0.1;
        self.guide.can_continue = true;
        self.guide.clone()
    }

    pub fn add_sample(&mut self, samples: Vec<f32>) -> Result<EnrollmentGuide, String> {
        let current_sample = self.samples.len() + 1;
        self.status = EnrollmentStatus::Processing;
        self.guide.message = "正在分析音频质量...".to_string();
        self.guide.can_continue = false;

        let quality_score = self
            .feature_extractor
            .lock()
            .compute_speech_quality_score(&samples);

        if quality_score < self.config.min_quality_score {
            let message = self.get_quality_issue_message(quality_score, &samples);
            self.status = EnrollmentStatus::Failed(message.clone());
            self.guide.message = message;
            self.guide.hint = "请改善录音环境后重试".to_string();
            self.guide.can_continue = false;
            return Ok(self.guide.clone());
        }

        if self.check_clipping(&samples) {
            self.status = EnrollmentStatus::Failed("音频出现削波，请减小音量".to_string());
            self.guide.message = "音频出现削波".to_string();
            self.guide.hint = "请离麦克风远一点或减小说话音量".to_string();
            self.guide.can_continue = false;
            return Ok(self.guide.clone());
        }

        self.samples.push(samples);
        self.sample_quality.push(quality_score);

        let current = self.samples.len();
        if current >= self.config.required_samples {
            self.status = EnrollmentStatus::CheckingQuality;
            self.guide.step = current + 1;
            self.guide.message = "正在验证发音一致性...".to_string();
            self.guide.progress = 0.8;

            let (consistency, _) = self
                .feature_extractor
                .lock()
                .check_pronunciation_consistency(&self.samples);

            if consistency < self.config.min_consistency_score {
                self.status = EnrollmentStatus::Failed(format!(
                    "发音一致性不足: {:.0}%，需要 {:.0}%",
                    consistency * 100.0,
                    self.config.min_consistency_score * 100.0
                ));
                self.guide.message = "发音一致性不足".to_string();
                self.guide.hint = "请每次以相同的语调和语速说出命令词".to_string();
                self.guide.can_continue = false;
                return Ok(self.guide.clone());
            }

            self.status = EnrollmentStatus::Completed;
            self.guide.step = self.guide.total_steps;
            self.guide.message = "注册成功！".to_string();
            self.guide.hint = format!(
                "已录制 {} 次，平均质量: {:.0}%，一致性: {:.0}%",
                self.samples.len(),
                self.sample_quality.iter().sum::<f32>() / self.sample_quality.len() as f32 * 100.0,
                consistency * 100.0
            );
            self.guide.progress = 1.0;
            self.guide.can_continue = true;
        } else {
            self.status = EnrollmentStatus::Recording(current + 1);
            self.guide.step = current + 1;
            self.guide.message = format!("第 {} / {} 次录制", current + 1, self.config.required_samples);
            self.guide.hint = format!(
                "很好！质量: {:.0}%，请再用相同方式说一次",
                quality_score * 100.0
            );
            self.guide.progress = (current as f32 + 1.0) / (self.config.required_samples as f32 + 2.0);
            self.guide.can_continue = true;
        }

        Ok(self.guide.clone())
    }

    pub fn cancel(&mut self) {
        self.status = EnrollmentStatus::Cancelled;
        self.samples.clear();
        self.sample_quality.clear();
        self.guide.message = "已取消注册".to_string();
        self.guide.can_continue = false;
    }

    pub fn build_command(&self) -> Result<CustomCommand, String> {
        if !matches!(self.status, EnrollmentStatus::Completed) {
            return Err("注册尚未完成".to_string());
        }
        if self.samples.len() < self.config.required_samples {
            return Err("样本数量不足".to_string());
        }

        let mut templates = Vec::with_capacity(self.samples.len());
        let mut all_aggregate = Vec::new();

        for sample in &self.samples {
            let phoneme_features = self
                .feature_extractor
                .lock()
                .extract_mfcc_sequence(sample);
            templates.push(phoneme_features);

            let agg = self
                .feature_extractor
                .lock()
                .extract_aggregate_features(sample);
            all_aggregate.push(agg);
        }

        let feature_dim = all_aggregate[0].len();
        let mut mean_aggregate = vec![0.0; feature_dim];
        for agg in &all_aggregate {
            for i in 0..feature_dim {
                mean_aggregate[i] += agg[i];
            }
        }
        for x in mean_aggregate.iter_mut() {
            *x /= all_aggregate.len() as f32;
        }

        let avg_quality = self.sample_quality.iter().sum::<f32>() / self.sample_quality.len() as f32;

        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        let id = format!("cmd_{}", now);

        Ok(CustomCommand {
            id,
            name: self.command_name.clone(),
            action: self.command_action.clone(),
            description: self.command_description.clone(),
            created_at: now,
            updated_at: now,
            is_active: true,
            templates,
            aggregate_features: mean_aggregate,
            sample_count: self.samples.len(),
            avg_quality_score: avg_quality,
            match_threshold: 0.7,
            trigger_count: 0,
            lora_trained: false,
        })
    }

    fn get_quality_issue_message(&self, score: f32, samples: &[f32]) -> String {
        let rms: f32 = samples.iter().map(|&x| x * x).sum::<f32>() / samples.len() as f32;
        let rms = rms.sqrt();

        if rms < 0.01 {
            "音量太小，请靠近麦克风".to_string()
        } else if rms > 0.8 {
            "音量太大，请离麦克风远一点".to_string()
        } else if score < 0.3 {
            "环境噪音太大，请找个安静的地方".to_string()
        } else {
            format!("音频质量不足: {:.0}%，请重试", score * 100.0)
        }
    }

    fn check_clipping(&self, samples: &[f32]) -> bool {
        samples.iter().any(|&x| x.abs() > self.config.max_amplitude)
    }

    pub fn check_environment_noise(&self, samples: &[f32]) -> f32 {
        let rms: f32 = samples.iter().map(|&x| x * x).sum::<f32>() / samples.len() as f32;
        let rms = rms.sqrt();

        let zero_crossings: usize = samples.windows(2)
            .filter(|w| (w[0] > 0.0 && w[1] <= 0.0) || (w[0] < 0.0 && w[1] >= 0.0))
            .count();
        let zcr = zero_crossings as f32 / samples.len() as f32 * 1000.0;

        let mut noise_level = rms.min(1.0);
        if zcr > 50.0 {
            noise_level += 0.1;
        }
        if zcr > 80.0 {
            noise_level += 0.1;
        }

        noise_level.min(1.0).max(0.0)
    }

    pub fn get_status(&self) -> EnrollmentStatus {
        self.status.clone()
    }

    pub fn get_current_step(&self) -> usize {
        self.samples.len()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EnrollmentCaptureResult {
    pub status: String,
    pub quality_score: f32,
    pub consistency_score: f32,
    pub duration_ms: u64,
    pub message: Option<String>,
}

pub struct CommandRegistry {
    commands: Vec<CustomCommand>,
    scorer: SimilarityScorer,
    feature_extractor: FeatureExtractor,
    storage_path: PathBuf,
}

impl CommandRegistry {
    pub fn new(storage_path: PathBuf) -> Self {
        let mut scorer = SimilarityScorer::default();
        scorer.set_combined_threshold(0.7);

        Self {
            commands: Vec::new(),
            scorer,
            feature_extractor: FeatureExtractor::new(16_000, 13),
            storage_path,
        }
    }

    pub fn load(&mut self) -> Result<(), String> {
        if !self.storage_path.exists() {
            return Ok(());
        }

        let data = std::fs::read_to_string(&self.storage_path)
            .map_err(|e| format!("读取命令词库失败: {}", e))?;

        if data.is_empty() {
            return Ok(());
        }

        self.commands = serde_json::from_str(&data)
            .map_err(|e| format!("解析命令词库失败: {}", e))?;

        log::info!("已加载 {} 个自定义命令词", self.commands.len());
        Ok(())
    }

    pub fn save(&self) -> Result<(), String> {
        if let Some(parent) = self.storage_path.parent() {
            std::fs::create_dir_all(parent).map_err(|e| format!("创建目录失败: {}", e))?;
        }

        let data = serde_json::to_string_pretty(&self.commands)
            .map_err(|e| format!("序列化命令词库失败: {}", e))?;

        std::fs::write(&self.storage_path, data)
            .map_err(|e| format!("保存命令词库失败: {}", e))?;

        Ok(())
    }

    pub fn add_command(&mut self, command: CustomCommand) -> Result<(), String> {
        if self.commands.iter().any(|c| c.name == command.name) {
            return Err(format!("命令词 '{}' 已存在", command.name));
        }

        self.commands.push(command);
        self.save()?;
        Ok(())
    }

    pub fn remove_command(&mut self, command_id: &str) -> Result<(), String> {
        let len_before = self.commands.len();
        self.commands.retain(|c| c.id != command_id);

        if self.commands.len() == len_before {
            return Err(format!("未找到命令词 ID: {}", command_id));
        }

        self.save()?;
        Ok(())
    }

    pub fn update_command(
        &mut self,
        command_id: &str,
        new_templates: Vec<PhonemeFeatures>,
    ) -> Result<(), String> {
        let command = self
            .commands
            .iter_mut()
            .find(|c| c.id == command_id)
            .ok_or_else(|| format!("未找到命令词 ID: {}", command_id))?;

        command.templates = new_templates;
        command.updated_at = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        self.save()?;
        Ok(())
    }

    pub fn get_commands(&self) -> Vec<CustomCommand> {
        self.commands.clone()
    }

    pub fn get_active_commands(&self) -> Vec<&CustomCommand> {
        self.commands.iter().filter(|c| c.is_active).collect()
    }

    pub fn recognize(&mut self, samples: &[f32]) -> Option<(CustomCommand, SimilarityResult)> {
        let input_features = self.feature_extractor.extract_mfcc_sequence(samples);

        let mut best_result: Option<(usize, SimilarityResult)> = None;
        let mut best_score = 0.0;

        for (idx, command) in self.commands.iter().enumerate() {
            if !command.is_active || command.templates.is_empty() {
                continue;
            }

            let mut scorer = self.scorer.clone();
            scorer.set_combined_threshold(command.match_threshold);

            let result = scorer.score_multiple_templates(&command.templates, &input_features);

            if result.matched && result.combined_score > best_score {
                best_score = result.combined_score;
                best_result = Some((idx, result));
            }
        }

        if let Some((idx, result)) = best_result {
            self.commands[idx].trigger_count = self.commands[idx].trigger_count.saturating_add(1);
            self.commands[idx].updated_at = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            let _ = self.save();
            Some((self.commands[idx].clone(), result))
        } else {
            None
        }
    }

    pub fn recognize_with_all_scores(&mut self, samples: &[f32]) -> Vec<(String, f32)> {
        let input_features = self.feature_extractor.extract_mfcc_sequence(samples);

        let mut scores: Vec<(String, f32)> = self
            .commands
            .iter()
            .filter(|c| c.is_active && !c.templates.is_empty())
            .map(|command| {
                let mut scorer = self.scorer.clone();
                scorer.set_combined_threshold(command.match_threshold);
                let result = scorer.score_multiple_templates(&command.templates, &input_features);
                (command.name.clone(), result.combined_score)
            })
            .collect();

        scores.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
        scores
    }

    pub fn set_match_threshold(&mut self, command_id: &str, threshold: f32) -> Result<(), String> {
        let command = self
            .commands
            .iter_mut()
            .find(|c| c.id == command_id)
            .ok_or_else(|| format!("未找到命令词 ID: {}", command_id))?;

        command.match_threshold = threshold.clamp(0.3, 0.95);
        self.save()?;
        Ok(())
    }

    pub fn set_command_active(&mut self, command_id: &str, active: bool) -> Result<(), String> {
        let command = self
            .commands
            .iter_mut()
            .find(|c| c.id == command_id)
            .ok_or_else(|| format!("未找到命令词 ID: {}", command_id))?;

        command.is_active = active;
        self.save()?;
        Ok(())
    }

    pub fn len(&self) -> usize {
        self.commands.len()
    }

    pub fn is_empty(&self) -> bool {
        self.commands.is_empty()
    }
}

impl Clone for CommandRegistry {
    fn clone(&self) -> Self {
        Self {
            commands: self.commands.clone(),
            scorer: self.scorer.clone(),
            feature_extractor: FeatureExtractor::new(16_000, 13),
            storage_path: self.storage_path.clone(),
        }
    }
}
