use indicatif::{ProgressBar, ProgressStyle};

pub struct TransferProgress {
    bar: ProgressBar,
}

impl TransferProgress {
    pub fn new(total_size: u64, msg: &str) -> Self {
        let bar = ProgressBar::new(total_size);
        bar.set_style(
            ProgressStyle::with_template(
                "{msg}\n{spinner:.green} [{elapsed_precise}] [{wide_bar:.cyan/blue}] {bytes}/{total_bytes} ({bytes_per_sec}, {percent}%) ETA: {eta_precise}",
            )
            .unwrap()
            .progress_chars("#>-"),
        );
        bar.set_message(msg.to_string());

        Self {
            bar,
        }
    }

    pub fn update(&mut self, bytes_processed: u64) {
        self.bar.set_position(bytes_processed);
    }

    pub fn increment(&mut self, delta: u64) {
        self.bar.inc(delta);
    }

    pub fn finish(&mut self) {
        self.bar.finish_with_message("Transfer complete!".to_string());
    }

    pub fn finish_with_error(&mut self, msg: &str) {
        self.bar.abandon_with_message(msg.to_string());
    }
}
