import { createRouter, createWebHashHistory } from 'vue-router'

const routes = [
  {
    path: '/',
    redirect: '/upload'
  },
  {
    path: '/upload',
    name: 'Upload',
    component: () => import('@/views/Upload.vue')
  },
  {
    path: '/files',
    name: 'Files',
    component: () => import('@/views/FileManage.vue')
  },
  {
    path: '/shares',
    name: 'Shares',
    component: () => import('@/views/ShareManage.vue')
  },
  {
    path: '/access',
    name: 'Access',
    component: () => import('@/views/AccessShare.vue')
  },
  {
    path: '/stats',
    name: 'Stats',
    component: () => import('@/views/Stats.vue')
  }
]

const router = createRouter({
  history: createWebHashHistory(),
  routes
})

export default router
