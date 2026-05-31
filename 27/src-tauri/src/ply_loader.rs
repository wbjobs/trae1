use crate::point_cloud::{Color, Normal, Point, PointCloud};
use byteorder::{LittleEndian, ReadBytesExt};
use std::io::{BufRead, BufReader, Read};
use std::path::Path;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum PlyError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Invalid PLY format: {0}")]
    InvalidFormat(String),
    #[error("Unsupported PLY format: {0}")]
    UnsupportedFormat(String),
    #[error("Parse error: {0}")]
    ParseError(String),
    #[error("File too large: max {max} points, found {found}")]
    TooManyPoints { max: usize, found: usize },
}

const MAX_POINTS: usize = 5_000_000;

#[derive(Debug, Clone, Copy, PartialEq)]
enum PlyFormat {
    Ascii,
    BinaryLittleEndian,
    BinaryBigEndian,
}

#[derive(Debug)]
enum PropertyType {
    Char,
    UChar,
    Short,
    UShort,
    Int,
    UInt,
    Float,
    Double,
}

impl PropertyType {
    fn from_str(s: &str) -> Result<Self, PlyError> {
        match s.to_lowercase().as_str() {
            "char" | "int8" => Ok(PropertyType::Char),
            "uchar" | "uint8" => Ok(PropertyType::UChar),
            "short" | "int16" => Ok(PropertyType::Short),
            "ushort" | "uint16" => Ok(PropertyType::UShort),
            "int" | "int32" => Ok(PropertyType::Int),
            "uint" | "uint32" => Ok(PropertyType::UInt),
            "float" | "float32" => Ok(PropertyType::Float),
            "double" | "float64" => Ok(PropertyType::Double),
            _ => Err(PlyError::ParseError(format!("Unknown property type: {}", s))),
        }
    }

    fn byte_size(&self) -> usize {
        match self {
            PropertyType::Char | PropertyType::UChar => 1,
            PropertyType::Short | PropertyType::UShort => 2,
            PropertyType::Int | PropertyType::UInt | PropertyType::Float => 4,
            PropertyType::Double => 8,
        }
    }
}

#[derive(Debug)]
struct Property {
    name: String,
    prop_type: PropertyType,
}

#[derive(Debug)]
struct Element {
    name: String,
    count: usize,
    properties: Vec<Property>,
}

pub fn load_ply(path: &Path) -> Result<PointCloud, PlyError> {
    let file = std::fs::File::open(path)?;
    let file_size = file.metadata()?.len() as usize;
    let reader = BufReader::new(file);

    let mut reader = PlyReader::new(reader, file_size);
    reader.parse()
}

struct PlyReader<R: BufRead> {
    reader: R,
    file_size: usize,
    format: PlyFormat,
    elements: Vec<Element>,
}

impl<R: BufRead> PlyReader<R> {
    fn new(reader: R, file_size: usize) -> Self {
        Self {
            reader,
            file_size,
            format: PlyFormat::Ascii,
            elements: Vec::new(),
        }
    }

