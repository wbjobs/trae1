use crate::math::*;
use crate::stl::Mesh;

pub struct SdfField {
    pub nx: usize,
    pub ny: usize,
    pub nz: usize,
    pub bmin: Vec3,
    pub bmax: Vec3,
    pub size: Vec3,
    pub cell: Vec3,
    pub grid: Vec<f32>,
}

impl SdfField {
    pub fn at(&self, i: usize, j: usize, k: usize) -> f32 {
        self.grid[(k * (self.nx * self.ny)) + (j * self.nx) + i]
    }
    pub fn set(&mut self, i: usize, j: usize, k: usize, v: f32) {
        self.grid[(k * (self.nx * self.ny)) + (j * self.nx) + i] = v;
    }
    pub fn corner(&self, i: usize, j: usize, k: usize) -> Vec3 {
        [
            self.bmin[0] + self.cell[0] * (i as f32),
            self.bmin[1] + self.cell[1] * (j as f32),
            self.bmin[2] + self.cell[2] * (k as f32),
        ]
    }
}

pub fn voxelize(mesh: &Mesh, grid_size: usize, pad_ratio: f32) -> SdfField {
    let (mut bmin, mut bmax) = mesh.bbox();
    let mut diag = v_sub(bmax, bmin);
    if diag[0] < EPS {
        diag[0] = 1.0;
    }
    if diag[1] < EPS {
        diag[1] = 1.0;
    }
    if diag[2] < EPS {
        diag[2] = 1.0;
    }
    let pad = [
        diag[0] * pad_ratio,
        diag[1] * pad_ratio,
        diag[2] * pad_ratio,
    ];
    bmin = [
        bmin[0] - pad[0],
        bmin[1] - pad[1],
        bmin[2] - pad[2],
    ];
    bmax = [
        bmax[0] + pad[0],
        bmax[1] + pad[1],
        bmax[2] + pad[2],
    ];
    let d = v_sub(bmax, bmin);
    let max_side = d[0].max(d[1]).max(d[2]);
    let cell_len = max_side / (grid_size as f32);
    let nx = (d[0] / cell_len).ceil() as usize + 1;
    let ny = (d[1] / cell_len).ceil() as usize + 1;
    let nz = (d[2] / cell_len).ceil() as usize + 1;
    let size = d;
    let cell = [
        size[0] / (nx as f32 - 1.0).max(1.0),
        size[1] / (ny as f32 - 1.0).max(1.0),
        size[2] / (nz as f32 - 1.0).max(1.0),
    ];
    let total = nx * ny * nz;
    let mut grid = vec![0.0f32; total];

    let mut tri_grid = TriGrid::build(mesh, cell_len);

    for k in 0..nz {
        for j in 0..ny {
            for i in 0..nx {
                let p = [
                    bmin[0] + cell[0] * (i as f32),
                    bmin[1] + cell[1] * (j as f32),
                    bmin[2] + cell[2] * (k as f32),
                ];
                let inside = point_inside_mesh(mesh, p, &tri_grid);
                let d_surf = point_surface_distance(mesh, p, cell_len * 3.0, &tri_grid);
                let sdf = if inside { -d_surf } else { d_surf };
                grid[(k * (nx * ny)) + (j * nx) + i] = sdf;
            }
        }
    }

    SdfField {
        nx,
        ny,
        nz,
        bmin,
        bmax,
        size,
        cell,
        grid,
    }
}

pub struct TriGrid {
    cell_len: f32,
    bmin: Vec3,
    nx: usize,
    ny: usize,
    nz: usize,
    cells: Vec<Vec<u32>>,
}

