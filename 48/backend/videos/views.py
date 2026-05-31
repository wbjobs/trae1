import os
import json
import time
import logging
from datetime import datetime, timezone as dt_timezone

from django.conf import settings
from django.http import HttpResponse, JsonResponse, Http404, StreamingHttpResponse
from django.views import View
from django.views.decorators.csrf import csrf_exempt
from django.utils.decorators import method_decorator
from django.db.models import Count, Avg, Q, F
from django.shortcuts import get_object_or_404

from rest_framework import viewsets, status, permissions
from rest_framework.decorators import action, api_view, permission_classes
from rest_framework.response import Response
from rest_framework.parsers import MultiPartParser, FormParser, JSONParser

from .models import Video, VideoResolution, PlaybackRecord
from .serializers import (
    VideoSerializer, VideoUploadSerializer, PlaybackRecordSerializer,
    VideoStatsSerializer, PlaybackStartSerializer, PlaybackUpdateSerializer,
    PlaybackEndSerializer,
)
from .key_manager import key_manager
from .services.transcode import TranscodeService

logger = logging.getLogger('videos')


def get_client_ip(request):
    x_forwarded_for = request.META.get('HTTP_X_FORWARDED_FOR')
    if x_forwarded_for:
        ip = x_forwarded_for.split(',')[0].strip()
    else:
        ip = request.META.get('REMOTE_ADDR', '0.0.0.0')
    return ip


class VideoViewSet(viewsets.ModelViewSet):
    queryset = Video.objects.all()
    parser_classes = [MultiPartParser, FormParser, JSONParser]

    def get_serializer_class(self):
        if self.action == 'create':
            return VideoUploadSerializer
        return VideoSerializer

    def get_permissions(self):
        if self.action in ['list', 'retrieve']:
            return [permissions.AllowAny()]
        return [permissions.IsAuthenticated()]

    def get_serializer_context(self):
        context = super().get_serializer_context()
        context['request'] = self.request
        return context

    def create(self, request, *args, **kwargs):
        serializer = self.get_serializer(data=request.data)
        serializer.is_valid(raise_exception=True)
        client_ip = get_client_ip(request)
        video = serializer.save(uploader=request.user, uploader_ip=client_ip)
        video.file_size = video.source_file.size
        video.save()

        from .tasks import transcode_video
        transcode_video.delay(str(video.id))

        output_serializer = VideoSerializer(video, context={'request': request})
        return Response(output_serializer.data, status=status.HTTP_201_CREATED)

    @action(detail=True, methods=['post'], url_path='retry-transcode')
    def retry_transcode(self, request, pk=None):
        video = self.get_object()
        if video.status not in ['failed', 'pending']:
            return Response(
                {'error': '只有失败或待转码的视频才能重试'},
                status=status.HTTP_400_BAD_REQUEST,
            )
        video.status = 'pending'
        video.error_message = ''
        video.save()
        from .tasks import transcode_video
        transcode_video.delay(str(video.id))
        return Response({'status': '转码任务已重新提交'})

    @action(detail=True, methods=['delete'], url_path='delete-files')
    def delete_files(self, request, pk=None):
        video = self.get_object()
        service = TranscodeService()
        service.cleanup_video_files(video)
        video.delete()
        return Response(status=status.HTTP_204_NO_CONTENT)


@api_view(['GET'])
@permission_classes([permissions.AllowAny])
def video_stats(request):
    video_id = request.query_params.get('video_id')
    if video_id:
        records = PlaybackRecord.objects.filter(video_id=video_id)
    else:
        records = PlaybackRecord.objects.all()

    total_plays = records.count()
    completed_plays = records.filter(is_completed=True).count()
    completion_rate = (completed_plays / total_plays * 100) if total_plays > 0 else 0
    avg_watched = records.aggregate(avg=Avg('watched_duration'))['avg'] or 0
    avg_bitrate = records.aggregate(avg=Avg('avg_bitrate'))['avg'] or 0

    if video_id:
        video = Video.objects.filter(id=video_id).first()
        title = video.title if video else ''
        total_duration = video.duration if video else 0
    else:
        title = '全部视频'
        total_duration = 0

    data = {
        'video_id': video_id,
        'title': title,
        'total_plays': total_plays,
        'completed_plays': completed_plays,
        'completion_rate': round(completion_rate, 2),
        'avg_watched_duration': round(avg_watched, 2),
        'avg_bitrate': round(avg_bitrate, 2),
        'total_duration': total_duration,
    }
    return Response(data)


