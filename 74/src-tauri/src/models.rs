use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fmt;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceInfo {
    pub id: Uuid,
    pub vendor_id: u16,
    pub product_id: u16,
    pub path: String,
    pub manufacturer: Option<String>,
    pub product: Option<String>,
    pub serial_number: Option<String>,
    pub connected: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ScannerConfig {
    pub scan_mode: ScanMode,
    pub suffix_char: Option<String>,
    pub prefix_char: Option<String>,
    pub beeper_enabled: bool,
    pub led_color: LedColor,
}

impl ScannerConfig {
    pub fn serialized_size(&self) -> usize {
        let mut size = 0;
        size += 1;
        if let Some(ref s) = self.suffix_char {
            size += s.len();
        }
        if let Some(ref s) = self.prefix_char {
            size += s.len();
        }
        size += 1;
        size += 1;
        size
    }
}

impl Default for ScannerConfig {
    fn default() -> Self {
        Self {
            scan_mode: ScanMode::Continuous,
            suffix_char: Some("\r".to_string()),
            prefix_char: None,
            beeper_enabled: true,
            led_color: LedColor::Green,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum ScanMode {
    Manual,
    Continuous,
    Trigger,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum LedColor {
    Red,
    Green,
    Blue,
    Yellow,
    Off,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceCapabilities {
    pub max_input_report_size: usize,
    pub max_output_report_size: usize,
    pub max_feature_report_size: usize,
    pub max_prefix_length: usize,
    pub max_suffix_length: usize,
    pub supported_scan_modes: Vec<ScanMode>,
    pub supported_led_colors: Vec<LedColor>,
    pub raw_report_descriptor: Vec<u8>,
}

impl DeviceCapabilities {
    pub fn default_scanner() -> Self {
        Self {
            max_input_report_size: 64,
            max_output_report_size: 64,
            max_feature_report_size: 64,
            max_prefix_length: 32,
            max_suffix_length: 32,
            supported_scan_modes: vec![
                ScanMode::Manual,
                ScanMode::Continuous,
                ScanMode::Trigger,
            ],
            supported_led_colors: vec![
                LedColor::Red,
                LedColor::Green,
                LedColor::Blue,
                LedColor::Yellow,
                LedColor::Off,
            ],
            raw_report_descriptor: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PresetTemplate {
    pub name: String,
    pub description: String,
    pub config: ScannerConfig,
}

pub fn get_preset_templates() -> Vec<PresetTemplate> {
    vec![
        PresetTemplate {
            name: "仓储模式".to_string(),
            description: "适合仓库库存管理，连续扫描模式，支持快速条码录入".to_string(),
            config: ScannerConfig {
                scan_mode: ScanMode::Continuous,
                suffix_char: Some("\r".to_string()),
                prefix_char: None,
                beeper_enabled: true,
                led_color: LedColor::Green,
            },
        },
        PresetTemplate {
            name: "零售模式".to_string(),
            description: "适合零售POS收银，手动触发扫描，蜂鸣器提示".to_string(),
            config: ScannerConfig {
                scan_mode: ScanMode::Trigger,
                suffix_char: Some("\r\n".to_string()),
                prefix_char: None,
                beeper_enabled: true,
                led_color: LedColor::Blue,
            },
        },
        PresetTemplate {
            name: "医疗模式".to_string(),
            description: "适合医疗环境，静音模式，关闭蜂鸣器".to_string(),
            config: ScannerConfig {
                scan_mode: ScanMode::Trigger,
                suffix_char: Some("\r".to_string()),
                prefix_char: Some("M".to_string()),
                beeper_enabled: false,
                led_color: LedColor::Yellow,
            },
        },
    ]
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BackupData {
    pub timestamp: String,
    pub devices: HashMap<Uuid, DeviceBackup>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceBackup {
    pub device_info: DeviceInfo,
    pub config: ScannerConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WriteError {
    pub code: WriteErrorCode,
    pub message: String,
    pub details: Option<WriteErrorDetails>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum WriteErrorCode {
    ValidationFailed,
    LengthExceeded,
    DeviceWriteFailed,
    VerificationFailed,
    RollbackFailed,
    DeviceNotReady,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WriteErrorDetails {
    pub expected_size: usize,
    pub actual_size: usize,
    pub limit: usize,
    pub field_name: String,
    pub allowed_range: Option<String>,
    pub actual_value: Option<String>,
    pub retry_count: u32,
}

impl fmt::Display for WriteError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self.details {
            Some(d) => {
                write!(f, "{}", self.message)?;
                if d.actual_size > 0 || d.limit > 0 {
                    write!(
                        f,
                        "（配置参数长度{}字节超出设备限制{}字节）",
                        d.actual_size, d.limit
                    )?;
                }
                if d.retry_count > 0 {
                    write!(f, "（已重试{}次）", d.retry_count)?;
                }
                Ok(())
            }
            None => write!(f, "{}", self.message),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WriteResult {
    pub success: bool,
    pub message: String,
    pub config: Option<ScannerConfig>,
    pub error: Option<WriteError>,
    pub rolled_back: bool,
    pub retries_used: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ScanRecord {
    pub id: Uuid,
    pub device_id: Uuid,
    pub barcode: String,
    pub barcode_type: BarcodeType,
    pub timestamp: String,
    pub success: bool,
    pub duration_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum BarcodeType {
    EAN13,
    Code128,
    QRCode,
    Code39,
    EAN8,
    UPCA,
    UPCE,
    Code93,
    ITF,
    Codabar,
    DataMatrix,
    PDF417,
    Unknown,
}

impl BarcodeType {
    pub fn from_barcode(barcode: &str) -> Self {
        let len = barcode.len();
        let is_numeric = barcode.chars().all(|c| c.is_ascii_digit());

        if barcode.starts_with("http") || barcode.starts_with("www.") || len > 40 {
            return BarcodeType::QRCode;
        }

        if is_numeric && len == 13 {
            return BarcodeType::EAN13;
        }
        if is_numeric && len == 8 {
            return BarcodeType::EAN8;
        }
        if is_numeric && len == 12 {
            return BarcodeType::UPCA;
        }
        if is_numeric && len == 6 {
            return BarcodeType::UPCE;
        }

        let is_code128 = barcode.chars().all(|c| {
            c.is_ascii_alphanumeric() || " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~".contains(c)
        });
        if is_code128 && len >= 1 && len <= 80 {
            return BarcodeType::Code128;
        }

        BarcodeType::Unknown
    }

    pub fn display_name(&self) -> &str {
            match self {
            BarcodeType::EAN13 => "EAN-13",
            BarcodeType::Code128 => "Code 128",
            BarcodeType::QRCode => "QR Code",
            BarcodeType::Code39 => "Code 39",
            BarcodeType::EAN8 => "EAN-8",
            BarcodeType::UPCA => "UPC-A",
            BarcodeType::UPCE => "UPC-E",
            BarcodeType::Code93 => "Code 93",
            BarcodeType::ITF => "ITF",
            BarcodeType::Codabar => "Codabar",
            BarcodeType::DataMatrix => "Data Matrix",
            BarcodeType::PDF417 => "PDF417",
            BarcodeType::Unknown => "Unknown",
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ScanStatistics {
    pub total_count: u64,
    pub success_count: u64,
    pub failed_count: u64,
    pub success_rate: f64,
    pub avg_interval_ms: f64,
    pub today_count: u64,
    pub today_success: u64,
    pub today_failed: u64,
    pub today_target: u64,
    pub today_progress: f64,
    pub by_type: Vec<TypeStatistic>,
    pub by_hour: Vec<HourStatistic>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TypeStatistic {
    pub barcode_type: BarcodeType,
    pub type_name: String,
    pub count: u64,
    pub percentage: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HourStatistic {
    pub hour: u32,
    pub count: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DailyTarget {
    pub date: String,
    pub target: u64,
    pub current: u64,
    pub progress: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ExportConfig {
    pub daily_target: u64,
}

impl Default for ExportConfig {
    fn default() -> Self {
        Self { daily_target: 5000 }
    }
}
