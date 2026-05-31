use std::path::PathBuf;
use std::sync::Arc;

use parking_lot::Mutex;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoraConfig {
    pub rank: usize,
    pub alpha: f32,
    pub dropout: f32,
    pub learning_rate: f32,
    pub lora_a_init_std: f32,
    pub adapter_input_dim: usize,
    pub adapter_hidden_dim: usize,
    pub num_classes: usize,
}

impl Default for LoraConfig {
    fn default() -> Self {
        Self {
            rank: 8,
            alpha: 16.0,
            dropout: 0.1,
            learning_rate: 0.001,
            lora_a_init_std: 0.01,
            adapter_input_dim: 39,
            adapter_hidden_dim: 128,
            num_classes: 10,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoraAdapter {
    pub id: String,
    pub name: String,
    pub config: LoraConfig,
    pub lora_a: Vec<Vec<f32>>,
    pub lora_b: Vec<Vec<f32>>,
    pub base_weights: Vec<f32>,
    pub scaling: f32,
    pub is_trained: bool,
    pub trained_samples: usize,
    pub avg_loss: f32,
    pub created_at: u64,
    pub updated_at: u64,
}

impl LoraAdapter {
    pub fn new(id: String, name: String, config: LoraConfig) -> Self {
        let scaling = config.alpha / config.rank as f32;

        let mut lora_a = vec![vec![0.0; config.rank]; config.adapter_hidden_dim];
        for i in 0..config.adapter_hidden_dim {
            for j in 0..config.rank {
                lora_a[i][j] = rand_normal(0.0, config.lora_a_init_std);
            }
        }

        let lora_b = vec![vec![0.0; config.num_classes]; config.rank];

        let base_weights = vec![0.0; config.adapter_hidden_dim * config.num_classes];

        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        Self {
            id,
            name,
            config,
            lora_a,
            lora_b,
            base_weights,
            scaling,
            is_trained: false,
            trained_samples: 0,
            avg_loss: f32::INFINITY,
            created_at: now,
            updated_at: now,
        }
    }

    pub fn forward(&self, features: &[f32]) -> Vec<f32> {
        if features.len() < self.config.adapter_input_dim {
            return vec![0.0; self.config.num_classes];
        }

        let mut hidden = vec![0.0; self.config.adapter_hidden_dim];
        for i in 0..self.config.adapter_hidden_dim {
            let mut sum = 0.0;
            for j in 0..self.config.adapter_input_dim {
                sum += features[j] * 0.0;
            }
            hidden[i] = sum.tanh();
        }

        for i in 0..self.config.adapter_hidden_dim {
            let mut sum = 0.0;
            for j in 0..self.config.adapter_input_dim.min(features.len()) {
                sum += features[j] * (if i < features.len() { features[j] } else { 0.0 });
            }
            hidden[i] = sum.tanh();
        }

        let mut lora_out = vec![0.0; self.config.num_classes];

        for i in 0..self.config.rank {
            let mut a_sum = 0.0;
            for j in 0..self.config.adapter_hidden_dim {
                a_sum += hidden[j] * self.lora_a[j][i];
            }

            for k in 0..self.config.num_classes {
                lora_out[k] += a_sum * self.lora_b[i][k];
            }
        }

        let alpha = self.scaling;
        lora_out.iter().map(|&x| x * alpha).collect()
    }

    pub fn merge_with_logits(&self, base_logits: &[f32], lora_logits: &[f32]) -> Vec<f32> {
        let weight = if self.is_trained { 0.3 } else { 0.0 };

        base_logits
            .iter()
            .zip(lora_logits.iter())
            .enumerate()
            .map(|(i, (&b, &l))| {
                if i < self.config.num_classes {
                    b * (1.0 - weight) + l * weight
                } else {
                    b
                }
            })
            .collect()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoraTrainingSample {
    pub features: Vec<f32>,
    pub label_index: usize,
    pub confidence: f32,
    pub custom_command_id: Option<String>,
}

pub struct LoraManager {
    adapters: Vec<LoraAdapter>,
    active_adapter: Option<String>,
    storage_path: PathBuf,
    training_samples: Vec<LoraTrainingSample>,
    max_training_samples: usize,
}

impl LoraManager {
    pub fn new(storage_path: PathBuf) -> Self {
        let mut manager = Self {
            adapters: Vec::new(),
            active_adapter: None,
            storage_path,
            training_samples: Vec::new(),
            max_training_samples: 1000,
        };

        let _ = manager.load();
        manager
    }

    pub fn load(&mut self) -> Result<(), String> {
        if !self.storage_path.exists() {
            return Ok(());
        }

        let data = std::fs::read_to_string(&self.storage_path)
            .map_err(|e| format!("读取LoRA适配器失败: {}", e))?;

        if data.is_empty() {
            return Ok(());
        }

        self.adapters = serde_json::from_str(&data)
            .map_err(|e| format!("解析LoRA适配器失败: {}", e))?;

        log::info!("已加载 {} 个LoRA适配器", self.adapters.len());
        Ok(())
    }

    pub fn save(&self) -> Result<(), String> {
        if let Some(parent) = self.storage_path.parent() {
            std::fs::create_dir_all(parent).map_err(|e| format!("创建目录失败: {}", e))?;
        }

        let data = serde_json::to_string_pretty(&self.adapters)
            .map_err(|e| format!("序列化LoRA适配器失败: {}", e))?;

        std::fs::write(&self.storage_path, data)
            .map_err(|e| format!("保存LoRA适配器失败: {}", e))?;

        Ok(())
    }

    pub fn create_adapter(&mut self, name: String, num_custom_classes: usize) -> Result<LoraAdapter, String> {
        let config = LoraConfig {
            num_classes: 10 + num_custom_classes,
            ..LoraConfig::default()
        };

        let id = format!("lora_{}", std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs());

        let adapter = LoraAdapter::new(id.clone(), name, config);
        self.adapters.push(adapter.clone());
        self.active_adapter = Some(id);
        self.save()?;

        Ok(adapter)
    }

    pub fn add_training_sample(&mut self, sample: LoraTrainingSample) {
        self.training_samples.push(sample);
        if self.training_samples.len() > self.max_training_samples {
            self.training_samples.remove(0);
        }
    }

    pub fn train_adapter(
        &mut self,
        adapter_id: &str,
        epochs: usize,
    ) -> Result<(f32, usize), String> {
        let adapter = self
            .adapters
            .iter_mut()
            .find(|a| a.id == adapter_id)
            .ok_or_else(|| format!("未找到LoRA适配器 ID: {}", adapter_id))?;

        if self.training_samples.len() < 10 {
            return Err(format!(
                "训练样本不足，需要至少10个，当前只有{}个",
                self.training_samples.len()
            ));
        }

        let mut total_loss = 0.0;
        let mut trained_batches = 0;
        let lr = adapter.config.learning_rate;
        let scaling = adapter.scaling;

        for epoch in 0..epochs {
            let mut epoch_loss = 0.0;
            let batch_size = 8;

            for batch_start in (0..self.training_samples.len()).step_by(batch_size) {
                let batch_end = (batch_start + batch_size).min(self.training_samples.len());
                let batch = &self.training_samples[batch_start..batch_end];

                let mut grad_a = vec![vec![0.0; adapter.config.rank]; adapter.config.adapter_hidden_dim];
                let mut grad_b = vec![vec![0.0; adapter.config.num_classes]; adapter.config.rank];

                for sample in batch {
                    let features = &sample.features;
                    let label = sample.label_index.min(adapter.config.num_classes - 1);
                    let sample_weight = sample.confidence.max(0.5);

                    let hidden: Vec<f32> = (0..adapter.config.adapter_hidden_dim)
                        .map(|i| {
                            features
                                .get(i % features.len())
                                .copied()
                                .unwrap_or(0.0)
                                .tanh()
                        })
                        .collect();

                    let mut lora_output = vec![0.0; adapter.config.num_classes];
                    for r in 0..adapter.config.rank {
                        let mut a_sum = 0.0;
                        for h in 0..adapter.config.adapter_hidden_dim {
                            a_sum += hidden[h] * adapter.lora_a[h][r];
                        }

                        for c in 0..adapter.config.num_classes {
                            lora_output[c] += a_sum * adapter.lora_b[r][c] * scaling;
                        }
                    }

                    let softmax_output = softmax(&lora_output);

                    for c in 0..adapter.config.num_classes {
                        let target = if c == label { 1.0 } else { 0.0 };
                        let error = softmax_output[c] - target;
                        let weighted_error = error * sample_weight;

                        for r in 0..adapter.config.rank {
                            let mut a_sum = 0.0;
                            for h in 0..adapter.config.adapter_hidden_dim {
                                a_sum += hidden[h] * adapter.lora_a[h][r];
                            }

                            grad_b[r][c] += weighted_error * a_sum * scaling;

                            for h in 0..adapter.config.adapter_hidden_dim {
                                grad_a[h][r] += weighted_error * adapter.lora_b[r][c] * hidden[h] * scaling;
                            }
                        }
                    }

                    let loss = -softmax_output[label].ln().max(-10.0);
                    epoch_loss += loss * sample_weight;
                }

                let norm_factor = 1.0 / batch.len() as f32;
                for h in 0..adapter.config.adapter_hidden_dim {
                    for r in 0..adapter.config.rank {
                        adapter.lora_a[h][r] -= lr * grad_a[h][r] * norm_factor;
                    }
                }
                for r in 0..adapter.config.rank {
                    for c in 0..adapter.config.num_classes {
                        adapter.lora_b[r][c] -= lr * grad_b[r][c] * norm_factor;
                    }
                }

                trained_batches += 1;
            }

            epoch_loss /= self.training_samples.len() as f32;
            total_loss += epoch_loss;

            log::debug!(
                "LoRA训练 Epoch {}/{} 平均损失: {:.6}",
                epoch + 1,
                epochs,
                epoch_loss
            );
        }

        adapter.is_trained = true;
        adapter.trained_samples = self.training_samples.len();
        adapter.avg_loss = total_loss / epochs as f32;
        adapter.updated_at = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        self.save()?;

        Ok((adapter.avg_loss, trained_batches))
    }

    pub fn forward_with_adapter(
        &self,
        features: &[f32],
        base_logits: &[f32],
    ) -> Vec<f32> {
        let active_id = match &self.active_adapter {
            Some(id) => id,
            None => return base_logits.to_vec(),
        };

        let adapter = match self.adapters.iter().find(|a| &a.id == active_id) {
            Some(a) => a,
            None => return base_logits.to_vec(),
        };

        if !adapter.is_trained {
            return base_logits.to_vec();
        }

        let lora_logits = adapter.forward(features);
        adapter.merge_with_logits(base_logits, &lora_logits)
    }

    pub fn get_adapters(&self) -> Vec<LoraAdapter> {
        self.adapters.clone()
    }

    pub fn delete_adapter(&mut self, adapter_id: &str) -> Result<(), String> {
        let len_before = self.adapters.len();
        self.adapters.retain(|a| a.id != adapter_id);

        if self.adapters.len() == len_before {
            return Err(format!("未找到LoRA适配器 ID: {}", adapter_id));
        }

        if self.active_adapter.as_deref() == Some(adapter_id) {
            self.active_adapter = None;
        }

        self.save()?;
        Ok(())
    }

    pub fn set_active_adapter(&mut self, adapter_id: Option<String>) -> Result<(), String> {
        if let Some(id) = &adapter_id {
            if !self.adapters.iter().any(|a| &a.id == id) {
                return Err(format!("未找到LoRA适配器 ID: {}", id));
            }
        }
        self.active_adapter = adapter_id;
        Ok(())
    }

    pub fn clear_training_samples(&mut self) {
        self.training_samples.clear();
    }

    pub fn training_sample_count(&self) -> usize {
        self.training_samples.len()
    }
}

impl Clone for LoraManager {
    fn clone(&self) -> Self {
        Self {
            adapters: self.adapters.clone(),
            active_adapter: self.active_adapter.clone(),
            storage_path: self.storage_path.clone(),
            training_samples: self.training_samples.clone(),
            max_training_samples: self.max_training_samples,
        }
    }
}

fn rand_normal(mean: f32, std: f32) -> f32 {
    let mut u = 0.0f32;
    let mut v = 0.0f32;

    while u <= std::f32::EPSILON {
        u = rand::random::<f32>();
    }
    while v <= std::f32::EPSILON {
        v = rand::random::<f32>();
    }

    let z = (-2.0 * u.ln()).sqrt() * (2.0 * std::f32::consts::PI * v).cos();
    mean + z * std
}

fn softmax(x: &[f32]) -> Vec<f32> {
    let max_val = x.iter().cloned().fold(f32::NEG_INFINITY, f32::max);
    let exps: Vec<f32> = x.iter().map(|&s| (s - max_val).exp()).collect();
    let sum: f32 = exps.iter().sum();
    exps.iter().map(|&e| e / sum).collect()
}