    fn parse(&mut self) -> Result<PointCloud, PlyError> {
        self.parse_header()?;

        let vertex_element = self
            .elements
            .iter()
            .find(|e| e.name == "vertex")
            .ok_or_else(|| PlyError::InvalidFormat("No vertex element found".to_string()))?;

        let num_points = vertex_element.count;
        if num_points > MAX_POINTS {
            return Err(PlyError::TooManyPoints {
                max: MAX_POINTS,
                found: num_points,
            });
        }

        let has_x = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "x");
        let has_y = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "y");
        let has_z = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "z");

        if !has_x || !has_y || !has_z {
            return Err(PlyError::InvalidFormat(
                "Vertex element must have x, y, z properties".to_string(),
            ));
        }

        let has_r = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "red" || p.name == "r");
        let has_g = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "green" || p.name == "g");
        let has_b = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "blue" || p.name == "b");
        let has_a = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "alpha" || p.name == "a");

        let has_nx = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "nx");
        let has_ny = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "ny");
        let has_nz = vertex_element
            .properties
            .iter()
            .any(|p| p.name == "nz");
        let has_normals = has_nx && has_ny && has_nz;

        match self.format {
            PlyFormat::Ascii => self.parse_ascii_vertices(
                num_points,
                &vertex_element.properties,
                has_r && has_g && has_b,
                has_a,
                has_normals,
            ),
            PlyFormat::BinaryLittleEndian => self.parse_binary_vertices::<LittleEndian>(
                num_points,
                &vertex_element.properties,
                has_r && has_g && has_b,
                has_a,
                has_normals,
            ),
            PlyFormat::BinaryBigEndian => {
                return Err(PlyError::UnsupportedFormat(
                    "Binary big endian not supported".to_string(),
                ))
            }
        }
    }

    fn parse_header(&mut self) -> Result<(), PlyError> {
        let mut line = String::new();
        self.reader.read_line(&mut line)?;
        line = line.trim().to_string();

        if line != "ply" {
            return Err(PlyError::InvalidFormat(
                "First line must be 'ply'".to_string(),
            ));
        }

        line.clear();
        self.reader.read_line(&mut line)?;
        let format_line = line.trim().to_string();

        if let Some(format_str) = format_line.strip_prefix("format ") {
            let parts: Vec<&str> = format_str.split_whitespace().collect();
            if parts.is_empty() {
                return Err(PlyError::InvalidFormat(
                    "Invalid format line".to_string(),
                ));
            }
            self.format = match parts[0] {
                "ascii" => PlyFormat::Ascii,
                "binary_little_endian" => PlyFormat::BinaryLittleEndian,
                "binary_big_endian" => PlyFormat::BinaryBigEndian,
                _ => {
                    return Err(PlyError::UnsupportedFormat(format!(
                        "Unsupported format: {}",
                        parts[0]
                    )))
                }
            };
        } else {
            return Err(PlyError::InvalidFormat(
                "Missing format specification".to_string(),
            ));
        }

        let mut current_element: Option<Element> = None;

        loop {
            line.clear();
            self.reader.read_line(&mut line)?;
            let trimmed = line.trim().to_string();

            if trimmed == "end_header" {
                if let Some(elem) = current_element.take() {
                    self.elements.push(elem);
                }
                break;
            }

            if let Some(rest) = trimmed.strip_prefix("element ") {
                if let Some(elem) = current_element.take() {
                    self.elements.push(elem);
                }
                let parts: Vec<&str> = rest.split_whitespace().collect();
                if parts.len() >= 2 {
                    let count = parts[1]
                        .parse::<usize>()
                        .map_err(|_| PlyError::ParseError("Invalid element count".to_string()))?;
                    current_element = Some(Element {
                        name: parts[0].to_string(),
                        count,
                        properties: Vec::new(),
                    });
                }
            } else if let Some(rest) = trimmed.strip_prefix("property ") {
                if let Some(elem) = &mut current_element {
                    let parts: Vec<&str> = rest.split_whitespace().collect();
                    if parts.len() >= 2 {
                        let prop_type = PropertyType::from_str(parts[0])?;
                        elem.properties.push(Property {
                            name: parts[1].to_string(),
                            prop_type,
                        });
                    }
                }
            }
        }

        Ok(())
    }

    fn parse_ascii_vertices(
        &mut self,
        count: usize,
        properties: &[Property],
        has_colors: bool,
        has_alpha: bool,
        has_normals: bool,
    ) -> Result<PointCloud, PlyError> {
        let mut points = Vec::with_capacity(count);
        let mut colors = Vec::with_capacity(count);
        let mut normals = if has_normals {
            Some(Vec::with_capacity(count))
        } else {
            None
        };

        let prop_names: Vec<String> = properties.iter().map(|p| p.name.clone()).collect();

        for _ in 0..count {
            let mut line = String::new();
            self.reader.read_line(&mut line)?;
            let parts: Vec<&str> = line.trim().split_whitespace().collect();

            let mut x = 0.0f32;
            let mut y = 0.0f32;
            let mut z = 0.0f32;
            let mut r = 255u8;
            let mut g = 255u8;
            let mut b = 255u8;
            let mut a = 255u8;
            let mut nx = 0.0f32;
            let mut ny = 0.0f32;
            let mut nz = 0.0f32;

            for (i, name) in prop_names.iter().enumerate() {
                if i < parts.len() {
                    match name.as_str() {
                        "x" => x = parts[i].parse::<f32>().unwrap_or(0.0),
                        "y" => y = parts[i].parse::<f32>().unwrap_or(0.0),
                        "z" => z = parts[i].parse::<f32>().unwrap_or(0.0),
                        "red" | "r" => {
                            if let Ok(v) = parts[i].parse::<f32>() {
                                r = if v <= 1.0 { (v * 255.0) as u8 } else { v as u8 };
                            }
                        }
                        "green" | "g" => {
                            if let Ok(v) = parts[i].parse::<f32>() {
                                g = if v <= 1.0 { (v * 255.0) as u8 } else { v as u8 };
                            }
                        }
                        "blue" | "b" => {
                            if let Ok(v) = parts[i].parse::<f32>() {
                                b = if v <= 1.0 { (v * 255.0) as u8 } else { v as u8 };
                            }
                        }
                        "alpha" | "a" => {
                            if let Ok(v) = parts[i].parse::<f32>() {
                                a = if v <= 1.0 { (v * 255.0) as u8 } else { v as u8 };
                            }
                        }
                        "nx" => nx = parts[i].parse::<f32>().unwrap_or(0.0),
                        "ny" => ny = parts[i].parse::<f32>().unwrap_or(0.0),
                        "nz" => nz = parts[i].parse::<f32>().unwrap_or(0.0),
                        _ => {}
                    }
                }
            }

            points.push(Point::new(x, y, z));
            colors.push(Color::new(r, g, b));
            if has_normals {
                if let Some(ref mut ns) = normals {
                    ns.push(Normal { nx, ny, nz });
                }
            }
        }

        if !has_colors {
            let mut pc = PointCloud::new(points, colors, normals);
            pc.apply_height_coloring();
            Ok(pc)
        } else {
            Ok(PointCloud::new(points, colors, normals))
        }
    }

    fn parse_binary_vertices<T: byteorder::ByteOrder + 'static>(
        &mut self,
        count: usize,
        properties: &[Property],
        has_colors: bool,
        has_alpha: bool,
        has_normals: bool,
    ) -> Result<PointCloud, PlyError> {
        let mut points = Vec::with_capacity(count);
        let mut colors = Vec::with_capacity(count);
        let mut normals = if has_normals {
            Some(Vec::with_capacity(count))
        } else {
            None
        };

        let prop_names: Vec<String> = properties.iter().map(|p| p.name.clone()).collect();
        let prop_types: Vec<&PropertyType> = properties.iter().map(|p| &p.prop_type).collect();

        for _ in 0..count {
            let mut x = 0.0f32;
            let mut y = 0.0f32;
            let mut z = 0.0f32;
            let mut r = 255u8;
            let mut g = 255u8;
            let mut b = 255u8;
            let mut a = 255u8;
            let mut nx = 0.0f32;
            let mut ny = 0.0f32;
            let mut nz = 0.0f32;

            for (i, name) in prop_names.iter().enumerate() {
                let prop_type = &prop_types[i];
                let val = read_property::<T, _>(&mut self.reader, prop_type)?;

                match name.as_str() {
                    "x" => x = val.as_f32(),
                    "y" => y = val.as_f32(),
                    "z" => z = val.as_f32(),
                    "red" | "r" => r = val.as_color(),
                    "green" | "g" => g = val.as_color(),
                    "blue" | "b" => b = val.as_color(),
                    "alpha" | "a" => a = val.as_color(),
                    "nx" => nx = val.as_f32(),
                    "ny" => ny = val.as_f32(),
                    "nz" => nz = val.as_f32(),
                    _ => {}
                }
            }

            points.push(Point::new(x, y, z));
            colors.push(Color::new(r, g, b));
            if let Some(ref mut ns) = normals {
                ns.push(Normal { nx, ny, nz });
            }
        }

        if !has_colors {
            let mut pc = PointCloud::new(points, colors, normals);
            pc.apply_height_coloring();
            Ok(pc)
        } else {
            Ok(PointCloud::new(points, colors, normals))
        }
    }
}

