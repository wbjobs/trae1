import os
import shutil
import logging
import subprocess
import json
from pathlib import Path

from django.conf import settings
from django.core.files import File

logger = logging.getLogger('videos')

WATERMARK_POSITION_MAP = {
    'bottom_right': 'x=w-tw-{mx}:y=h-th-{my}',
    'bottom_left': 'x={mx}:y=h-th-{my}',
    'top_right': 'x=w-tw-{mx}:y={my}',
    'top_left': 'x={mx}:y={my}',
}


class TranscodeService:

    def get_ffprobe_info(self, file_path):
        cmd = [
            'ffprobe', '-v', 'quiet', '-print_format', 'json',
            '-show_format', '-show_streams', file_path,
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            if result.returncode == 0:
                return json.loads(result.stdout)
        except Exception as e:
            logger.error(f'ffprobe error: {e}')
        return None

    def get_video_duration(self, file_path):
        info = self.get_ffprobe_info(file_path)
        if info and 'format' in info:
            return float(info['format'].get('duration', 0))
        return 0

    def _build_watermark_text(self, video):
        if video.watermark_custom_text:
            return video.watermark_custom_text

        username = video.uploader.username if video.uploader else 'unknown'
        ip_suffix = ''
        if video.uploader_ip:
            ip_str = video.uploader_ip
            if '.' in ip_str:
                last_octet = ip_str.split('.')[-1]
                ip_suffix = last_octet[-4:] if len(last_octet) >= 4 else last_octet
            elif ':' in ip_str:
                parts = ip_str.split(':')
                last_part = parts[-1] if parts[-1] else parts[-2]
                ip_suffix = last_part[-4:] if len(last_part) >= 4 else last_part

        text = username
        if ip_suffix:
            text = f'{username} IP{ip_suffix}'

        return text

    def _build_watermark_filter(self, video, res_config):
        if not video.enable_watermark:
            return None

        wm_defaults = settings.VIDEO_WATERMARK
        position = video.watermark_position or wm_defaults['POSITION']
        opacity = video.watermark_opacity or wm_defaults['OPACITY']
        font_size = video.watermark_font_size or wm_defaults['FONT_SIZE']
        font_file = wm_defaults.get('FONT_FILE', '')
        margin_x = wm_defaults.get('MARGIN_X', 10)
        margin_y = wm_defaults.get('MARGIN_Y', 10)
        refresh_interval = video.watermark_refresh_interval or wm_defaults['REFRESH_INTERVAL']

        base_text = self._build_watermark_text(video)

        pos_template = WATERMARK_POSITION_MAP.get(position, WATERMARK_POSITION_MAP['bottom_right'])
        pos_expr = pos_template.format(mx=margin_x, my=margin_y)

        fontcolor = f'white@{opacity}'

        text_template = (
            f"{base_text} "
            f"%{{localtime\\:%Y-%m-%d %H\\:%M}}"
        )

        filter_parts = []
        if font_file and os.path.exists(font_file):
            filter_parts.append(f"fontfile='{font_file}'")

        filter_parts.append(f"text='{text_template}'")
        filter_parts.append(f"fontsize={font_size}")
        filter_parts.append(f"fontcolor={fontcolor}")
        filter_parts.append(pos_expr)
        filter_parts.append("borderw=1")
        filter_parts.append("bordercolor=black@0.3")
        filter_parts.append("expansion=strftime")

        drawtext_filter = 'drawtext=' + ':'.join(filter_parts)

        return drawtext_filter

    def transcode(self, video):
        from .key_manager import key_manager

        source_path = video.source_file.path
        if not os.path.exists(source_path):
            raise FileNotFoundError(f'源文件不存在: {source_path}')

        video.duration = self.get_video_duration(source_path)
        video.save(update_fields=['duration'])

        hls_dir = os.path.join(settings.MEDIA_ROOT, video.get_hls_dir())
        os.makedirs(hls_dir, exist_ok=True)

        key_id, key_bytes, key_index = key_manager.get_or_create_current_key()
        key_info_path = os.path.join(hls_dir, 'enc.keyinfo')
        key_file_path = os.path.join(hls_dir, 'enc.key')

        with open(key_file_path, 'wb') as f:
            f.write(key_bytes)

        key_uri = f'/api/videos/{video.id}/key?token=TOKEN_PLACEHOLDER&index={key_index}'
        with open(key_info_path, 'w') as f:
            f.write(f'{key_uri}\n')
            f.write(f'{key_file_path}\n')
            f.write(f'{video.id.hex.replace("-", "")[:32].zfill(32)}\n')

        segment_duration = settings.VIDEO_HLS_SEGMENT_DURATION

        video.resolutions.all().delete()

        for res_config in settings.VIDEO_TRANSCODE_RESOLUTIONS:
            res_dir = os.path.join(hls_dir, res_config['name'])
            os.makedirs(res_dir, exist_ok=True)

            playlist_filename = f'playlist.m3u8'
            playlist_path = os.path.join(res_dir, playlist_filename)
            segment_pattern = os.path.join(res_dir, 'segment_%03d.ts')

            vf_filters = [
                f"scale={res_config['width']}:{res_config['height']}:force_original_aspect_ratio=decrease",
            ]

            watermark_filter = self._build_watermark_filter(video, res_config)
            if watermark_filter:
                vf_filters.append(watermark_filter)

            vf_arg = ','.join(vf_filters)

            cmd = [
                'ffmpeg', '-y',
                '-i', source_path,
                '-c:v', 'libx264',
                '-preset', 'fast',
                '-crf', str(res_config['crf']),
                '-maxrate', res_config['bitrate'],
                '-bufsize', res_config['bitrate'],
                '-vf', vf_arg,
                '-c:a', 'aac',
                '-b:a', '128k',
                '-hls_time', str(segment_duration),
                '-hls_playlist_type', 'vod',
                '-hls_segment_filename', segment_pattern,
                '-hls_key_info_file', key_info_path,
                '-hls_flags', 'independent_segments',
                '-f', 'hls',
                playlist_path,
            ]

            logger.info(f'Transcoding {video.id} to {res_config["name"]} with watermark={watermark_filter is not None}')

            try:
                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=3600,
                )
                if result.returncode != 0:
                    logger.error(f'FFmpeg error for {video.id} {res_config["name"]}: {result.stderr}')
                    raise RuntimeError(f'FFmpeg转码失败: {result.stderr[-500:]}')
            except subprocess.TimeoutExpired:
                raise RuntimeError('转码超时')
            except FileNotFoundError:
                raise RuntimeError('ffmpeg未安装，请先安装ffmpeg')

            playlist_rel_path = os.path.join(video.get_hls_dir(), res_config['name'], playlist_filename)
            bandwidth_map = {
                '720p': 2500000,
                '1080p': 5000000,
            }

            from .models import VideoResolution
            VideoResolution.objects.create(
                video=video,
                name=res_config['name'],
                width=res_config['width'],
                height=res_config['height'],
                bitrate=res_config['bitrate'],
                playlist_path=playlist_rel_path,
                bandwidth=bandwidth_map.get(res_config['name'], 2500000),
            )

        self._create_master_playlist(video, hls_dir)

        os.remove(key_info_path)
        os.remove(key_file_path)

        video.status = 'ready'
        video.save(update_fields=['status'])

        logger.info(f'Transcode completed: {video.id}, watermark enabled={video.enable_watermark}')

    def _create_master_playlist(self, video, hls_dir):
        resolutions = video.resolutions.all().order_by('-bandwidth')
        lines = [
            '#EXTM3U',
            '#EXT-X-VERSION:7',
            '',
        ]

        for res in resolutions:
            lines.append(
                f'#EXT-X-STREAM-INF:BANDWIDTH={res.bandwidth},'
                f'RESOLUTION={res.width}x{res.height},'
                f'CODECS="avc1.4d401f,mp4a.40.2"'
            )
            lines.append(f'{res.name}/playlist.m3u8')

        master_path = os.path.join(hls_dir, 'master.m3u8')
        with open(master_path, 'w') as f:
            f.write('\n'.join(lines) + '\n')

    def cleanup_video_files(self, video):
        hls_dir = os.path.join(settings.MEDIA_ROOT, video.get_hls_dir())
        if os.path.exists(hls_dir):
            shutil.rmtree(hls_dir, ignore_errors=True)

        if video.source_file and os.path.exists(video.source_file.path):
            try:
                video.source_file.delete(save=False)
            except Exception:
                pass

        if video.cover_image and os.path.exists(video.cover_image.path):
            try:
                video.cover_image.delete(save=False)
            except Exception:
                pass
