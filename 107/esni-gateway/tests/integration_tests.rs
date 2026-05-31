use crate::tls_parser::TlsParser;

fn create_client_hello_with_sni(hostname: &str) -> Vec<u8> {
    let mut hello = Vec::new();

    hello.push(0x16);
    hello.extend_from_slice(&[0x03, 0x01]);

    let mut inner = Vec::new();
    inner.push(0x01);
    inner.extend_from_slice(&[0x00, 0x00, 0x00]);
    inner.extend_from_slice(&[0x03, 0x03]);
    inner.extend_from_slice(&[0u8; 32]);
    inner.push(0x00);
    inner.extend_from_slice(&[0x00, 0x02]);
    inner.extend_from_slice(&[0x13, 0x01]);
    inner.push(0x01);
    inner.push(0x00);

    let mut sni_extension = Vec::new();
    sni_extension.push(0x00);
    let hostname_bytes = hostname.as_bytes();
    let name_len = hostname_bytes.len() as u16;
    sni_extension.extend_from_slice(&(name_len + 3).to_be_bytes());
    sni_extension.push(0x00);
    sni_extension.extend_from_slice(&name_len.to_be_bytes());
    sni_extension.extend_from_slice(hostname_bytes);

    inner.extend_from_slice(&(sni_extension.len() as u16).to_be_bytes());
    inner.extend_from_slice(&sni_extension);

    let inner_len = inner.len() as u16;
    hello.extend_from_slice(&inner_len.to_be_bytes());
    hello.extend_from_slice(&inner);

    hello
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_client_hello() {
        let hello = create_client_hello_with_sni("example.com");
        let result = TlsParser::parse_client_hello(&hello);

        assert!(result.is_ok());
        let info = result.unwrap();
        assert_eq!(info.sni, Some("example.com".to_string()));
    }

    #[test]
    fn test_parse_multiple_snis() {
        let domains = vec![
            "google.com",
            "github.com",
            "rust-lang.org",
            "cloudflare.com",
        ];

        for domain in domains {
            let hello = create_client_hello_with_sni(domain);
            let result = TlsParser::parse_client_hello(&hello);

            assert!(result.is_ok());
            let info = result.unwrap();
            assert_eq!(info.sni, Some(domain.to_string()));
        }
    }

    #[test]
    fn test_performance() {
        let hello = create_client_hello_with_sni("example.com");
        let iterations = 100_000;
        let start = std::time::Instant::now();

        for _ in 0..iterations {
            let _ = TlsParser::parse_client_hello(&hello);
        }

        let elapsed = start.elapsed();
        let ops_per_sec = iterations as f64 / elapsed.as_secs_f64();

        println!("TLS Parser Performance: {:.2} ops/sec", ops_per_sec);
        assert!(ops_per_sec > 100_000.0, "Performance too low");
    }
}
