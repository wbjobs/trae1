from rest_framework import serializers
from .models import Video, VideoResolution, PlaybackRecord


class VideoResolutionSerializer(serializers.ModelSerializer):
    class Meta:
        model = VideoResolution
        fields = ['id', 'name', 'width', 'height', 'bitrate', 'bandwidth']


class VideoSerializer(serializers.ModelSerializer):
    resolutions = VideoResolutionSerializer(many=True, read_only=True)
    uploader_name = serializers.CharField(source='uploader.username', read_only=True)
    hls_url = serializers.SerializerMethodField()

    class Meta:
        model = Video
        fields = [
            'id', 'title', 'description', 'uploader', 'uploader_name',
            'cover_image', 'status', 'duration', 'file_size',
            'play_count', 'error_message', 'resolutions', 'hls_url',
            'enable_watermark', 'watermark_position', 'watermark_opacity',
            'watermark_font_size', 'watermark_refresh_interval',
            'watermark_custom_text',
            'created_at', 'updated_at',
        ]
        read_only_fields = ['id', 'status', 'duration', 'file_size', 'play_count', 'error_message', 'created_at', 'updated_at']

    def get_hls_url(self, obj):
        if obj.status != 'ready':
            return None
        request = self.context.get('request')
        if request:
            return request.build_absolute_uri(f'/api/videos/{obj.id}/master.m3u8')
        return f'/api/videos/{obj.id}/master.m3u8'


class VideoUploadSerializer(serializers.ModelSerializer):
    class Meta:
        model = Video
        fields = [
            'id', 'title', 'description', 'source_file', 'cover_image',
            'enable_watermark', 'watermark_position', 'watermark_opacity',
            'watermark_font_size', 'watermark_refresh_interval',
            'watermark_custom_text',
        ]
        read_only_fields = ['id']

    def validate_source_file(self, value):
        max_size = 2 * 1024 * 1024 * 1024
        if value.size > max_size:
            raise serializers.ValidationError('文件大小超过2GB限制')
        allowed_extensions = ['.mp4', '.avi', '.mov', '.mkv', '.flv', '.wmv', '.webm', '.ts']
        ext = '.' + value.name.rsplit('.', 1)[-1].lower() if '.' in value.name else ''
        if ext not in allowed_extensions:
            raise serializers.ValidationError(f'不支持的文件格式: {ext}')
        return value

    def validate_watermark_opacity(self, value):
        if value < 0 or value > 1:
            raise serializers.ValidationError('透明度必须在0到1之间')
        return value

    def validate_watermark_refresh_interval(self, value):
        if value < 1 or value > 3600:
            raise serializers.ValidationError('刷新间隔必须在1到3600秒之间')
        return value


class PlaybackRecordSerializer(serializers.ModelSerializer):
    video_title = serializers.CharField(source='video.title', read_only=True)

    class Meta:
        model = PlaybackRecord
        fields = [
            'id', 'video', 'video_title', 'viewer_ip', 'user_agent',
            'resolution', 'start_time', 'end_time', 'watched_duration',
            'is_completed', 'avg_bitrate', 'created_at',
        ]


class VideoStatsSerializer(serializers.Serializer):
    video_id = serializers.UUIDField()
    title = serializers.CharField()
    total_plays = serializers.IntegerField()
    completed_plays = serializers.IntegerField()
    completion_rate = serializers.FloatField()
    avg_watched_duration = serializers.FloatField()
    avg_bitrate = serializers.FloatField()
    total_duration = serializers.FloatField()


class PlaybackStartSerializer(serializers.Serializer):
    token = serializers.CharField()


class PlaybackUpdateSerializer(serializers.Serializer):
    record_id = serializers.UUIDField()
    watched_duration = serializers.FloatField(required=False, default=0)
    resolution = serializers.CharField(required=False, default='')
    avg_bitrate = serializers.FloatField(required=False, default=0)


class PlaybackEndSerializer(serializers.Serializer):
    record_id = serializers.UUIDField()
    watched_duration = serializers.FloatField()
    is_completed = serializers.BooleanField(default=False)
    avg_bitrate = serializers.FloatField(required=False, default=0)
