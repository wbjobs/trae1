import os
from typing import Optional


class Settings:
    def __init__(self):
        self.APP_NAME = os.getenv("APP_NAME", "Frontend Performance Monitor")
        self.APP_VERSION = os.getenv("APP_VERSION", "1.0.0")
        self.DEBUG = os.getenv("DEBUG", "true").lower() == "true"

        self.INFLUXDB_URL = os.getenv("INFLUXDB_URL", "http://localhost:8086")
        self.INFLUXDB_TOKEN = os.getenv("INFLUXDB_TOKEN", "my-super-secret-token")
        self.INFLUXDB_ORG = os.getenv("INFLUXDB_ORG", "monitor-org")
        self.INFLUXDB_BUCKET = os.getenv("INFLUXDB_BUCKET", "frontend-metrics")

        cors_origins = os.getenv("CORS_ORIGINS", "*")
        self.CORS_ORIGINS = [origin.strip() for origin in cors_origins.split(",")]


settings = Settings()
