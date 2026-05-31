use rand::RngCore;
use std::time::{SystemTime, UNIX_EPOCH};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("Generating ECH key pair...");
    
    let mut rng = rand::thread_rng();
    
    // Generate X25519 key pair
    let mut private_key_bytes = [0u8; 32];
    rng.fill_bytes(&mut private_key_bytes);
    
    let private_key = x25519_dalek::StaticSecret::from(private_key_bytes);
    let public_key = x25519_dalek::PublicKey::from(&private_key);
    
    // Generate random key ID
    let mut key_id_bytes = [0u8; 16];
    rng.fill_bytes(&mut key_id_bytes);
    
    // Set validity (24 hours from now)
    let now = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
    let valid_from = now;
    let valid_until = now + 86400; // 24 hours
    
    let key_pair = serde_json::json!({
        "key_id": base64::encode(&key_id_bytes),
        "private_key": base64::encode(&private_key_bytes),
        "public_key": base64::encode(public_key.as_bytes()),
        "valid_from": valid_from,
        "valid_until": valid_until,
        "kdf_id": 1,
        "aead_id": 2,
        "maximum_name_length": 255,
        "public_name": "public.example.com"
    });
    
    let config = serde_json::json!({
        "key_pairs": [key_pair],
        "key_rotation_interval": 3600,
        "fallback_enabled": true
    });
    
    let config_json = serde_json::to_string_pretty(&config)?;
    
    println!("\nGenerated ECH key configuration:");
    println!("{}", config_json);
    
    std::fs::write("ech_keys.json", &config_json)?;
    println!("\nConfiguration saved to ech_keys.json");
    
    Ok(())
}
