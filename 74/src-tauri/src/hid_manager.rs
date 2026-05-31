use crate::models::{
    DeviceCapabilities, DeviceInfo, ScannerConfig, WriteError, WriteErrorCode, WriteErrorDetails,
    WriteResult,
};
use hidapi::{HidApi, HidDevice};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};
use uuid::Uuid;

const MAX_RETRIES: u32 = 3;
const RETRY_DELAY_MS: u64 = 100;
const HID_REPORT_ID: u8 = 0x01;
const CONFIG_REPORT_ID: u8 = 0x02;

pub struct HidManager {
    api: Option<HidApi>,
    connected_devices: HashMap<Uuid, ConnectedDevice>,
    max_devices: usize,
}

struct ConnectedDevice {
    device_info: DeviceInfo,
    config: ScannerConfig,
    capabilities: DeviceCapabilities,
    device: Arc<Mutex<HidDevice>>,
}

impl HidManager {
    pub fn new() -> Self {
        Self {
            api: None,
            connected_devices: HashMap::new(),
            max_devices: 5,
        }
    }

    fn ensure_api(&mut self) -> Result<(), String> {
        if self.api.is_none() {
            self.api = Some(
                HidApi::new().map_err(|e| format!("Failed to initialize HID API: {}", e))?,
            );
        }
        Ok(())
    }

    pub fn list_devices(&mut self) -> Result<Vec<DeviceInfo>, String> {
        self.ensure_api()?;
        let api = self.api.as_ref().unwrap();

        api.refresh_devices()
            .map_err(|e| format!("Failed to refresh devices: {}", e))?;

        let devices: Vec<DeviceInfo> = api
            .device_list()
            .map(|device_info| {
                let connected = self.connected_devices.values().any(|d| {
                    d.device_info.vendor_id == device_info.vendor_id()
                        && d.device_info.product_id == device_info.product_id()
                        && d.device_info.path
                            == device_info.path().to_string_lossy().to_string()
                });

                DeviceInfo {
                    id: Uuid::new_v4(),
                    vendor_id: device_info.vendor_id(),
                    product_id: device_info.product_id(),
                    path: device_info.path().to_string_lossy().to_string(),
                    manufacturer: device_info.manufacturer_string().map(|s| s.to_string()),
                    product: device_info.product_string().map(|s| s.to_string()),
                    serial_number: device_info.serial_number_string().map(|s| s.to_string()),
                    connected,
                }
            })
            .collect();

        Ok(devices)
    }

    pub fn connect(
        &mut self,
        vendor_id: u16,
        product_id: u16,
        path: &str,
    ) -> Result<DeviceInfo, String> {
        if self.connected_devices.len() >= self.max_devices {
            return Err(format!("已达到最大设备连接数 ({})", self.max_devices));
        }

        self.ensure_api()?;
        let api = self.api.as_ref().unwrap();

        let device_info_opt = api.device_list().find(|d| {
            d.vendor_id() == vendor_id
                && d.product_id() == product_id
                && d.path().to_string_lossy() == path
        });

        let device_info = match device_info_opt {
            Some(info) => info,
            None => return Err("Device not found".to_string()),
        };

        let device = device_info
            .open_device(api)
            .map_err(|e| format!("Failed to open device: {}", e))?;

        let id = Uuid::new_v4();
        let info = DeviceInfo {
            id,
            vendor_id,
            product_id,
            path: path.to_string(),
            manufacturer: device_info.manufacturer_string().map(|s| s.to_string()),
            product: device_info.product_string().map(|s| s.to_string()),
            serial_number: device_info.serial_number_string().map(|s| s.to_string()),
            connected: true,
        };

        let capabilities = Self::parse_report_descriptor(&device);
        let config = self.load_config_from_device(&device)?;

        self.connected_devices.insert(
            id,
            ConnectedDevice {
                device_info: info.clone(),
                config,
                capabilities,
                device: Arc::new(Mutex::new(device)),
            },
        );

        Ok(info)
    }

    pub fn disconnect(&mut self, device_id: Uuid) -> Result<(), String> {
        self.connected_devices
            .remove(&device_id)
            .map(|_| ())
            .ok_or_else(|| "Device not found".to_string())
    }

    pub fn get_connected_devices(&self) -> Vec<DeviceInfo> {
        self.connected_devices
            .values()
            .map(|d| d.device_info.clone())
            .collect()
    }

