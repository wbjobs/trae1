"""SQLAlchemy ORM models for captured syscall events and alerts."""
from datetime import datetime

from sqlalchemy import BigInteger, Column, DateTime, Index, Integer, String, Text

from .database import Base


class SyscallEvent(Base):
    __tablename__ = "syscall_events"

    id = Column(BigInteger, primary_key=True, autoincrement=True)
    pid = Column(Integer, nullable=False, index=True)
    comm = Column(String(64), nullable=False)
    syscall_name = Column(String(32), nullable=False, index=True)
    timestamp = Column(DateTime, nullable=False, index=True, default=datetime.utcnow)
    duration_ns = Column(BigInteger, nullable=True)
    return_value = Column(BigInteger, nullable=True)
    file_path = Column(Text, nullable=True)
    extra_args = Column(Text, nullable=True)

    __table_args__ = (
        Index("ix_events_pid_syscall", "pid", "syscall_name"),
        Index("ix_events_pid_time", "pid", "timestamp"),
    )

    def __repr__(self) -> str:  # pragma: no cover
        return (
            f"<SyscallEvent pid={self.pid} syscall={self.syscall_name} "
            f"file={self.file_path}>"
        )


class Alert(Base):
    """Alert record emitted when a syscall exceeds the slow threshold."""
    __tablename__ = "alerts"

    id = Column(BigInteger, primary_key=True, autoincrement=True)
    pid = Column(Integer, nullable=False, index=True)
    comm = Column(String(64), nullable=False)
    syscall_name = Column(String(32), nullable=False, index=True)
    timestamp = Column(DateTime, nullable=False, index=True)
    duration_ns = Column(BigInteger, nullable=False)
    threshold_ns = Column(BigInteger, nullable=False)
    file_path = Column(Text, nullable=True)
    message = Column(Text, nullable=False)

    __table_args__ = (
        Index("ix_alerts_pid_time", "pid", "timestamp"),
    )

    def __repr__(self) -> str:  # pragma: no cover
        return (
            f"<Alert pid={self.pid} syscall={self.syscall_name} "
            f"duration_ns={self.duration_ns}>"
        )
