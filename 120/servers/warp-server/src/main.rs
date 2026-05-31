use common::{db, scenarios::sample_json_response, Framework};
use futures_util::{SinkExt, StreamExt};
use once_cell::sync::Lazy;
use std::path::PathBuf;
use tera::Tera;
use warp::Filter;

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

async fn json_handler() -> Result<impl warp::Reply, warp::Rejection> {
    Ok(warp::reply::json(&sample_json_response()))
}

async fn db_handler() -> Result<impl warp::Reply, warp::Rejection> {
    match db::get_all_users() {
        Ok(users) => Ok(warp::reply::json(&users)),
        Err(_) => Err(warp::reject::not_found()),
    }
}

async fn template_handler() -> Result<impl warp::Reply, warp::Rejection> {
    let users = match db::get_all_users() {
        Ok(u) => u,
        Err(_) => return Err(warp::reject::not_found()),
    };

    let mut context = tera::Context::new();
    context.insert("title", "User List - Warp");
    context.insert("heading", "Registered Users");
    context.insert("description", "List of all registered users in the system");
    context.insert("users", &users);
    context.insert("generated_at", &chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string());

    match TEMPLATES.render("users.html", &context) {
        Ok(html) => Ok(warp::reply::html(html)),
        Err(_) => Err(warp::reject::not_found()),
    }
}

async fn static_handler(path: warp::path::Tail) -> Result<impl warp::Reply, warp::Rejection> {
    let file_path = STATIC_DIR.join(path.as_str());
    
    if !file_path.exists() || !file_path.is_file() {
        return Err(warp::reject::not_found());
    }

    match tokio::fs::read(&file_path).await {
        Ok(contents) => {
            let mime = mime_guess::from_path(&file_path)
                .first_or_octet_stream()
                .to_string();
            Ok(warp::reply::with_header(contents, "Content-Type", mime))
        }
        Err(_) => Err(warp::reject::not_found()),
    }
}

async fn ws_handler(ws: warp::ws::Ws) -> Result<impl warp::Reply, warp::Rejection> {
    Ok(ws.on_upgrade(|socket| async {
        let (mut tx, mut rx) = socket.split();
        while let Some(Ok(msg)) = rx.next().await {
            if tx.send(msg).await.is_err() {
                break;
            }
        }
    }))
}

#[tokio::main]
async fn main() {
    if let Err(e) = db::init_db() {
        eprintln!("Failed to initialize database: {}", e);
        std::process::exit(1);
    }

    let port = Framework::Warp.port();

    let json_route = warp::path("json")
        .and(warp::get())
        .and_then(json_handler);

    let db_route = warp::path("db")
        .and(warp::get())
        .and_then(db_handler);

    let template_route = warp::path("template")
        .and(warp::get())
        .and_then(template_handler);

    let static_route = warp::path("static")
        .and(warp::path::tail())
        .and(warp::get())
        .and_then(static_handler);

    let ws_route = warp::path("ws")
        .and(warp::ws())
        .and_then(ws_handler);

    let routes = json_route
        .or(db_route)
        .or(template_route)
        .or(static_route)
        .or(ws_route)
        .with(warp::cors().allow_any_origin());

    println!("Warp server listening on 127.0.0.1:{}", port);

    warp::serve(routes)
        .run(([127, 0, 0, 1], port))
        .await;
}