    pub fn get_config(&self, device_id: Uuid) -> Result<ScannerConfig, String> {
        self.connected_devices
            .get(&device_id)
            .map(|d| d.config.clone())
            .ok_or_else(|| "Device not found".to_string())
    }

    pub fn get_capabilities(&self, device_id: Uuid) -> Result<DeviceCapabilities, String> {
        self.connected_devices
            .get(&device_id)
            .map(|d| d.capabilities.clone())
            .ok_or_else(|| "Device not found".to_string())
    }

    pub fn set_config(
        &mut self,
        device_id: Uuid,
        new_config: ScannerConfig,
    ) -> Result<WriteResult, WriteError> {
        let connected = self
            .connected_devices
            .get(&device_id)
            .ok_or_else(|| WriteError {
                code: WriteErrorCode::DeviceNotReady,
                message: "设备未连接".to_string(),
                details: None,
            })?;

        let old_config = connected.config.clone();
        let capabilities = connected.capabilities.clone();
        let device_arc = connected.device.clone();

        self.validate_config(&new_config, &capabilities)?;

        let serialized = Self::serialize_config(&new_config);
        let actual_size = serialized.len();

        if actual_size > capabilities.max_output_report_size {
            return Err(WriteError {
                code: WriteErrorCode::LengthExceeded,
                message: format!(
                    "配置参数长度{}字节超出设备限制{}字节",
                    actual_size, capabilities.max_output_report_size
                ),
                details: Some(WriteErrorDetails {
                    expected_size: capabilities.max_output_report_size,
                    actual_size,
                    limit: capabilities.max_output_report_size,
                    field_name: "config".to_string(),
                    allowed_range: Some(format!(
                        "最大允许{}字节",
                        capabilities.max_output_report_size
                    )),
                    actual_value: Some(format!("{}字节", actual_size)),
                    retry_count: 0,
                }),
            });
        }

        let mut last_error: Option<WriteError> = None;

        for retry in 0..MAX_RETRIES {
            match Self::write_and_verify(&device_arc, &new_config, &old_config) {
                Ok(verified_config) => {
                    if let Some(c) = self.connected_devices.get_mut(&device_id) {
                        c.config = verified_config.clone();
                    }
                    return Ok(WriteResult {
                        success: true,
                        message: format!("配置写入成功（重试{}次）", retry),
                        config: Some(verified_config),
                        error: None,
                        rolled_back: false,
                        retries_used: retry,
                    });
                }
                Err(e) => {
                    log::warn!(
                        "写入配置第{}次尝试失败: {:?}",
                        retry + 1,
                        e.message
                    );

                    let mut err = e;
                    if let Some(ref mut details) = err.details {
                        details.retry_count = retry;
                    } else {
                        err.details = Some(WriteErrorDetails {
                            expected_size: capabilities.max_output_report_size,
                            actual_size,
                            limit: capabilities.max_output_report_size,
                            field_name: "config".to_string(),
                            allowed_range: None,
                            actual_value: None,
                            retry_count: retry,
                        });
                    }
                    last_error = Some(err);

                    if retry < MAX_RETRIES - 1 {
                        std::thread::sleep(Duration::from_millis(RETRY_DELAY_MS));
                    }
                }
            }
        }

        match Self::rollback_config(&device_arc, &old_config) {
            Ok(_) => {
                if let Some(c) = self.connected_devices.get_mut(&device_id) {
                    c.config = old_config.clone();
                }

                let mut err = last_error.unwrap_or(WriteError {
                    code: WriteErrorCode::VerificationFailed,
                    message: "配置写入失败，已自动回滚到之前的配置".to_string(),
                    details: None,
                });
                err.message = format!("{}；已自动回滚到写入前的配置", err.message);
                err.code = WriteErrorCode::RollbackFailed;

                Ok(WriteResult {
                    success: false,
                    message: err.message.clone(),
                    config: Some(old_config),
                    error: Some(err),
                    rolled_back: true,
                    retries_used: MAX_RETRIES,
                })
            }
            Err(rollback_err) => {
                log::error!("回滚配置失败: {:?}", rollback_err);

                Ok(WriteResult {
                    success: false,
                    message: format!(
                        "配置写入失败且回滚失败: {}；回滚错误: {}",
                        last_error
                            .as_ref()
                            .map(|e| e.message.clone())
                            .unwrap_or_else(|| "未知错误".to_string()),
                        rollback_err.message
                    ),
                    config: None,
                    error: last_error,
                    rolled_back: false,
                    retries_used: MAX_RETRIES,
                })
            }
        }
    }

