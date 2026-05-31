import logging
import asyncio
import httpx
from typing import Dict, Any, List, Optional
from datetime import datetime

from config import settings
from services.metrics_service import get_metrics_service
from services.filter import DataFilter

logger = logging.getLogger(__name__)


class RemoteCollector:
    def __init__(self):
        self.timeout = settings.REMOTE_COLLECT_TIMEOUT
        self.max_concurrent = settings.MAX_CONCURRENT_COLLECTIONS

    async def collect_remote(self, url: str, hostname: str = None) -> Dict[str, Any]:
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                response = await client.post(f"{url.rstrip('/')}/api/metrics/collect")
                if response.status_code == 200:
                    data = response.json()
                    if data.get('success'):
                        return {
                            'hostname': hostname or url,
                            'remote': True,
                            'url': url,
                            'data': data.get('data', {})
                        }
                    else:
                        return {'hostname': hostname or url, 'remote': True, 'error': data.get('message', 'Unknown error')}
                else:
                    return {'hostname': hostname or url, 'remote': True, 'error': f'HTTP {response.status_code}'}
        except httpx.TimeoutException:
            return {'hostname': hostname or url, 'remote': True, 'error': 'Connection timeout'}
        except Exception as e:
            return {'hostname': hostname or url, 'remote': True, 'error': str(e)}

    async def collect_batch(self, targets: List[Dict[str, str]]) -> List[Dict[str, Any]]:
        semaphore = asyncio.Semaphore(self.max_concurrent)

        async def _collect_with_semaphore(target):
            async with semaphore:
                return await self.collect_remote(
                    url=target.get('url', ''),
                    hostname=target.get('hostname', '')
                )

        tasks = [_collect_with_semaphore(t) for t in targets]
        results = await asyncio.gather(*tasks, return_exceptions=True)

        final_results = []
        for i, result in enumerate(results):
            if isinstance(result, Exception):
                final_results.append({
                    'hostname': targets[i].get('hostname', targets[i].get('url', 'unknown')),
                    'remote': True,
                    'error': str(result)
                })
            else:
                final_results.append(result)

        return final_results


class BatchCollector:
    def __init__(self):
        self.remote_collector = RemoteCollector()
        self.local_service = get_metrics_service()
        self.filter = DataFilter()

    def collect_local(self) -> Dict[str, Any]:
        try:
            result = self.local_service.collect_and_report()
            return {
                'hostname': result.get('hostname', 'local'),
                'remote': False,
                'success': result.get('success', False),
                'data': result
            }
        except Exception as e:
            logger.error(f"本地采集失败: {e}")
            return {'hostname': 'local', 'remote': False, 'error': str(e)}

    async def collect_all(self, remote_targets: List[Dict[str, str]] = None) -> Dict[str, Any]:
        results = []

        local_result = await asyncio.to_thread(self.collect_local)
        results.append(local_result)

        if remote_targets:
            remote_results = await self.remote_collector.collect_batch(remote_targets)
            results.extend(remote_results)

        success_count = len([r for r in results if r.get('success') or (r.get('data') and r['data'].get('success'))])
        error_count = len([r for r in results if r.get('error')])

        return {
            'success': True,
            'message': f'批量采集完成: 成功{success_count}台, 失败{error_count}台',
            'timestamp': datetime.utcnow().isoformat(),
            'total': len(results),
            'success_count': success_count,
            'error_count': error_count,
            'results': results
        }

    async def collect_from_hosts(self, hostnames: List[str], base_url_template: str = None) -> Dict[str, Any]:
        if base_url_template is None:
            base_url_template = 'http://{hostname}:8000'

        targets = [
            {'hostname': h, 'url': base_url_template.format(hostname=h)}
            for h in hostnames
        ]
        return await self.collect_all(targets)


batch_collector = BatchCollector()


def get_batch_collector() -> BatchCollector:
    return batch_collector
