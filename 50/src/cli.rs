use clap::{Parser, Subcommand, ValueEnum};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "quic-transfer", version, about = "QUIC based file transfer tool with P2P NAT traversal")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Copy, Clone, Debug, ValueEnum)]
pub enum CongestionControl {
    Bbr,
    Cubic,
    Reno,
}

#[derive(Subcommand)]
pub enum Commands {
    #[command(about = "Start as receiver (server)")]
    Receive {
        #[arg(short, long, help = "IP address to bind (e.g., 0.0.0.0)")]
        bind: Option<String>,

        #[arg(short, long, help = "Port to listen on")]
        port: u16,

        #[arg(short = 'd', long, help = "Output directory for received files")]
        output_dir: PathBuf,

        #[arg(long, help = "Certificate file (auto-generated if not provided)")]
        cert: Option<PathBuf>,

        #[arg(long, help = "Key file (auto-generated if not provided)")]
        key: Option<PathBuf>,

        #[arg(long, value_enum, default_value_t = CongestionControl::Bbr, help = "Congestion control algorithm")]
        congestion: CongestionControl,

        #[arg(long, help = "Enable P2P mode with peer ID")]
        peer_id: Option<String>,

        #[arg(long, help = "Signaling server URL (e.g., ws://signaling.example.com:8888)")]
        signaling: Option<String>,
    },

    #[command(about = "Start as sender (client)")]
    Send {
        #[arg(short, long, help = "Receiver IP address (direct mode)")]
        ip: Option<String>,

        #[arg(short, long, help = "Receiver port (direct mode)")]
        port: Option<u16>,

        #[arg(short, long, help = "Files to send (up to 100 files)")]
        files: Vec<PathBuf>,

        #[arg(long, help = "Output directory for temp files (tar)")]
        temp_dir: Option<PathBuf>,

        #[arg(long, value_enum, default_value_t = CongestionControl::Bbr, help = "Congestion control algorithm")]
        congestion: CongestionControl,

        #[arg(long, help = "Target peer ID for P2P mode")]
        peer_id: Option<String>,

        #[arg(long, help = "Signaling server URL (e.g., ws://signaling.example.com:8888)")]
        signaling: Option<String>,

        #[arg(long, help = "Local peer ID for P2P mode")]
        local_id: Option<String>,
    },
}