    fn validate_config(
        &self,
        config: &ScannerConfig,
        capabilities: &DeviceCapabilities,
    ) -> Result<(), WriteError> {
        if let Some(ref prefix) = config.prefix_char {
            if prefix.len() > capabilities.max_prefix_length {
                return Err(WriteError {
                    code: WriteErrorCode::ValidationFailed,
                    message: format!(
                        "前缀字符长度{}字节超出设备限制{}字节",
                        prefix.len(),
                        capabilities.max_prefix_length
                    ),
                    details: Some(WriteErrorDetails {
                        expected_size: capabilities.max_prefix_length,
                        actual_size: prefix.len(),
                        limit: capabilities.max_prefix_length,
                        field_name: "prefix_char".to_string(),
                        allowed_range: Some(format!(
                            "最大允许{}字节",
                            capabilities.max_prefix_length
                        )),
                        actual_value: Some(prefix.clone()),
                        retry_count: 0,
                    }),
                });
            }
        }

        if let Some(ref suffix) = config.suffix_char {
            if suffix.len() > capabilities.max_suffix_length {
                return Err(WriteError {
                    code: WriteErrorCode::ValidationFailed,
                    message: format!(
                        "后缀字符长度{}字节超出设备限制{}字节",
                        suffix.len(),
                        capabilities.max_suffix_length
                    ),
                    details: Some(WriteErrorDetails {
                        expected_size: capabilities.max_suffix_length,
                        actual_size: suffix.len(),
                        limit: capabilities.max_suffix_length,
                        field_name: "suffix_char".to_string(),
                        allowed_range: Some(format!(
                            "最大允许{}字节",
                            capabilities.max_suffix_length
                        )),
                        actual_value: Some(suffix.clone()),
                        retry_count: 0,
                    }),
                });
            }
        }

        if !capabilities
            .supported_scan_modes
            .contains(&config.scan_mode)
        {
            return Err(WriteError {
                code: WriteErrorCode::ValidationFailed,
                message: format!(
                    "扫描模式 '{:?}' 不被此设备支持",
                    config.scan_mode
                ),
                details: Some(WriteErrorDetails {
                    expected_size: 0,
                    actual_size: 0,
                    limit: 0,
                    field_name: "scan_mode".to_string(),
                    allowed_range: Some(format!(
                        "支持的模式: {:?}",
                        capabilities.supported_scan_modes
                    )),
                    actual_value: Some(format!("{:?}", config.scan_mode)),
                    retry_count: 0,
                }),
            });
        }

        if !capabilities
            .supported_led_colors
            .contains(&config.led_color)
        {
            return Err(WriteError {
                code: WriteErrorCode::ValidationFailed,
                message: format!("LED颜色 '{:?}' 不被此设备支持", config.led_color),
                details: Some(WriteErrorDetails {
                    expected_size: 0,
                    actual_size: 0,
                    limit: 0,
                    field_name: "led_color".to_string(),
                    allowed_range: Some(format!(
                        "支持的颜色: {:?}",
                        capabilities.supported_led_colors
                    )),
                    actual_value: Some(format!("{:?}", config.led_color)),
                    retry_count: 0,
                }),
            });
        }

        let total_size = config.serialized_size();
        if total_size > capabilities.max_output_report_size {
            return Err(WriteError {
                code: WriteErrorCode::LengthExceeded,
                message: format!(
                    "配置参数总长度{}字节超出设备HID报告限制{}字节",
                    total_size, capabilities.max_output_report_size
                ),
                details: Some(WriteErrorDetails {
                    expected_size: capabilities.max_output_report_size,
                    actual_size: total_size,
                    limit: capabilities.max_output_report_size,
                    field_name: "config_total".to_string(),
                    allowed_range: Some(format!(
                        "单包最大{}字节",
                        capabilities.max_output_report_size
                    )),
                    actual_value: Some(format!("{}字节", total_size)),
                    retry_count: 0,
                }),
            });
        }

        Ok(())
    }

