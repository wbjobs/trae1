from django.db import migrations, models
import django.db.models.deletion
import uuid


class Migration(migrations.Migration):

    initial = True

    dependencies = [
        ('auth', '0012_alter_user_first_name_max_length'),
    ]

    operations = [
        migrations.CreateModel(
            name='Video',
            fields=[
                ('id', models.UUIDField(default=uuid.uuid4, editable=False, primary_key=True, serialize=False)),
                ('title', models.CharField(max_length=255, verbose_name='视频标题')),
                ('description', models.TextField(blank=True, default='', verbose_name='视频描述')),
                ('source_file', models.FileField(upload_to='videos/source/', verbose_name='源文件')),
                ('cover_image', models.ImageField(blank=True, null=True, upload_to='videos/covers/', verbose_name='封面图')),
                ('status', models.CharField(choices=[('pending', '待转码'), ('processing', '转码中'), ('ready', '已就绪'), ('failed', '转码失败')], default='pending', max_length=20, verbose_name='状态')),
                ('duration', models.FloatField(default=0, verbose_name='时长(秒)')),
                ('file_size', models.BigIntegerField(default=0, verbose_name='文件大小(字节)')),
                ('play_count', models.PositiveIntegerField(default=0, verbose_name='播放次数')),
                ('error_message', models.TextField(blank=True, default='', verbose_name='错误信息')),
                ('created_at', models.DateTimeField(auto_now_add=True, verbose_name='创建时间')),
                ('updated_at', models.DateTimeField(auto_now=True, verbose_name='更新时间')),
                ('uploader', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, related_name='videos', to='auth.user', verbose_name='上传者')),
            ],
            options={
                'verbose_name': '视频',
                'verbose_name_plural': '视频',
                'ordering': ['-created_at'],
            },
        ),
        migrations.CreateModel(
            name='VideoResolution',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('name', models.CharField(max_length=20, verbose_name='分辨率名称')),
                ('width', models.IntegerField(verbose_name='宽度')),
                ('height', models.IntegerField(verbose_name='高度')),
                ('bitrate', models.CharField(max_length=20, verbose_name='码率')),
                ('playlist_path', models.CharField(max_length=500, verbose_name='播放列表路径')),
                ('bandwidth', models.IntegerField(default=0, verbose_name='带宽')),
                ('created_at', models.DateTimeField(auto_now_add=True, verbose_name='创建时间')),
                ('video', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, related_name='resolutions', to='videos.video', verbose_name='视频')),
            ],
            options={
                'verbose_name': '视频分辨率',
                'verbose_name_plural': '视频分辨率',
                'unique_together': {('video', 'name')},
            },
        ),
        migrations.CreateModel(
            name='PlaybackRecord',
            fields=[
                ('id', models.UUIDField(default=uuid.uuid4, editable=False, primary_key=True, serialize=False)),
                ('viewer_ip', models.GenericIPAddressField(verbose_name='观众IP')),
                ('user_agent', models.CharField(blank=True, default='', max_length=500, verbose_name='User Agent')),
                ('resolution', models.CharField(blank=True, default='', max_length=20, verbose_name='观看分辨率')),
                ('start_time', models.DateTimeField(auto_now_add=True, verbose_name='开始时间')),
                ('end_time', models.DateTimeField(blank=True, null=True, verbose_name='结束时间')),
                ('watched_duration', models.FloatField(default=0, verbose_name='观看时长(秒)')),
                ('is_completed', models.BooleanField(default=False, verbose_name='是否看完')),
                ('avg_bitrate', models.FloatField(default=0, verbose_name='平均码率(kbps)')),
                ('created_at', models.DateTimeField(auto_now_add=True, verbose_name='创建时间')),
                ('video', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, related_name='playback_records', to='videos.video', verbose_name='视频')),
            ],
            options={
                'verbose_name': '播放记录',
                'verbose_name_plural': '播放记录',
                'ordering': ['-created_at'],
            },
        ),
        migrations.CreateModel(
            name='EncryptionKeyLog',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('key_id', models.CharField(max_length=100, verbose_name='密钥ID')),
                ('key_index', models.IntegerField(verbose_name='密钥索引')),
                ('created_at', models.DateTimeField(auto_now_add=True, verbose_name='创建时间')),
                ('expires_at', models.DateTimeField(verbose_name='过期时间')),
                ('is_active', models.BooleanField(default=True, verbose_name='是否活跃')),
            ],
            options={
                'verbose_name': '加密密钥日志',
                'verbose_name_plural': '加密密钥日志',
                'ordering': ['-created_at'],
            },
        ),
    ]
