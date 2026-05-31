import logging
from celery import shared_task
from django.utils import timezone

logger = logging.getLogger('videos')


@shared_task(bind=True, max_retries=2, default_retry_delay=60)
def transcode_video(self, video_id):
    from .models import Video
    from .services.transcode import TranscodeService

    try:
        video = Video.objects.get(id=video_id)
    except Video.DoesNotExist:
        logger.error(f'Video not found: {video_id}')
        return

    if video.status == 'ready':
        return

    video.status = 'processing'
    video.save(update_fields=['status'])

    try:
        service = TranscodeService()
        service.transcode(video)
    except Exception as e:
        logger.error(f'Transcode failed for {video_id}: {e}')
        video.status = 'failed'
        video.error_message = str(e)[:1000]
        video.save(update_fields=['status', 'error_message'])
        try:
            self.retry(exc=e)
        except self.MaxRetriesExceededError:
            logger.error(f'Transcode max retries exceeded: {video_id}')
            video.status = 'failed'
            video.error_message = str(e)[:1000] + ' (已达最大重试次数)'
            video.save(update_fields=['status', 'error_message'])


@shared_task
def rotate_encryption_key():
    from .key_manager import key_manager
    from .models import EncryptionKeyLog

    key_id, key_bytes, key_index = key_manager.rotate_key()

    from django.conf import settings
    from datetime import timedelta
    EncryptionKeyLog.objects.create(
        key_id=key_id,
        key_index=key_index,
        expires_at=timezone.now() + timedelta(
            seconds=settings.HLS_ENCRYPTION_KEY_ROTATION_INTERVAL +
                    settings.HLS_ENCRYPTION_KEY_GRACE_PERIOD
        ),
    )

    logger.info(f'Key rotated: index={key_index}, id={key_id}')
