use wasm_bindgen::prelude::*;
use std::collections::HashMap;

#[wasm_bindgen]
pub fn handle_request(
    method_ptr: i32,
    method_len: i32,
    path_ptr: i32,
    path_len: i32,
    body_ptr: i32,
    body_len: i32,
    headers_ptr: i32,
    headers_len: i32,
) -> i32 {
    let method = unsafe {
        let slice = std::slice::from_raw_parts(method_ptr as *const u8, method_len as usize);
        std::str::from_utf8_unchecked(slice)
    };

    let path = unsafe {
        let slice = std::slice::from_raw_parts(path_ptr as *const u8, path_len as usize);
        std::str::from_utf8_unchecked(slice)
    };

    let body = unsafe {
        std::slice::from_raw_parts(body_ptr as *const u8, body_len as usize)
    };

    let mut response = Response::new();

    response.status = 200;
    response.headers.insert("Content-Type".to_string(), "text/plain".to_string());

    let response_body = format!("Hello from WASM! Method: {}, Path: {}", method, path);
    response.body = response_body.into_bytes();

    let result_ptr = allocate_response(&response);
    result_ptr as i32
}

#[wasm_bindgen]
pub fn health() -> i32 {
    0
}

struct Response {
    status: u16,
    headers: HashMap<String, String>,
    body: Vec<u8>,
}

impl Response {
    fn new() -> Self {
        Response {
            status: 200,
            headers: HashMap::new(),
            body: Vec::new(),
        }
    }
}

fn allocate_response(response: &Response) -> *mut u8 {
    let response_size = 8 + response.body.len() +
        response.headers.iter().map(|(k, v)| 4 + k.len() + v.len()).sum::<usize>();

    let buffer = vec![0u8; response_size];
    let mut offset = 0;

    buffer[offset] = (response.status >> 8) as u8;
    offset += 1;
    buffer[offset] = (response.status & 0xFF) as u8;
    offset += 1;

    let header_count = response.headers.len() as u16;
    buffer[offset] = (header_count >> 8) as u8;
    offset += 1;
    buffer[offset] = (header_count & 0xFF) as u8;
    offset += 1;

    let body_len = response.body.len() as u32;
    buffer[offset] = (body_len >> 24) as u8;
    offset += 1;
    buffer[offset] = ((body_len >> 16) & 0xFF) as u8;
    offset += 1;
    buffer[offset] = ((body_len >> 8) & 0xFF) as u8;
    offset += 1;
    buffer[offset] = (body_len & 0xFF) as u8;
    offset += 1;

    for (key, value) in &response.headers {
        let key_len = key.len() as u16;
        let value_len = value.len() as u16;

        buffer[offset] = (key_len >> 8) as u8;
        offset += 1;
        buffer[offset] = (key_len & 0xFF) as u8;
        offset += 1;

        buffer[offset..offset+key.len()].copy_from_slice(key.as_bytes());
        offset += key.len();

        buffer[offset] = (value_len >> 8) as u8;
        offset += 1;
        buffer[offset] = (value_len & 0xFF) as u8;
        offset += 1;

        buffer[offset..offset+value.len()].copy_from_slice(value.as_bytes());
        offset += value.len();
    }

    buffer[offset..offset+response.body.len()].copy_from_slice(&response.body);

    Box::into_raw(buffer.into_boxed_slice()) as *mut u8
}
