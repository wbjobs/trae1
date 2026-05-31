import { createRouter, createWebHistory } from 'vue-router'
import Dashboard from '../views/Dashboard.vue'
import Authorizations from '../views/Authorizations.vue'
import Logs from '../views/Logs.vue'
import AuthorizationPage from '../views/AuthorizationPage.vue'
import RiskEvents from '../views/RiskEvents.vue'

const routes = [
  {
    path: '/',
    name: 'dashboard',
    component: Dashboard
  },
  {
    path: '/authorizations',
    name: 'authorizations',
    component: Authorizations
  },
  {
    path: '/logs',
    name: 'logs',
    component: Logs
  },
  {
    path: '/risk-events',
    name: 'risk-events',
    component: RiskEvents
  },
  {
    path: '/authorize/:clientId',
    name: 'authorize',
    component: AuthorizationPage,
    props: route => ({
      clientId: route.params.clientId,
      scopes: route.query.scopes ? route.query.scopes.split(',') : [],
      clientName: route.query.clientName || '第三方应用',
      redirectUri: route.query.redirectUri || ''
    })
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

export default router