@api_view(['GET'])
@permission_classes([permissions.AllowAny])
def video_stats_list(request):
    videos = Video.objects.filter(status='ready')
    stats = []
    for video in videos:
        records = PlaybackRecord.objects.filter(video=video)
        total_plays = records.count()
        completed_plays = records.filter(is_completed=True).count()
        completion_rate = (completed_plays / total_plays * 100) if total_plays > 0 else 0
        avg_watched = records.aggregate(avg=Avg('watched_duration'))['avg'] or 0
        avg_bitrate = records.aggregate(avg=Avg('avg_bitrate'))['avg'] or 0
        stats.append({
            'video_id': str(video.id),
            'title': video.title,
            'total_plays': total_plays,
            'completed_plays': completed_plays,
            'completion_rate': round(completion_rate, 2),
            'avg_watched_duration': round(avg_watched, 2),
            'avg_bitrate': round(avg_bitrate, 2),
            'total_duration': video.duration,
        })
    return Response(stats)


@method_decorator(csrf_exempt, name='dispatch')
class PlaybackView(View):

    def post(self, request, action_type, video_id):
        try:
            data = json.loads(request.body)
        except json.JSONDecodeError:
            return JsonResponse({'error': '无效的JSON数据'}, status=400)

        if action_type == 'start':
            return self._start_playback(request, video_id, data)
        elif action_type == 'update':
            return self._update_playback(request, video_id, data)
        elif action_type == 'end':
            return self._end_playback(request, video_id, data)
        else:
            return JsonResponse({'error': '未知的操作类型'}, status=400)

    def _start_playback(self, request, video_id, data):
        token = data.get('token', '')
        client_ip = get_client_ip(request)

        if not key_manager.validate_play_token(token, video_id, client_ip):
            return JsonResponse({'error': '无效或已过期的播放令牌'}, status=403)

        video = get_object_or_404(Video, id=video_id)
        user_agent = request.META.get('HTTP_USER_AGENT', '')[:500]

        record = PlaybackRecord.objects.create(
            video=video,
            viewer_ip=client_ip,
            user_agent=user_agent,
            resolution=data.get('resolution', ''),
            avg_bitrate=data.get('avg_bitrate', 0),
        )

        video.play_count = F('play_count') + 1
        video.save(update_fields=['play_count'])

        return JsonResponse({
            'record_id': str(record.id),
            'video_id': str(video.id),
            'title': video.title,
            'duration': video.duration,
        })

    def _update_playback(self, request, video_id, data):
        record_id = data.get('record_id')
        if not record_id:
            return JsonResponse({'error': '缺少record_id'}, status=400)

        try:
            record = PlaybackRecord.objects.get(id=record_id, video_id=video_id)
        except PlaybackRecord.DoesNotExist:
            return JsonResponse({'error': '播放记录不存在'}, status=404)

        record.watched_duration = data.get('watched_duration', record.watched_duration)
        record.resolution = data.get('resolution', record.resolution)
        record.avg_bitrate = data.get('avg_bitrate', record.avg_bitrate)
        record.save(update_fields=['watched_duration', 'resolution', 'avg_bitrate'])

        return JsonResponse({'status': 'ok'})

    def _end_playback(self, request, video_id, data):
        record_id = data.get('record_id')
        if not record_id:
            return JsonResponse({'error': '缺少record_id'}, status=400)

        try:
            record = PlaybackRecord.objects.get(id=record_id, video_id=video_id)
        except PlaybackRecord.DoesNotExist:
            return JsonResponse({'error': '播放记录不存在'}, status=404)

        record.end_time = datetime.now(dt_timezone.utc)
        record.watched_duration = data.get('watched_duration', record.watched_duration)
        record.is_completed = data.get('is_completed', False)
        record.avg_bitrate = data.get('avg_bitrate', record.avg_bitrate)
        record.save(update_fields=['end_time', 'watched_duration', 'is_completed', 'avg_bitrate'])

        return JsonResponse({'status': 'ok'})


