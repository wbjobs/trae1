import { createRouter, createWebHistory } from 'vue-router'
import NProgress from 'nprogress'
import 'nprogress/nprogress.css'
import { useUserStore } from '@/stores/user'

NProgress.configure({ showSpinner: false })

const routes = [
  {
    path: '/',
    redirect: '/documents'
  },
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/pages/Login.vue'),
    meta: { title: '登录', requiresAuth: false }
  },
  {
    path: '/register',
    name: 'Register',
    component: () => import('@/pages/Register.vue'),
    meta: { title: '注册', requiresAuth: false }
  },
  {
    path: '/documents',
    name: 'DocumentList',
    component: () => import('@/pages/document/List.vue'),
    meta: { title: '文档列表', requiresAuth: true }
  },
  {
    path: '/documents/create',
    name: 'DocumentCreate',
    component: () => import('@/pages/document/Create.vue'),
    meta: { title: '创建文档', requiresAuth: true }
  },
  {
    path: '/documents/:id',
    name: 'DocumentDetail',
    component: () => import('@/pages/document/Detail.vue'),
    meta: { title: '文档详情', requiresAuth: true }
  },
  {
    path: '/documents/:id/edit',
    name: 'DocumentEdit',
    component: () => import('@/pages/document/Edit.vue'),
    meta: { title: '编辑文档', requiresAuth: true }
  },
  {
    path: '/documents/:id/annotations',
    name: 'AnnotationList',
    component: () => import('@/pages/annotation/List.vue'),
    meta: { title: '批注列表', requiresAuth: true }
  },
  {
    path: '/documents/:id/permissions',
    name: 'PermissionManage',
    component: () => import('@/pages/permission/Manage.vue'),
    meta: { title: '权限管理', requiresAuth: true }
  },
  {
    path: '/documents/:id/versions',
    name: 'VersionHistory',
    component: () => import('@/pages/history/List.vue'),
    meta: { title: '历史版本', requiresAuth: true }
  },
  {
    path: '/profile',
    name: 'Profile',
    component: () => import('@/pages/Profile.vue'),
    meta: { title: '个人中心', requiresAuth: true }
  },
  {
    path: '/:pathMatch(.*)*',
    name: 'NotFound',
    component: () => import('@/pages/NotFound.vue'),
    meta: { title: '页面不存在' }
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, from, next) => {
  NProgress.start()
  document.title = `${to.meta.title || '企业文档协同批注系统'} - 企业文档协同批注系统`
  
  const userStore = useUserStore()
  
  if (to.meta.requiresAuth !== false && !userStore.isLoggedIn) {
    next({ name: 'Login', query: { redirect: to.fullPath } })
  } else {
    next()
  }
})

router.afterEach(() => {
  NProgress.done()
})

export default router
