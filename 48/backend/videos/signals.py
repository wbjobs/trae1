from django.db.models.signals import post_save
from django.dispatch import receiver
from django.db.models import F
from .models import PlaybackRecord


@receiver(post_save, sender=PlaybackRecord)
def update_video_play_count(sender, instance, created, **kwargs):
    if created:
        from channels.layers import get_channel_layer
        from asgiref.sync import async_to_sync

        try:
            channel_layer = get_channel_layer()
            async_to_sync(channel_layer.group_send)(
                f'video_stats_{instance.video_id}',
                {
                    'type': 'stats_update',
                    'data': {
                        'event': 'playback_started',
                        'video_id': str(instance.video_id),
                        'record_id': str(instance.id),
                    },
                }
            )
            async_to_sync(channel_layer.group_send)(
                'video_stats_all',
                {
                    'type': 'stats_update',
                    'data': {
                        'event': 'playback_started',
                        'video_id': str(instance.video_id),
                    },
                }
            )
        except Exception:
            pass