    fn write_and_verify(
        device: &Arc<Mutex<HidDevice>>,
        config: &ScannerConfig,
        _old_config: &ScannerConfig,
    ) -> Result<ScannerConfig, WriteError> {
        let mut dev = device.lock();

        let data = Self::serialize_config(config);

        let mut report = vec![CONFIG_REPORT_ID];
        report.extend_from_slice(&data);

        while report.len() < 64 {
            report.push(0);
        }

        dev.set_blocking_mode(true)
            .map_err(|e| WriteError {
                code: WriteErrorCode::DeviceWriteFailed,
                message: format!("设置HID阻塞模式失败: {}", e),
                details: None,
            })?;

        let start = Instant::now();
        let bytes_written = dev.write(&report).map_err(|e| WriteError {
            code: WriteErrorCode::DeviceWriteFailed,
            message: format!("HID写入失败: {}", e),
            details: Some(WriteErrorDetails {
                expected_size: report.len(),
                actual_size: 0,
                limit: report.len(),
                field_name: "hid_write".to_string(),
                allowed_range: None,
                actual_value: None,
                retry_count: 0,
            }),
        })?;

        log::debug!(
            "写入{}字节（耗时{:.1}ms），期望{}字节",
            bytes_written,
            start.elapsed().as_secs_f64() * 1000.0,
            report.len()
        );

        if bytes_written != report.len() {
            return Err(WriteError {
                code: WriteErrorCode::DeviceWriteFailed,
                message: format!(
                    "HID写入字节数不匹配: 实际{}字节，期望{}字节",
                    bytes_written,
                    report.len()
                ),
                details: Some(WriteErrorDetails {
                    expected_size: report.len(),
                    actual_size: bytes_written,
                    limit: report.len(),
                    field_name: "hid_write_bytes".to_string(),
                    allowed_range: None,
                    actual_value: Some(format!("{}字节", bytes_written)),
                    retry_count: 0,
                }),
            });
        }

        std::thread::sleep(Duration::from_millis(50));

        let read_back = Self::read_config_from_hid(&mut dev)?;

        if !Self::configs_are_equivalent(config, &read_back) {
            return Err(WriteError {
                code: WriteErrorCode::VerificationFailed,
                message: format!(
                    "写入验证失败：写入配置与设备读回配置不一致（写入={:?}, 读回={:?}）",
                    config, read_back
                ),
                details: Some(WriteErrorDetails {
                    expected_size: 0,
                    actual_size: 0,
                    limit: 0,
                    field_name: "verification".to_string(),
                    allowed_range: Some(format!("{:?}", config)),
                    actual_value: Some(format!("{:?}", read_back)),
                    retry_count: 0,
                }),
            });
        }

        Ok(read_back)
    }

    fn rollback_config(
        device: &Arc<Mutex<HidDevice>>,
        old_config: &ScannerConfig,
    ) -> Result<(), WriteError> {
        let mut dev = device.lock();

        let data = Self::serialize_config(old_config);
        let mut report = vec![CONFIG_REPORT_ID];
        report.extend_from_slice(&data);
        while report.len() < 64 {
            report.push(0);
        }

        let bytes_written = dev.write(&report).map_err(|e| WriteError {
            code: WriteErrorCode::RollbackFailed,
            message: format!("回滚写入HID失败: {}", e),
            details: None,
        })?;

        if bytes_written != report.len() {
            return Err(WriteError {
                code: WriteErrorCode::RollbackFailed,
                message: format!(
                    "回滚写入字节数不匹配: 实际{}字节，期望{}字节",
                    bytes_written,
                    report.len()
                ),
                details: None,
            });
        }

        std::thread::sleep(Duration::from_millis(50));

        let read_back = Self::read_config_from_hid(&mut dev)?;

        if !Self::configs_are_equivalent(old_config, &read_back) {
            return Err(WriteError {
                code: WriteErrorCode::RollbackFailed,
                message: "回滚验证失败：读回配置与原始配置不一致".to_string(),
                details: None,
            });
        }

        Ok(())
    }

