use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PointCloud {
    pub points: Vec<Point>,
    pub colors: Vec<Color>,
    pub normals: Option<Vec<Normal>>,
    pub bounds: BoundingBox,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct Point {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Point {
    pub fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    pub fn to_array(&self) -> [f32; 3] {
        [self.x, self.y, self.z]
    }
}

impl From<[f32; 3]> for Point {
    fn from(arr: [f32; 3]) -> Self {
        Self {
            x: arr[0],
            y: arr[1],
            z: arr[2],
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

impl Color {
    pub fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b, a: 255 }
    }

    pub fn rgba(&self) -> [f32; 4] {
        [
            self.r as f32 / 255.0,
            self.g as f32 / 255.0,
            self.b as f32 / 255.0,
            self.a as f32 / 255.0,
        ]
    }

    pub fn from_height(height: f32, min_h: f32, max_h: f32) -> Self {
        let t = if max_h > min_h {
            (height - min_h) / (max_h - min_h)
        } else {
            0.5
        };

        let (r, g, b) = height_to_rgb(t);
        Self { r, g, b, a: 255 }
    }
}

fn height_to_rgb(t: f32) -> (u8, u8, u8) {
    let t = t.clamp(0.0, 1.0);
    let r: u8;
    let g: u8;
    let b: u8;

    if t < 0.25 {
        let lt = t / 0.25;
        r = (0.0 + lt * 0.0) as u8;
        g = (0.0 + lt * 255.0) as u8;
        b = (128.0 - lt * 128.0) as u8;
    } else if t < 0.5 {
        let lt = (t - 0.25) / 0.25;
        r = (0.0 + lt * 255.0) as u8;
        g = 255;
        b = (0.0 - lt * 0.0) as u8;
    } else if t < 0.75 {
        let lt = (t - 0.5) / 0.25;
        r = 255;
        g = (255.0 - lt * 255.0) as u8;
        b = (0.0 + lt * 0.0) as u8;
    } else {
        let lt = (t - 0.75) / 0.25;
        r = 255;
        g = (0.0 + lt * 0.0) as u8;
        b = (0.0 + lt * 255.0) as u8;
    }

    (r, g, b)
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct Normal {
    pub nx: f32,
    pub ny: f32,
    pub nz: f32,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct BoundingBox {
    pub min: Point,
    pub max: Point,
}

impl BoundingBox {
    pub fn new(min: Point, max: Point) -> Self {
        Self { min, max }
    }

    pub fn center(&self) -> Point {
        Point::new(
            (self.min.x + self.max.x) / 2.0,
            (self.min.y + self.max.y) / 2.0,
            (self.min.z + self.max.z) / 2.0,
        )
    }

    pub fn size(&self) -> [f32; 3] {
        [
            self.max.x - self.min.x,
            self.max.y - self.min.y,
            self.max.z - self.min.z,
        ]
    }

    pub fn max_extent(&self) -> f32 {
        let size = self.size();
        size[0].max(size[1]).max(size[2])
    }
}

impl PointCloud {
    pub fn new(points: Vec<Point>, colors: Vec<Color>, normals: Option<Vec<Normal>>) -> Self {
        let bounds = Self::compute_bounds(&points);
        Self {
            points,
            colors,
            normals,
            bounds,
        }
    }

    pub fn compute_bounds(points: &[Point]) -> BoundingBox {
        if points.is_empty() {
            return BoundingBox::new(Point::new(0.0, 0.0, 0.0), Point::new(1.0, 1.0, 1.0));
        }

        let mut min_x = f32::MAX;
        let mut min_y = f32::MAX;
        let mut min_z = f32::MAX;
        let mut max_x = f32::MIN;
        let mut max_y = f32::MIN;
        let mut max_z = f32::MIN;

        for p in points {
            min_x = min_x.min(p.x);
            min_y = min_y.min(p.y);
            min_z = min_z.min(p.z);
            max_x = max_x.max(p.x);
            max_y = max_y.max(p.y);
            max_z = max_z.max(p.z);
        }

        BoundingBox::new(
            Point::new(min_x, min_y, min_z),
            Point::new(max_x, max_y, max_z),
        )
    }

    pub fn len(&self) -> usize {
        self.points.len()
    }

    pub fn is_empty(&self) -> bool {
        self.points.is_empty()
    }

    pub fn apply_height_coloring(&mut self) {
        let min_h = self.bounds.min.z;
        let max_h = self.bounds.max.z;

        self.colors = self
            .points
            .iter()
            .map(|p| Color::from_height(p.z, min_h, max_h))
            .collect();
    }

    pub fn translate_to_origin(&mut self) {
        let center = self.bounds.center();
        for p in &mut self.points {
            p.x -= center.x;
            p.y -= center.y;
            p.z -= center.z;
        }
        self.bounds = Self::compute_bounds(&self.points);
    }

    pub fn normalize_scale(&mut self) {
        let max_extent = self.bounds.max_extent();
        if max_extent > 0.0 {
            let scale = 2.0 / max_extent;
            for p in &mut self.points {
                p.x *= scale;
                p.y *= scale;
                p.z *= scale;
            }
            self.bounds = Self::compute_bounds(&self.points);
        }
    }
}
