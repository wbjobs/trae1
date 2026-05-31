use std::path::PathBuf;
use std::time::Instant;

use image::imageops::FilterType;
use image::{GenericImageView, ImageBuffer, Rgb};
use ort::environment::Environment;
use ort::session::{Session, SessionBuilder};
use ort::value::Value;
use ort::{Arc as OrtArc, GraphOptimizationLevel, LoggingLevel};
use serde::{Deserialize, Serialize};

use crate::error::AppError;
use crate::state::{AppState, ModelStatus};

pub const DEFAULT_LABELS: &[&str] = &[
    "like",
    "fist",
    "v_sign",
    "ok",
    "palm",
    "digit_1",
    "digit_2",
    "digit_5",
];

const CLAHE_TILE_GRID: u32 = 4;
const CLAHE_CLIP_RATIO: f32 = 0.025;
const GRID_SIZE: u32 = 3;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GesturePosition {
    pub x: f32,
    pub y: f32,
    pub region_confidence: f32,
}

#[derive(Debug, Clone, Serialize)]
pub struct InferenceResult {
    pub label: String,
    pub label_index: usize,
    pub confidence: f32,
    pub probabilities: Vec<f32>,
    pub inference_ms: f32,
    pub position: Option<GesturePosition>,
}

pub struct InferenceEngine {
    pub _env: OrtArc<Environment>,
    pub session: Session,
    pub input_shape: Vec<u64>,
    pub labels: Vec<String>,
}

#[derive(Debug, Deserialize)]
pub struct LoadModelArgs {
    pub path: Option<String>,
    pub labels: Option<Vec<String>>,
    pub num_threads: Option<i16>,
}

#[tauri::command]
pub async fn load_model(
    state: tauri::State<'_, AppState>,
    app: tauri::AppHandle,
    args: Option<LoadModelArgs>,
) -> Result<ModelStatus, AppError> {
    let args = args.unwrap_or(LoadModelArgs {
        path: None,
        labels: None,
        num_threads: None,
    });

    let default_path = resolve_default_model_path(&app);
    let model_path = match args.path {
        Some(p) => PathBuf::from(p),
        None => default_path
            .ok_or_else(|| AppError::Other("No model path provided and no default model found in resources".to_string()))?,
    };

    if !model_path.exists() {
        return Err(AppError::Io(format!(
            "Model file not found: {}",
            model_path.display()
        )));
    }

    let labels = args
        .labels
        .unwrap_or_else(|| DEFAULT_LABELS.iter().map(|s| s.to_string()).collect());

    let env = Environment::builder()
        .with_name("GestureRecognizer")
        .with_log_level(LoggingLevel::Warning)
        .build()?;

    let mut builder = SessionBuilder::new(&env)?
        .with_optimization_level(GraphOptimizationLevel::Level3)?
        .with_intra_threads(args.num_threads.unwrap_or(4))?;

    #[cfg(feature = "cuda")]
    {
        builder = builder.with_cuda(0)?;
    }

    let session = builder.with_model_from_file(&model_path)?;

    let input_shape = session
        .input_types
        .first()
        .map(|(_, ty)| {
            ty.dimensions
                .iter()
                .map(|d| d.unwrap_or(1))
                .collect::<Vec<u64>>()
        })
        .unwrap_or_else(|| vec![1, 3, 224, 224]);

    let engine = InferenceEngine {
        _env: env,
        session,
        input_shape: input_shape.clone(),
        labels: labels.clone(),
    };

    let mut state_engine = state.engine.lock();
    *state_engine = Some(engine);

    let status = ModelStatus {
        loaded: true,
        path: Some(model_path),
        input_shape: Some(input_shape),
        labels,
        error: None,
    };
    *state.status.lock() = status.clone();

    Ok(status)
}

#[tauri::command]
pub async fn unload_model(state: tauri::State<'_, AppState>) -> Result<(), AppError> {
    *state.engine.lock() = None;
    *state.status.lock() = ModelStatus::default();
    Ok(())
}

#[tauri::command]
pub async fn get_model_status(state: tauri::State<'_, AppState>) -> ModelStatus {
    state.status.lock().clone()
}

