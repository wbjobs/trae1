use common::{db, scenarios::sample_json_response, Framework};
use once_cell::sync::Lazy;
use std::path::PathBuf;
use tera::Tera;
use tide::{Body, Request, Response, StatusCode};
use tide_websockets::{Message, WebSocket};

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

async fn json_handler(_req: Request<()>) -> tide::Result {
    let resp = Response::builder(StatusCode::Ok)
        .body(Body::from_json(&sample_json_response())?)
        .build();
    Ok(resp)
}

async fn db_handler(_req: Request<()>) -> tide::Result {
    match db::get_all_users() {
        Ok(users) => {
            let resp = Response::builder(StatusCode::Ok)
                .body(Body::from_json(&users)?)
                .build();
            Ok(resp)
        }
        Err(_) => Ok(Response::new(StatusCode::InternalServerError)),
    }
}

async fn template_handler(_req: Request<()>) -> tide::Result {
    let users = match db::get_all_users() {
        Ok(u) => u,
        Err(_) => return Ok(Response::new(StatusCode::InternalServerError)),
    };

    let mut context = tera::Context::new();
    context.insert("title", "User List - Tide");
    context.insert("heading", "Registered Users");
    context.insert("description", "List of all registered users in the system");
    context.insert("users", &users);
    context.insert("generated_at", &chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string());

    match TEMPLATES.render("users.html", &context) {
        Ok(html) => {
            let resp = Response::builder(StatusCode::Ok)
                .header("Content-Type", "text/html; charset=utf-8")
                .body(Body::from_string(html))
                .build();
            Ok(resp)
        }
        Err(_) => Ok(Response::new(StatusCode::InternalServerError)),
    }
}

async fn static_handler(req: Request<()>) -> tide::Result {
    let path: String = req.param("path").unwrap_or_default().to_string();
    let file_path = STATIC_DIR.join(path);
    
    if !file_path.exists() || !file_path.is_file() {
        return Ok(Response::new(StatusCode::NotFound));
    }

    match async_std::fs::read(&file_path).await {
        Ok(contents) => {
            let mime = mime_guess::from_path(&file_path)
                .first_or_octet_stream()
                .to_string();
            
            let resp = Response::builder(StatusCode::Ok)
                .header("Content-Type", mime)
                .body(Body::from_bytes(contents))
                .build();
            Ok(resp)
        }
        Err(_) => Ok(Response::new(StatusCode::InternalServerError)),
    }
}

async fn ws_handler(_request: Request<()>, mut stream: tide_websockets::WebSocketConnection) -> tide::Result<()> {
    use futures_util::StreamExt;
    
    while let Some(Ok(msg)) = stream.next().await {
        match msg {
            Message::Ping(_) => continue,
            Message::Pong(_) => continue,
            Message::Close(_) => break,
            _ => {
                if stream.send(msg).await.is_err() {
                    break;
                }
            }
        }
    }
    Ok(())
}

#[async_std::main]
async fn main() -> tide::Result<()> {
    if let Err(e) = db::init_db() {
        eprintln!("Failed to initialize database: {}", e);
        std::process::exit(1);
    }

    let port = Framework::Tide.port();
    let mut app = tide::new();

    app.at("/json").get(json_handler);
    app.at("/db").get(db_handler);
    app.at("/template").get(template_handler);
    app.at("/static/*path").get(static_handler);
    app.at("/ws").get(WebSocket::new(ws_handler));

    println!("Tide server listening on 127.0.0.1:{}", port);
    app.listen(format!("127.0.0.1:{}", port)).await?;

    Ok(())
}
