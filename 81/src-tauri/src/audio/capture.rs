use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{Device, Stream, StreamConfig};
use parking_lot::Mutex;
use tokio::sync::mpsc;

pub const SAMPLE_RATE: u32 = 16_000;
pub const CHANNELS: u16 = 1;
pub const FRAME_DURATION_MS: u64 = 1000;
pub const HOTWORD_WINDOW_MS: u64 = 1500;
pub const OVERLAP_MS: u64 = 300;

pub const SAMPLES_PER_FRAME: usize = (SAMPLE_RATE as u64 * FRAME_DURATION_MS / 1000) as usize;
pub const SAMPLES_PER_HOTWORD_WINDOW: usize =
    (SAMPLE_RATE as u64 * HOTWORD_WINDOW_MS / 1000) as usize;
pub const SAMPLES_PER_OVERLAP: usize = (SAMPLE_RATE as u64 * OVERLAP_MS / 1000) as usize;

#[derive(Debug, Clone)]
pub struct AudioFrame {
    pub samples: Vec<f32>,
    pub timestamp: std::time::Instant,
}

#[derive(Debug, Clone)]
pub struct AudioConfig {
    pub sample_rate: u32,
    pub channels: u16,
    pub frame_duration_ms: u64,
    pub overlap_ms: u64,
    pub hotword_window_ms: u64,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: SAMPLE_RATE,
            channels: CHANNELS,
            frame_duration_ms: FRAME_DURATION_MS,
            overlap_ms: OVERLAP_MS,
            hotword_window_ms: HOTWORD_WINDOW_MS,
        }
    }
}

struct RingBuffer {
    buffer: Vec<f32>,
    capacity: usize,
    write_idx: usize,
}

impl RingBuffer {
    fn new(capacity: usize) -> Self {
        Self {
            buffer: vec![0.0; capacity],
            capacity,
            write_idx: 0,
        }
    }

    fn push(&mut self, samples: &[f32]) {
        for &s in samples {
            self.buffer[self.write_idx] = s;
            self.write_idx = (self.write_idx + 1) % self.capacity;
        }
    }

    fn read_last_n(&self, n: usize) -> Vec<f32> {
        let n = n.min(self.capacity);
        let mut result = Vec::with_capacity(n);
        let start = (self.write_idx + self.capacity - n) % self.capacity;
        for i in 0..n {
            let idx = (start + i) % self.capacity;
            result.push(self.buffer[idx]);
        }
        result
    }
}

pub struct AudioCapture {
    config: AudioConfig,
    stream: Option<Stream>,
    _device: Device,
    running: Arc<AtomicBool>,
    ring_buffer: Arc<Mutex<RingBuffer>>,
    frame_collector: Arc<Mutex<Vec<f32>>>,
    frame_tx: Option<mpsc::Sender<AudioFrame>>,
    hotword_tx: Option<mpsc::Sender<AudioFrame>>,
    last_hotword_send: Arc<Mutex<std::time::Instant>>,
}

impl AudioCapture {
    pub fn new(config: AudioConfig) -> Result<Self, String> {
        let host = cpal::default_host();
        let device = host
            .default_input_device()
            .ok_or_else(|| "未找到默认输入设备".to_string())?;

        log::info!("使用麦克风设备: {}", device.name().unwrap_or_default());

        Ok(Self {
            config,
            stream: None,
            _device: device,
            running: Arc::new(AtomicBool::new(false)),
            ring_buffer: Arc::new(Mutex::new(RingBuffer::new(
                SAMPLES_PER_HOTWORD_WINDOW * 2,
            ))),
            frame_collector: Arc::new(Mutex::new(Vec::with_capacity(SAMPLES_PER_FRAME))),
            frame_tx: None,
            hotword_tx: None,
            last_hotword_send: Arc::new(Mutex::new(std::time::Instant::now())),
        })
    }

    pub fn set_frame_sender(&mut self, tx: mpsc::Sender<AudioFrame>) {
        self.frame_tx = Some(tx);
    }

    pub fn set_hotword_sender(&mut self, tx: mpsc::Sender<AudioFrame>) {
        self.hotword_tx = Some(tx);
    }

    pub fn start(&mut self) -> Result<(), String> {
        if self.running.load(Ordering::SeqCst) {
            return Ok(());
        }

        let host = cpal::default_host();
        let device = host
            .default_input_device()
            .ok_or_else(|| "未找到默认输入设备".to_string())?;

        let config: StreamConfig = cpal::StreamConfig {
            channels: self.config.channels,
            sample_rate: cpal::SampleRate(self.config.sample_rate),
            buffer_size: cpal::BufferSize::Default,
        };

        let ring_buffer = Arc::clone(&self.ring_buffer);
        let frame_collector = Arc::clone(&self.frame_collector);
        let frame_tx = self.frame_tx.clone();
        let hotword_tx = self.hotword_tx.clone();
        let running = Arc::clone(&self.running);
        let last_hotword_send = Arc::clone(&self.last_hotword_send);
        let frame_size = SAMPLES_PER_FRAME;
        let overlap_size = SAMPLES_PER_OVERLAP;
        let hotword_window_size = SAMPLES_PER_HOTWORD_WINDOW;

        let err_fn = |err| {
            log::error!("音频流错误: {}", err);
        };

        let stream = device
            .build_input_stream(
                &config,
                move |data: &[f32], _| {
                    if !running.load(Ordering::SeqCst) {
                        return;
                    }

                    {
                        let mut rb = ring_buffer.lock();
                        rb.push(data);
                    }

                    if let Some(ref tx) = hotword_tx {
                        let mut last = last_hotword_send.lock();
                        if last.elapsed() >= Duration::from_millis(300) {
                            let rb = ring_buffer.lock();
                            let window = rb.read_last_n(hotword_window_size);
                            if window.len() >= hotword_window_size {
                                let _ = tx.try_send(AudioFrame {
                                    samples: window,
                                    timestamp: std::time::Instant::now(),
                                });
                            }
                            *last = std::time::Instant::now();
                        }
                    }

                    {
                        let mut collector = frame_collector.lock();
                        collector.extend_from_slice(data);

                        if collector.len() >= frame_size {
                            let frame_samples: Vec<f32> = collector[..frame_size].to_vec();
                            let frame = AudioFrame {
                                samples: frame_samples,
                                timestamp: std::time::Instant::now(),
                            };

                            if let Some(ref tx) = frame_tx {
                                let _ = tx.try_send(frame);
                            }

                            let retain_start = frame_size.saturating_sub(overlap_size);
                            let retained: Vec<f32> = collector[retain_start..].to_vec();
                            collector.clear();
                            collector.extend_from_slice(&retained);
                        }
                    }
                },
                err_fn,
                None,
            )
            .map_err(|e| format!("创建音频流失败: {}", e))?;

        stream
            .play()
            .map_err(|e| format!("启动音频流失败: {}", e))?;

        self.stream = Some(stream);
        self.running.store(true, Ordering::SeqCst);

        log::info!("音频采集已启动: {}Hz 单声道", self.config.sample_rate);
        Ok(())
    }

    pub fn stop(&mut self) {
        self.running.store(false, Ordering::SeqCst);
        if let Some(stream) = self.stream.take() {
            drop(stream);
        }
        log::info!("音频采集已停止");
    }

    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }
}

impl Drop for AudioCapture {
    fn drop(&mut self) {
        self.stop();
    }
}
