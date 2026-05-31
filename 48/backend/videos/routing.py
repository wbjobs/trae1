from django.urls import re_path
from . import consumers

websocket_urlpatterns = [
    re_path(r'ws/stats/(?P<video_id>[0-9a-f-]+)/$', consumers.StatsConsumer.as_asgi()),
    re_path(r'ws/stats/$', consumers.StatsConsumer.as_asgi()),
]
