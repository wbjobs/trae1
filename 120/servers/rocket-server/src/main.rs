use common::{db, scenarios::sample_json_response, Framework};
use rocket::futures::{SinkExt, StreamExt};
use once_cell::sync::Lazy;
use rocket::{get, http::Status, response::content, routes, serde::json::Json, Build, Rocket};
use rocket_dyn_templates::{context, Template};
use rocket_ws as ws;
use std::path::PathBuf;

static STATIC_DIR: Lazy<PathBuf> = Lazy::new(|| {
    let mut path = std::env::current_exe().unwrap();
    path.pop();
    path.pop();
    path.pop();
    path.push("static");
    path
});

#[get("/json")]
fn json_handler() -> Json<common::scenarios::JsonResponse> {
    Json(sample_json_response())
}

#[get("/db")]
fn db_handler() -> Result<Json<Vec<common::scenarios::User>>, Status> {
    match db::get_all_users() {
        Ok(users) => Ok(Json(users)),
        Err(_) => Err(Status::InternalServerError),
    }
}

#[get("/template")]
fn template_handler() -> Result<Template, Status> {
    let users = match db::get_all_users() {
        Ok(u) => u,
        Err(_) => return Err(Status::InternalServerError),
    };

    Ok(Template::render(
        "users",
        context! {
            title: "User List - Rocket",
            heading: "Registered Users",
            description: "List of all registered users in the system",
            users: users,
            generated_at: chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string(),
        },
    ))
}

#[get("/static/<path..>")]
fn static_handler(path: std::path::PathBuf) -> Result<content::RawHtml<Vec<u8>>, Status> {
    let file_path = STATIC_DIR.join(path);
    
    if !file_path.exists() || !file_path.is_file() {
        return Err(Status::NotFound);
    }

    match std::fs::read(&file_path) {
        Ok(contents) => Ok(content::RawHtml(contents)),
        Err(_) => Err(Status::InternalServerError),
    }
}

#[get("/ws")]
fn ws_handler(ws: ws::WebSocket) -> ws::Channel<'static> {
    ws.channel(move |mut stream| {
        Box::pin(async move {
            while let Some(message) = stream.next().await {
                let message = message?;
                stream.send(message).await?;
            }
            Ok(())
        })
    })
}

fn rocket() -> Rocket<Build> {
    let mut template_path = std::env::current_exe().unwrap();
    template_path.pop();
    template_path.pop();
    template_path.pop();
    template_path.push("templates");

    rocket::build()
        .attach(Template::custom(move |engines| {
            match tera::Tera::new(template_path.join("**/*").to_str().unwrap()) {
                Ok(t) => engines.tera = t,
                Err(e) => eprintln!("Failed to load templates: {}", e),
            }
        }))
        .mount("/", routes![json_handler, db_handler, template_handler, static_handler, ws_handler])
}

#[rocket::main]
async fn main() -> Result<(), rocket::Error> {
    if let Err(e) = db::init_db() {
        eprintln!("Failed to initialize database: {}", e);
        std::process::exit(1);
    }

    let port = Framework::Rocket.port();
    println!("Rocket server listening on 127.0.0.1:{}", port);

    let figment = rocket::Config::figment()
        .merge(("port", port))
        .merge(("address", "127.0.0.1"))
        .merge(("log_level", "critical"));

    rocket().configure(figment).launch().await?;

    Ok(())
}
