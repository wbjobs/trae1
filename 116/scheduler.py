import re
import threading
import time
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Callable, Any
from loguru import logger
from dataclasses import dataclass, field


@dataclass
class CronJob:
    id: str
    name: str
    cron_expression: str
    func: Callable
    args: tuple = ()
    kwargs: dict = field(default_factory=dict)
    enabled: bool = True
    last_run: Optional[datetime] = None
    next_run: Optional[datetime] = None
    run_count: int = 0
    last_error: Optional[str] = None

    def to_dict(self) -> Dict:
        return {
            "id": self.id,
            "name": self.name,
            "cron_expression": self.cron_expression,
            "enabled": self.enabled,
            "last_run": self.last_run.isoformat() if self.last_run else None,
            "next_run": self.next_run.isoformat() if self.next_run else None,
            "run_count": self.run_count,
            "last_error": self.last_error
        }


class CronParser:
    @staticmethod
    def parse_field(field: str, min_val: int, max_val: int) -> List[int]:
        if field == '*':
            return list(range(min_val, max_val + 1))
        
        values = []
        for part in field.split(','):
            if '/' in part:
                base, step = part.split('/')
                step = int(step)
                if base == '*':
                    base_list = list(range(min_val, max_val + 1))
                else:
                    base_list = CronParser.parse_range(base, min_val, max_val)
                values.extend(base_list[::step])
            elif '-' in part:
                values.extend(CronParser.parse_range(part, min_val, max_val))
            else:
                val = int(part)
                if min_val <= val <= max_val:
                    values.append(val)
        
        return sorted(set(values))

    @staticmethod
    def parse_range(range_str: str, min_val: int, max_val: int) -> List[int]:
        start, end = range_str.split('-')
        start = int(start)
        end = int(end)
        return list(range(max(start, min_val), min(end, max_val) + 1))

    @staticmethod
    def parse_cron(expression: str) -> Dict[str, List[int]]:
        parts = expression.strip().split()
        if len(parts) != 5:
            raise ValueError(f"Invalid cron expression: {expression}, expected 5 fields")

        return {
            'minute': CronParser.parse_field(parts[0], 0, 59),
            'hour': CronParser.parse_field(parts[1], 0, 23),
            'day': CronParser.parse_field(parts[2], 1, 31),
            'month': CronParser.parse_field(parts[3], 1, 12),
            'weekday': CronParser.parse_field(parts[4], 0, 6)
        }

    @staticmethod
    def get_next_run(expression: str, from_time: Optional[datetime] = None) -> Optional[datetime]:
        try:
            cron = CronParser.parse_cron(expression)
        except ValueError:
            return None

        if from_time is None:
            from_time = datetime.now()

        current = from_time.replace(second=0, microsecond=0) + timedelta(minutes=1)

        for _ in range(525600):
            if (current.minute in cron['minute'] and
                current.hour in cron['hour'] and
                current.day in cron['day'] and
                current.month in cron['month'] and
                current.weekday() in cron['weekday']):
                return current
            
            current += timedelta(minutes=1)

        return None


class TaskScheduler:
    def __init__(self):
        self.jobs: Dict[str, CronJob] = {}
        self._stop_event = threading.Event()
        self._scheduler_thread: Optional[threading.Thread] = None
        self._running = False
        self._job_lock = threading.Lock()

    def add_job(self, job_id: str, name: str, cron_expression: str,
                func: Callable, *args, **kwargs) -> bool:
        with self._job_lock:
            if job_id in self.jobs:
                logger.warning(f"Job {job_id} already exists")
                return False

            job = CronJob(
                id=job_id,
                name=name,
                cron_expression=cron_expression,
                func=func,
                args=args,
                kwargs=kwargs
            )
            
            job.next_run = CronParser.get_next_run(cron_expression)
            self.jobs[job_id] = job
            logger.info(f"已添加定时任务: {name} ({job_id}), cron: {cron_expression}")
            return True

    def remove_job(self, job_id: str) -> bool:
        with self._job_lock:
            if job_id in self.jobs:
                del self.jobs[job_id]
                logger.info(f"已移除定时任务: {job_id}")
                return True
            return False

    def enable_job(self, job_id: str) -> bool:
        with self._job_lock:
            if job_id in self.jobs:
                self.jobs[job_id].enabled = True
                self.jobs[job_id].next_run = CronParser.get_next_run(
                    self.jobs[job_id].cron_expression
                )
                logger.info(f"已启用定时任务: {job_id}")
                return True
            return False

    def disable_job(self, job_id: str) -> bool:
        with self._job_lock:
            if job_id in self.jobs:
                self.jobs[job_id].enabled = False
                self.jobs[job_id].next_run = None
                logger.info(f"已禁用定时任务: {job_id}")
                return True
            return False

    def get_jobs(self) -> List[Dict]:
        with self._job_lock:
            return [job.to_dict() for job in self.jobs.values()]

    def get_job(self, job_id: str) -> Optional[Dict]:
        with self._job_lock:
            job = self.jobs.get(job_id)
            return job.to_dict() if job else None

    def run_job_now(self, job_id: str) -> bool:
        with self._job_lock:
            job = self.jobs.get(job_id)
            if not job:
                return False

        logger.info(f"立即执行任务: {job.name}")
        self._execute_job(job)
        return True

    def _execute_job(self, job: CronJob) -> None:
        try:
            job.func(*job.args, **job.kwargs)
            job.last_run = datetime.now()
            job.run_count += 1
            job.last_error = None
            logger.info(f"任务执行成功: {job.name}")
        except Exception as e:
            job.last_error = str(e)
            logger.error(f"任务执行失败 {job.name}: {e}")
        finally:
            job.next_run = CronParser.get_next_run(job.cron_expression)

    def _scheduler_loop(self) -> None:
        logger.info("任务调度器已启动")
        
        while not self._stop_event.is_set():
            try:
                now = datetime.now().replace(second=0, microsecond=0)
                
                with self._job_lock:
                    jobs_to_run = [
                        job for job in self.jobs.values()
                        if job.enabled and job.next_run and job.next_run <= now
                    ]

                for job in jobs_to_run:
                    logger.info(f"触发定时任务: {job.name}")
                    self._execute_job(job)

            except Exception as e:
                logger.error(f"调度器异常: {e}")

            self._stop_event.wait(30)

        logger.info("任务调度器已停止")

    def start(self) -> None:
        if self._running:
            return

        self._stop_event.clear()
        self._scheduler_thread = threading.Thread(target=self._scheduler_loop, daemon=True)
        self._scheduler_thread.start()
        self._running = True
        logger.info("任务调度器线程已启动")

    def stop(self) -> None:
        if not self._running:
            return

        self._stop_event.set()
        if self._scheduler_thread and self._scheduler_thread.is_alive():
            self._scheduler_thread.join(timeout=5)
        self._running = False
        logger.info("任务调度器已停止")
