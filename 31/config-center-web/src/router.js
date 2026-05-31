import { createRouter, createWebHashHistory } from 'vue-router'
import ConfigView from './views/ConfigView.vue'
import GrayscaleView from './views/GrayscaleView.vue'

export default createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', component: ConfigView, name: 'config' },
    { path: '/gray', component: GrayscaleView, name: 'gray' }
  ]
})
