import Vue from 'vue';
import VueRouter from 'vue-router';
import { PERMISSION_CONFIG } from '@/config';
import Layout from '@/layout/index.vue';

Vue.use(VueRouter);

const constantRoutes = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/login/index.vue'),
    meta: { title: '登录', public: true }
  },
  {
    path: '/403',
    name: 'Forbidden',
    component: () => import('@/views/error/403.vue'),
    meta: { title: '无权限', public: true }
  },
  {
    path: '/404',
    name: 'NotFound',
    component: () => import('@/views/error/404.vue'),
    meta: { title: '页面不存在', public: true }
  },
  {
    path: '/',
    component: Layout,
    redirect: '/dashboard',
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/dashboard/index.vue'),
        meta: { title: '仪表盘', icon: 'dashboard' }
      }
    ]
  }
];

const asyncRoutes = [
  {
    path: '/system-config',
    component: Layout,
    meta: { app: 'system-config', icon: 'setting', title: '系统配置' },
    children: [
      {
        path: '',
        name: 'SystemConfig',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '系统配置', icon: 'setting', app: 'system-config' }
      },
      {
        path: '*',
        name: 'SystemConfigWildcard',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '系统配置', icon: 'setting', app: 'system-config', hidden: true }
      }
    ]
  },
  {
    path: '/data-audit',
    component: Layout,
    meta: { app: 'data-audit', icon: 'audit', title: '数据审计' },
    children: [
      {
        path: '',
        name: 'DataAudit',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '数据审计', icon: 'audit', app: 'data-audit' }
      },
      {
        path: '*',
        name: 'DataAuditWildcard',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '数据审计', icon: 'audit', app: 'data-audit', hidden: true }
      }
    ]
  },
  {
    path: '/operation-log',
    component: Layout,
    meta: { app: 'operation-log', icon: 'log', title: '操作日志' },
    children: [
      {
        path: '',
        name: 'OperationLog',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '操作日志', icon: 'log', app: 'operation-log' }
      },
      {
        path: '*',
        name: 'OperationLogWildcard',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '操作日志', icon: 'log', app: 'operation-log', hidden: true }
      }
    ]
  },
  {
    path: '/role-management',
    component: Layout,
    meta: { app: 'role-management', icon: 'role', title: '角色分级' },
    children: [
      {
        path: '',
        name: 'RoleManagement',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '角色分级', icon: 'role', app: 'role-management' }
      },
      {
        path: '*',
        name: 'RoleManagementWildcard',
        component: () => import('@/views/subapp-container/index.vue'),
        meta: { title: '角色分级', icon: 'role', app: 'role-management', hidden: true }
      }
    ]
  }
];

const routes = [...constantRoutes, ...asyncRoutes, { path: '*', redirect: '/404' }];

const createRouter = () => new VueRouter({
  mode: 'history',
  base: process.env.BASE_URL,
  routes
});

const router = createRouter();

export function resetRouter() {
  const newRouter = createRouter();
  router.matcher = newRouter.matcher;
}

export { constantRoutes, asyncRoutes };

export default router;
