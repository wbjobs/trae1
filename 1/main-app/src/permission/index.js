import router from '@/router';
import store from '@/store';
import { API_CONFIG, PERMISSION_CONFIG } from '@/config';
import Cookies from 'js-cookie';

const WHITE_LIST = PERMISSION_CONFIG.PUBLIC_ROUTES;

router.beforeEach(async (to, from, next) => {
  const token = Cookies.get(API_CONFIG.TOKEN_KEY);

  if (!token) {
    if (WHITE_LIST.includes(to.path)) {
      next();
    } else {
      next(`/login?redirect=${to.path}`);
    }
    return;
  }

  if (to.path === '/login') {
    next(PERMISSION_CONFIG.DEFAULT_ROUTE);
    return;
  }

  try {
    if (!store.getters.userInfo) {
      await store.dispatch('user/getUserInfo');
    }

    if (!store.getters.permissions || store.getters.permissions.length === 0) {
      await store.dispatch('permission/getPermissions');
    }

    const hasPermission = checkPermission(to, store.getters.permissions);

    if (!hasPermission) {
      next('/403');
      return;
    }

    next();
  } catch (error) {
    console.error('Route guard error:', error);
    Cookies.remove(API_CONFIG.TOKEN_KEY);
    next(`/login?redirect=${to.path}`);
  }
});

function checkPermission(route, permissions) {
  const userInfo = store.getters.userInfo;

  if (userInfo && userInfo.roles) {
    const isSuperAdmin = userInfo.roles.some(role => role.code === PERMISSION_CONFIG.SUPER_ADMIN_ROLE);
    if (isSuperAdmin) {
      return true;
    }
  }

  if (route.meta && route.meta.app) {
    if (!permissions || permissions.length === 0) {
      return false;
    }
    return permissions.some(p => p.app === route.meta.app);
  }

  if (route.meta && route.meta.permission) {
    if (!permissions || permissions.length === 0) {
      return false;
    }
    return permissions.some(p => p.code === route.meta.permission);
  }

  return true;
}

export default router;
