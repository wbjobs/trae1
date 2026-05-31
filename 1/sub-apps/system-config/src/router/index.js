const routes = [
  {
    path: '/',
    name: 'SystemConfig',
    component: () => import('../App.vue')
  }
];

export { routes };
export default routes;