impl TriGrid {
    pub fn build(mesh: &Mesh, cell_len: f32) -> TriGrid {
        let (bmin, bmax) = mesh.bbox();
        let d = v_sub(bmax, bmin);
        let nx = (d[0] / cell_len).ceil() as usize + 1;
        let ny = (d[1] / cell_len).ceil() as usize + 1;
        let nz = (d[2] / cell_len).ceil() as usize + 1;
        let total = (nx * ny * nz).max(1);
        let mut cells = vec![Vec::new(); total];
        let n_tris = mesh.indices.len() / 3;
        for t in 0..n_tris {
            let a = mesh.vertices[mesh.indices[t * 3] as usize];
            let b = mesh.vertices[mesh.indices[t * 3 + 1] as usize];
            let c = mesh.vertices[mesh.indices[t * 3 + 2] as usize];
            let tmin = [
                a[0].min(b[0]).min(c[0]),
                a[1].min(b[1]).min(c[1]),
                a[2].min(b[2]).min(c[2]),
            ];
            let tmax = [
                a[0].max(b[0]).max(c[0]),
                a[1].max(b[1]).max(c[1]),
                a[2].max(b[2]).max(c[2]),
            ];
            let imin = ((tmin[0] - bmin[0]) / cell_len).floor() as isize;
            let jmin = ((tmin[1] - bmin[1]) / cell_len).floor() as isize;
            let kmin = ((tmin[2] - bmin[2]) / cell_len).floor() as isize;
            let imax = ((tmax[0] - bmin[0]) / cell_len).floor() as isize;
            let jmax = ((tmax[1] - bmin[1]) / cell_len).floor() as isize;
            let kmax = ((tmax[2] - bmin[2]) / cell_len).floor() as isize;
            let imin = imin.max(0) as usize;
            let jmin = jmin.max(0) as usize;
            let kmin = kmin.max(0) as usize;
            let imax = (imax as usize).min(nx - 1);
            let jmax = (jmax as usize).min(ny - 1);
            let kmax = (kmax as usize).min(nz - 1);
            for k in kmin..=kmax {
                for j in jmin..=jmax {
                    for i in imin..=imax {
                        let idx = (k * (nx * ny)) + (j * nx) + i;
                        cells[idx].push(t as u32);
                    }
                }
            }
        }
        TriGrid {
            cell_len,
            bmin,
            nx,
            ny,
            nz,
            cells,
        }
    }

    pub fn candidate_tris(&self, p: Vec3) -> Vec<u32> {
        let i = ((p[0] - self.bmin[0]) / self.cell_len).floor() as isize;
        let j = ((p[1] - self.bmin[1]) / self.cell_len).floor() as isize;
        let k = ((p[2] - self.bmin[2]) / self.cell_len).floor() as isize;
        let imin = (i - 1).max(0) as usize;
        let jmin = (j - 1).max(0) as usize;
        let kmin = (k - 1).max(0) as usize;
        let imax = ((i + 1) as usize).min(self.nx - 1);
        let jmax = ((j + 1) as usize).min(self.ny - 1);
        let kmax = ((k + 1) as usize).min(self.nz - 1);
        let mut out = Vec::new();
        for kk in kmin..=kmax {
            for jj in jmin..=jmax {
                for ii in imin..=imax {
                    let idx = (kk * (self.nx * self.ny)) + (jj * self.nx) + ii;
                    out.extend_from_slice(&self.cells[idx]);
                }
            }
        }
        out
    }
}

fn point_inside_mesh(mesh: &Mesh, p: Vec3, grid: &TriGrid) -> bool {
    let dir: Vec3 = [1.0, 0.0001327, 0.0000739];
    let mut count = 0i32;
    let candidates = grid.candidate_tris(p);
    let n_tris = mesh.indices.len() / 3;
    let tris: Box<dyn Iterator<Item = usize>> = if candidates.len() < n_tris / 2 + 1 {
        Box::new(candidates.into_iter().map(|t| t as usize))
    } else {
        Box::new(0..n_tris)
    };
    for t in tris {
        let a = mesh.vertices[mesh.indices[t * 3] as usize];
        let b = mesh.vertices[mesh.indices[t * 3 + 1] as usize];
        let c = mesh.vertices[mesh.indices[t * 3 + 2] as usize];
        if ray_tri_intersect(p, dir, a, b, c) {
            count += 1;
        }
    }
    (count & 1) == 1
}

fn ray_tri_intersect(o: Vec3, d: Vec3, a: Vec3, b: Vec3, c: Vec3) -> bool {
    let ea = v_sub(b, a);
    let eb = v_sub(c, a);
    let pvec = v_cross(d, eb);
    let det = v_dot(ea, pvec);
    if det.abs() < EPS {
        return false;
    }
    let inv_det = 1.0 / det;
    let tvec = v_sub(o, a);
    let u = v_dot(tvec, pvec) * inv_det;
    if u < -1e-5 || u > 1.0 + 1e-5 {
        return false;
    }
    let qvec = v_cross(tvec, ea);
    let v = v_dot(d, qvec) * inv_det;
    if v < -1e-5 || u + v > 1.0 + 1e-5 {
        return false;
    }
    let t = v_dot(eb, qvec) * inv_det;
    t > EPS
}

