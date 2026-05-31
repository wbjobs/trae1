use crate::point_cloud::{Point, PointCloud};
use nalgebra::{Matrix3, Matrix4, Vector3, Vector4};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RegistrationResult {
    pub transformation: Matrix4Data,
    pub aligned_points: Vec<Point>,
    pub rmse: f64,
    pub iterations: usize,
    pub converged: bool,
    pub fitness: f64,
    pub inlier_rmse: f64,
    pub correspondence_count: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Matrix4Data {
    pub data: [[f64; 4]; 4],
}

impl From<Matrix4<f64>> for Matrix4Data {
    fn from(mat: Matrix4<f64>) -> Self {
        let data = [
            [mat[(0, 0)], mat[(0, 1)], mat[(0, 2)], mat[(0, 3)]],
            [mat[(1, 0)], mat[(1, 1)], mat[(1, 2)], mat[(1, 3)]],
            [mat[(2, 0)], mat[(2, 1)], mat[(2, 2)], mat[(2, 3)]],
            [mat[(3, 0)], mat[(3, 1)], mat[(3, 2)], mat[(3, 3)]],
        ];
        Matrix4Data { data }
    }
}

impl From<Matrix4Data> for Matrix4<f64> {
    fn from(data: Matrix4Data) -> Self {
        Matrix4::from_row_slice(&[
            data.data[0][0], data.data[0][1], data.data[0][2], data.data[0][3],
            data.data[1][0], data.data[1][1], data.data[1][2], data.data[1][3],
            data.data[2][0], data.data[2][1], data.data[2][2], data.data[2][3],
            data.data[3][0], data.data[3][1], data.data[3][2], data.data[3][3],
        ])
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Correspondence {
    pub source_index: usize,
    pub target_index: usize,
    pub source_point: Point,
    pub target_point: Point,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ICPConfig {
    pub max_iterations: usize,
    pub tolerance: f64,
    pub max_correspondence_distance: f64,
    pub use_robust_kernel: bool,
    pub robust_kernel_threshold: f64,
    pub initial_transformation: Option<Matrix4Data>,
}

impl Default for ICPConfig {
    fn default() -> Self {
        Self {
            max_iterations: 100,
            tolerance: 1e-8,
            max_correspondence_distance: f64::MAX,
            use_robust_kernel: true,
            robust_kernel_threshold: 0.5,
            initial_transformation: None,
        }
    }
}

pub fn point_to_vec3(p: &Point) -> Vector3<f64> {
    Vector3::new(p.x as f64, p.y as f64, p.z as f64)
}

pub fn vec3_to_point(v: &Vector3<f64>) -> Point {
    Point::new(v.x as f32, v.y as f32, v.z as f32)
}

pub fn apply_transformation(points: &[Point], transform: &Matrix4<f64>) -> Vec<Point> {
    points
        .iter()
        .map(|p| {
            let v = point_to_vec3(p);
            let v_homogeneous = Vector4::new(v.x, v.y, v.z, 1.0);
            let transformed = transform * v_homogeneous;
            Point::new(
                transformed.x as f32,
                transformed.y as f32,
                transformed.z as f32,
            )
        })
        .collect()
}

pub fn find_closest_point(
    source: &[Point],
    target: &[Point],
    max_distance: f64,
) -> Vec<Correspondence> {
    let mut correspondences = Vec::new();
    let target_points: Vec<Vector3<f64>> =
        target.iter().map(point_to_vec3).collect();

    for (i, src_pt) in source.iter().enumerate() {
        let src_vec = point_to_vec3(src_pt);
        let mut closest_dist = f64::MAX;
        let mut closest_idx = 0;

        for (j, tgt_vec) in target_points.iter().enumerate() {
            let dist = (src_vec - tgt_vec).norm();
            if dist < closest_dist {
                closest_dist = dist;
                closest_idx = j;
            }
        }

        if closest_dist <= max_distance {
            correspondences.push(Correspondence {
                source_index: i,
                target_index: closest_idx,
                source_point: *src_pt,
                target_point: target[closest_idx],
            });
        }
    }

    correspondences
}

pub fn compute_rigid_transformation(
    correspondences: &[Correspondence],
) -> Option<Matrix4<f64>> {
    if correspondences.len() < 3 {
        return None;
    }

    let n = correspondences.len() as f64;

    let mut centroid_src = Vector3::zeros();
    let mut centroid_tgt = Vector3::zeros();

    for corr in correspondences {
        centroid_src += point_to_vec3(&corr.source_point);
        centroid_tgt += point_to_vec3(&corr.target_point);
    }

    centroid_src /= n;
    centroid_tgt /= n;

    let mut h = Matrix3::zeros();
    for corr in correspondences {
        let src = point_to_vec3(&corr.source_point) - centroid_src;
        let tgt = point_to_vec3(&corr.target_point) - centroid_tgt;
        h += src * tgt.transpose();
    }

    let svd = h.svd(true, true);
    let u = svd.u.unwrap();
    let v_t = svd.v_t.unwrap();
    let s = svd.singular_values;

    let mut d = Matrix3::identity();
    if (u.determinant() * v_t.determinant()) < 0.0 {
        d[(2, 2)] = -1.0;
    }

    let r = u * d * v_t;
    let t = centroid_tgt - r * centroid_src;

    let mut transform = Matrix4::identity();
    transform.fixed_view_mut::<3, 3>(0, 0).copy_from(&r);
    transform.fixed_view_mut::<3, 1>(0, 3).copy_from(&t);

    Some(transform)
}

pub fn compute_rmse(correspondences: &[Correspondence]) -> f64 {
    if correspondences.is_empty() {
        return f64::MAX;
    }

    let sum_squared: f64 = correspondences
        .iter()
        .map(|c| {
            let src = point_to_vec3(&c.source_point);
            let tgt = point_to_vec3(&c.target_point);
            (src - tgt).norm_squared()
        })
        .sum();

    (sum_squared / correspondences.len() as f64).sqrt()
}

pub fn point_to_point_icp(
    source: &PointCloud,
    target: &PointCloud,
    config: &ICPConfig,
) -> RegistrationResult {
    let mut current_transform = config
        .initial_transformation
        .clone()
        .map(Matrix4::from)
        .unwrap_or_else(Matrix4::identity);

    let mut aligned_points = source.points.clone();
    let mut previous_rmse = f64::MAX;
    let mut iterations = 0;
    let mut converged = false;
    let mut final_rmse = f64::MAX;
    let mut final_fitness = 0.0;
    let mut final_inlier_rmse = 0.0;
    let mut final_correspondence_count = 0;

    for iteration in 0..config.max_iterations {
        let transformed = apply_transformation(&aligned_points, &current_transform);

        let correspondences = find_closest_point(
            &transformed,
            &target.points,
            config.max_correspondence_distance,
        );

        if correspondences.len() < 3 {
            break;
        }

        let rmse = compute_rmse(&correspondences);

        let mut filtered_correspondences = correspondences;
        if config.use_robust_kernel {
            let threshold = if rmse.is_finite() && rmse > 0.0 {
                rmse * config.robust_kernel_threshold
            } else {
                config.robust_kernel_threshold
            };

            filtered_correspondences.retain(|c| {
                let src = point_to_vec3(&c.source_point);
                let tgt = point_to_vec3(&c.target_point);
                (src - tgt).norm() < threshold
            });

            if filtered_correspondences.len() < 3 {
                filtered_correspondences = correspondences;
            }
        }

        if filtered_correspondences.len() < 3 {
            break;
        }

        let inlier_rmse = compute_rmse(&filtered_correspondences);
        let fitness = filtered_correspondences.len() as f64 / source.points.len() as f64;

        if let Some(delta_transform) =
            compute_rigid_transformation(&filtered_correspondences)
        {
            current_transform = delta_transform * current_transform;

            if let Some(src_centered) =
                compute_rigid_transformation(&filtered_correspondences)
            {
                aligned_points = apply_transformation(&aligned_points, &src_centered);
            }

            final_rmse = inlier_rmse;
            final_fitness = fitness;
            final_inlier_rmse = inlier_rmse;
            final_correspondence_count = filtered_correspondences.len();

            let delta = (previous_rmse - inlier_rmse).abs();
            if delta < config.tolerance && iteration > 0 {
                converged = true;
                iterations = iteration + 1;
                break;
            }

            previous_rmse = inlier_rmse;
        } else {
            break;
        }

        iterations = iteration + 1;
    }

    let final_transform = current_transform;
    let final_aligned = apply_transformation(&source.points, &final_transform);

    RegistrationResult {
        transformation: Matrix4Data::from(final_transform),
        aligned_points: final_aligned,
        rmse: final_rmse,
        iterations,
        converged,
        fitness: final_fitness,
        inlier_rmse: final_inlier_rmse,
        correspondence_count: final_correspondence_count,
    }
}

pub fn manual_registration(
    source: &PointCloud,
    target: &PointCloud,
    correspondences: &[Correspondence],
) -> Option<RegistrationResult> {
    if correspondences.len() < 3 {
        return None;
    }

    let transform = compute_rigid_transformation(correspondences)?;
    let aligned_points = apply_transformation(&source.points, &transform);

    let transformed_correspondences: Vec<Correspondence> = correspondences
        .iter()
        .map(|c| {
            let src_transformed = {
                let v = point_to_vec3(&c.source_point);
                let v_homogeneous = Vector4::new(v.x, v.y, v.z, 1.0);
                let transformed = transform * v_homogeneous;
                Point::new(
                    transformed.x as f32,
                    transformed.y as f32,
                    transformed.z as f32,
                )
            };

            Correspondence {
                source_index: c.source_index,
                target_index: c.target_index,
                source_point: src_transformed,
                target_point: c.target_point,
            }
        })
        .collect();

    let rmse = compute_rmse(&transformed_correspondences);

    Some(RegistrationResult {
        transformation: Matrix4Data::from(transform),
        aligned_points,
        rmse,
        iterations: 0,
        converged: true,
        fitness: 1.0,
        inlier_rmse: rmse,
        correspondence_count: correspondences.len(),
    })
}

pub fn combine_transformations(
    initial: &Matrix4Data,
    icp_result: &Matrix4Data,
) -> Matrix4Data {
    let t1: Matrix4<f64> = initial.clone().into();
    let t2: Matrix4<f64> = icp_result.clone().into();
    Matrix4Data::from(t2 * t1)
}

pub fn transform_point_cloud(
    cloud: &PointCloud,
    transform: &Matrix4Data,
) -> PointCloud {
    let matrix: Matrix4<f64> = transform.clone().into();
    let points = apply_transformation(&cloud.points, &matrix);

    let mut result = PointCloud::new(points, cloud.colors.clone(), cloud.normals.clone());
    result
}

pub fn merge_point_clouds(cloud1: &PointCloud, cloud2: &PointCloud) -> PointCloud {
    let mut points = cloud1.points.clone();
    points.extend_from_slice(&cloud2.points);

    let mut colors = cloud1.colors.clone();
    for i in 0..cloud2.colors.len().min(cloud2.points.len()) {
        let c = &cloud2.colors[i];
        colors.push(crate::point_cloud::Color::new(
            (c.r as f32 * 0.7) as u8,
            (c.g as f32 * 0.9) as u8,
            (c.b as f32 * 1.0) as u8,
        ));
    }
    while colors.len() < points.len() {
        colors.push(crate::point_cloud::Color::new(180, 200, 255));
    }

    PointCloud::new(points, colors, None)
}

pub fn compute_transformation_matrix_from_correspondences(
    correspondences: &[Correspondence],
) -> Option<Matrix4Data> {
    compute_rigid_transformation(correspondences).map(Matrix4Data::from)
}

pub fn evaluate_registration(
    source: &PointCloud,
    target: &PointCloud,
    transform: &Matrix4Data,
    max_distance: f64,
) -> RegistrationEvaluation {
    let matrix: Matrix4<f64> = transform.clone().into();
    let aligned = apply_transformation(&source.points, &matrix);

    let correspondences = find_closest_point(&aligned, &target.points, max_distance);

    let rmse = if correspondences.is_empty() {
        f64::MAX
    } else {
        compute_rmse(&correspondences)
    };

    let fitness = correspondences.len() as f64 / source.points.len() as f64;

    RegistrationEvaluation {
        rmse,
        fitness,
        correspondence_count: correspondences.len(),
        inlier_rmse: rmse,
        max_correspondence_distance: max_distance,
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RegistrationEvaluation {
    pub rmse: f64,
    pub fitness: f64,
    pub correspondence_count: usize,
    pub inlier_rmse: f64,
    pub max_correspondence_distance: f64,
}