#[derive(Debug)]
enum PropertyValue {
    I32(i32),
    U32(u32),
    F32(f32),
    F64(f64),
}

impl PropertyValue {
    fn as_f32(&self) -> f32 {
        match self {
            PropertyValue::I32(v) => *v as f32,
            PropertyValue::U32(v) => *v as f32,
            PropertyValue::F32(v) => *v,
            PropertyValue::F64(v) => *v as f32,
        }
    }

    fn as_color(&self) -> u8 {
        match self {
            PropertyValue::I32(v) => {
                if *v <= 1 { (*v * 255) as u8 } else { *v as u8 }
            }
            PropertyValue::U32(v) => {
                if *v <= 1 { (*v * 255) as u8 } else { *v as u8 }
            }
            PropertyValue::F32(v) => {
                if *v <= 1.0 { (*v * 255.0) as u8 } else { *v as u8 }
            }
            PropertyValue::F64(v) => {
                if *v <= 1.0 { (*v * 255.0) as u8 } else { *v as u8 }
            }
        }
    }
}

fn read_property<T: byteorder::ByteOrder + 'static, R: Read>(
    reader: &mut R,
    prop_type: &PropertyType,
) -> Result<PropertyValue, PlyError> {
    match prop_type {
        PropertyType::Char => Ok(PropertyValue::I32(reader.read_i8()? as i32)),
        PropertyType::UChar => Ok(PropertyValue::U32(reader.read_u8()? as u32)),
        PropertyType::Short => Ok(PropertyValue::I32(reader.read_i16::<T>()? as i32)),
        PropertyType::UShort => Ok(PropertyValue::U32(reader.read_u16::<T>()? as u32)),
        PropertyType::Int => Ok(PropertyValue::I32(reader.read_i32::<T>()?)),
        PropertyType::UInt => Ok(PropertyValue::U32(reader.read_u32::<T>()?)),
        PropertyType::Float => Ok(PropertyValue::F32(reader.read_f32::<T>()?)),
        PropertyType::Double => Ok(PropertyValue::F64(reader.read_f64::<T>()?)),
    }
}
