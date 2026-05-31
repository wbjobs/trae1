use actix::ActorContext;
use actix_web::{
    get, web, App, Error, HttpRequest, HttpResponse, HttpServer, Responder,
};
use actix_web_actors::ws;
use common::{db, scenarios::sample_json_response, Framework};
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
struct EchoWs;

impl actix::Actor for EchoWs {
    type Context = ws::WebsocketContext<Self>;
}

impl actix::StreamHandler<Result<ws::Message, ws::ProtocolError>> for EchoWs {
    fn handle(&mut self, msg: Result<ws::Message, ws::ProtocolError>, ctx: &mut Self::Context) {
        if let Ok(msg) = msg {
            match msg {
                ws::Message::Ping(msg) => ctx.pong(&msg),
                ws::Message::Pong(_) => (),
                ws::Message::Text(text) => ctx.text(text),
                ws::Message::Binary(bin) => ctx.binary(bin),
                ws::Message::Close(reason) => {
                    ctx.close(reason);
                    ctx.stop();
                }
                ws::Message::Continuation(_) => (),
                ws::Message::Nop => (),
            }
        }
    }
}

#[get("/json")]
async fn json_handler() -> impl Responder {
    HttpResponse::Ok().json(sample_json_response())
}

#[get("/db")]
async fn db_handler() -> impl Responder {
    match db::get_all_users() {
        Ok(users) => HttpResponse::Ok().json(users),
        Err(_) => HttpResponse::InternalServerError().finish(),
    }
}

#[get("/template")]
async fn template_handler() -> impl Responder {
    let users = match db::get_all_users() {
        Ok(u) => u,
        Err(_) => return HttpResponse::InternalServerError().finish(),
    };

    let mut context = tera::Context::new();
    context.insert("title", "User List - Actix-web");
    context.insert("heading", "Registered Users");
    context.insert("description", "List of all registered users in the system");
    context.insert("users", &users);
    context.insert("generated_at", &chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string());

    match TEMPLATES.render("users.html", &context) {
        Ok(html) => HttpResponse::Ok()
            .content_type("text/html; charset=utf-8")
            .body(html),
        Err(_) => HttpResponse::InternalServerError().finish(),
    }
}

#[get("/static/{filename:.*}")]
async fn static_handler(path: web::Path<String>) -> impl Responder {
    let file_path = STATIC_DIR.join(path.into_inner());
    
    if !file_path.exists() || !file_path.is_file() {
        return HttpResponse::NotFound().finish();
    }

    match std::fs::read(&file_path) {
        Ok(contents) => {
            let mime = mime_guess::from_path(&file_path)
                .first_or_octet_stream()
                .to_string();
            
            HttpResponse::Ok()
                .content_type(mime)
                .body(contents)
        }
        Err(_) => HttpResponse::InternalServerError().finish(),
    }
}

async fn ws_handler(req: HttpRequest, stream: web::Payload) -> Result<HttpResponse, Error> {
    let resp = ws::start(EchoWs, &req, stream)?;
    Ok(resp)
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    if let Err(e) = db::init_db() {
        eprintln!("Failed to initialize database: {}", e);
        std::process::exit(1);
    }

    let port = Framework::ActixWeb.port();
    println!("Actix-web server listening on 127.0.0.1:{}", port);

    HttpServer::new(|| {
        App::new()
            .service(json_handler)
            .service(db_handler)
            .service(template_handler)
            .service(static_handler)
            .route("/ws", web::get().to(ws_handler))
    })
    .bind(format!("127.0.0.1:{}", port))?
    .run()
    .await
}
