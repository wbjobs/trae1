from pydantic_settings import BaseSettings
from pydantic import Field


class Settings(BaseSettings):
    database_url: str = Field(
        default="postgresql://postgres:postgres@localhost:5432/malware_scanner",
        description="PostgreSQL connection URL",
    )
    max_file_size_mb: int = Field(
        default=50,
        description="Maximum file size in MB",
    )
    max_parallel_files: int = Field(
        default=10,
        description="Maximum number of files for parallel batch scan",
    )
    yara_rules_dir: str = Field(
        default="rules",
        description="Directory containing YARA rule files",
    )

    class Config:
        env_prefix = "SCANNER_"
        env_file = ".env"


settings = Settings()