fn resolve_default_model_path(app: &tauri::AppHandle) -> Option<PathBuf> {
    let resource_path = app
        .path()
        .resolve("resources/model.onnx", tauri::path::BaseDirectory::Resource)
        .ok();
    if let Some(p) = resource_path {
        if p.exists() {
            return Some(p);
        }
    }
    None
}

impl InferenceEngine {
    pub fn infer_from_bytes(&self, jpeg_bytes: &[u8]) -> Result<InferenceResult, AppError> {
        let img = image::load_from_memory(jpeg_bytes)?.to_rgb8();
        let (n, c, h, w) = self.read_nchw();
        let start = Instant::now();

        let (label_index, confidence, probs) = self.infer_image(&img, h as u32, w as u32)?;

        let position = self.estimate_position(&img, h as u32, w as u32, label_index)?;

        let label = self
            .labels
            .get(label_index)
            .cloned()
            .unwrap_or_else(|| format!("class_{label_index}"));

        Ok(InferenceResult {
            label,
            label_index,
            confidence,
            probabilities: probs,
            inference_ms: start.elapsed().as_secs_f32() * 1000.0,
            position: Some(position),
        })
    }

    fn infer_image(
        &self,
        img: &ImageBuffer<Rgb<u8>, Vec<u8>>,
        h: u32,
        w: u32,
    ) -> Result<(usize, f32, Vec<f32>), AppError> {
        let (n, c, _, _) = self.read_nchw();
        let tensor_data = preprocess(img, h, w);

        let input_array =
            ndarray::Array4::from_shape_vec((n, c, h as usize, w as usize), tensor_data).map_err(|e| {
                AppError::InvalidFrame(format!("Failed to build input tensor: {e}"))
            })?;

        let input_value = Value::from_array(&mut self.session.allocator, input_array)?;
        let outputs = self.session.run(vec![input_value])?;

        let output = outputs
            .first()
            .ok_or_else(|| AppError::Ort("No output from session".into()))?;

        let output_array = output.try_extract::<f32>()?.view().to_owned().into_dyn();
        let probs = softmax(output_array.as_slice().unwrap_or(&[]));
        let (label_index, confidence) =
            probs
                .iter()
                .enumerate()
                .fold((0usize, 0.0f32), |(idx, best), (i, &p)| {
                    if p > best {
                        (i, p)
                    } else {
                        (idx, best)
                    }
                });

        Ok((label_index, confidence, probs))
    }

