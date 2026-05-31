import os
from celery import Celery

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'config.settings')

app = Celery('config')
app.config_from_object('django.conf:settings', namespace='CELERY')
app.autodiscover_tasks()

app.conf.beat_schedule = {
    'rotate-encryption-key-every-minute': {
        'task': 'videos.tasks.rotate_encryption_key',
        'schedule': 60.0,
    },
}
