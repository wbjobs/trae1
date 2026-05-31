use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConvLayer {
    pub input_channels: usize,
    pub output_channels: usize,
    pub kernel_size: usize,
    pub stride: usize,
    pub weights: Vec<f32>,
    pub biases: Vec<f32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FullyConnectedLayer {
    pub input_size: usize,
    pub output_size: usize,
    pub weights: Vec<f32>,
    pub biases: Vec<f32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModelWeights {
    pub conv_layers: Vec<ConvLayer>,
    pub fc_layers: Vec<FullyConnectedLayer>,
    pub num_classes: usize,
    pub input_shape: Vec<usize>,
}

pub struct CnnModel {
    weights: ModelWeights,
}

impl CnnModel {
    pub fn from_file<P: AsRef<std::path::Path>>(path: P) -> Result<Self, String> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| format!("Failed to read model file: {}", e))?;
        
        let weights: ModelWeights = serde_json::from_str(&content)
            .map_err(|e| format!("Failed to parse model: {}", e))?;
        
        Ok(Self { weights })
    }

    pub fn from_bytes(data: &[u8]) -> Result<Self, String> {
        let weights: ModelWeights = serde_json::from_slice(data)
            .map_err(|e| format!("Failed to deserialize model: {}", e))?;
        
        Ok(Self { weights })
    }

    pub fn predict(&self, input: &[f32]) -> Vec<f32> {
        let mut output = input.to_vec();
        
        for conv in &self.weights.conv_layers {
            output = Self::conv1d(&output, conv);
            output = Self::relu(&output);
            output = Self::max_pool1d(&output, 2);
        }
        
        for fc in &self.weights.fc_layers {
            output = Self::dense(&output, fc);
            if fc.output_size != self.weights.num_classes {
                output = Self::relu(&output);
            }
        }
        
        output = Self::softmax(&output);
        output
    }

    fn conv1d(input: &[f32], layer: &ConvLayer) -> Vec<f32> {
        let output_len = (input.len() - layer.kernel_size) / layer.stride + 1;
        let mut output = vec![0.0; layer.output_channels * output_len];
        
        for out_ch in 0..layer.output_channels {
            for i in 0..output_len {
                let mut sum = layer.biases[out_ch];
                for in_ch in 0..layer.input_channels {
                    for k in 0..layer.kernel_size {
                        let input_idx = in_ch * input.len() / layer.input_channels + i * layer.stride + k;
                        let weight_idx = out_ch * layer.input_channels * layer.kernel_size 
                                        + in_ch * layer.kernel_size + k;
                        if input_idx < input.len() && weight_idx < layer.weights.len() {
                            sum += input[input_idx] * layer.weights[weight_idx];
                        }
                    }
                }
                output[out_ch * output_len + i] = sum;
            }
        }
        
        output
    }

    fn relu(input: &[f32]) -> Vec<f32> {
        input.iter().map(|x| x.max(0.0)).collect()
    }

    fn max_pool1d(input: &[f32], pool_size: usize) -> Vec<f32> {
        let output_len = input.len() / pool_size;
        let mut output = vec![0.0; output_len];
        
        for i in 0..output_len {
            let start = i * pool_size;
            let end = start + pool_size;
            output[i] = input[start..end].iter().cloned().fold(f32::MIN, f32::max);
        }
        
        output
    }

    fn dense(input: &[f32], layer: &FullyConnectedLayer) -> Vec<f32> {
        let mut output = vec![0.0; layer.output_size];
        
        for i in 0..layer.output_size {
            let mut sum = layer.biases[i];
            for j in 0..layer.input_size {
                if j < input.len() && i * layer.input_size + j < layer.weights.len() {
                    sum += input[j] * layer.weights[i * layer.input_size + j];
                }
            }
            output[i] = sum;
        }
        
        output
    }

    fn softmax(input: &[f32]) -> Vec<f32> {
        let max_val = input.iter().cloned().fold(f32::MIN, f32::max);
        let exp_sum: f32 = input.iter().map(|x| (x - max_val).exp()).sum();
        
        input.iter().map(|x| (x - max_val).exp() / exp_sum).collect()
    }

    pub fn get_num_classes(&self) -> usize {
        self.weights.num_classes
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Prediction {
    pub class_id: u32,
    pub class_name: String,
    pub confidence: f32,
}

pub struct CnnInference {
    model: Option<CnnModel>,
    class_names: HashMap<u32, String>,
}

impl CnnInference {
    pub fn new() -> Self {
        Self {
            model: None,
            class_names: Self::default_class_names(),
        }
    }

    pub fn load_model<P: AsRef<std::path::Path>>(&mut self, model_path: P) -> Result<(), String> {
        self.model = Some(CnnModel::from_file(model_path)?);
        Ok(())
    }

    pub fn load_model_from_bytes(&mut self, data: &[u8]) -> Result<(), String> {
        self.model = Some(CnnModel::from_bytes(data)?);
        Ok(())
    }

    pub fn set_class_names(&mut self, names: HashMap<u32, String>) {
        self.class_names = names;
    }

    pub fn predict(&self, features: &[f32]) -> Option<Vec<Prediction>> {
        let model = self.model.as_ref()?;
        
        let scores = model.predict(features);
        
        let mut predictions: Vec<Prediction> = scores
            .iter()
            .enumerate()
            .map(|(id, &conf)| Prediction {
                class_id: id as u32,
                class_name: self.class_names.get(&(id as u32)).cloned().unwrap_or_else(|| format!("unknown_{}", id)),
                confidence: conf,
            })
            .collect();
        
        predictions.sort_by(|a, b| b.confidence.partial_cmp(&a.confidence).unwrap());
        
        Some(predictions)
    }

    pub fn predict_top(&self, features: &[f32], top_k: usize) -> Option<Vec<Prediction>> {
        self.predict(features).map(|mut preds| {
            preds.truncate(top_k);
            preds
        })
    }

    pub fn is_loaded(&self) -> bool {
        self.model.is_some()
    }

    fn default_class_names() -> HashMap<u32, String> {
        let mut names = HashMap::new();
        names.insert(0, "google".to_string());
        names.insert(1, "facebook".to_string());
        names.insert(2, "twitter".to_string());
        names.insert(3, "netflix".to_string());
        names.insert(4, "amazon".to_string());
        names.insert(5, "apple".to_string());
        names.insert(6, "microsoft".to_string());
        names.insert(7, "cloudflare".to_string());
        names.insert(8, "unknown".to_string());
        names
    }
}

impl Default for CnnInference {
    fn default() -> Self {
        Self::new()
    }
}