fn point_surface_distance(mesh: &Mesh, p: Vec3, _max_search: f32, grid: &TriGrid) -> f32 {
    let candidates = grid.candidate_tris(p);
    let mut best = f32::INFINITY;
    let n_tris = mesh.indices.len() / 3;
    let tris: Box<dyn Iterator<Item = usize>> = if !candidates.is_empty() {
        Box::new(candidates.into_iter().map(|t| t as usize))
    } else {
        Box::new(0..n_tris)
    };
    for t in tris {
        let a = mesh.vertices[mesh.indices[t * 3] as usize];
        let b = mesh.vertices[mesh.indices[t * 3 + 1] as usize];
        let c = mesh.vertices[mesh.indices[t * 3 + 2] as usize];
        let d = point_tri_distance(p, a, b, c);
        if d < best {
            best = d;
        }
    }
    if best == f32::INFINITY {
        1e6
    } else {
        best
    }
}

fn point_tri_distance(p: Vec3, a: Vec3, b: Vec3, c: Vec3) -> f32 {
    let ab = v_sub(b, a);
    let ac = v_sub(c, a);
    let ap = v_sub(p, a);
    let d1 = v_dot(ab, ap);
    let d2 = v_dot(ac, ap);
    if d1 <= 0.0 && d2 <= 0.0 {
        return v_len(ap);
    }
    let bp = v_sub(p, b);
    let d3 = v_dot(ab, bp);
    let d4 = v_dot(ac, bp);
    if d3 >= 0.0 && d4 <= d3 {
        return v_len(bp);
    }
    let vc = d1 * d4 - d3 * d2;
    if vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 {
        let v = d1 / (d1 - d3);
        return v_len(v_sub(p, v_add(a, v_scale(ab, v))));
    }
    let cp = v_sub(p, c);
    let d5 = v_dot(ab, cp);
    let d6 = v_dot(ac, cp);
    if d6 >= 0.0 && d5 <= d6 {
        return v_len(cp);
    }
    let vb = d5 * d2 - d1 * d6;
    if vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 {
        let w = d2 / (d2 - d6);
        return v_len(v_sub(p, v_add(a, v_scale(ac, w))));
    }
    let va = d3 * d6 - d5 * d4;
    if va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0 {
        let bc = v_sub(c, b);
        let w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return v_len(v_sub(p, v_add(b, v_scale(bc, w))));
    }
    let det = v_dot(v_cross(ab, ac), v_cross(ab, ac));
    if det.abs() < EPS {
        return v_len(ap);
    }
    let v = vc / det;
    let w = vb / det;
    v_len(v_sub(p, v_add(a, v_add(v_scale(ab, v), v_scale(ac, w)))))
}

pub fn boolean_op(a: &SdfField, b: &SdfField, op: &str, coplanar_tol: f32) -> SdfField {
    let bmin = [
        a.bmin[0].min(b.bmin[0]),
        a.bmin[1].min(b.bmin[1]),
        a.bmin[2].min(b.bmin[2]),
    ];
    let bmax = [
        a.bmax[0].max(b.bmax[0]),
        a.bmax[1].max(b.bmax[1]),
        a.bmax[2].max(b.bmax[2]),
    ];
    let nx = a.nx.max(b.nx);
    let ny = a.ny.max(b.ny);
    let nz = a.nz.max(b.nz);
    let size = v_sub(bmax, bmin);
    let cell = [
        size[0] / (nx as f32 - 1.0).max(1.0),
        size[1] / (ny as f32 - 1.0).max(1.0),
        size[2] / (nz as f32 - 1.0).max(1.0),
    ];
    let total = nx * ny * nz;
    let mut grid = vec![0.0f32; total];

    let sample = |f: &SdfField, p: Vec3| -> f32 {
        let fi = ((p[0] - f.bmin[0]) / f.cell[0].max(EPS))
            .clamp(0.0, (f.nx as f32 - 1.0).max(0.0));
        let fj = ((p[1] - f.bmin[1]) / f.cell[1].max(EPS))
            .clamp(0.0, (f.ny as f32 - 1.0).max(0.0));
        let fk = ((p[2] - f.bmin[2]) / f.cell[2].max(EPS))
            .clamp(0.0, (f.nz as f32 - 1.0).max(0.0));
        let i0 = fi as usize;
        let j0 = fj as usize;
        let k0 = fk as usize;
        let i1 = (i0 + 1).min(f.nx - 1);
        let j1 = (j0 + 1).min(f.ny - 1);
        let k1 = (k0 + 1).min(f.nz - 1);
        let u = fi - i0 as f32;
        let v = fj - j0 as f32;
        let w = fk - k0 as f32;
        let c000 = f.at(i0, j0, k0);
        let c100 = f.at(i1, j0, k0);
        let c010 = f.at(i0, j1, k0);
        let c110 = f.at(i1, j1, k0);
        let c001 = f.at(i0, j0, k1);
        let c101 = f.at(i1, j0, k1);
        let c011 = f.at(i0, j1, k1);
        let c111 = f.at(i1, j1, k1);
        let x00 = c000 * (1.0 - u) + c100 * u;
        let x10 = c010 * (1.0 - u) + c110 * u;
        let x01 = c001 * (1.0 - u) + c101 * u;
        let x11 = c011 * (1.0 - u) + c111 * u;
        let y0 = x00 * (1.0 - v) + x10 * v;
        let y1 = x01 * (1.0 - v) + x11 * v;
        y0 * (1.0 - w) + y1 * w
    };

    for k in 0..nz {
        for j in 0..ny {
            for i in 0..nx {
                let p = [
                    bmin[0] + cell[0] * (i as f32),
                    bmin[1] + cell[1] * (j as f32),
                    bmin[2] + cell[2] * (k as f32),
                ];
                let va = sample(a, p);
                let vb = sample(b, p);
                let v = match op {
                    "union" => va.min(vb),
                    "intersect" => va.max(vb),
                    "difference" => (va).max(-vb),
                    _ => va,
                };
                grid[(k * (nx * ny)) + (j * nx) + i] = v;
            }
        }
    }

    let mut out = SdfField {
        nx,
        ny,
        nz,
        bmin,
        bmax,
        size,
        cell,
        grid,
    };

    if op == "difference" {
        resolve_coplanar_artifacts(&mut out, a, b, coplanar_tol);
    }

    out
}

