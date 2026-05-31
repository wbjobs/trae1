use std::time::{Duration, Instant};

pub struct Benchmark {
    name: String,
    iterations: usize,
    start: Instant,
}

impl Benchmark {
    pub fn new(name: &str, iterations: usize) -> Self {
        Self {
            name: name.to_string(),
            iterations,
            start: Instant::now(),
        }
    }

    pub fn finish(&self) -> Duration {
        self.start.elapsed()
    }

    pub fn ops_per_sec(&self) -> f64 {
        let elapsed = self.finish();
        if elapsed.as_secs_f64() > 0.0 {
            self.iterations as f64 / elapsed.as_secs_f64()
        } else {
            0.0
        }
    }

    pub fn print_results(&self) {
        let elapsed = self.finish();
        let ops = self.ops_per_sec();
        println!(
            "{}: {:?} for {} iterations ({:.2} ops/sec)",
            self.name, elapsed, self.iterations, ops
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_benchmark() {
        let bench = Benchmark::new("test", 1000);
        std::thread::sleep(Duration::from_millis(10));
        bench.print_results();
    }
}