@method_decorator(csrf_exempt, name='dispatch')
class HLSKeyView(View):

    def get(self, request, video_id):
        key_index = request.GET.get('index', None)
        token = request.GET.get('token', '')
        client_ip = get_client_ip(request)

        if not key_manager.validate_play_token(token, video_id, client_ip):
            return HttpResponse('Forbidden', status=403, content_type='text/plain')

        if key_index is not None:
            try:
                key_index = int(key_index)
            except (ValueError, TypeError):
                return HttpResponse('Invalid key index', status=400, content_type='text/plain')
            key_id, key_bytes = key_manager.get_key_by_index(key_index)
        else:
            key_id, key_bytes, key_index = key_manager.get_or_create_current_key()

        if key_bytes is None:
            logger.warning(f'Key not found for index={key_index}, video={video_id}, ip={client_ip}')
            min_idx, max_idx = key_manager.get_valid_key_range()
            response = HttpResponse('Key not found', status=404, content_type='text/plain')
            response['X-Key-Min-Index'] = str(min_idx)
            response['X-Key-Max-Index'] = str(max_idx)
            return response

        response = HttpResponse(key_bytes, content_type='application/octet-stream')
        response['X-Key-Id'] = key_id
        response['X-Key-Index'] = str(key_index)
        response['Cache-Control'] = 'no-cache, no-store, must-revalidate'
        response['Pragma'] = 'no-cache'
        response['Expires'] = '0'
        return response


@method_decorator(csrf_exempt, name='dispatch')
class HLSMasterPlaylistView(View):

    def get(self, request, video_id):
        token = request.GET.get('token', '')
        client_ip = get_client_ip(request)

        if not key_manager.validate_play_token(token, video_id, client_ip):
            return HttpResponse('Forbidden', status=403, content_type='application/vnd.apple.mpegurl')

        video = get_object_or_404(Video, id=video_id, status='ready')

        key_info = key_manager.get_key_info_for_manifest()
        current_index = key_info['current_index']
        min_valid_index = key_info['min_valid_index']
        max_valid_index = key_info['max_valid_index']

        base_url = request.build_absolute_uri('/api/videos')
        key_url_base = f'{base_url}/{video_id}/key'

        lines = [
            '#EXTM3U',
            '#EXT-X-VERSION:7',
            f'#EXT-X-KEY-METHOD:AES-128',
            f'#EXT-X-KEY-URI:{key_url_base}?token={token}&index=CURRENT',
            f'#EXT-X-KEYFORMAT:identity',
            f'#EXT-X-KEYFORMATVERSIONS:{min_valid_index}-{max_valid_index}',
            f'#EXT-X-KEY-ROTATION:YES',
            f'#EXT-X-KEY-CURRENT-INDEX:{current_index}',
            '',
        ]

        resolutions = video.resolutions.all().order_by('-bandwidth')
        for res in resolutions:
            playlist_url = f'{base_url}/{video_id}/playlist/{res.name}?token={token}'
            lines.append(f'#EXT-X-STREAM-INF:BANDWIDTH={res.bandwidth},RESOLUTION={res.width}x{res.height},CODECS="avc1.4d401f,mp4a.40.2"')
            lines.append(playlist_url)

        content = '\n'.join(lines) + '\n'
        response = HttpResponse(content, content_type='application/vnd.apple.mpegurl')
        response['Cache-Control'] = 'no-cache, no-store, must-revalidate'
        response['X-Key-Current-Index'] = str(current_index)
        response['X-Key-Min-Index'] = str(min_valid_index)
        response['X-Key-Max-Index'] = str(max_valid_index)
        return response


