import os
from datetime import datetime

from fastapi import FastAPI, File, UploadFile, Depends, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from sqlalchemy.orm import Session

from .database import SessionLocal, engine, Base
from .models import OpLog
from .schemas import OpLogCreate, OpLogOut

Base.metadata.create_all(bind=engine)

UPLOAD_DIR = "uploads"
os.makedirs(UPLOAD_DIR, exist_ok=True)

app = FastAPI(title="STL Boolean Backend")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.mount("/uploads", StaticFiles(directory=UPLOAD_DIR), name="uploads")


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


@app.post("/api/upload")
async def upload(file: UploadFile = File(...)):
    if not file.filename or not file.filename.lower().endswith(".stl"):
        raise HTTPException(400, "仅支持 .stl 文件")
    ts = datetime.utcnow().strftime("%Y%m%d_%H%M%S_%f")
    safe_name = f"{ts}_{file.filename.replace(' ', '_')}"
    path = os.path.join(UPLOAD_DIR, safe_name)
    data = await file.read()
    with open(path, "wb") as f:
        f.write(data)
    return {"filename": safe_name, "url": f"/uploads/{safe_name}"}


@app.get("/api/logs", response_model=list[OpLogOut])
def list_logs(db: Session = Depends(get_db)):
    items = db.query(OpLog).order_by(OpLog.created_at.desc()).limit(100).all()
    return items


@app.post("/api/logs", response_model=OpLogOut)
def create_log(payload: OpLogCreate, db: Session = Depends(get_db)):
    item = OpLog(**payload.model_dump())
    db.add(item)
    db.commit()
    db.refresh(item)
    return item
