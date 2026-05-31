<template>
  <el-container class="admin-container">
    <el-aside width="220px" class="admin-aside">
      <el-menu
        :default-active="activeMenu"
        router
        class="admin-menu"
        background-color="#001529"
        text-color="#fff"
        active-text-color="#ffd04b"
      >
        <el-menu-item index="/admin/videos">
          <el-icon><VideoCamera /></el-icon>
          <span>视频管理</span>
        </el-menu-item>
        <el-menu-item index="/admin/upload">
          <el-icon><Upload /></el-icon>
          <span>上传视频</span>
        </el-menu-item>
        <el-menu-item index="/admin/stats">
          <el-icon><DataAnalysis /></el-icon>
          <span>播放统计</span>
        </el-menu-item>
        <el-menu-item index="/admin/records">
          <el-icon><List /></el-icon>
          <span>播放记录</span>
        </el-menu-item>
      </el-menu>
    </el-aside>
    <el-main class="admin-main">
      <router-view />
    </el-main>
  </el-container>
</template>

<script setup>
import { computed } from 'vue'
import { useRoute } from 'vue-router'

const route = useRoute()
const activeMenu = computed(() => route.path)
</script>

<style scoped>
.admin-container {
  min-height: calc(100vh - 60px);
}

.admin-aside {
  background: #001529;
}

.admin-menu {
  border-right: none;
  height: 100%;
}

.admin-main {
  background: #f5f7fa;
  padding: 20px;
}
</style>
