import logging
from typing import List, Optional, Dict, Any
from datetime import datetime
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS, WriteOptions
from influxdb_client.client.exceptions import InfluxDBError
from config import settings

logger = logging.getLogger(__name__)


class InfluxDBManager:
    _instance = None
    _client = None
    _write_api = None
    _query_api = None
    _initialized = False

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    @classmethod
    def initialize(cls):
        if cls._initialized:
            return

        try:
            cls._client = InfluxDBClient(
                url=settings.INFLUXDB_URL,
                token=settings.INFLUXDB_TOKEN,
                org=settings.INFLUXDB_ORG,
                timeout=30000,
                enable_gzip=True
            )

            cls._write_api = cls._client.write_api(
                write_options=WriteOptions(
                    batch_size=500,
                    flush_interval=1000,
                    jitter_interval=0,
                    retry_interval=5000,
                    max_retries=5,
                    max_retry_delay=30000,
                    exponential_base=2
                )
            )

            cls._query_api = cls._client.query_api()
            cls._initialized = True

            logger.info(f"InfluxDB connected: {settings.INFLUXDB_URL}")
            print(f"InfluxDB connected: {settings.INFLUXDB_URL}")

        except Exception as e:
            logger.error(f"Failed to connect to InfluxDB: {e}")
            print(f"Warning: Failed to connect to InfluxDB: {e}")
            raise

    @classmethod
    def close(cls):
        if cls._client:
            try:
                if cls._write_api:
                    cls._write_api.close()
                cls._client.close()
            except Exception as e:
                logger.error(f"Error closing InfluxDB connection: {e}")
            finally:
                cls._client = None
                cls._write_api = None
                cls._query_api = None
                cls._initialized = False

    @classmethod
    def _ensure_connected(cls):
        if not cls._initialized or cls._client is None:
            try:
                cls.initialize()
            except Exception as e:
                logger.error(f"Cannot reconnect to InfluxDB: {e}")
                raise

    @classmethod
    def write_point(cls, measurement: str, tags: Dict[str, str],
                    fields: Dict[str, Any], timestamp: Optional[datetime] = None) -> bool:
        try:
            cls._ensure_connected()

            point = Point(measurement)

            for key, value in tags.items():
                if value is not None:
                    point = point.tag(key, str(value))

            for key, value in fields.items():
                if value is not None:
                    if isinstance(value, bool):
                        point = point.field(key, value)
                    elif isinstance(value, int):
                        point = point.field(key, value)
                    elif isinstance(value, float):
                        point = point.field(key, value)
                    else:
                        point = point.field(key, str(value))

            if timestamp:
                point = point.time(timestamp, WritePrecision.MS)

            cls._write_api.write(
                bucket=settings.INFLUXDB_BUCKET,
                org=settings.INFLUXDB_ORG,
                record=point
            )

            return True

        except InfluxDBError as e:
            logger.error(f"InfluxDB write error: {e}")
            return False
        except Exception as e:
            logger.error(f"Unexpected error writing to InfluxDB: {e}")
            return False

    @classmethod
    def write_points(cls, points: List[Point]) -> bool:
        if not points:
            return True

        try:
            cls._ensure_connected()

            cls._write_api.write(
                bucket=settings.INFLUXDB_BUCKET,
                org=settings.INFLUXDB_ORG,
                record=points
            )

            return True

        except InfluxDBError as e:
            logger.error(f"InfluxDB batch write error: {e}")
            return False
        except Exception as e:
            logger.error(f"Unexpected error batch writing to InfluxDB: {e}")
            return False

    @classmethod
    def query(cls, flux_query: str):
        try:
            cls._ensure_connected()
            return cls._query_api.query(flux_query, org=settings.INFLUXDB_ORG)
        except InfluxDBError as e:
            logger.error(f"InfluxDB query error: {e}")
            return None
        except Exception as e:
            logger.error(f"Unexpected error querying InfluxDB: {e}")
            return None

    @classmethod
    def query_data_frame(cls, flux_query: str):
        try:
            cls._ensure_connected()
            return cls._query_api.query_data_frame(flux_query, org=settings.INFLUXDB_ORG)
        except InfluxDBError as e:
            logger.error(f"InfluxDB query dataframe error: {e}")
            return None
        except Exception as e:
            logger.error(f"Unexpected error querying dataframe from InfluxDB: {e}")
            return None

    @classmethod
    def health_check(cls) -> bool:
        try:
            cls._ensure_connected()
            health = cls._client.health()
            return health.status == "pass"
        except Exception:
            return False


def init_db():
    try:
        InfluxDBManager.initialize()
    except Exception as e:
        print(f"Warning: Database initialization failed: {e}")
        print("The application will continue running but data will not be stored.")


def close_db():
    InfluxDBManager.close()
