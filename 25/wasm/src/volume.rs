use crate::math::*;
use crate::stl::Mesh;

pub fn mesh_volume(mesh: &Mesh) -> f64 {
    let mut sum: f64 = 0.0;
    for t in 0..(mesh.indices.len() / 3) {
        let a = mesh.vertices[mesh.indices[t * 3] as usize];
        let b = mesh.vertices[mesh.indices[t * 3 + 1] as usize];
        let c = mesh.vertices[mesh.indices[t * 3 + 2] as usize];
        let cross = v_cross(b, c);
        let dot = v_dot(a, cross) as f64;
        sum += dot;
    }
    (sum / 6.0).abs()
}

pub fn stl_volume(bytes: &[u8]) -> Result<f64, String> {
    let mesh = crate::stl::parse_stl(bytes)?;
    Ok(mesh_volume(&mesh))
}
