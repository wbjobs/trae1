from pydantic import BaseModel
from typing import Optional


class OpLogCreate(BaseModel):
    file_a: str
    file_b: str
    operation: str
    grid_size: int
    status: str = "pending"
    result_file: Optional[str] = None
    duration_ms: Optional[int] = None
    error_message: Optional[str] = None


class OpLogOut(BaseModel):
    id: int
    file_a: str
    file_b: str
    operation: str
    grid_size: int
    status: str
    result_file: Optional[str]
    created_at: datetime
    duration_ms: Optional[int]
    error_message: Optional[str]

    class Config:
        from_attributes = True