fn resolve_coplanar_artifacts(
    out: &mut SdfField,
    a: &SdfField,
    b: &SdfField,
    tol: f32,
) {
    let t = tol.max(out.cell[0].max(out.cell[1]).max(out.cell[2]) * 0.75);
    let mut mask = vec![false; out.nx * out.ny * out.nz];
    for k in 0..out.nz {
        for j in 0..out.ny {
            for i in 0..out.nx {
                let idx = (k * (out.nx * out.ny)) + (j * out.nx) + i;
                let v = out.grid[idx];
                if v.abs() < t {
                    mask[idx] = true;
                }
            }
        }
    }
    let mut dilated = mask.clone();
    for _ in 0..1 {
        let prev = dilated.clone();
        for k in 0..out.nz {
            for j in 0..out.ny {
                for i in 0..out.nx {
                    let idx = (k * (out.nx * out.ny)) + (j * out.nx) + i;
                    if prev[idx] {
                        for (di, dj, dk) in &[
                            (1isize, 0isize, 0isize),
                            (-1, 0, 0),
                            (0, 1, 0),
                            (0, -1, 0),
                            (0, 0, 1),
                            (0, 0, -1),
                        ] {
                            let ni = (i as isize + di).clamp(0, out.nx as isize - 1) as usize;
                            let nj = (j as isize + dj).clamp(0, out.ny as isize - 1) as usize;
                            let nk = (k as isize + dk).clamp(0, out.nz as isize - 1) as usize;
                            let nidx = (nk * (out.nx * out.ny)) + (nj * out.nx) + ni;
                            dilated[nidx] = true;
                        }
                    }
                }
            }
        }
    }

    let _ = (a, b);
    for k in 0..out.nz {
        for j in 0..out.ny {
            for i in 0..out.nx {
                let idx = (k * (out.nx * out.ny)) + (j * out.nx) + i;
                if !dilated[idx] {
                    continue;
                }
                if !mask[idx] {
                    continue;
                }
                let v = out.grid[idx];
                let neighbors = [
                    (i + 1, j, k),
                    (i.saturating_sub(1), j, k),
                    (i, j + 1, k),
                    (i, j.saturating_sub(1), k),
                    (i, j, k + 1),
                    (i, j, k.saturating_sub(1)),
                ];
                let mut outside_max = f32::NEG_INFINITY;
                let mut inside_min = f32::INFINITY;
                for (ni, nj, nk) in neighbors {
                    if ni >= out.nx || nj >= out.ny || nk >= out.nz {
                        continue;
                    }
                    let nv = out.at(ni, nj, nk);
                    if nv > 0.0 && nv > outside_max {
                        outside_max = nv;
                    }
                    if nv < 0.0 && nv < inside_min {
                        inside_min = nv;
                    }
                }
                if outside_max == f32::NEG_INFINITY {
                    if inside_min != f32::INFINITY {
                        out.set(i, j, k, inside_min * 0.5 + v * 0.5);
                    }
                } else if inside_min == f32::INFINITY {
                    out.set(i, j, k, outside_max * 0.5 + v * 0.5);
                } else if v >= 0.0 {
                    out.set(i, j, k, outside_max.max(v) + t * 0.5);
                } else {
                    out.set(i, j, k, inside_min.min(v) - t * 0.5);
                }
            }
        }
    }
}
