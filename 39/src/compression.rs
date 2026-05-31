use std::fmt;

use anyhow::Result;
use flate2::write::{DeflateEncoder, GzEncoder};
use flate2::Compression;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Algo {
    Gzip,
    Brotli,
    Deflate,
    Identity,
}

impl Algo {
    pub fn content_encoding(&self) -> &'static str {
        match self {
            Algo::Gzip => "gzip",
            Algo::Brotli => "br",
            Algo::Deflate => "deflate",
            Algo::Identity => "identity",
        }
    }

    pub fn from_accept_encoding(header: &str) -> Algo {
        let mut best = (Algo::Identity, 0.0f32);
        for part in header.split(',') {
            let part = part.trim();
            if part.is_empty() {
                continue;
            }
            let (name, q) = match part.split_once(";q=") {
                Some((n, q)) => (n.trim(), q.trim().parse::<f32>().unwrap_or(1.0)),
                None => (part, 1.0),
            };
            let algo = match name.to_ascii_lowercase().as_str();
            let algo = match algo {
                "br" => Algo::Brotli,
                "gzip" | "x-gzip" => Algo::Gzip,
                "deflate" => Algo::Deflate,
                "identity" | "*" => Algo::Identity,
                _ => continue,
            };
            if q > best.1 {
                best = (algo, q);
            }
        }
        best.0
    }

    pub fn all() -> [Algo; 3] {
        [Algo::Brotli, Algo::Gzip, Algo::Deflate]
    }
}

impl fmt::Display for Algo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.content_encoding())
    }
}

pub fn compress(data: &[u8], algo: Algo, gzip_level: u32, brotli_level: u32, deflate_level: u32) -> Result<Vec<u8>> {
    match algo {
        Algo::Gzip => {
            let mut enc = GzEncoder::new(Vec::new(), Compression::new(gzip_level));
            std::io::Write::write_all(&mut enc, data)?;
            Ok(enc.finish()?)
        }
        Algo::Deflate => {
            let mut enc = DeflateEncoder::new(Vec::new(), Compression::new(deflate_level));
            std::io::Write::write_all(&mut enc, data)?;
            Ok(enc.finish()?)
        }
        Algo::Brotli => {
            let mut out = Vec::new();
            let mut reader = std::io::Cursor::new(data);
            let params = brotli::enc::BrotliEncoderParams {
                quality: brotli_level as i32,
                ..Default::default()
            };
            brotli::BrotliCompress(&mut reader, &mut out, &params)?;
            Ok(out)
        }
        Algo::Identity => Ok(data.to_vec()),
    }
}
