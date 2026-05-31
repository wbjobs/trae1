from django.contrib import admin
from .models import Video, VideoResolution, PlaybackRecord, EncryptionKeyLog


class VideoResolutionInline(admin.TabularInline):
    model = VideoResolution
    extra = 0
    readonly_fields = ['name', 'width', 'height', 'bitrate', 'bandwidth', 'playlist_path']


@admin.register(Video)
class VideoAdmin(admin.ModelAdmin):
    list_display = ['title', 'uploader', 'status', 'enable_watermark', 'watermark_position', 'duration', 'play_count', 'created_at']
    list_filter = ['status', 'enable_watermark', 'watermark_position', 'created_at']
    search_fields = ['title', 'description', 'uploader__username']
    readonly_fields = ['id', 'status', 'duration', 'file_size', 'play_count', 'error_message', 'created_at', 'updated_at']
    fieldsets = [
        (None, {
            'fields': ['title', 'description', 'uploader', 'uploader_ip', 'source_file', 'cover_image', 'status'],
        }),
        ('水印设置', {
            'fields': ['enable_watermark', 'watermark_position', 'watermark_opacity', 'watermark_font_size', 'watermark_refresh_interval', 'watermark_custom_text'],
        }),
        ('统计信息', {
            'fields': ['duration', 'file_size', 'play_count', 'error_message'],
        }),
        ('时间信息', {
            'fields': ['created_at', 'updated_at'],
        }),
    ]
    inlines = [VideoResolutionInline]


@admin.register(PlaybackRecord)
class PlaybackRecordAdmin(admin.ModelAdmin):
    list_display = ['video', 'viewer_ip', 'resolution', 'watched_duration', 'is_completed', 'avg_bitrate', 'created_at']
    list_filter = ['is_completed', 'resolution', 'created_at']
    search_fields = ['video__title', 'viewer_ip', 'user_agent']
    readonly_fields = ['id', 'created_at']


@admin.register(EncryptionKeyLog)
class EncryptionKeyLogAdmin(admin.ModelAdmin):
    list_display = ['key_id', 'key_index', 'is_active', 'created_at', 'expires_at']
    list_filter = ['is_active', 'created_at']
    readonly_fields = ['key_id', 'key_index', 'created_at', 'expires_at']