@method_decorator(csrf_exempt, name='dispatch')
class HLSPlaylistView(View):

    def get(self, request, video_id, resolution_name):
        token = request.GET.get('token', '')
        client_ip = get_client_ip(request)

        if not key_manager.validate_play_token(token, video_id, client_ip):
            return HttpResponse('Forbidden', status=403, content_type='application/vnd.apple.mpegurl')

        video = get_object_or_404(Video, id=video_id, status='ready')
        resolution = get_object_or_404(VideoResolution, video=video, name=resolution_name)

        playlist_file = os.path.join(settings.MEDIA_ROOT, resolution.playlist_path)
        if not os.path.exists(playlist_file):
            raise Http404('播放列表文件不存在')

        key_info = key_manager.get_key_info_for_manifest()
        current_index = key_info['current_index']
        min_valid_index = key_info['min_valid_index']
        max_valid_index = key_info['max_valid_index']
        current_key_id = key_info['current_key_id']

        base_url = request.build_absolute_uri('/api/videos')
        key_url_template = f'{base_url}/{video_id}/key?token={token}&index=INDEX_PLACEHOLDER'
        segment_base_url = f'{base_url}/{video_id}/segment/{resolution_name}'

        with open(playlist_file, 'r', encoding='utf-8') as f:
            content = f.read()

        iv_hex = video_id.hex.replace('-', '')[:32].zfill(32)
        lines = content.split('\n')
        new_lines = []
        for line in lines:
            if line.startswith('#EXT-X-KEY:'):
                key_url = key_url_template.replace('INDEX_PLACEHOLDER', str(current_index))
                new_lines.append(
                    f'#EXT-X-KEY:METHOD=AES-128,URI="{key_url}",IV=0x{iv_hex},'
                    f'KEYFORMAT="identity",KEYFORMATVERSIONS="{min_valid_index}-{max_valid_index}",'
                    f'KEYINDEX="{current_index}"'
                )
            elif line.startswith('#EXT-X-KEYFORMATVERSIONS:'):
                continue
            elif line.startswith('#EXT-X-KEYINDEX:'):
                continue
            elif line.endswith('.ts') or line.endswith('.m4s'):
                segment_url = f'{segment_base_url}/{line}?token={token}'
                new_lines.append(segment_url)
            else:
                new_lines.append(line)

        content = '\n'.join(new_lines)
        response = HttpResponse(content, content_type='application/vnd.apple.mpegurl')
        response['Cache-Control'] = 'no-cache, no-store, must-revalidate'
        response['X-Key-Current-Index'] = str(current_index)
        response['X-Key-Min-Index'] = str(min_valid_index)
        response['X-Key-Max-Index'] = str(max_valid_index)
        return response


@method_decorator(csrf_exempt, name='dispatch')
class HLSSegmentView(View):

    def get(self, request, video_id, resolution_name, segment_name):
        token = request.GET.get('token', '')
        client_ip = get_client_ip(request)

        if not key_manager.validate_play_token(token, video_id, client_ip):
            return HttpResponse('Forbidden', status=403, content_type='video/mp2t')

        video = get_object_or_404(Video, id=video_id, status='ready')

        segment_path = os.path.join(
            settings.MEDIA_ROOT,
            video.get_hls_dir(),
            resolution_name,
            segment_name,
        )

        if not os.path.exists(segment_path):
            raise Http404('分片文件不存在')

        ext = os.path.splitext(segment_name)[1].lower()
        content_type = 'video/mp2t' if ext == '.ts' else 'video/mp4'

        def file_iterator(file_path, chunk_size=8192):
            with open(file_path, 'rb') as f:
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    yield chunk

        response = StreamingHttpResponse(
            file_iterator(segment_path),
            content_type=content_type,
        )
        response['Content-Length'] = os.path.getsize(segment_path)
        response['Cache-Control'] = 'public, max-age=3600'
        return response


@api_view(['POST'])
@permission_classes([permissions.AllowAny])
def generate_play_token(request, video_id):
    video = get_object_or_404(Video, id=video_id, status='ready')
    client_ip = get_client_ip(request)
    token = key_manager.generate_play_token(str(video.id), client_ip)
    return Response({
        'token': token,
        'expires_in': settings.HLS_ANTI_HOTLINK_TTL,
        'video_id': str(video.id),
    })


@api_view(['GET'])
@permission_classes([permissions.AllowAny])
def key_info(request):
    info = key_manager.get_key_info_for_manifest()
    return Response(info)


@api_view(['GET'])
@permission_classes([permissions.AllowAny])
def playback_records(request):
    video_id = request.query_params.get('video_id')
    queryset = PlaybackRecord.objects.all()
    if video_id:
        queryset = queryset.filter(video_id=video_id)
    queryset = queryset.select_related('video')[:100]
    data = PlaybackRecordSerializer(queryset, many=True).data
    return Response(data)
