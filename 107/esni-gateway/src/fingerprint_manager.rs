use crate::cnn_model::{CnnInference, Prediction};
use crate::fingerprint::{ApplicationDatabase, FingerprintCache, FingerprintExtractor, TlsFingerprint};
use crate::metrics::METRICS;
use crate::online_learning::OnlineLearning;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;

pub struct FingerprintManager {
    cnn_inference: RwLock<CnnInference>,
    app_database: Arc<ApplicationDatabase>,
    cache: Arc<FingerprintCache>,
    online_learning: Arc<OnlineLearning>,
    confidence_threshold: f32,
    enabled: bool,
    use_cache: bool,
}

impl FingerprintManager {
    pub fn new() -> Self {
        Self {
            cnn_inference: RwLock::new(CnnInference::new()),
            app_database: Arc::new(ApplicationDatabase::new()),
            cache: Arc::new(FingerprintCache::new(10000)),
            online_learning: Arc::new(OnlineLearning::new(1000, 60)),
            confidence_threshold: 0.5,
            enabled: false,
            use_cache: true,
        }
    }

    pub fn enable(&mut self) {
        self.enabled = true;
    }

    pub fn disable(&mut self) {
        self.enabled = false;
    }

    pub fn set_confidence_threshold(&mut self, threshold: f32) {
        self.confidence_threshold = threshold;
    }

    pub fn set_use_cache(&mut self, use_cache: bool) {
        self.use_cache = use_cache;
    }

    pub fn load_model<P: AsRef<std::path::Path>>(&self, path: P) -> Result<(), String> {
        let mut inference = self.cnn_inference.write();
        inference.load_model(path)?;
        
        tracing::info!("Loaded TLS fingerprint model");
        Ok(())
    }

    pub fn load_model_from_bytes(&self, data: &[u8]) -> Result<(), String> {
        let mut inference = self.cnn_inference.write();
        inference.load_model_from_bytes(data)?;
        
        tracing::info!("Loaded TLS fingerprint model from bytes");
        Ok(())
    }

    pub fn load_app_database<P: AsRef<std::path::Path>>(&self, path: P) -> Result<(), String> {
        let count = self.app_database.load_from_file(path)?;
        tracing::info!("Loaded {} applications to database", count);
        Ok(())
    }

    pub fn set_class_names(&self, names: HashMap<u32, String>) {
        let mut inference = self.cnn_inference.write();
        inference.set_class_names(names);
    }

    pub fn identify(
        &self,
        data: &[u8],
        client_addr: &str,
    ) -> Option<FingerprintResult> {
        if !self.enabled {
            return None;
        }

        let fingerprint = match FingerprintExtractor::extract(data) {
            Ok(fp) => fp,
            Err(e) => {
                tracing::debug!("Failed to extract fingerprint: {}", e);
                METRICS.increment_fingerprint_extraction_failure();
                return None;
            }
        };

        let fp_hash = self.compute_fingerprint_hash(&fingerprint);

        if self.use_cache {
            if let Some(cached) = self.cache.get(&fp_hash) {
                METRICS.increment_fingerprint_cache_hit();
                return Some(FingerprintResult {
                    app_id: cached.app_id,
                    app_name: cached.app_name,
                    confidence: cached.confidence,
                    from_cache: true,
                });
            }
        }

        METRICS.increment_fingerprint_extraction();

        let features = FingerprintExtractor::to_feature_vector(&fingerprint);
        let feature_vector = self.features_to_vector(&features);

        let inference = self.cnn_inference.read();
        let predictions = inference.predict_top(&feature_vector, 5)?;

        if predictions.is_empty() {
            return None;
        }

        let top_pred = &predictions[0];

        if top_pred.confidence < self.confidence_threshold {
            METRICS.increment_fingerprint_low_confidence();
            return None;
        }

        let app_name = top_pred.class_name.clone();
        let app_id = top_pred.class_id;

        if self.use_cache {
            let cached = crate::fingerprint::CachedFingerprint {
                app_id: Some(app_id),
                app_name: Some(app_name.clone()),
                confidence: top_pred.confidence,
                timestamp: Instant::now(),
            };
            self.cache.put(fp_hash, cached);
        }

        self.online_learning.record_prediction(
            fp_hash,
            app_id,
            top_pred.confidence,
            client_addr.to_string(),
        );

        METRICS.increment_fingerprint_prediction(&top_pred.class_name);
        METRICS.observe_fingerprint_confidence(top_pred.confidence);

        Some(FingerprintResult {
            app_id,
            app_name,
            confidence: top_pred.confidence,
            from_cache: false,
        })
    }

    pub fn submit_correction(&self, fingerprint_hash: String, actual_class: u32) -> Result<(), String> {
        self.online_learning.submit_correction(fingerprint_hash, actual_class)
    }

    pub fn get_online_learning_stats(&self) -> (usize, usize, f32) {
        let stats = self.online_learning.get_stats();
        let accuracy = self.online_learning.get_accuracy();
        (stats.total_predictions, stats.mispredictions, accuracy)
    }

    fn compute_fingerprint_hash(&self, fingerprint: &TlsFingerprint) -> String {
        use std::collections::hash_map::DefaultHasher;
        use std::hash::{Hash, Hasher};
        
        let mut hasher = DefaultHasher::new();
        fingerprint.tls_version.hash(&mut hasher);
        for cs in &fingerprint.cipher_suites {
            cs.hash(&mut hasher);
        }
        for ext in &fingerprint.extensions {
            ext.hash(&mut hasher);
        }
        format!("{:x}", hasher.finish())
    }

    fn features_to_vector(&self, features: &crate::fingerprint::FingerprintFeature) -> Vec<f32> {
        let mut vector = Vec::with_capacity(490);
        
        vector.extend_from_slice(&features.cipher_suite_vector[..300.min(features.cipher_suite_vector.len())]);
        while vector.len() < 300 {
            vector.push(0.0);
        }
        
        vector.extend_from_slice(&features.extension_vector[..100.min(features.extension_vector.len())]);
        while vector.len() < 400 {
            vector.push(0.0);
        }
        
        vector.extend_from_slice(&features.elliptic_curve_vector[..30.min(features.elliptic_curve_vector.len())]);
        while vector.len() < 430 {
            vector.push(0.0);
        }
        
        vector.extend_from_slice(&features.signature_algorithm_vector[..50.min(features.signature_algorithm_vector.len())]);
        while vector.len() < 480 {
            vector.push(0.0);
        }
        
        vector.extend_from_slice(&features.compression_vector[..10.min(features.compression_vector.len())]);
        while vector.len() < 490 {
            vector.push(0.0);
        }
        
        vector.push(features.tls_version / 768.0);
        vector.push(features.session_id_len as f32 / 32.0);
        
        vector
    }
}

impl Default for FingerprintManager {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Debug, Clone)]
pub struct FingerprintResult {
    pub app_id: u32,
    pub app_name: String,
    pub confidence: f32,
    pub from_cache: bool,
}
