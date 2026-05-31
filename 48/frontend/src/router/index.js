import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/',
    name: 'Home',
    component: () => import('@/views/HomeView.vue'),
  },
  {
    path: '/video/:id',
    name: 'VideoPlayer',
    component: () => import('@/views/VideoPlayerView.vue'),
  },
  {
    path: '/admin',
    name: 'Admin',
    component: () => import('@/views/AdminView.vue'),
    redirect: '/admin/videos',
    children: [
      {
        path: 'videos',
        name: 'AdminVideos',
        component: () => import('@/views/admin/VideoList.vue'),
      },
      {
        path: 'upload',
        name: 'AdminUpload',
        component: () => import('@/views/admin/VideoUpload.vue'),
      },
      {
        path: 'stats',
        name: 'AdminStats',
        component: () => import('@/views/admin/StatsDashboard.vue'),
      },
      {
        path: 'records',
        name: 'AdminRecords',
        component: () => import('@/views/admin/PlaybackRecords.vue'),
      },
    ],
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

export default router
