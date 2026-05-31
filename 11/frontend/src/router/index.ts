import { createRouter, createWebHistory, RouteRecordRaw } from 'vue-router'
import { useUserStore } from '@/stores/user'

const routes: RouteRecordRaw[] = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/auth/Login.vue'),
    meta: { title: '登录' }
  },
  {
    path: '/register',
    name: 'Register',
    component: () => import('@/views/auth/Register.vue'),
    meta: { title: '注册' }
  },
  {
    path: '/',
    component: () => import('@/layouts/BasicLayout.vue'),
    redirect: '/dashboard',
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/dashboard/Index.vue'),
        meta: { title: '控制台', icon: 'Odometer' }
      },
      {
        path: 'projects',
        name: 'Projects',
        component: () => import('@/views/projects/Index.vue'),
        meta: { title: '项目管理', icon: 'Folder' }
      },
      {
        path: 'projects/:projectId/docs',
        name: 'ProjectDocs',
        component: () => import('@/views/docs/Index.vue'),
        meta: { title: '文档列表', icon: 'Document' }
      },
      {
        path: 'docs/:docId',
        name: 'DocDetail',
        component: () => import('@/views/docs/Detail.vue'),
        meta: { title: '文档详情', icon: 'Document' }
      },
      {
        path: 'docs/:docId/edit',
        name: 'DocEdit',
        component: () => import('@/views/docs/Edit.vue'),
        meta: { title: '编辑文档', icon: 'Edit' }
      },
      {
        path: 'docs/:docId/version',
        name: 'DocVersion',
        component: () => import('@/views/docs/version/Compare.vue'),
        meta: { title: '版本对比', icon: 'Refresh' }
      },
      {
        path: 'docs/:docId/debug',
        name: 'DocDebug',
        component: () => import('@/views/docs/debug/Index.vue'),
        meta: { title: '在线调试', icon: 'Connection' }
      },
      {
        path: 'members',
        name: 'Members',
        component: () => import('@/views/members/Index.vue'),
        meta: { title: '成员管理', icon: 'User' }
      }
    ]
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, _from, next) => {
  const userStore = useUserStore()
  if (to.meta.title) {
    document.title = `${to.meta.title} - 接口文档平台`
  }
  if (to.path === '/login' || to.path === '/register') {
    return next()
  }
  if (!userStore.token) {
    return next('/login')
  }
  next()
})

export default router
