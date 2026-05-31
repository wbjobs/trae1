#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

mod audio;
mod inference;
mod processing;
mod state;
mod websocket;

use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;

use audio::{AudioCapture, AudioConfig, AudioFrame};
use inference::{CommandModel, CommandResult, HotwordDetector};
use processing::{
    CommandRegistry, CustomCommand, EnrollmentConfig, EnrollmentGuide, EnrollmentSession,
    EnrollmentStatus, FeatureExtractor, LoraConfig, LoraManager, LoraTrainingSample,
    MultiFrameVoter, NoiseSuppressor, SimilarityScorer, VadConfig, VoiceActivityDetector,
    VotingConfig,
};
use state::{AppState, RecognitionHistory};
use websocket::SmartHomeClient;

const MODEL_PATH: &str = "models/command_model.onnx";
const DEFAULT_WS_URL: &str = "ws://localhost:8765";
const CONFIDENCE_THRESHOLD: f32 = 0.7;
const HOTWORD_THRESHOLD: f32 = 0.7;
const WAKE_DURATION_MS: u64 = 10_000;

const VOTING_MIN_FRAMES: usize = 3;
const VOTING_MIN_CONSENSUS: f32 = 0.6;
const VOTING_MIN_AVG_CONFIDENCE: f32 = 0.7;

