<template>
  <el-container class="main-layout">
    <el-aside width="220px" class="aside">
      <div class="logo">
        <el-icon :size="28"><Rocket /></el-icon>
        <span>压测平台</span>
      </div>
      <el-menu
        :default-active="activeMenu"
        class="menu"
        router
        background-color="#001529"
        text-color="#fff"
        active-text-color="#1890ff"
      >
        <el-menu-item index="/dashboard">
          <el-icon><DataBoard /></el-icon>
          <span>仪表盘</span>
        </el-menu-item>
        <el-menu-item index="/configs">
          <el-icon><Setting /></el-icon>
          <span>压测配置</span>
        </el-menu-item>
        <el-menu-item index="/tasks">
          <el-icon><List /></el-icon>
          <span>任务管理</span>
        </el-menu-item>
        <el-menu-item index="/results">
          <el-icon><DataAnalysis /></el-icon>
          <span>结果展示</span>
        </el-menu-item>
        <el-menu-item index="/reports">
          <el-icon><Document /></el-icon>
          <span>报告导出</span>
        </el-menu-item>
      </el-menu>
    </el-aside>
    <el-container>
      <el-header class="header">
        <div class="header-left">
          <span class="page-title">{{ currentPageTitle }}</span>
        </div>
        <div class="header-right">
          <el-tooltip content="刷新" placement="bottom">
            <el-button :icon="Refresh" circle @click="handleRefresh" />
          </el-tooltip>
        </div>
      </el-header>
      <el-main class="main">
        <router-view v-slot="{ Component }">
          <transition name="fade" mode="out-in">
            <component :is="Component" />
          </transition>
        </router-view>
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup>
import { computed, ref } from 'vue'
import { useRoute } from 'vue-router'
import { Refresh } from '@element-plus/icons-vue'

const route = useRoute()
const activeMenu = computed(() => route.path)
const currentPageTitle = computed(() => route.meta.title || '')

const refreshKey = ref(0)
const handleRefresh = () => {
  refreshKey.value++
  window.location.reload()
}
</script>

<style lang="scss" scoped>
.main-layout {
  height: 100vh;
}

.aside {
  background: #001529;
  display: flex;
  flex-direction: column;
}

.logo {
  height: 60px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  font-size: 18px;
  font-weight: 600;
  gap: 10px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.menu {
  flex: 1;
  border-right: none;
}

.header {
  background: #fff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  box-shadow: 0 1px 4px rgba(0, 21, 41, 0.08);
}

.page-title {
  font-size: 18px;
  font-weight: 600;
  color: #1f2937;
}

.main {
  background: #f0f2f5;
  padding: 20px;
  overflow-y: auto;
}

.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
