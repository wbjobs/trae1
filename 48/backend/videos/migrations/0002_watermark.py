from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('videos', '0001_initial'),
    ]

    operations = [
        migrations.AddField(
            model_name='video',
            name='uploader_ip',
            field=models.GenericIPAddressField(blank=True, null=True, verbose_name='上传者IP'),
        ),
        migrations.AddField(
            model_name='video',
            name='enable_watermark',
            field=models.BooleanField(default=True, verbose_name='启用水印'),
        ),
        migrations.AddField(
            model_name='video',
            name='watermark_position',
            field=models.CharField(
                choices=[
                    ('bottom_right', '右下角'),
                    ('bottom_left', '左下角'),
                    ('top_right', '右上角'),
                    ('top_left', '左上角'),
                ],
                default='bottom_right',
                max_length=20,
                verbose_name='水印位置',
            ),
        ),
        migrations.AddField(
            model_name='video',
            name='watermark_opacity',
            field=models.FloatField(default=0.3, verbose_name='水印透明度'),
        ),
        migrations.AddField(
            model_name='video',
            name='watermark_font_size',
            field=models.IntegerField(default=24, verbose_name='水印字号'),
        ),
        migrations.AddField(
            model_name='video',
            name='watermark_refresh_interval',
            field=models.IntegerField(default=30, verbose_name='水印刷新间隔(秒)'),
        ),
        migrations.AddField(
            model_name='video',
            name='watermark_custom_text',
            field=models.CharField(blank=True, default='', max_length=100, verbose_name='自定义水印文本'),
        ),
    ]