    fn estimate_position(
        &self,
        img: &ImageBuffer<Rgb<u8>, Vec<u8>>,
        model_h: u32,
        model_w: u32,
        target_label: usize,
    ) -> Result<GesturePosition, AppError> {
        let (img_w, img_h) = img.dimensions();

        if img_w < 64 || img_h < 64 {
            return Ok(GesturePosition {
                x: 0.5,
                y: 0.5,
                region_confidence: 1.0,
            });
        }

        let region_w = img_w / GRID_SIZE;
        let region_h = img_h / GRID_SIZE;
        if region_w < 32 || region_h < 32 {
            return Ok(GesturePosition {
                x: 0.5,
                y: 0.5,
                region_confidence: 1.0,
            });
        }

        let mut best_region: Option<(f32, f32, f32)> = None;

        for gy in 0..GRID_SIZE {
            for gx in 0..GRID_SIZE {
                let x0 = gx * region_w;
                let y0 = gy * region_h;
                let x1 = if gx == GRID_SIZE - 1 {
                    img_w
                } else {
                    (gx + 1) * region_w
                };
                let y1 = if gy == GRID_SIZE - 1 {
                    img_h
                } else {
                    (gy + 1) * region_h
                };

                let region = img.view(x0, y0, x1 - x0, y1 - y0).to_image();

                if let Ok((_, conf, _)) =
                    self.infer_image(&region, model_h, model_w)
                {
                    let cx = ((gx as f32 + 0.5) / GRID_SIZE as f32;
                    let cy = ((gy as f32 + 0.5) / GRID_SIZE as f32;

                    match best_region {
                        None => {
                            best_region = Some((cx, cy, conf));
                        }
                        Some((_, _, best_conf)) => {
                            if conf > best_conf {
                                best_region = Some((cx, cy, conf));
                            }
                        }
                    }
                }
            }
        }

        let (x, y, region_confidence) = best_region.unwrap_or((0.5, 0.5, 1.0));

        Ok(GesturePosition {
            x,
            y,
            region_confidence,
        })
    }

    fn read_nchw(&self) -> (usize, usize, usize, usize) {
        let s = &self.input_shape;
        if s.len() == 4 {
            (
                s[0] as usize,
                s[1] as usize,
                s[2] as usize,
                s[3] as usize,
            )
        } else {
            (1, 3, 224, 224)
        }
    }
}

fn preprocess(img: &ImageBuffer<Rgb<u8>, Vec<u8>>, h: u32, w: u32) -> Vec<f32> {
    let gamma_corrected = gamma_correct(img);
    let clahe_enhanced = clahe(&gamma_corrected);
    let resized = clahe_enhanced.resize_exact(w, h, FilterType::Triangle);

    let mut out = Vec::with_capacity((3 * h * w) as usize);
    for c in 0..3 {
        for y in 0..h {
            for x in 0..w {
                let pixel = resized.get_pixel(x, y);
                let raw = pixel[c] as f32 / 255.0;
                out.push(normalize(raw, c));
            }
        }
    }
    out
}

fn normalize(v: f32, channel: u32) -> f32 {
    let mean = match channel {
        0 => 0.485f32,
        1 => 0.456f32,
        _ => 0.406f32,
    };
    let std = match channel {
        0 => 0.229f32,
        1 => 0.224f32,
        _ => 0.225f32,
    };
    (v - mean) / std
}

fn gamma_correct(img: &ImageBuffer<Rgb<u8>, Vec<u8>>) -> ImageBuffer<Rgb<u8>, Vec<u8>> {
    let (w, h) = img.dimensions();
    let pixel_count = (w * h) as f64;
    let mut sum_luma: f64 = 0.0;

    for p in img.pixels() {
        let r = p[0] as f64;
        let g = p[1] as f64;
        let b = p[2] as f64;
        sum_luma += 0.299 * r + 0.587 * g + 0.114 * b;
    }
    let avg_luma = sum_luma / pixel_count;

    let gamma = if avg_luma < 60.0 {
        0.55
    } else if avg_luma < 90.0 {
        0.70
    } else if avg_luma > 220.0 {
        1.60
    } else if avg_luma > 200.0 {
        1.30
    } else {
        1.0
    };

    let inv_gamma = 1.0 / gamma;
    let mut out: Vec<u8> = Vec::with_capacity(img.as_raw().len());
    for p in img.pixels() {
        for c in 0..3 {
            let v = p[c] as f64 / 255.0;
            let corrected = v.powf(inv_gamma) * 255.0;
            out.push(corrected.clamp(0.0, 255.0) as u8);
        }
    }
    ImageBuffer::from_raw(w, h, out).unwrap()
}

fn clahe(img: &ImageBuffer<Rgb<u8>, Vec<u8>>) -> ImageBuffer<Rgb<u8>, Vec<u8>> {
    let (w, h) = img.dimensions();
    let total = (w * h) as usize;

    let mut y_plane = vec![0u8; total];
    let mut cb_plane = vec![0u8; total];
    let mut cr_plane = vec![0u8; total];

    for (i, p) in img.pixels().enumerate() {
        let r = p[0] as f32;
        let g = p[1] as f32;
        let b = p[2] as f32;
        y_plane[i] = (0.299 * r + 0.587 * g + 0.114 * b).clamp(0.0, 255.0) as u8;
        cb_plane[i] = (-0.169 * r - 0.331 * g + 0.5 * b + 128.0).clamp(0.0, 255.0) as u8;
        cr_plane[i] = (0.5 * r - 0.419 * g - 0.081 * b + 128.0).clamp(0.0, 255.0) as u8;
    }

    let enhanced_y = clahe_luminance(&y_plane, w, h);

    let mut raw = vec![0u8; total * 3];
    for i in 0..total {
        let y = enhanced_y[i] as f32;
        let cb = cb_plane[i] as f32 - 128.0;
        let cr = cr_plane[i] as f32 - 128.0;

        let r = (y + 1.402 * cr).clamp(0.0, 255.0) as u8;
        let g = (y - 0.344136 * cb - 0.714136 * cr).clamp(0.0, 255.0) as u8;
        let b = (y + 1.772 * cb).clamp(0.0, 255.0) as u8;

        raw[i * 3] = r;
        raw[i * 3 + 1] = g;
        raw[i * 3 + 2] = b;
    }

    ImageBuffer::from_raw(w, h, raw).unwrap()
}

fn clahe_luminance(y: &[u8], w: u32, h: u32) -> Vec<u8> {
    let tile_x = CLAHE_TILE_GRID;
    let tile_y = CLAHE_TILE_GRID;
    let tile_w = w / tile_x;
    let tile_h = h / tile_y;

    if tile_w < 4 || tile_h < 4 {
        return y.to_vec();
    }

    let clip_limit = (CLAHE_CLIP_RATIO * (tile_w * tile_h) as f32) as u32;
    let clip_limit = clip_limit.max(1);

    let mut lut: Vec<Vec<u8>> = Vec::with_capacity((tile_x * tile_y) as usize);

    for ty in 0..tile_y {
        for tx in 0..tile_x {
            let mut hist = [0u32; 256];

            let x0 = tx * tile_w;
            let y0 = ty * tile_h;
            let x1 = if tx == tile_x - 1 {
                w
            } else {
                (tx + 1) * tile_w
            };
            let y1 = if ty == tile_y - 1 {
                h
            } else {
                (ty + 1) * tile_h
            };

            for row in y0..y1 {
                for col in x0..x1 {
                    let idx = (row * w + col) as usize;
                    let v = y[idx] as usize;
                    hist[v] += 1;
                }
            }

            let mut excess = 0u32;
            for h in hist.iter_mut() {
                if *h > clip_limit {
                    excess += *h - clip_limit;
                    *h = clip_limit;
                }
            }
            let redistribute = excess / 256;
            for h in hist.iter_mut() {
                *h += redistribute;
            }

            let mut cdf = [0u32; 256];
            let mut cumsum = 0u32;
            for i in 0..256 {
                cumsum += hist[i];
                cdf[i] = cumsum;
            }
            let total = cdf[255].max(1);
            let mut tile_lut = vec![0u8; 256];
            for i in 0..256 {
                tile_lut[i] = ((cdf[i] as f32 / total as f32) * 255.0).clamp(0.0, 255.0) as u8;
            }

            lut.push(tile_lut);
        }
    }

    let mut out = vec![0u8; y.len()];
    let tile_wf = tile_w as f32;
    let tile_hf = tile_h as f32;

    for row in 0..h {
        for col in 0..w {
            let idx = (row * w + col) as usize;
            let v = y[idx] as usize;

            let fx = col as f32 / tile_wf - 0.5;
            let fy = row as f32 / tile_hf - 0.5;

            let tx_low = fx.floor().max(0.0) as i32;
            let ty_low = fy.floor().max(0.0) as i32;
            let tx_high = (tx_low + 1).min(tile_x as i32 - 1);
            let ty_high = (ty_low + 1).min(tile_y as i32 - 1);
            let tx_low = tx_low.max(0);
            let ty_low = ty_low.max(0);

            let wx = fx - tx_low as f32;
            let wy = fy - ty_low as f32;

            let idx00 = (ty_low as u32 * tile_x + tx_low as u32) as usize;
            let idx10 = (ty_low as u32 * tile_x + tx_high as u32) as usize;
            let idx01 = (ty_high as u32 * tile_x + tx_low as u32) as usize;
            let idx11 = (ty_high as u32 * tile_x + tx_high as u32) as usize;

            let v00 = lut[idx00][v] as f32;
            let v10 = lut[idx10][v] as f32;
            let v01 = lut[idx01][v] as f32;
            let v11 = lut[idx11][v] as f32;

            let top = v00 * (1.0 - wx) + v10 * wx;
            let bot = v01 * (1.0 - wx) + v11 * wx;
            let result = top * (1.0 - wy) + bot * wy;

            out[idx] = result.clamp(0.0, 255.0) as u8;
        }
    }

    out
}

fn softmax(xs: &[f32]) -> Vec<f32> {
    if xs.is_empty() {
        return Vec::new();
    }
    let max = xs.iter().cloned().fold(f32::NEG_INFINITY, f32::max);
    let exps: Vec<f32> = xs.iter().map(|x| (x - max).exp()).collect();
    let sum: f32 = exps.iter().sum();
    if sum <= 0.0 {
        vec![0.0; exps.len()]
    } else {
        exps.iter().map(|e| e / sum).collect()
    }
}
