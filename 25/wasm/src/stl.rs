use crate::math::Vec3;

#[derive(Clone, Debug)]
pub struct Mesh {
    pub vertices: Vec<Vec3>,
    pub indices: Vec<u32>,
}

impl Mesh {
    pub fn empty() -> Self {
        Mesh {
            vertices: Vec::new(),
            indices: Vec::new(),
        }
    }

    pub fn triangles(&self) -> impl Iterator<Item = (Vec3, Vec3, Vec3)> + '_ {
        self.indices.chunks_exact(3).map(|t| {
            (
                self.vertices[t[0] as usize],
                self.vertices[t[1] as usize],
                self.vertices[t[2] as usize],
            )
        })
    }

    pub fn bbox(&self) -> (Vec3, Vec3) {
        if self.vertices.is_empty() {
            return ([0.0, 0.0, 0.0], [0.0, 0.0, 0.0]);
        }
        let mut mn = self.vertices[0];
        let mut mx = self.vertices[0];
        for v in &self.vertices[1..] {
            mn[0] = mn[0].min(v[0]);
            mn[1] = mn[1].min(v[1]);
            mn[2] = mn[2].min(v[2]);
            mx[0] = mx[0].max(v[0]);
            mx[1] = mx[1].max(v[1]);
            mx[2] = mx[2].max(v[2]);
        }
        (mn, mx)
    }
}

pub fn parse_stl(bytes: &[u8]) -> Result<Mesh, String> {
    if bytes.is_empty() {
        return Err("empty file".into());
    }
    let is_ascii = bytes.iter().take(5).any(|&b| b == b's' || b == b'S')
        && (bytes.len() < 84
            || (bytes[0] == b's' || bytes[0] == b'S' || bytes[0] == b'o' || bytes[0] == b'O'));
    if is_ascii {
        parse_stl_ascii(bytes)
    } else {
        parse_stl_binary(bytes)
    }
}

fn parse_stl_binary(bytes: &[u8]) -> Result<Mesh, String> {
    if bytes.len() < 84 {
        return Err("binary STL too short".into());
    }
    let count = u32::from_le_bytes([bytes[80], bytes[81], bytes[82], bytes[83]]) as usize;
    let expected = 84 + count * 50;
    if bytes.len() < expected {
        return Err(format!("binary STL truncated, need {} bytes", expected));
    }
    let mut mesh = Mesh::empty();
    let mut vert_map: std::collections::HashMap<(i32, i32, i32), u32> =
        std::collections::HashMap::new();
    const QUANT: f32 = 1e6;
    let mut add = |m: &mut Mesh, x: f32, y: f32, z: f32| -> u32 {
        let key = (
            (x * QUANT).round() as i32,
            (y * QUANT).round() as i32,
            (z * QUANT).round() as i32,
        );
        if let Some(&i) = vert_map.get(&key) {
            return i;
        }
        let idx = m.vertices.len() as u32;
        m.vertices.push([x, y, z]);
        vert_map.insert(key, idx);
        idx
    };
    for i in 0..count {
        let base = 84 + i * 50;
        let nx = f32::from_le_bytes([
            bytes[base],
            bytes[base + 1],
            bytes[base + 2],
            bytes[base + 3],
        ]);
        let _ = nx;
        let mut vs = [[0.0f32; 3]; 3];
        for k in 0..3 {
            let off = base + 4 + k * 12;
            vs[k] = [
                f32::from_le_bytes([bytes[off], bytes[off + 1], bytes[off + 2], bytes[off + 3]]),
                f32::from_le_bytes([
                    bytes[off + 4],
                    bytes[off + 5],
                    bytes[off + 6],
                    bytes[off + 7],
                ]),
                f32::from_le_bytes([
                    bytes[off + 8],
                    bytes[off + 9],
                    bytes[off + 10],
                    bytes[off + 11],
                ]),
            ];
        }
        let a = add(&mut mesh, vs[0][0], vs[0][1], vs[0][2]);
        let b = add(&mut mesh, vs[1][0], vs[1][1], vs[1][2]);
        let c = add(&mut mesh, vs[2][0], vs[2][1], vs[2][2]);
        if a != b && b != c && c != a {
            mesh.indices.push(a);
            mesh.indices.push(b);
            mesh.indices.push(c);
        }
    }
    Ok(mesh)
}

fn parse_stl_ascii(bytes: &[u8]) -> Result<Mesh, String> {
    let text = std::str::from_utf8(bytes).map_err(|e| format!("ascii decode: {}", e))?;
    let mut mesh = Mesh::empty();
    let mut vert_map: std::collections::HashMap<(i32, i32, i32), u32> =
        std::collections::HashMap::new();
    const QUANT: f32 = 1e6;
    let mut add = |m: &mut Mesh, x: f32, y: f32, z: f32| -> u32 {
        let key = (
            (x * QUANT).round() as i32,
            (y * QUANT).round() as i32,
            (z * QUANT).round() as i32,
        );
        if let Some(&i) = vert_map.get(&key) {
            return i;
        }
        let idx = m.vertices.len() as u32;
        m.vertices.push([x, y, z]);
        vert_map.insert(key, idx);
        idx
    };
    let mut current: [u32; 3] = [0, 0, 0];
    let mut n_verts = 0usize;
    for line in text.lines() {
        let l = line.trim();
        if l.starts_with("vertex") {
            let mut it = l.split_whitespace().skip(1);
            let x: f32 = it.next().and_then(|s| s.parse().ok()).unwrap_or(0.0);
            let y: f32 = it.next().and_then(|s| s.parse().ok()).unwrap_or(0.0);
            let z: f32 = it.next().and_then(|s| s.parse().ok()).unwrap_or(0.0);
            if n_verts < 3 {
                current[n_verts] = add(&mut mesh, x, y, z);
                n_verts += 1;
            }
        } else if l.starts_with("endfacet") {
            if n_verts == 3 {
                mesh.indices.push(current[0]);
                mesh.indices.push(current[1]);
                mesh.indices.push(current[2]);
            }
            n_verts = 0;
        }
    }
    Ok(mesh)
}
