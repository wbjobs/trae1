<template>
  <div class="sidebar">
    <div class="logo-container">
      <h2 class="logo-title">{{ collapsed ? 'MF' : '微前端管理平台' }}</h2>
    </div>
    <el-scrollbar wrap-class="scrollbar-wrapper">
      <el-menu
        :default-active="activeMenu"
        :collapse="collapsed"
        :collapse-transition="false"
        background-color="#304156"
        text-color="#bfcbd9"
        active-text-color="#409EFF"
        mode="vertical"
        @select="handleMenuSelect">
        <sidebar-item
          v-for="route in menuRoutes"
          :key="route.path"
          :item="route"
          :base-path="route.path" />
      </el-menu>
    </el-scrollbar>
  </div>
</template>

<script>
import SidebarItem from './SidebarItem.vue';
import { mapGetters } from 'vuex';

export default {
  name: 'Sidebar',
  components: {
    SidebarItem
  },
  computed: {
    ...mapGetters(['sidebarRouters']),
    activeMenu() {
      const route = this.$route;
      const { meta, path } = route;
      if (meta.activeMenu) {
        return meta.activeMenu;
      }
      return path;
    },
    menuRoutes() {
      return this.sidebarRouters.filter(
        route => !route.hidden && route.children && route.children.length > 0
      );
    },
    collapsed() {
      return !this.$store.state.app.sidebar.opened;
    }
  },
  methods: {
    handleMenuSelect(index) {
      this.$router.push(index);
    }
  }
};
</script>

<style scoped>
.sidebar {
  height: 100%;
  width: 100%;
  background-color: #304156;
  display: flex;
  flex-direction: column;
}
.logo-container {
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  background-color: #2b3648;
}
.logo-title {
  color: #fff;
  font-size: 16px;
  font-weight: bold;
  margin: 0;
  white-space: nowrap;
}
</style>