const CUSTOM_COMMAND_THRESHOLD: f32 = 0.75;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct RecognitionEvent {
    label: String,
    label_index: usize,
    confidence: f32,
    inference_time_ms: u64,
    all_confidences: Vec<(String, f32)>,
    is_custom_command: bool,
    custom_action: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct DenoiseStatus {
    enabled: bool,
    noise_learned: bool,
    snr_estimate: f32,
    learning: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct CustomCommandInfo {
    id: String,
    name: String,
    action: String,
    description: String,
    sample_count: usize,
    avg_quality_score: f32,
    match_threshold: f32,
    is_active: bool,
    created_at: u64,
    trigger_count: u32,
    lora_trained: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct CompleteEnrollmentResult {
    success: bool,
    message: Option<String>,
    suggestion: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct EnrollmentStatusInfo {
    current_step: usize,
    total_steps: usize,
    status: EnrollmentStatus,
    quality_scores: Vec<f32>,
}

struct Engine {
    audio_capture: Option<AudioCapture>,
    command_model: Arc<Mutex<Option<CommandModel>>>,
    hotword_detector: HotwordDetector,
    smart_home_client: SmartHomeClient,
    app_state: Arc<AppState>,
    awake_until: Arc<Mutex<Option<std::time::Instant>>>,
    noise_suppressor: Arc<Mutex<NoiseSuppressor>>,
    vad: Arc<Mutex<VoiceActivityDetector>>,
    multi_frame_voter: Arc<Mutex<MultiFrameVoter>>,
    denoise_enabled: bool,
    voting_enabled: bool,
    base_confidence_threshold: f32,
    command_registry: Arc<Mutex<CommandRegistry>>,
    feature_extractor: Arc<Mutex<FeatureExtractor>>,
    similarity_scorer: SimilarityScorer,
    enrollment_session: Option<EnrollmentSession>,
    lora_manager: Arc<Mutex<LoraManager>>,
    custom_commands_enabled: bool,
    auto_lora_training: bool,
    enrollment_buffer: Arc<Mutex<Option<Vec<f32>>>>,
}

impl Engine {
    fn new(noise_profile_path: Option<String>) -> Self {
        let app_state = Arc::new(AppState::new(200));
        let smart_home_client = SmartHomeClient::new(DEFAULT_WS_URL.to_string());
        let hotword_detector = HotwordDetector::new(HOTWORD_THRESHOLD);
        let noise_suppressor = Arc::new(Mutex::new(NoiseSuppressor::new(16_000)));
        let vad = Arc::new(Mutex::new(VoiceActivityDetector::new(VadConfig::default())));
        let voting_config = VotingConfig {
            min_frames: VOTING_MIN_FRAMES,
            max_frames: 5,
            min_consensus: VOTING_MIN_CONSENSUS,
            min_avg_confidence: VOTING_MIN_AVG_CONFIDENCE,
            timeout_ms: 2000,
        };
        let multi_frame_voter = Arc::new(Mutex::new(MultiFrameVoter::new(voting_config)));

        let data_dir = dirs_next::data_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("voice_command");
        let command_registry = Arc::new(Mutex::new(CommandRegistry::new(
            data_dir.join("custom_commands.json"),
        )));
        let _ = command_registry.lock().load();

        let lora_manager = Arc::new(Mutex::new(LoraManager::new(
            data_dir.join("lora_adapters.json"),
        )));

        let feature_extractor = Arc::new(Mutex::new(FeatureExtractor::new(16_000, 13)));
        let similarity_scorer = SimilarityScorer::with_thresholds(
            0.7,
            50.0,
            CUSTOM_COMMAND_THRESHOLD,
        );

        let mut engine = Self {
            audio_capture: None,
            command_model: Arc::new(Mutex::new(None)),
            hotword_detector,
            smart_home_client,
            app_state,
            awake_until: Arc::new(Mutex::new(None)),
            noise_suppressor,
            vad,
            multi_frame_voter,
            denoise_enabled: true,
            voting_enabled: true,
            base_confidence_threshold: CONFIDENCE_THRESHOLD,
            command_registry,
            feature_extractor,
            similarity_scorer,
            enrollment_session: None,
            lora_manager,
            custom_commands_enabled: true,
            auto_lora_training: true,
            enrollment_buffer: Arc::new(Mutex::new(None)),
        };

        if let Some(path) = noise_profile_path {
            if std::path::Path::new(&path).exists() {
                if let Ok(data) = std::fs::read_to_string(&path) {
                    if let Ok(profile) = serde_json::from_str(&data) {
                        engine.noise_suppressor.lock().load_noise_profile(profile);
                    }
                }
            }
        }

        engine
    }

    fn load_model(&mut self, model_path: &str) -> Result<(), String> {
        if !std::path::Path::new(model_path).exists() {
            log::warn!("模型文件不存在: {}", model_path);
            return Err(format!("模型文件不存在: {}", model_path));
        }

        let model = CommandModel::new(model_path, self.base_confidence_threshold)?;
        *self.command_model.lock() = Some(model);
        self.app_state.set_model_loaded(true);
        Ok(())
    }

    fn start_enrollment(
        &mut self,
        name: String,
        action: String,
        description: String,
        config: Option<EnrollmentConfig>,
    ) -> Result<EnrollmentGuide, String> {
        if self.enrollment_session.is_some() {
            return Err("已有注册会话进行中".to_string());
        }

        if self
            .command_registry
            .lock()
            .get_commands()
            .iter()
            .any(|c| c.name == name)
        {
            return Err(format!("命令词 '{}' 已存在", name));
        }

        let config = config.unwrap_or_default();
        let mut session = EnrollmentSession::new(name, action, description, config);
        let guide = session.start();

        self.enrollment_session = Some(session);
        self.app_state.set_recording(true);

        Ok(guide)
    }

    fn add_enrollment_sample(&mut self, samples: Vec<f32>) -> Result<EnrollmentGuide, String> {
        let session = self
            .enrollment_session
            .as_mut()
            .ok_or_else(|| "没有进行中的注册会话".to_string())?;

        session.add_sample(samples)
    }

    fn cancel_enrollment(&mut self) {
        if let Some(mut session) = self.enrollment_session.take() {
            session.cancel();
        }
    }

    fn complete_enrollment(&mut self) -> Result<CustomCommandInfo, String> {
        let session = self
            .enrollment_session
            .take()
            .ok_or_else(|| "没有进行中的注册会话".to_string())?;

        let command = session.build_command()?;
        let info = CustomCommandInfo {
            id: command.id.clone(),
            name: command.name.clone(),
            action: command.action.clone(),
            description: command.description.clone(),
            sample_count: command.sample_count,
            avg_quality_score: command.avg_quality_score,
            match_threshold: command.match_threshold,
            is_active: command.is_active,
            created_at: command.created_at,
            trigger_count: command.trigger_count,
            lora_trained: command.lora_trained,
        };

        self.command_registry.lock().add_command(command.clone())?;

        if self.auto_lora_training {
            let custom_count = self.command_registry.lock().len();
            let mut lora = self.lora_manager.lock();
            if lora.get_adapters().is_empty() {
                let adapter = lora.create_adapter("Custom Commands".to_string(), custom_count)?;
                lora.set_active_adapter(Some(adapter.id))?;
            }

            for sample in &session.samples {
                let features = self
                    .feature_extractor
                    .lock()
                    .extract_aggregate_features(sample);
                let lora_sample = LoraTrainingSample {
                    features,
                    label_index: 10 + custom_count.saturating_sub(1),
                    confidence: session.avg_quality_score(),
                    custom_command_id: Some(command.id.clone()),
                };
                lora.add_training_sample(lora_sample);
            }

            if lora.training_sample_count() >= 15 {
                let active_id = lora
                    .get_adapters()
                    .first()
                    .map(|a| a.id.clone());
                if let Some(id) = active_id {
                    if let Ok((loss, _)) = lora.train_adapter(&id, 5) {
                        log::info!("LoRA增量训练完成，平均损失: {:.6}", loss);
                    }
                }
            }
        }

        Ok(info)
    }

    fn get_enrollment_status(&self) -> Option<EnrollmentStatusInfo> {
        self.enrollment_session.as_ref().map(|s| EnrollmentStatusInfo {
            current_step: s.get_current_step(),
            total_steps: s.config.required_samples,
            status: s.status.clone(),
            quality_scores: s.sample_quality.clone(),
        })
    }

    fn list_custom_commands(&self) -> Vec<CustomCommandInfo> {
        self.command_registry
            .lock()
            .get_commands()
            .iter()
            .map(|c| CustomCommandInfo {
                id: c.id.clone(),
                name: c.name.clone(),
                action: c.action.clone(),
                description: c.description.clone(),
                sample_count: c.sample_count,
                avg_quality_score: c.avg_quality_score,
                match_threshold: c.match_threshold,
                is_active: c.is_active,
                created_at: c.created_at,
                trigger_count: c.trigger_count,
                lora_trained: c.lora_trained,
            })
            .collect()
    }

    fn delete_custom_command(&mut self, command_id: &str) -> Result<(), String> {
        self.command_registry.lock().remove_command(command_id)
    }

    fn re_enroll_command(
        &mut self,
        command_id: &str,
        samples: Vec<Vec<f32>>,
    ) -> Result<(), String> {
        let mut features = Vec::with_capacity(samples.len());
        for sample in &samples {
            let phoneme_features = self
                .feature_extractor
                .lock()
                .extract_mfcc_sequence(sample);
            features.push(phoneme_features);
        }

        self.command_registry
            .lock()
            .update_command(command_id, features)
    }

    fn set_command_threshold(&mut self, command_id: &str, threshold: f32) -> Result<(), String> {
        self.command_registry
            .lock()
            .set_match_threshold(command_id, threshold)
    }

    fn set_command_active(&mut self, command_id: &str, active: bool) -> Result<(), String> {
        self.command_registry
            .lock()
            .set_command_active(command_id, active)
    }

    fn start_audio_capture(&mut self) -> Result<(), String> {
        let config = AudioConfig::default();
        let mut capture = AudioCapture::new(config)?;

        let (frame_tx, mut frame_rx) = mpsc::channel::<AudioFrame>(10);
        let (hotword_tx, mut hotword_rx) = mpsc::channel::<AudioFrame>(5);

        capture.set_frame_sender(frame_tx);
        capture.set_hotword_sender(hotword_tx);
        capture.start()?;

        self.app_state.set_recording(true);

        let app_state = Arc::clone(&self.app_state);
        let command_model = Arc::clone(&self.command_model);
        let smart_home = self.smart_home_client.clone();
        let awake_until = Arc::clone(&self.awake_until);
        let noise_suppressor = Arc::clone(&self.noise_suppressor);
        let vad = Arc::clone(&self.vad);
        let multi_frame_voter = Arc::clone(&self.multi_frame_voter);
        let command_registry = Arc::clone(&self.command_registry);
        let feature_extractor = Arc::clone(&self.feature_extractor);
        let lora_manager = Arc::clone(&self.lora_manager);
        let denoise_enabled = self.denoise_enabled;
        let voting_enabled = self.voting_enabled;
        let base_threshold = self.base_confidence_threshold;
        let custom_enabled = self.custom_commands_enabled;
        let enrollment_buffer = Arc::clone(&self.enrollment_buffer);

        tauri::async_runtime::spawn(async move {
            while let Some(frame) = frame_rx.recv().await {
                if enrollment_buffer.lock().is_some() {
                    let mut buffer = enrollment_buffer.lock();
                    if let Some(ref mut buf) = *buffer {
                        buf.extend_from_slice(&frame.samples);
                    }
                    continue;
                }

                let is_awake = {
                    let awake_until_guard = awake_until.lock();
                    match *awake_until_guard {
                        Some(until) => std::time::Instant::now() < until,
                        None => false,
                    }
                };

                if !is_awake {
                    continue;
                }

                let vad_state = vad.lock().process(&frame.samples);
                if !matches!(
                    vad_state,
                    processing::VadState::Voice | processing::VadState::Transition
                ) {
                    continue;
                }

                let processed_samples = if denoise_enabled {
                    noise_suppressor.lock().process(&frame.samples)
                } else {
                    frame.samples.clone()
                };

                let dynamic_threshold = if denoise_enabled {
                    noise_suppressor
                        .lock()
                        .get_dynamic_confidence_threshold(base_threshold, &processed_samples)
                } else {
                    base_threshold
                };

                let custom_result = if custom_enabled {
                    command_registry.lock().recognize(&processed_samples)
                } else {
                    None
                };

                if let Some((custom_cmd, sim_result)) = custom_result {
                    if sim_result.combined_score >= custom_cmd.match_threshold
                        && sim_result.combined_score >= dynamic_threshold
                    {
                        let label_index = 10 + command_registry
                            .lock()
                            .get_commands()
                            .iter()
                            .position(|c| c.id == custom_cmd.id)
                            .unwrap_or(0);

                        let all_scores = command_registry
                            .lock()
                            .recognize_with_all_scores(&processed_samples);

                        let custom_labels: Vec<String> = command_registry
                            .lock()
                            .get_commands()
                            .iter()
                            .map(|c| c.name.clone())
                            .collect();

                        let mut all_confidences: Vec<(String, f32)> = inference::COMMAND_LABELS
                            .iter()
                            .map(|&l| (l.to_string(), 0.0))
                            .collect();

                        for (name, score) in all_scores {
                            all_confidences.push((name, score));
                        }

                        let result = CommandResult {
                            label: custom_cmd.name.clone(),
                            label_index,
                            confidence: sim_result.combined_score,
                            all_confidences: all_confidences.clone(),
                            inference_time_ms: 20,
                            timestamp: std::time::Instant::now(),
                        };

                        let history = RecognitionHistory {
                            label: custom_cmd.name.clone(),
                            confidence: sim_result.combined_score,
                            inference_time_ms: 20,
                            timestamp: std::time::SystemTime::now()
                                .duration_since(std::time::UNIX_EPOCH)
                                .unwrap_or_default()
                                .as_secs(),
                            sent_to_smart_home: smart_home.send_custom_command(
                                &custom_cmd.name,
                                &custom_cmd.action,
                                sim_result.combined_score,
                            ),
                        };
                        app_state.add_history(history);
                        app_state.set_current_result(result);

                        if !smart_home.is_connected() {
                            log::info!(
                                "识别到自定义命令: {} (动作: {})，置信度: {:.0}%",
                                custom_cmd.name,
                                custom_cmd.action,
                                sim_result.combined_score * 100.0
                            );
                        }

                        if auto_lora_training {
                            let features = feature_extractor
                                .lock()
                                .extract_aggregate_features(&processed_samples);
                            let lora_sample = LoraTrainingSample {
                                features,
                                label_index,
                                confidence: sim_result.combined_score,
                                custom_command_id: Some(custom_cmd.id.clone()),
                            };
                            lora_manager.lock().add_training_sample(lora_sample);
                        }

                        continue;
                    }
                }

                let model_guard = command_model.lock();
                if let Some(ref model) = *model_guard {
                    match model.infer(&processed_samples) {
                        Ok(mut result) => {
                            result.confidence = result.confidence.min(0.99);

                            let features = feature_extractor
                                .lock()
                                .extract_aggregate_features(&processed_samples);

                            let enhanced_logits = lora_manager
                                .lock()
                                .forward_with_adapter(
                                    &features,
                                    &result
                                        .all_confidences
                                        .iter()
                                        .map(|(_, c)| *c)
                                        .collect::<Vec<_>>(),
                                );

                            for (i, logit) in enhanced_logits.iter().enumerate() {
                                if i < result.all_confidences.len() {
                                    result.all_confidences[i].1 = *logit;
                                }
                            }

                            if result.label_index < 10 {
                                let mut max_logit = 0.0f32;
                                let mut max_idx = result.label_index;
                                for (i, (_, c)) in result.all_confidences.iter().enumerate() {
                                    if i < 10 && *c > max_logit {
                                        max_logit = *c;
                                        max_idx = i;
                                    }
                                }
                                result.label_index = max_idx;
                                result.label = inference::COMMAND_LABELS[max_idx].to_string();
                                result.confidence = max_logit;
                            }

                            let final_result = if voting_enabled {
                                let vote_result = multi_frame_voter.lock().add_vote(&result);

                                if vote_result.accepted
                                    && multi_frame_voter.lock().can_trigger()
                                {
                                    multi_frame_voter.lock().mark_triggered();

                                    let sent = if vote_result.avg_confidence
                                        >= dynamic_threshold
                                    {
                                        smart_home.send_command(&result)
                                    } else {
                                        false
                                    };

                                    let history = RecognitionHistory {
                                        label: vote_result.final_label.clone(),
                                        confidence: vote_result.avg_confidence,
                                        inference_time_ms: result.inference_time_ms,
                                        timestamp: std::time::SystemTime::now()
                                            .duration_since(std::time::UNIX_EPOCH)
                                            .unwrap_or_default()
                                            .as_secs(),
                                        sent_to_smart_home: sent,
                                    };
                                    app_state.add_history(history);

                                    let mut voted_result = result.clone();
                                    voted_result.label = vote_result.final_label;
                                    voted_result.label_index = vote_result.final_label_index;
                                    voted_result.confidence = vote_result.avg_confidence;
                                    app_state.set_current_result(voted_result);
                                }

                                result
                            } else {
                                let sent = if result.confidence >= dynamic_threshold {
                                    smart_home.send_command(&result)
                                } else {
                                    false
                                };

                                let history = RecognitionHistory {
                                    label: result.label.clone(),
                                    confidence: result.confidence,
                                    inference_time_ms: result.inference_time_ms,
                                    timestamp: std::time::SystemTime::now()
                                        .duration_since(std::time::UNIX_EPOCH)
                                        .unwrap_or_default()
                                        .as_secs(),
                                    sent_to_smart_home: sent,
                                };
                                app_state.add_history(history);
                                app_state.set_current_result(result.clone());

                                result
                            };
                        }
                        Err(e) => {
                            log::error!("命令词推理失败: {}", e);
                        }
                    }
                }
            }
        });

        let app_state_hw = Arc::clone(&self.app_state);
        let hotword_detector = self.hotword_detector.clone();
        let awake_until_hw = Arc::clone(&self.awake_until);
        let noise_suppressor_hw = Arc::clone(&self.noise_suppressor);
        let denoise_enabled_hw = self.denoise_enabled;

        tauri::async_runtime::spawn(async move {
            while let Some(frame) = hotword_rx.recv().await {
                let processed = if denoise_enabled_hw {
                    noise_suppressor_hw.lock().process(&frame.samples)
                } else {
                    frame.samples.clone()
                };

                let result = hotword_detector.detect(&processed);
                app_state_hw.set_last_hotword(result.clone());

                if result.detected {
                    log::info!("检测到热词，置信度: {:.2}", result.confidence);
                    let mut awake_until_guard = awake_until_hw.lock();
                    *awake_until_guard =
                        Some(std::time::Instant::now() + Duration::from_millis(WAKE_DURATION_MS));
                    drop(awake_until_guard);
                    app_state_hw.set_awake(true);
                } else {
                    let awake_until_guard = awake_until_hw.lock();
                    if let Some(until) = *awake_until_guard {
                        if std::time::Instant::now() >= until {
                            drop(awake_until_guard);
                            app_state_hw.set_awake(false);
                        }
                    }
                }
            }
        });

        self.audio_capture = Some(capture);
        Ok(())
    }

    fn stop_audio_capture(&mut self) {
        if let Some(mut capture) = self.audio_capture.take() {
            capture.stop();
        }
        self.app_state.set_recording(false);
        self.app_state.set_awake(false);
        self.multi_frame_voter.lock().reset();
    }

    fn start_noise_learning(&mut self) {
        self.noise_suppressor.lock().start_learning();
        log::info!("开始环境噪声学习，请保持安静...");
    }

    fn stop_noise_learning(&mut self) -> processing::NoiseProfile {
        self.noise_suppressor.lock().stop_learning();
        self.noise_suppressor.lock().get_noise_profile().clone()
    }

    fn get_denoise_status(&self) -> DenoiseStatus {
        let ns = self.noise_suppressor.lock();
        DenoiseStatus {
            enabled: self.denoise_enabled,
            noise_learned: ns.get_noise_profile().is_learned(),
            snr_estimate: ns.get_noise_profile().snr_estimate,
            learning: ns.is_learning(),
        }
    }

    fn set_denoise_enabled(&mut self, enabled: bool) {
        self.denoise_enabled = enabled;
        log::info!("降噪功能: {}", if enabled { "启用" } else { "禁用" });
    }

    fn set_voting_enabled(&mut self, enabled: bool) {
        self.voting_enabled = enabled;
        log::info!("多帧投票: {}", if enabled { "启用" } else { "禁用" });
    }

    fn set_voting_config(&mut self, min_frames: usize, min_consensus: f32, min_avg_confidence: f32) {
        let config = VotingConfig {
            min_frames,
            max_frames: (min_frames + 2).min(8),
            min_consensus,
            min_avg_confidence,
            timeout_ms: 2000,
        };
        self.multi_frame_voter.lock().set_config(config);
        log::info!(
            "投票配置: min_frames={}, min_consensus={}, min_avg_confidence={}",
            min_frames,
            min_consensus,
            min_avg_confidence
        );
    }

    fn start_enrollment_capture(&mut self) {
        *self.enrollment_buffer.lock() = Some(Vec::new());
    }

    fn stop_enrollment_capture(&mut self) -> Result<EnrollmentCaptureResult, String> {
        let buffer = self.enrollment_buffer.lock().take();
        if let Some(samples) = buffer {
            let duration_ms = (samples.len() as u64 * 1000) / 16_000;

            let session = self
                .enrollment_session
                .as_mut()
                .ok_or_else(|| "没有进行中的注册会话".to_string())?;

            let quality_score = session
                .feature_extractor
                .lock()
                .compute_speech_quality_score(&samples);

            if quality_score < session.config.min_quality_score {
                let message = session.get_quality_issue_message(quality_score, &samples);
                return Ok(EnrollmentCaptureResult {
                    status: "retry".to_string(),
                    quality_score,
                    consistency_score: 0.0,
                    duration_ms,
                    message: Some(message),
                });
            }

            if session.check_clipping(&samples) {
                return Ok(EnrollmentCaptureResult {
                    status: "retry".to_string(),
                    quality_score,
                    consistency_score: 0.0,
                    duration_ms,
                    message: Some("音频出现削波，请减小音量".to_string()),
                });
            }

            let guide = session.add_sample(samples)?;

            let mut consistency_score = 0.0;
            if session.samples.len() >= 2 {
                let (consistency, _) = session
                    .feature_extractor
                    .lock()
                    .check_pronunciation_consistency(&session.samples);
                consistency_score = consistency;
            }

            if guide.can_continue && matches!(session.status, EnrollmentStatus::Completed) {
                Ok(EnrollmentCaptureResult {
                    status: "success".to_string(),
                    quality_score,
                    consistency_score,
                    duration_ms,
                    message: Some("录制完成".to_string()),
                })
            } else if guide.can_continue {
                Ok(EnrollmentCaptureResult {
                    status: "success".to_string(),
                    quality_score,
                    consistency_score,
                    duration_ms,
                    message: None,
                })
            } else {
                Ok(EnrollmentCaptureResult {
                    status: "retry".to_string(),
                    quality_score,
                    consistency_score,
                    duration_ms,
                    message: Some(guide.message),
                })
            }
        } else {
            Err("没有录制到音频".to_string())
        }
    }

    fn check_environment_noise(&self) -> Result<f32, String> {
        if let Some(buffer) = self.enrollment_buffer.lock().as_ref() {
            if buffer.is_empty() {
                return Ok(0.0);
            }

            if let Some(session) = &self.enrollment_session {
                Ok(session.check_environment_noise(buffer))
            } else {
                let rms: f32 = buffer.iter().map(|&x| x * x).sum::<f32>() / buffer.len() as f32;
                Ok(rms.sqrt().min(1.0))
            }
        } else {
            Err("没有录制音频数据".to_string())
        }
    }

    fn train_lora(&mut self, epochs: usize) -> Result<(f32, usize), String> {
        let mut lora = self.lora_manager.lock();
        let adapters = lora.get_adapters();

        if adapters.is_empty() {
            let custom_count = self.command_registry.lock().len();
            let adapter = lora.create_adapter("Custom Commands".to_string(), custom_count)?;
            lora.set_active_adapter(Some(adapter.id.clone()))?;
            lora.train_adapter(&adapter.id, epochs)
        } else {
            let adapter_id = adapters[0].id.clone();
            lora.train_adapter(&adapter_id, epochs)
        }
    }
}

impl EnrollmentSession {
    fn avg_quality_score(&self) -> f32 {
        if self.sample_quality.is_empty() {
            0.0
        } else {
            self.sample_quality.iter().sum::<f32>() / self.sample_quality.len() as f32
        }
    }
}

struct EngineHandle(Mutex<Engine>);

#[tauri::command]
fn load_model(engine: tauri::State<EngineHandle>, model_path: Option<String>) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    let path = model_path.unwrap_or_else(|| MODEL_PATH.to_string());

    match engine_guard.load_model(&path) {
        Ok(_) => {
            log::info!("模型加载成功: {}", path);
            Ok(())
        }
        Err(e) => {
            log::warn!("模型加载失败: {}", e);
            Ok(())
        }
    }
}

#[tauri::command]
fn start_recording(engine: tauri::State<EngineHandle>) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.start_audio_capture()?;
    Ok(())
}

#[tauri::command]
fn stop_recording(engine: tauri::State<EngineHandle>) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.stop_audio_capture();
    Ok(())
}

#[tauri::command]
fn get_status(engine: tauri::State<EngineHandle>) -> Result<state::AppStatus, String> {
    let engine_guard = engine.0.lock();
    Ok(engine_guard.app_state.get_status())
}

#[tauri::command]
fn get_history(
    engine: tauri::State<EngineHandle>,
    limit: Option<usize>,
) -> Result<Vec<RecognitionHistory>, String> {
    let engine_guard = engine.0.lock();
    Ok(engine_guard.app_state.get_history(limit.unwrap_or(50)))
}

#[tauri::command]
fn clear_history(engine: tauri::State<EngineHandle>) -> Result<(), String> {
    let engine_guard = engine.0.lock();
    engine_guard.app_state.clear_history();
    Ok(())
}

#[tauri::command]
fn set_confidence_threshold(
    engine: tauri::State<EngineHandle>,
    threshold: f32,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.base_confidence_threshold = threshold;
    let model_guard = engine_guard.command_model.lock();
    if let Some(ref model) = *model_guard {
        drop(model_guard);
        drop(engine_guard);
        log::info!("置信度阈值已设置为: {}", threshold);
    } else {
        drop(model_guard);
        drop(engine_guard);
        log::info!("模型未加载，阈值将在加载后生效: {}", threshold);
    }
    Ok(())
}

#[tauri::command]
fn set_hotword_threshold(
    engine: tauri::State<EngineHandle>,
    threshold: f32,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.hotword_detector.set_threshold(threshold);
    log::info!("热词检测阈值已设置为: {}", threshold);
    Ok(())
}

#[tauri::command]
fn connect_smart_home(
    engine: tauri::State<EngineHandle>,
    url: Option<String>,
) -> Result<(), String> {
    let engine_guard = engine.0.lock();
    let ws_url = url.unwrap_or_else(|| DEFAULT_WS_URL.to_string());
    engine_guard.smart_home_client = SmartHomeClient::new(ws_url);
    engine_guard.smart_home_client.connect();
    Ok(())
}

#[tauri::command]
fn disconnect_smart_home(engine: tauri::State<EngineHandle>) -> Result<(), String> {
    let engine_guard = engine.0.lock();
    engine_guard.smart_home_client.disconnect();
    Ok(())
}

#[tauri::command]
fn get_smart_home_status(engine: tauri::State<EngineHandle>) -> Result<bool, String> {
    let engine_guard = engine.0.lock();
    Ok(engine_guard.smart_home_client.is_connected())
}

#[tauri::command]
fn simulate_command(
    engine: tauri::State<EngineHandle>,
    label: String,
    confidence: f32,
) -> Result<(), String> {
    let engine_guard = engine.0.lock();
    let label_index = inference::COMMAND_LABELS
        .iter()
        .position(|&l| l == label)
        .unwrap_or(10);

    let result = CommandResult {
        label: label.clone(),
        label_index,
        confidence,
        all_confidences: inference::COMMAND_LABELS
            .iter()
            .map(|&l| (l.to_string(), if l == label { confidence } else { 0.0 }))
            .collect(),
        inference_time_ms: 42,
        timestamp: std::time::Instant::now(),
    };

    let sent = engine_guard.smart_home_client.send_command(&result);
    let history = RecognitionHistory {
        label: result.label.clone(),
        confidence: result.confidence,
        inference_time_ms: result.inference_time_ms,
        timestamp: std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs(),
        sent_to_smart_home: sent,
    };
    engine_guard.app_state.add_history(history);
    engine_guard.app_state.set_current_result(result);

    Ok(())
}

#[tauri::command]
fn get_available_models() -> Result<Vec<String>, String> {
    let models_dir = PathBuf::from("models");
    let mut models = Vec::new();

    if models_dir.exists() {
        if let Ok(entries) = std::fs::read_dir(&models_dir) {
            for entry in entries.flatten() {
                if let Some(name) = entry.file_name().to_str() {
                    if name.ends_with(".onnx") {
                        models.push(name.to_string());
                    }
                }
            }
        }
    }

    Ok(models)
}

#[tauri::command]
fn start_noise_learning(engine: tauri::State<EngineHandle>) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.start_noise_learning();
    Ok(())
}

