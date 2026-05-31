import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/',
    component: () => import('@/layouts/MainLayout.vue'),
    redirect: '/dashboard',
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/Dashboard.vue'),
        meta: { title: '仪表盘' }
      },
      {
        path: 'configs',
        name: 'ConfigList',
        component: () => import('@/views/config/ConfigList.vue'),
        meta: { title: '压测配置' }
      },
      {
        path: 'configs/create',
        name: 'ConfigCreate',
        component: () => import('@/views/config/ConfigForm.vue'),
        meta: { title: '创建配置' }
      },
      {
        path: 'configs/edit/:id',
        name: 'ConfigEdit',
        component: () => import('@/views/config/ConfigForm.vue'),
        meta: { title: '编辑配置' }
      },
      {
        path: 'tasks',
        name: 'TaskList',
        component: () => import('@/views/task/TaskList.vue'),
        meta: { title: '任务管理' }
      },
      {
        path: 'tasks/create',
        name: 'TaskCreate',
        component: () => import('@/views/task/TaskCreate.vue'),
        meta: { title: '创建任务' }
      },
      {
        path: 'tasks/:id',
        name: 'TaskDetail',
        component: () => import('@/views/task/TaskDetail.vue'),
        meta: { title: '任务详情' }
      },
      {
        path: 'tasks/compare',
        name: 'TaskCompare',
        component: () => import('@/views/task/TaskCompare.vue'),
        meta: { title: '任务对比' }
      },
      {
        path: 'results',
        name: 'ResultList',
        component: () => import('@/views/result/ResultList.vue'),
        meta: { title: '结果展示' }
      },
      {
        path: 'results/:id',
        name: 'ResultDetail',
        component: () => import('@/views/result/ResultDetail.vue'),
        meta: { title: '结果详情' }
      },
      {
        path: 'reports',
        name: 'ReportList',
        component: () => import('@/views/report/ReportList.vue'),
        meta: { title: '报告导出' }
      }
    ]
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, from, next) => {
  document.title = `${to.meta.title || '接口压测平台'} - 接口压测配置管理平台`
  next()
})

export default router
