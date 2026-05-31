mod cli;
mod client;
mod hash;
mod p2p;
mod progress;
mod protocol;
mod server;
mod tar_util;

use anyhow::Result;
use clap::Parser;

use crate::cli::{Cli, Commands};

#[tokio::main]
async fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Receive {
            bind,
            port,
            output_dir,
            cert,
            key,
            congestion,
            peer_id,
            signaling,
        } => {
            let bind_addr = bind.unwrap_or_else(|| "0.0.0.0".to_string());

            if let (Some(peer_id), Some(signaling_url)) = (peer_id, signaling) {
                println!("Starting QUIC file receiver (P2P mode)...");
                println!("  Peer ID: {}", peer_id);
                println!("  Signaling: {}", signaling_url);
                println!("  Congestion: {:?}", congestion);
                server::run_server_p2p(&bind_addr, port, output_dir, cert, key, congestion, &peer_id, &signaling_url).await?;
            } else {
                println!("Starting QUIC file receiver (direct mode)...");
                println!("  Congestion: {:?}", congestion);
                server::run_server(&bind_addr, port, output_dir, cert, key, congestion).await?;
            }
        }
        Commands::Send {
            ip,
            port,
            files,
            temp_dir,
            congestion,
            peer_id,
            signaling,
            local_id,
        } => {
            if let (Some(target_peer_id), Some(signaling_url), Some(local_peer_id)) = (peer_id, signaling, local_id) {
                println!("Starting QUIC file sender (P2P mode)...");
                println!("  Local ID: {}", local_peer_id);
                println!("  Target peer: {}", target_peer_id);
                println!("  Signaling: {}", signaling_url);
                println!("  Congestion: {:?}", congestion);
                client::run_client_p2p(&local_peer_id, &target_peer_id, &signaling_url, files, temp_dir, congestion).await?;
            } else if let (Some(ip), Some(port)) = (ip, port) {
                println!("Starting QUIC file sender (direct mode)...");
                println!("  Congestion: {:?}", congestion);
                client::run_client(&ip, port, files, temp_dir, congestion).await?;
            } else {
                anyhow::bail!("Either direct mode (--ip/--port) or P2P mode (--peer-id/--signaling/--local-id) must be specified");
            }
        }
    }

    Ok(())
}