#[tauri::command]
fn stop_noise_learning(
    engine: tauri::State<EngineHandle>,
    save_path: Option<String>,
) -> Result<processing::NoiseProfile, String> {
    let mut engine_guard = engine.0.lock();
    let profile = engine_guard.stop_noise_learning();

    if let Some(path) = save_path {
        if let Ok(json) = serde_json::to_string(&profile) {
            let _ = std::fs::write(&path, json);
            log::info!("噪声配置已保存到: {}", path);
        }
    }

    Ok(profile)
}

#[tauri::command]
fn get_denoise_status(engine: tauri::State<EngineHandle>) -> Result<DenoiseStatus, String> {
    let engine_guard = engine.0.lock();
    Ok(engine_guard.get_denoise_status())
}

#[tauri::command]
fn set_denoise_enabled(
    engine: tauri::State<EngineHandle>,
    enabled: bool,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.set_denoise_enabled(enabled);
    Ok(())
}

#[tauri::command]
fn set_voting_enabled(
    engine: tauri::State<EngineHandle>,
    enabled: bool,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.set_voting_enabled(enabled);
    Ok(())
}

#[tauri::command]
fn set_voting_config(
    engine: tauri::State<EngineHandle>,
    min_frames: usize,
    min_consensus: f32,
    min_avg_confidence: f32,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.set_voting_config(min_frames, min_consensus, min_avg_confidence);
    Ok(())
}

