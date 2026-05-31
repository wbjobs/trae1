use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonResponse {
    pub message: String,
    pub status: String,
    pub data: JsonData,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonData {
    pub id: u64,
    pub name: String,
    pub value: f64,
    pub tags: Vec<String>,
    pub timestamp: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct User {
    pub id: i64,
    pub name: String,
    pub email: String,
    pub created_at: String,
}

pub fn sample_json_response() -> JsonResponse {
    JsonResponse {
        message: "success".to_string(),
        status: "ok".to_string(),
        data: JsonData {
            id: 12345,
            name: "test_item".to_string(),
            value: 42.5,
            tags: vec![
                "rust".to_string(),
                "web".to_string(),
                "benchmark".to_string(),
                "performance".to_string(),
            ],
            timestamp: chrono::Utc::now().timestamp(),
        },
    }
}

pub fn sample_users() -> Vec<User> {
    vec![
        User {
            id: 1,
            name: "Alice Johnson".to_string(),
            email: "alice@example.com".to_string(),
            created_at: "2024-01-15 10:30:00".to_string(),
        },
        User {
            id: 2,
            name: "Bob Smith".to_string(),
            email: "bob@example.com".to_string(),
            created_at: "2024-01-16 14:20:00".to_string(),
        },
        User {
            id: 3,
            name: "Charlie Brown".to_string(),
            email: "charlie@example.com".to_string(),
            created_at: "2024-01-17 09:15:00".to_string(),
        },
        User {
            id: 4,
            name: "Diana Prince".to_string(),
            email: "diana@example.com".to_string(),
            created_at: "2024-01-18 16:45:00".to_string(),
        },
        User {
            id: 5,
            name: "Edward Norton".to_string(),
            email: "edward@example.com".to_string(),
            created_at: "2024-01-19 11:00:00".to_string(),
        },
    ]
}
