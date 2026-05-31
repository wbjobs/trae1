import logging
from typing import Dict, Any, List, Optional
from datetime import datetime, timedelta
import re

logger = logging.getLogger(__name__)


class DataCleaningService:

    INVALID_PATTERNS = [
        re.compile(r'<script[^>]*>.*?</script>', re.IGNORECASE | re.DOTALL),
        re.compile(r'<[^>]+>'),
        re.compile(r'javascript:', re.IGNORECASE),
        re.compile(r'on\w+\s*=', re.IGNORECASE),
    ]

    SUSPICIOUS_PATTERNS = [
        re.compile(r'(?:<|>).*?(?:<|>)', re.DOTALL),
        re.compile(r'\.\./\.\./', re.IGNORECASE),
        re.compile(r'(?:union\s+select|insert\s+into|delete\s+from|drop\s+table)', re.IGNORECASE),
    ]

    PERFORMANCE_OUTLIERS = {
        'fp': {'min': 0, 'max': 60000},
        'fcp': {'min': 0, 'max': 60000},
        'lcp': {'min': 0, 'max': 60000},
        'ttfb': {'min': 0, 'max': 60000},
        'dom_ready': {'min': 0, 'max': 120000},
        'load_time': {'min': 0, 'max': 120000},
        'fps': {'min': 0, 'max': 120},
    }

    MAX_STRING_LENGTHS = {
        'message': 2000,
        'stack': 5000,
        'filename': 512,
        'page_url': 512,
        'user_agent': 256,
        'error_message': 512,
    }

    @classmethod
    def clean_performance_data(cls, data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            cleaned = {}

            cleaned['app_id'] = cls._clean_string(data.get('app_id', ''), 64)
            cleaned['user_id'] = cls._clean_string(data.get('user_id', ''), 64)
            cleaned['session_id'] = cls._clean_string(data.get('session_id', ''), 128)
            cleaned['page_url'] = cls._clean_url(data.get('page_url', ''))
            cleaned['user_agent'] = cls._clean_string(data.get('user_agent', ''), 256)

            cleaned['timestamp'] = cls._validate_timestamp(data.get('timestamp'))

            metrics = data.get('metrics', {})
            if not metrics:
                logger.warning("No metrics data in performance report")
                return None

            cleaned_metrics = {}
            for key, value in metrics.items():
                if value is None:
                    continue

                cleaned_value = cls._clean_performance_metric(key, value)
                if cleaned_value is not None:
                    cleaned_metrics[key] = cleaned_value

            if not cleaned_metrics:
                logger.warning("No valid metrics after cleaning")
                return None

            cleaned['metrics'] = cleaned_metrics
            return cleaned

        except Exception as e:
            logger.error(f"Error cleaning performance data: {e}")
            return None

    @classmethod
    def clean_error_data(cls, data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            cleaned = {}

            cleaned['app_id'] = cls._clean_string(data.get('app_id', ''), 64)
            cleaned['user_id'] = cls._clean_string(data.get('user_id', ''), 64)
            cleaned['session_id'] = cls._clean_string(data.get('session_id', ''), 128)
            cleaned['page_url'] = cls._clean_url(data.get('page_url', ''))
            cleaned['user_agent'] = cls._clean_string(data.get('user_agent', ''), 256)
            cleaned['timestamp'] = cls._validate_timestamp(data.get('timestamp'))

            error = data.get('error', {})
            if not error:
                logger.warning("No error data in report")
                return None

            cleaned_error = {}
            cleaned_error['type'] = cls._clean_string(error.get('type', ''), 32)
            cleaned_error['error_type'] = cls._clean_string(error.get('error_type', ''), 32)
            cleaned_error['message'] = cls._clean_error_message(error.get('message', ''))
            cleaned_error['stack'] = cls._clean_error_stack(error.get('stack', ''))
            cleaned_error['filename'] = cls._clean_string(error.get('filename', ''), 512)

            lineno = error.get('lineno')
            if lineno is not None:
                try:
                    cleaned_error['lineno'] = int(lineno)
                except (ValueError, TypeError):
                    pass

            colno = error.get('colno')
            if colno is not None:
                try:
                    cleaned_error['colno'] = int(colno)
                except (ValueError, TypeError):
                    pass

            if not cleaned_error.get('message'):
                logger.warning("No valid error message after cleaning")
                return None

            cleaned['error'] = cleaned_error
            return cleaned

        except Exception as e:
            logger.error(f"Error cleaning error data: {e}")
            return None

    @classmethod
    def clean_renderer_data(cls, data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            cleaned = {}

            cleaned['app_id'] = cls._clean_string(data.get('app_id', ''), 64)
            cleaned['user_id'] = cls._clean_string(data.get('user_id', ''), 64)
            cleaned['session_id'] = cls._clean_string(data.get('session_id', ''), 128)
            cleaned['page_url'] = cls._clean_url(data.get('page_url', ''))
            cleaned['user_agent'] = cls._clean_string(data.get('user_agent', ''), 256)
            cleaned['timestamp'] = cls._validate_timestamp(data.get('timestamp'))

            renderer = data.get('renderer', {})
            if not renderer:
                logger.warning("No renderer data in report")
                return None

            cleaned_renderer = {}

            fps = renderer.get('fps')
            if fps is not None:
                try:
                    fps_value = float(fps)
                    if 0 <= fps_value <= 120:
                        cleaned_renderer['fps'] = fps_value
                    else:
                        logger.warning(f"Outlier fps value: {fps}")
                except (ValueError, TypeError):
                    pass

            if 'fps' not in cleaned_renderer:
                logger.warning("No valid fps value")
                return None

            long_task_count = renderer.get('long_task_count', 0)
            try:
                cleaned_renderer['long_task_count'] = max(0, int(long_task_count))
            except (ValueError, TypeError):
                cleaned_renderer['long_task_count'] = 0

            jank_count = renderer.get('jank_count', 0)
            try:
                cleaned_renderer['jank_count'] = max(0, int(jank_count))
            except (ValueError, TypeError):
                cleaned_renderer['jank_count'] = 0

            memory_used = renderer.get('memory_used')
            if memory_used is not None:
                try:
                    memory_value = float(memory_used)
                    if 0 <= memory_value <= 10000:
                        cleaned_renderer['memory_used'] = memory_value
                except (ValueError, TypeError):
                    pass

            cleaned['renderer'] = cleaned_renderer
            return cleaned

        except Exception as e:
            logger.error(f"Error cleaning renderer data: {e}")
            return None

    @classmethod
    def detect_anomalies(cls, metrics: Dict[str, float]) -> List[str]:
        anomalies = []

        if metrics.get('lcp') and metrics.get('fcp'):
            if metrics['lcp'] < metrics['fcp']:
                anomalies.append('LCP is less than FCP')

        if metrics.get('load_time') and metrics.get('dom_ready'):
            if metrics['load_time'] < metrics['dom_ready']:
                anomalies.append('Load time is less than DOM ready time')

        if metrics.get('ttfb') and metrics.get('ttfb') > 30000:
            anomalies.append(f'Abnormally high TTFB: {metrics["ttfb"]}ms')

        if metrics.get('fps') and metrics['fps'] < 1:
            anomalies.append(f'Abnormally low FPS: {metrics["fps"]}')

        return anomalies

    @classmethod
    def validate_batch_data(cls, batch_data: Dict[str, list]) -> Dict[str, list]:
        validated = {}

        for data_type in ['performance', 'errors', 'renderer']:
            items = batch_data.get(data_type, [])
            if not items:
                continue

            valid_items = []
            for item in items:
                if data_type == 'performance':
                    cleaned = cls.clean_performance_data(item)
                elif data_type == 'errors':
                    cleaned = cls.clean_error_data(item)
                elif data_type == 'renderer':
                    cleaned = cls.clean_renderer_data(item)
                else:
                    continue

                if cleaned:
                    valid_items.append(cleaned)

            if valid_items:
                validated[data_type] = valid_items

        return validated

    @classmethod
    def _clean_string(cls, value: str, max_length: int = 256) -> str:
        if not value:
            return ''

        try:
            value = str(value)
            value = value.strip()

            for pattern in cls.INVALID_PATTERNS:
                value = pattern.sub('', value)

            if any(pattern.search(value) for pattern in cls.SUSPICIOUS_PATTERNS):
                logger.warning(f"Suspicious content detected: {value[:100]}...")
                value = ''

            if len(value) > max_length:
                value = value[:max_length]

            return value

        except Exception as e:
            logger.error(f"Error cleaning string: {e}")
            return ''

    @classmethod
    def _clean_url(cls, url: str) -> str:
        if not url:
            return ''

        try:
            url = str(url).strip()

            if not url.startswith(('http://', 'https://', '/', 'file://')):
                logger.warning(f"Invalid URL format: {url[:50]}...")
                return ''

            if len(url) > 2048:
                url = url[:2048]

            return url

        except Exception as e:
            logger.error(f"Error cleaning URL: {e}")
            return ''

    @classmethod
    def _clean_error_message(cls, message: str) -> str:
        if not message:
            return ''

        try:
            message = str(message).strip()

            for pattern in cls.INVALID_PATTERNS:
                message = pattern.sub('[FILTERED]', message)

            if len(message) > cls.MAX_STRING_LENGTHS['message']:
                message = message[:cls.MAX_STRING_LENGTHS['message']] + '...'

            return message

        except Exception as e:
            logger.error(f"Error cleaning error message: {e}")
            return ''

    @classmethod
    def _clean_error_stack(cls, stack: str) -> str:
        if not stack:
            return ''

        try:
            stack = str(stack).strip()

            stack = re.sub(r'\b(?:password|token|secret|key|auth)\b[=:]\s*\S+',
                          lambda m: m.group().split('=')[0] + '=[REDACTED]',
                          stack, flags=re.IGNORECASE)

            if len(stack) > cls.MAX_STRING_LENGTHS['stack']:
                stack = stack[:cls.MAX_STRING_LENGTHS['stack']] + '\n...[truncated]'

            return stack

        except Exception as e:
            logger.error(f"Error cleaning error stack: {e}")
            return ''

    @classmethod
    def _clean_performance_metric(cls, name: str, value: Any) -> Optional[float]:
        try:
            numeric_value = float(value)

            if cls._is_outlier(name, numeric_value):
                logger.warning(f"Outlier detected for {name}: {numeric_value}")
                return None

            return round(numeric_value, 2)

        except (ValueError, TypeError) as e:
            logger.warning(f"Invalid metric value for {name}: {value}")
            return None

    @classmethod
    def _is_outlier(cls, metric_name: str, value: float) -> bool:
        bounds = cls.PERFORMANCE_OUTLIERS.get(metric_name)
        if not bounds:
            return False

        return value < bounds['min'] or value > bounds['max']

    @classmethod
    def _validate_timestamp(cls, timestamp) -> datetime:
        try:
            if isinstance(timestamp, datetime):
                return timestamp

            if isinstance(timestamp, (int, float)):
                if timestamp > 1e12:
                    timestamp = timestamp / 1000
                return datetime.fromtimestamp(timestamp)

            if isinstance(timestamp, str):
                return datetime.fromisoformat(timestamp.replace('Z', '+00:00'))

            return datetime.now()

        except (ValueError, TypeError, OSError) as e:
            logger.warning(f"Invalid timestamp: {timestamp}, using current time")
            return datetime.now()
