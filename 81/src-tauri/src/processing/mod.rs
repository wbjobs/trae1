pub mod feature_extraction;
pub mod similarity;
pub mod custom_command;
pub mod lora;
pub mod noise_suppression;
pub mod vad;
pub mod multi_frame_voting;

pub use feature_extraction::{FeatureExtractor, FeatureVector, PhonemeFeatures};
pub use similarity::{SimilarityScorer, SimilarityResult};
pub use custom_command::{
    CustomCommand, CommandRegistry, EnrollmentStatus, EnrollmentSession,
    EnrollmentConfig, EnrollmentGuide, EnrollmentCaptureResult,
};
pub use lora::{LoraConfig, LoraAdapter, LoraManager, LoraTrainingSample};
pub use noise_suppression::{NoiseSuppressor, NoiseProfile, WienerFilter};
pub use vad::{VoiceActivityDetector, VadConfig, VadState};
pub use multi_frame_voting::{MultiFrameVoter, VotingConfig, VotingResult};
