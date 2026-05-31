mod math;
mod mc;
mod stl;
mod voxel;
mod volume;

use std::sync::Mutex;

use wasm_bindgen::prelude::*;

static PROGRESS: Mutex<f32> = Mutex::new(0.0);
static MESSAGE: Mutex<String> = Mutex::new(String::new());

fn set_progress(p: f32, msg: &str) {
    if let Ok(mut g) = PROGRESS.lock() {
        *g = p;
    }
    if let Ok(mut g) = MESSAGE.lock() {
        *g = msg.to_string();
    }
}

#[wasm_bindgen]
pub fn get_progress() -> f32 {
    PROGRESS.lock().map(|g| *g).unwrap_or(0.0)
}

#[wasm_bindgen]
pub fn get_message() -> String {
    MESSAGE.lock().map(|g| g.clone()).unwrap_or_default()
}

#[wasm_bindgen]
pub fn boolean_operation(
    stl_a: &[u8],
    stl_b: &[u8],
    op: &str,
    grid_size: usize,
) -> Result<Vec<u8>, JsValue> {
    set_progress(0.0, "解析模型A...");
    let mesh_a = stl::parse_stl(stl_a).map_err(|e| JsValue::from_str(&e))?;
    set_progress(0.1, "解析模型B...");
    let mesh_b = stl::parse_stl(stl_b).map_err(|e| JsValue::from_str(&e))?;

    set_progress(0.2, "体素化模型A...");
    let field_a = voxel::voxelize(&mesh_a, grid_size, 0.02);
    set_progress(0.45, "体素化模型B...");
    let field_b = voxel::voxelize(&mesh_b, grid_size, 0.02);

    set_progress(0.7, format!("执行{}运算（含共面修复）...", op_name(op)).as_str());
    let tol = field_a
        .cell
        .iter()
        .cloned()
        .fold(f32::INFINITY, f32::min)
        .max(1e-4)
        * 0.75;
    let field_out = voxel::boolean_op(&field_a, &field_b, op, tol);

    set_progress(0.85, "重建三角网格...");
    let result = mc::extract_surface(&field_out, 0.0);

    set_progress(0.95, "编码结果...");
    let buf = encode_binary_stl(&result);

    set_progress(1.0, "完成");
    Ok(buf)
}

fn op_name(op: &str) -> &'static str {
    match op {
        "union" => "并集",
        "intersect" => "交集",
        "difference" => "差集",
        _ => "布尔",
    }
}

fn encode_binary_stl(mesh: &stl::Mesh) -> Vec<u8> {
    let n_tri = (mesh.indices.len() / 3) as u32;
    let mut out = Vec::with_capacity(84 + (n_tri as usize) * 50);
    for _ in 0..80 {
        out.push(0u8);
    }
    out.extend_from_slice(&n_tri.to_le_bytes());
    for t in 0..(n_tri as usize) {
        let a = mesh.vertices[mesh.indices[t * 3] as usize];
        let b = mesh.vertices[mesh.indices[t * 3 + 1] as usize];
        let c = mesh.vertices[mesh.indices[t * 3 + 2] as usize];
        let n = math::tri_normal(a, b, c);
        for v in &[n, a, b, c] {
            out.extend_from_slice(&v[0].to_le_bytes());
            out.extend_from_slice(&v[1].to_le_bytes());
            out.extend_from_slice(&v[2].to_le_bytes());
        }
        out.push(0);
        out.push(0);
    }
    out
}

#[wasm_bindgen]
pub fn init_hook() {
    console_error_panic_hook::set_once();
}

#[wasm_bindgen]
pub fn compute_volume(stl_bytes: &[u8]) -> Result<f64, JsValue> {
    volume::stl_volume(stl_bytes).map_err(|e| JsValue::from_str(&e))
}
