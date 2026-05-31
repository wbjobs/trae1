<template>
  <div class="layout-wrapper">
    <div class="sidebar-container">
      <sidebar />
    </div>
    <div class="main-container">
      <div class="header">
        <navbar />
      </div>
      <div class="content-wrapper">
        <app-main />
      </div>
    </div>
  </div>
</template>

<script>
import Sidebar from './components/Sidebar.vue';
import Navbar from './components/Navbar.vue';
import AppMain from './components/AppMain.vue';

export default {
  name: 'Layout',
  components: {
    Sidebar,
    Navbar,
    AppMain
  }
};
</script>

<style scoped>
.layout-wrapper {
  height: 100vh;
  width: 100%;
  display: flex;
  overflow: hidden;
}
.sidebar-container {
  width: 220px;
  background-color: #304156;
  transition: width 0.28s;
  overflow: hidden;
}
.sidebar-container.collapsed {
  width: 64px;
}
.main-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background-color: #f0f2f5;
}
.header {
  height: 56px;
  background-color: #fff;
  box-shadow: 0 1px 4px rgba(0, 21, 41, 0.08);
  z-index: 10;
}
.content-wrapper {
  flex: 1;
  overflow: auto;
  padding: 16px;
}
</style>
