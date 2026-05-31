use crate::math::*;
use crate::stl::Mesh;
use crate::voxel::SdfField;

include!(concat!(env!("OUT_DIR"), "/mc_tables.rs"));

fn vertex_interp(p1: Vec3, p2: Vec3, v1: f32, v2: f32) -> Vec3 {
    let mu = (-v1) / (v2 - v1);
    [
        p1[0] + mu * (p2[0] - p1[0]),
        p1[1] + mu * (p2[1] - p1[1]),
        p1[2] + mu * (p2[2] - p1[2]),
    ]
}

pub fn extract_surface(field: &SdfField, iso: f32) -> Mesh {
    let mut mesh = Mesh::empty();
    if field.nx < 2 || field.ny < 2 || field.nz < 2 {
        return mesh;
    }

    let mut unique: std::collections::HashMap<(i32, i32, i32), u32> =
        std::collections::HashMap::new();
    const QUANT: f32 = 1e6;
    let mut add_vtx = |mesh: &mut Mesh, p: Vec3| -> u32 {
        let key = (
            (p[0] * QUANT).round() as i32,
            (p[1] * QUANT).round() as i32,
            (p[2] * QUANT).round() as i32,
        );
        if let Some(&i) = unique.get(&key) {
            return i;
        }
        let idx = mesh.vertices.len() as u32;
        mesh.vertices.push(p);
        unique.insert(key, idx);
        idx
    };

    for k in 0..field.nz - 1 {
        for j in 0..field.ny - 1 {
            for i in 0..field.nx - 1 {
                let mut val = [0.0f32; 8];
                let mut p = [[0.0f32; 3]; 8];
                for idx in 0..8 {
                    let (di, dj, dk) = match idx {
                        0 => (0, 0, 0),
                        1 => (1, 0, 0),
                        2 => (1, 1, 0),
                        3 => (0, 1, 0),
                        4 => (0, 0, 1),
                        5 => (1, 0, 1),
                        6 => (1, 1, 1),
                        7 => (0, 1, 1),
                        _ => unreachable!(),
                    };
                    val[idx] = field.at(i + di, j + dj, k + dk) - iso;
                    p[idx] = field.corner(i + di, j + dj, k + dk);
                }
                let mut cube_idx = 0usize;
                if val[0] < 0.0 {
                    cube_idx |= 1;
                }
                if val[1] < 0.0 {
                    cube_idx |= 2;
                }
                if val[2] < 0.0 {
                    cube_idx |= 4;
                }
                if val[3] < 0.0 {
                    cube_idx |= 8;
                }
                if val[4] < 0.0 {
                    cube_idx |= 16;
                }
                if val[5] < 0.0 {
                    cube_idx |= 32;
                }
                if val[6] < 0.0 {
                    cube_idx |= 64;
                }
                if val[7] < 0.0 {
                    cube_idx |= 128;
                }

                let edge_table = EDGE_TABLE[cube_idx];
                if edge_table == 0 {
                    continue;
                }

                let mut vert_list = [[0.0f32; 3]; 12];
                if (edge_table & 1) != 0 {
                    vert_list[0] = vertex_interp(p[0], p[1], val[0], val[1]);
                }
                if (edge_table & 2) != 0 {
                    vert_list[1] = vertex_interp(p[1], p[2], val[1], val[2]);
                }
                if (edge_table & 4) != 0 {
                    vert_list[2] = vertex_interp(p[2], p[3], val[2], val[3]);
                }
                if (edge_table & 8) != 0 {
                    vert_list[3] = vertex_interp(p[3], p[0], val[3], val[0]);
                }
                if (edge_table & 16) != 0 {
                    vert_list[4] = vertex_interp(p[4], p[5], val[4], val[5]);
                }
                if (edge_table & 32) != 0 {
                    vert_list[5] = vertex_interp(p[5], p[6], val[5], val[6]);
                }
                if (edge_table & 64) != 0 {
                    vert_list[6] = vertex_interp(p[6], p[7], val[6], val[7]);
                }
                if (edge_table & 128) != 0 {
                    vert_list[7] = vertex_interp(p[4], p[7], val[4], val[7]);
                }
                if (edge_table & 256) != 0 {
                    vert_list[8] = vertex_interp(p[0], p[4], val[0], val[4]);
                }
                if (edge_table & 512) != 0 {
                    vert_list[9] = vertex_interp(p[1], p[5], val[1], val[5]);
                }
                if (edge_table & 1024) != 0 {
                    vert_list[10] = vertex_interp(p[2], p[6], val[2], val[6]);
                }
                if (edge_table & 2048) != 0 {
                    vert_list[11] = vertex_interp(p[3], p[7], val[3], val[7]);
                }

                let tri_row = &TRI_TABLE[cube_idx];
                for t in (0..tri_row.len()).step_by(3) {
                    if tri_row[t] < 0 {
                        break;
                    }
                    let a = add_vtx(&mut mesh, vert_list[tri_row[t] as usize]);
                    let b = add_vtx(&mut mesh, vert_list[tri_row[t + 1] as usize]);
                    let c = add_vtx(&mut mesh, vert_list[tri_row[t + 2] as usize]);
                    if a != b && b != c && c != a {
                        mesh.indices.push(a);
                        mesh.indices.push(b);
                        mesh.indices.push(c);
                    }
                }
            }
        }
    }

    mesh
}
