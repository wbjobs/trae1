from datetime import datetime
from sqlalchemy import Column, Integer, String, DateTime, BigInteger

from .database import Base


class OpLog(Base):
    __tablename__ = "op_logs"

    id = Column(Integer, primary_key=True, index=True)
    file_a = Column(String(512), nullable=False)
    file_b = Column(String(512), nullable=False)
    operation = Column(String(32), nullable=False)
    grid_size = Column(Integer, nullable=False)
    status = Column(String(32), nullable=False, default="pending")
    result_file = Column(String(512), nullable=True)
    created_at = Column(DateTime, default=datetime.utcnow)
    duration_ms = Column(BigInteger, nullable=True)
    error_message = Column(String(1024), nullable=True)