    fn read_config_from_hid(dev: &mut HidDevice) -> Result<ScannerConfig, WriteError> {
        let mut buf = vec![0u8; 64];

        let bytes_read = dev.read(&mut buf).map_err(|e| WriteError {
            code: WriteErrorCode::DeviceWriteFailed,
            message: format!("从HID读取配置失败: {}", e),
            details: None,
        })?;

        log::debug!("从HID读取{}字节", bytes_read);

        if bytes_read > 1 {
            Ok(Self::deserialize_config(&buf[1..bytes_read]))
        } else {
            Ok(ScannerConfig::default())
        }
    }

    fn parse_report_descriptor(device: &HidDevice) -> DeviceCapabilities {
        let mut caps = DeviceCapabilities::default_scanner();

        if let Ok(report_desc) = device.get_report_descriptor() {
            caps.raw_report_descriptor = report_desc.clone();

            let (max_input, max_output, max_feature) =
                Self::extract_report_sizes(&report_desc);

            if max_input > 0 {
                caps.max_input_report_size = max_input;
            }
            if max_output > 0 {
                caps.max_output_report_size = max_output;
            }
            if max_feature > 0 {
                caps.max_feature_report_size = max_feature;
            }

            log::info!(
                "HID报告描述符解析: 输入={}字节, 输出={}字节, 特性={}字节",
                caps.max_input_report_size,
                caps.max_output_report_size,
                caps.max_feature_report_size
            );
        } else {
            log::debug!("无法获取HID报告描述符，使用默认能力参数");

            caps.max_input_report_size = 64;
            caps.max_output_report_size = 64;
            caps.max_feature_report_size = 64;
            caps.max_prefix_length = 32;
            caps.max_suffix_length = 32;

            log::info!(
                "使用默认设备能力: 输入={}字节, 输出={}字节, 特性={}字节",
                caps.max_input_report_size,
                caps.max_output_report_size,
                caps.max_feature_report_size
            );
        }

        caps
    }

    fn extract_report_sizes(descriptor: &[u8]) -> (usize, usize, usize) {
        let mut max_input: usize = 0;
        let mut max_output: usize = 0;
        let mut max_feature: usize = 0;

        let mut pos = 0;
        let mut current_report_size: u32 = 0;
        let mut current_report_count: u32 = 1;

        while pos < descriptor.len() {
            let tag_byte = descriptor[pos];
            let b_tag = (tag_byte >> 4) & 0x0F;
            let b_type = (tag_byte >> 2) & 0x03;
            let b_size = tag_byte & 0x03;

            let data_size = match b_size {
                0 => 0,
                1 => 1,
                2 => 2,
                3 => 4,
                _ => 0,
            };

            let data = if data_size > 0 && pos + 1 + data_size <= descriptor.len() {
                let bytes = &descriptor[pos + 1..pos + 1 + data_size];
                match data_size {
                    1 => bytes[0] as u32,
                    2 => u16::from_le_bytes([bytes[0], bytes[1]]) as u32,
                    4 => u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]),
                    _ => 0,
                }
            } else {
                0
            };

            match (b_type, b_tag) {
                (0, 7) => {
                    current_report_size = data;
                }
                (0, 9) => {
                    current_report_count = data;
                }
                (1, 8) => {
                    let size = (current_report_size * current_report_count / 8) as usize;
                    if size > max_input {
                        max_input = size;
                    }
                }
                (1, 9) => {
                    let size = (current_report_size * current_report_count / 8) as usize;
                    if size > max_output {
                        max_output = size;
                    }
                }
                (1, 11) => {
                    let size = (current_report_size * current_report_count / 8) as usize;
                    if size > max_feature {
                        max_feature = size;
                    }
                }
                _ => {}
            }

