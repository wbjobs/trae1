from datetime import datetime
from sqlalchemy import (
    Column,
    String,
    DateTime,
    Text,
    Integer,
    create_engine,
)
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker
from app.config import settings

Base = declarative_base()


class ScanResult(Base):
    __tablename__ = "scan_results"

    id = Column(Integer, primary_key=True, autoincrement=True)
    file_md5 = Column(String(32), unique=True, index=True, nullable=False)
    file_name = Column(String(255), nullable=True)
    file_size = Column(Integer, nullable=True)
    matched_rules = Column(Text, nullable=True)
    scan_time = Column(DateTime, default=datetime.utcnow, nullable=False)
    total_matches = Column(Integer, default=0, nullable=False)
    highest_severity = Column(String(20), default="clean", nullable=False)

    def to_dict(self):
        import json

        return {
            "id": self.id,
            "file_md5": self.file_md5,
            "file_name": self.file_name,
            "file_size": self.file_size,
            "matched_rules": json.loads(self.matched_rules) if self.matched_rules else [],
            "scan_time": self.scan_time.isoformat() if self.scan_time else None,
            "total_matches": self.total_matches,
            "highest_severity": self.highest_severity,
        }


engine = create_engine(
    settings.database_url,
    pool_pre_ping=True,
    pool_recycle=3600,
)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)


def init_db():
    Base.metadata.create_all(bind=engine)


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
