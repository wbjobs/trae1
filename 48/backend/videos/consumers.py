import json
import logging
from channels.generic.websocket import AsyncWebsocketConsumer
from channels.db import database_sync_to_async
from django.utils import timezone
from datetime import timedelta

logger = logging.getLogger('videos')


class StatsConsumer(AsyncWebsocketConsumer):

    async def connect(self):
        self.video_id = self.scope['url_route']['kwargs'].get('video_id', None)
        if self.video_id:
            self.group_name = f'video_stats_{self.video_id}'
        else:
            self.group_name = 'video_stats_all'

        await self.channel_layer.group_add(
            self.group_name,
            self.channel_name,
        )
        await self.accept()
        await self.send_initial_stats()

    async def disconnect(self, close_code):
        await self.channel_layer.group_discard(
            self.group_name,
            self.channel_name,
        )

    async def receive(self, text_data):
        try:
            data = json.loads(text_data)
            action = data.get('action', 'get_stats')
            if action == 'get_stats':
                await self.send_initial_stats()
        except json.JSONDecodeError:
            pass

    async def send_initial_stats(self):
        stats = await self.get_stats()
        await self.send(text_data=json.dumps({
            'type': 'stats_update',
            'data': stats,
        }))

    @database_sync_to_async
    def get_stats(self):
        from .models import Video, PlaybackRecord

        if self.video_id:
            try:
                video = Video.objects.get(id=self.video_id)
                records = PlaybackRecord.objects.filter(video=video)
                title = video.title
            except Video.DoesNotExist:
                return {'error': '视频不存在'}
        else:
            records = PlaybackRecord.objects.all()
            title = '全部视频'

        total_plays = records.count()
        completed_plays = records.filter(is_completed=True).count()
        completion_rate = (completed_plays / total_plays * 100) if total_plays > 0 else 0

        from django.db.models import Avg
        avg_watched = records.aggregate(avg=Avg('watched_duration'))['avg'] or 0
        avg_bitrate = records.aggregate(avg=Avg('avg_bitrate'))['avg'] or 0

        now = timezone.now()
        recent_plays = records.filter(created_at__gte=now - timedelta(hours=24)).count()

        return {
            'title': title,
            'total_plays': total_plays,
            'completed_plays': completed_plays,
            'completion_rate': round(completion_rate, 2),
            'avg_watched_duration': round(avg_watched, 2),
            'avg_bitrate': round(avg_bitrate, 2),
            'recent_24h_plays': recent_plays,
            'updated_at': now.isoformat(),
        }

    async def stats_update(self, event):
        await self.send(text_data=json.dumps({
            'type': 'stats_update',
            'data': event['data'],
        }))