#[tauri::command]
fn get_snr_estimate(
    engine: tauri::State<EngineHandle>,
    samples: Vec<f32>,
) -> Result<f32, String> {
    let engine_guard = engine.0.lock();
    Ok(engine_guard.noise_suppressor.lock().estimate_snr(&samples))
}

#[tauri::command]
fn start_enrollment(
    engine: tauri::State<EngineHandle>,
    name: String,
    action: String,
    description: Option<String>,
    config: Option<EnrollmentConfig>,
) -> Result<EnrollmentGuide, String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.start_enrollment(name, action, description.unwrap_or_default(), config)
}

#[tauri::command]
fn start_enrollment_capture(engine: tauri::State<EngineHandle>) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.start_enrollment_capture();
    Ok(())
}

#[tauri::command]
fn stop_enrollment_capture(engine: tauri::State<EngineHandle>) -> Result<EnrollmentCaptureResult, String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.stop_enrollment_capture()
}

#[tauri::command]
fn cancel_enrollment(engine: tauri::State<EngineHandle>) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.cancel_enrollment();
    Ok(())
}

#[tauri::command]
fn check_environment_noise(engine: tauri::State<EngineHandle>) -> Result<NoiseCheckResult, String> {
    let engine_guard = engine.0.lock();
    let noise_level = engine_guard.check_environment_noise()?;
    Ok(NoiseCheckResult {
        noise_level,
        acceptable: noise_level < 0.5,
        suggestion: if noise_level > 0.5 {
            "环境噪声过高，请换一个安静的环境".to_string()
        } else if noise_level > 0.3 {
            "环境噪声中等，请尽量靠近麦克风".to_string()
        } else {
            "环境噪声良好".to_string()
        },
    })
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct NoiseCheckResult {
    noise_level: f32,
    acceptable: bool,
    suggestion: String,
}

#[tauri::command]
fn complete_enrollment(engine: tauri::State<EngineHandle>) -> Result<CustomCommandInfo, String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.complete_enrollment()
}

