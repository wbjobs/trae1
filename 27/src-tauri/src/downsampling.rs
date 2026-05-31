use crate::point_cloud::{Color, Point, PointCloud};
use std::collections::HashMap;

pub fn voxel_grid_downsample(cloud: &PointCloud, voxel_size: f32) -> PointCloud {
    if voxel_size <= 0.0 || cloud.is_empty() {
        return cloud.clone();
    }

    let voxel_map: HashMap<(i32, i32, i32), VoxelAccumulator> = cloud
        .points
        .iter()
        .enumerate()
        .map(|(i, p)| {
            let vx = (p.x / voxel_size).floor() as i32;
            let vy = (p.y / voxel_size).floor() as i32;
            let vz = (p.z / voxel_size).floor() as i32;
            ((vx, vy, vz), (i, p))
        })
        .fold(HashMap::new(), |mut map, (key, (idx, p))| {
            let entry = map.entry(key).or_default();
            entry.indices.push(idx);
            entry.sum_x += p.x;
            entry.sum_y += p.y;
            entry.sum_z += p.z;
            if let Some(color) = cloud.colors.get(idx) {
                entry.sum_r += color.r as u32;
                entry.sum_g += color.g as u32;
                entry.sum_b += color.b as u32;
                entry.count_color += 1;
            }
            map
        });

    let mut points = Vec::with_capacity(voxel_map.len());
    let mut colors = Vec::with_capacity(voxel_map.len());

    for entry in voxel_map.values() {
        let count = entry.indices.len() as f32;
        points.push(Point::new(
            entry.sum_x / count,
            entry.sum_y / count,
            entry.sum_z / count,
        ));

        if entry.count_color > 0 {
            let c = entry.count_color as u32;
            colors.push(Color::new(
                (entry.sum_r / c) as u8,
                (entry.sum_g / c) as u8,
                (entry.sum_b / c) as u8,
            ));
        } else {
            colors.push(Color::new(255, 255, 255));
        }
    }

    PointCloud::new(points, colors, None)
}

#[derive(Default)]
struct VoxelAccumulator {
    indices: Vec<usize>,
    sum_x: f32,
    sum_y: f32,
    sum_z: f32,
    sum_r: u32,
    sum_g: u32,
    sum_b: u32,
    count_color: u32,
}

pub fn adaptive_downsample(cloud: &PointCloud, target_points: usize) -> PointCloud {
    if cloud.len() <= target_points {
        return cloud.clone();
    }

    let bounds = &cloud.bounds;
    let size = bounds.size();
    let volume = size[0] * size[1] * size[2];

    if volume <= 0.0 {
        return cloud.clone();
    }

    let voxel_volume = volume / target_points as f32;
    let voxel_size = voxel_volume.cbrt();

    let mut downsampled = voxel_grid_downsample(cloud, voxel_size);

    if downsampled.len() > target_points * 2 {
        let scale = (target_points as f32 / downsampled.len() as f32).sqrt();
        downsampled = voxel_grid_downsample(cloud, voxel_size / scale);
    }

    downsampled
}

pub fn random_downsample(cloud: &PointCloud, target_points: usize) -> PointCloud {
    if cloud.len() <= target_points {
        return cloud.clone();
    }

    use rand::seq::SliceRandom;
    use rand::thread_rng;

    let mut rng = thread_rng();
    let indices: Vec<usize> = (0..cloud.len()).collect();
    let selected: Vec<&usize> = indices.choose_multiple(&mut rng, target_points).collect();

    let mut points = Vec::with_capacity(target_points);
    let mut colors = Vec::with_capacity(target_points);

    for &idx in &selected {
        points.push(cloud.points[*idx]);
        if *idx < cloud.colors.len() {
            colors.push(cloud.colors[*idx]);
        } else {
            colors.push(Color::new(255, 255, 255));
        }
    }

    PointCloud::new(points, colors, None)
}

pub fn compute_voxel_size_for_target(cloud: &PointCloud, target_points: usize) -> f32 {
    if cloud.len() <= target_points {
        return 0.0;
    }

    let bounds = &cloud.bounds;
    let size = bounds.size();
    let volume = size[0] * size[1] * size[2];

    if volume <= 0.0 {
        return 0.0;
    }

    let voxel_volume = volume / target_points as f32;
    voxel_volume.cbrt()
}
