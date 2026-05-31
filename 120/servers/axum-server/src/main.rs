use axum::{
    body::Body,
    extract::Path,
    http::{header, StatusCode, Uri},
    response::{IntoResponse, Json, Response},
    routing::get,
    Router,
};
use common::{db, scenarios::sample_json_response, Framework};
use futures_util::{SinkExt, StreamExt};
use once_cell::sync::Lazy;
use std::path::PathBuf;
use tera::Tera;

static TEMPLATES: Lazy<Tera> = Lazy::new(|| {
    let mut template_path = std::env::current_exe().unwrap();
    template_path.pop();
    template_path.pop();
    template_path.pop();
    template_path.push("templates");
    template_path.push("**/*");
    
    Tera::new(template_path.to_str().unwrap()).expect("Failed to initialize Tera templates")
});

static STATIC_DIR: Lazy<PathBuf> = Lazy::new(|| {
    let mut path = std::env::current_exe().unwrap();
    path.pop();
    path.pop();
    path.pop();
    path.push("static");
    path
});

#[derive(Clone)]
struct AppState {}

async fn json_handler() -> impl IntoResponse {
    Json(sample_json_response())
}

async fn db_handler() -> Result<Json<Vec<common::scenarios::User>>, StatusCode> {
    match db::get_all_users() {
        Ok(users) => Ok(Json(users)),
        Err(_) => Err(StatusCode::INTERNAL_SERVER_ERROR),
    }
}

async fn template_handler() -> Result<Response, StatusCode> {
    let users = match db::get_all_users() {
        Ok(u) => u,
        Err(_) => return Err(StatusCode::INTERNAL_SERVER_ERROR),
    };

    let mut context = tera::Context::new();
    context.insert("title", "User List - Axum");
    context.insert("heading", "Registered Users");
    context.insert("description", "List of all registered users in the system");
    context.insert("users", &users);
    context.insert("generated_at", &chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string());

    match TEMPLATES.render("users.html", &context) {
        Ok(html) => Ok(Response::builder()
            .header(header::CONTENT_TYPE, "text/html; charset=utf-8")
            .body(Body::from(html))
            .unwrap()),
        Err(_) => Err(StatusCode::INTERNAL_SERVER_ERROR),
    }
}

async fn static_handler(Path(path): Path<String>) -> Result<Response, StatusCode> {
    let file_path = STATIC_DIR.join(path);
    
    if !file_path.exists() || !file_path.is_file() {
        return Err(StatusCode::NOT_FOUND);
    }

    match tokio::fs::read(&file_path).await {
        Ok(contents) => {
            let mime = mime_guess::from_path(&file_path)
                .first_or_octet_stream()
                .to_string();
            
            Ok(Response::builder()
                .header(header::CONTENT_TYPE, mime)
                .body(Body::from(contents))
                .unwrap())
        }
        Err(_) => Err(StatusCode::INTERNAL_SERVER_ERROR),
    }
}

async fn ws_handler(ws: axum::extract::ws::WebSocketUpgrade) -> Response {
    ws.on_upgrade(|socket| async move {
        let (mut sender, mut receiver) = socket.split();
        
        while let Some(Ok(msg)) = receiver.next().await {
            if sender.send(msg).await.is_err() {
                break;
            }
        }
    })
}

async fn fallback(uri: Uri) -> impl IntoResponse {
    (StatusCode::NOT_FOUND, format!("No route for {}", uri))
}

#[tokio::main]
async fn main() {
    if let Err(e) = db::init_db() {
        eprintln!("Failed to initialize database: {}", e);
        std::process::exit(1);
    }

    let state = AppState {};
    let port = Framework::Axum.port();

    let app = Router::new()
        .route("/json", get(json_handler))
        .route("/db", get(db_handler))
        .route("/template", get(template_handler))
        .route("/static/*path", get(static_handler))
        .route("/ws", get(ws_handler))
        .with_state(state)
        .fallback(fallback);

    let listener = tokio::net::TcpListener::bind(format!("127.0.0.1:{}", port))
        .await
        .unwrap();
    
    println!("Axum server listening on 127.0.0.1:{}", port);
    
    axum::serve(listener, app).await.unwrap();
}
