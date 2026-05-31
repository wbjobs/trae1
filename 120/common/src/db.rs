use once_cell::sync::Lazy;
use rusqlite::{params, Connection, Result};
use std::path::PathBuf;

pub static DB_PATH: Lazy<PathBuf> = Lazy::new(|| {
    let mut path = std::env::current_exe().unwrap();
    path.pop();
    path.pop();
    path.pop();
    path.push("test_bench.db");
    path
});

pub fn init_db() -> Result<()> {
    let conn = Connection::open(&*DB_PATH)?;
    
    conn.execute(
        "CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            email TEXT NOT NULL UNIQUE,
            created_at TEXT NOT NULL
        )",
        [],
    )?;

    let count: i64 = conn.query_row("SELECT COUNT(*) FROM users", [], |row| row.get(0))?;
    
    if count == 0 {
        let users = vec![
            ("Alice Johnson", "alice@example.com", "2024-01-15 10:30:00"),
            ("Bob Smith", "bob@example.com", "2024-01-16 14:20:00"),
            ("Charlie Brown", "charlie@example.com", "2024-01-17 09:15:00"),
            ("Diana Prince", "diana@example.com", "2024-01-18 16:45:00"),
            ("Edward Norton", "edward@example.com", "2024-01-19 11:00:00"),
            ("Fiona Apple", "fiona@example.com", "2024-01-20 13:30:00"),
            ("George Clooney", "george@example.com", "2024-01-21 15:45:00"),
            ("Helen Hunt", "helen@example.com", "2024-01-22 08:00:00"),
            ("Ian McKellen", "ian@example.com", "2024-01-23 10:15:00"),
            ("Julia Roberts", "julia@example.com", "2024-01-24 12:30:00"),
        ];

        for (name, email, created_at) in users {
            conn.execute(
                "INSERT INTO users (name, email, created_at) VALUES (?1, ?2, ?3)",
                params![name, email, created_at],
            )?;
        }
    }

    Ok(())
}

pub fn get_all_users() -> Result<Vec<crate::scenarios::User>> {
    let conn = Connection::open(&*DB_PATH)?;
    let mut stmt = conn.prepare("SELECT id, name, email, created_at FROM users")?;
    
    let user_iter = stmt.query_map([], |row| {
        Ok(crate::scenarios::User {
            id: row.get(0)?,
            name: row.get(1)?,
            email: row.get(2)?,
            created_at: row.get(3)?,
        })
    })?;

    let mut users = Vec::new();
    for user in user_iter {
        users.push(user?);
    }

    Ok(users)
}
