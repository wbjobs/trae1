from django.urls import path, include
from rest_framework.routers import DefaultRouter
from . import views

router = DefaultRouter()
router.register(r'', views.VideoViewSet, basename='video')

urlpatterns = [
    path('', include(router.urls)),

    path('<uuid:video_id>/token/', views.generate_play_token, name='generate-play-token'),
    path('<uuid:video_id>/master.m3u8', views.HLSMasterPlaylistView.as_view(), name='hls-master-playlist'),
    path('<uuid:video_id>/playlist/<str:resolution_name>', views.HLSPlaylistView.as_view(), name='hls-playlist'),
    path('<uuid:video_id>/segment/<str:resolution_name>/<path:segment_name>', views.HLSSegmentView.as_view(), name='hls-segment'),
    path('<uuid:video_id>/key', views.HLSKeyView.as_view(), name='hls-key'),

    path('<uuid:video_id>/playback/<str:action_type>/', views.PlaybackView.as_view(), name='playback'),

    path('stats/', views.video_stats, name='video-stats'),
    path('stats/list/', views.video_stats_list, name='video-stats-list'),
    path('key/info/', views.key_info, name='key-info'),
    path('playback-records/', views.playback_records, name='playback-records'),
]