            pos += 1 + data_size;
        }

        (max_input, max_output, max_feature)
    }

    fn serialize_config(config: &ScannerConfig) -> Vec<u8> {
        let mut data = Vec::new();

        let scan_mode_byte = match config.scan_mode {
            crate::models::ScanMode::Manual => 0x01,
            crate::models::ScanMode::Continuous => 0x02,
            crate::models::ScanMode::Trigger => 0x03,
        };
        data.push(scan_mode_byte);

        if let Some(ref prefix) = config.prefix_char {
            data.push(0x01);
            data.push(prefix.len() as u8);
            data.extend_from_slice(prefix.as_bytes());
        } else {
            data.push(0x00);
            data.push(0x00);
        }

        if let Some(ref suffix) = config.suffix_char {
            data.push(0x01);
            data.push(suffix.len() as u8);
            data.extend_from_slice(suffix.as_bytes());
        } else {
            data.push(0x00);
            data.push(0x00);
        }

        data.push(if config.beeper_enabled { 0x01 } else { 0x00 });

        let led_byte = match config.led_color {
            crate::models::LedColor::Red => 0x01,
            crate::models::LedColor::Green => 0x02,
            crate::models::LedColor::Blue => 0x03,
            crate::models::LedColor::Yellow => 0x04,
            crate::models::LedColor::Off => 0x00,
        };
        data.push(led_byte);

        data
    }

    fn deserialize_config(data: &[u8]) -> ScannerConfig {
        if data.is_empty() {
            return ScannerConfig::default();
        }

        let mut pos = 0;

        let scan_mode = if pos < data.len() {
            match data[pos] {
                0x01 => crate::models::ScanMode::Manual,
                0x02 => crate::models::ScanMode::Continuous,
                0x03 => crate::models::ScanMode::Trigger,
                _ => crate::models::ScanMode::default(),
            }
        } else {
            crate::models::ScanMode::default()
        };
        pos += 1;

        let prefix_char = if pos + 1 < data.len() && data[pos] == 0x01 {
            let len = data[pos + 1] as usize;
            pos += 2;
            if pos + len <= data.len() {
                let s = String::from_utf8_lossy(&data[pos..pos + len]).to_string();
                pos += len;
                Some(s)
            } else {
                pos += 2;
                None
            }
        } else {
            pos += 2;
            None
        };

        let suffix_char = if pos + 1 < data.len() && data[pos] == 0x01 {
            let len = data[pos + 1] as usize;
            pos += 2;
            if pos + len <= data.len() {
                let s = String::from_utf8_lossy(&data[pos..pos + len]).to_string();
                pos += len;
                Some(s)
            } else {
                pos += 2;
                None
            }
        } else {
            pos += 2;
            None
        };

        let beeper_enabled = if pos < data.len() {
            data[pos] == 0x01
        } else {
            true
        };
        pos += 1;

        let led_color = if pos < data.len() {
            match data[pos] {
                0x01 => crate::models::LedColor::Red,
                0x02 => crate::models::LedColor::Green,
                0x03 => crate::models::LedColor::Blue,
                0x04 => crate::models::LedColor::Yellow,
                _ => crate::models::LedColor::Off,
            }
        } else {
            crate::models::LedColor::default()
        };

        ScannerConfig {
            scan_mode,
            suffix_char,
            prefix_char,
            beeper_enabled,
            led_color,
        }
    }

    fn configs_are_equivalent(a: &ScannerConfig, b: &ScannerConfig) -> bool {
        a.scan_mode == b.scan_mode
            && a.suffix_char == b.suffix_char
            && a.prefix_char == b.prefix_char
            && a.beeper_enabled == b.beeper_enabled
            && a.led_color == b.led_color
    }

    fn load_config_from_device(&self, device: &HidDevice) -> Result<ScannerConfig, String> {
        let mut buf = vec![0u8; 64];
        let mut dev = device;
        match dev.read(&mut buf) {
            Ok(bytes_read) => {
                if bytes_read > 1 {
                    Ok(Self::deserialize_config(&buf[1..bytes_read]))
                } else {
                    Ok(ScannerConfig::default())
                }
            }
            Err(_) => Ok(ScannerConfig::default()),
        }
    }

    pub fn check_device_alive(&self, device_id: Uuid) -> bool {
        if let Some(connected) = self.connected_devices.get(&device_id) {
            let device = connected.device.lock();
            device.write(&[0; 1]).is_ok()
                || device.get_feature_report(0, &mut [0; 1]).is_ok()
        } else {
            false
        }
    }

    pub fn is_connected(&self, device_id: Uuid) -> bool {
        self.connected_devices.contains_key(&device_id)
    }

    pub fn reconnect(&mut self, device_id: Uuid) -> Result<DeviceInfo, String> {
        let device_info = self
            .connected_devices
            .get(&device_id)
            .map(|d| d.device_info.clone())
            .ok_or_else(|| "Device not found".to_string())?;

        self.connected_devices.remove(&device_id);

        self.connect(
            device_info.vendor_id,
            device_info.product_id,
            &device_info.path,
        )
    }
}
