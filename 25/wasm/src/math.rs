use std::f32::consts::PI;

pub const EPS: f32 = 1e-6;
pub const EPS_COS: f32 = (PI / 180.0 * 0.5).cos();

pub type Vec3 = [f32; 3];

#[inline]
pub fn v_sub(a: Vec3, b: Vec3) -> Vec3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

#[inline]
pub fn v_add(a: Vec3, b: Vec3) -> Vec3 {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

#[inline]
pub fn v_scale(a: Vec3, s: f32) -> Vec3 {
    [a[0] * s, a[1] * s, a[2] * s]
}

#[inline]
pub fn v_dot(a: Vec3, b: Vec3) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

#[inline]
pub fn v_cross(a: Vec3, b: Vec3) -> Vec3 {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

#[inline]
pub fn v_len(a: Vec3) -> f32 {
    v_dot(a, a).sqrt()
}

#[inline]
pub fn v_norm(a: Vec3) -> Vec3 {
    let l = v_len(a);
    if l < EPS {
        [0.0, 0.0, 0.0]
    } else {
        v_scale(a, 1.0 / l)
    }
}

#[inline]
pub fn tri_normal(a: Vec3, b: Vec3, c: Vec3) -> Vec3 {
    v_norm(v_cross(v_sub(b, a), v_sub(c, a)))
}

#[inline]
pub fn v_eq(a: Vec3, b: Vec3, eps: f32) -> bool {
    (a[0] - b[0]).abs() < eps && (a[1] - b[1]).abs() < eps && (a[2] - b[2]).abs() < eps
}