#[tauri::command]
fn get_enrollment_status(
    engine: tauri::State<EngineHandle>,
) -> Result<Option<EnrollmentStatusInfo>, String> {
    let engine_guard = engine.0.lock();
    if let Some(session) = &engine_guard.enrollment_session {
        Ok(Some(EnrollmentStatusInfo {
            current_step: session.get_current_step(),
            total_steps: session.config.required_samples,
            status: session.get_status(),
            quality_scores: session.sample_quality.clone(),
        }))
    } else {
        Ok(None)
    }
}

#[tauri::command]
fn list_custom_commands(
    engine: tauri::State<EngineHandle>,
) -> Result<Vec<CustomCommandInfo>, String> {
    let engine_guard = engine.0.lock();
    Ok(engine_guard.list_custom_commands())
}

#[tauri::command]
fn delete_custom_command(
    engine: tauri::State<EngineHandle>,
    command_id: String,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.delete_custom_command(&command_id)
}

#[tauri::command]
fn set_command_threshold(
    engine: tauri::State<EngineHandle>,
    command_id: String,
    threshold: f32,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.set_command_threshold(&command_id, threshold)
}

#[tauri::command]
fn set_command_active(
    engine: tauri::State<EngineHandle>,
    command_id: String,
    active: bool,
) -> Result<(), String> {
    let mut engine_guard = engine.0.lock();
    engine_guard.set_command_active(&command_id, active)
}

