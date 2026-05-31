"""Application configuration loaded from environment variables."""
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    postgres_host: str = "localhost"
    postgres_port: int = 5432
    postgres_user: str = "ebpf"
    postgres_password: str = "ebpf"
    postgres_db: str = "ebpf_monitor"

    database_url: str = ""

    api_host: str = "0.0.0.0"
    api_port: int = 8000

    slow_threshold_ms: int = 10

    class Config:
        env_file = ".env"
        case_sensitive = False

    def get_database_url(self) -> str:
        if self.database_url:
            return self.database_url
        return (
            f"postgresql+psycopg2://{self.postgres_user}:{self.postgres_password}"
            f"@{self.postgres_host}:{self.postgres_port}/{self.postgres_db}"
        )


settings = Settings()