#[tauri::command]
fn train_lora(
    engine: tauri::State<EngineHandle>,
    command_id: Option<String>,
    epochs: Option<usize>,
) -> Result<LoraTrainResult, String> {
    let mut engine_guard = engine.0.lock();

    let (final_loss, training_samples) = engine_guard.train_lora(epochs.unwrap_or(10))?;

    if let Some(cmd_id) = command_id {
        let mut registry = engine_guard.command_registry.lock();
        for cmd in registry.commands.iter_mut() {
            if cmd.id == cmd_id {
                cmd.lora_trained = true;
                cmd.updated_at = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
            }
        }
        let _ = registry.save();
    } else {
        let mut registry = engine_guard.command_registry.lock();
        for cmd in registry.commands.iter_mut() {
            cmd.lora_trained = true;
            cmd.updated_at = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
        }
        let _ = registry.save();
    }

    Ok(LoraTrainResult {
        success: true,
        training_samples,
        final_loss,
        message: None,
    })
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct LoraTrainResult {
    success: bool,
    training_samples: usize,
    final_loss: f32,
    message: Option<String>,
}

#[tauri::command]
fn get_lora_status(engine: tauri::State<EngineHandle>) -> Result<(usize, bool), String> {
    let engine_guard = engine.0.lock();
    let lora = engine_guard.lora_manager.lock();
    let adapters = lora.get_adapters();
    let has_trained = adapters.iter().any(|a| a.is_trained);
    Ok((adapters.len(), has_trained))
}

fn parse_args() -> Option<String> {
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        if args[i] == "--noise-profile" && i + 1 < args.len() {
            return Some(args[i + 1].clone());
        }
        i += 1;
    }
    None
}

fn main() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();

    let noise_profile_path = parse_args();
    if noise_profile_path.is_some() {
        log::info!("检测到 --noise-profile 参数");
    }

    let engine = EngineHandle(Mutex::new(Engine::new(noise_profile_path)));

    tauri::Builder::default()
        .manage(engine)
        .invoke_handler(tauri::generate_handler![
            load_model,
            start_recording,
            stop_recording,
            get_status,
            get_history,
            clear_history,
            set_confidence_threshold,
            set_hotword_threshold,
            connect_smart_home,
            disconnect_smart_home,
            get_smart_home_status,
            simulate_command,
            get_available_models,
            start_noise_learning,
            stop_noise_learning,
            get_denoise_status,
            set_denoise_enabled,
            set_voting_enabled,
            set_voting_config,
            get_snr_estimate,
            start_enrollment,
            start_enrollment_capture,
            stop_enrollment_capture,
            cancel_enrollment,
            check_environment_noise,
            complete_enrollment,
            get_enrollment_status,
            list_custom_commands,
            delete_custom_command,
            set_command_threshold,
            set_command_active,
            train_lora,
            get_lora_status,
        ])
        .run(tauri::generate_context!())
        .expect("运行语音命令识别应用失败");
}
