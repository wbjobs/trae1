<template>
  <div id="app" class="app-wrapper">
    <el-container>
      <el-header class="app-header">
        <div class="header-left">
        <router-link to="/" class="logo">
          <el-icon :size="24"><VideoPlay /></el-icon>
          <span>HLS加密视频点播</span>
        </router-link>
        <el-menu
          mode="horizontal" :default-active="activeMenu" class="header-menu" @select="handleMenuSelect">
          <el-menu-item index="home">
            <router-link to="/">首页</router-link>
          </el-menu-item>
          <el-menu-item index="admin">
            <router-link to="/admin">管理后台</router-link>
          </el-menu-item>
        </el-menu>
      </div>
      <div class="header-right">
        <el-tag type="success" effect="dark" v-if="keyInfo">
          密钥索引: {{ keyInfo.current_index }} ({{ keyInfo.min_valid_index }}-{{ keyInfo.max_valid_index }})
        </el-tag>
      </div>
    </el-header>
    <el-main>
      <router-view />
    </el-main>
    </el-container>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue'
import { useRoute } from 'vue-router'
import { videoApi } from '@/api'

const route = useRoute()
const keyInfo = ref(null)
let keyInfoTimer = null

const activeMenu = computed(() => {
  if (route.path.startsWith('/admin')) return 'admin'
  return 'home'
})

const handleMenuSelect = () => {}

async function fetchKeyInfo() {
  try {
    const res = await videoApi.getKeyInfo()
    keyInfo.value = res.data
  } catch (e) {
    // ignore
  }
}

onMounted(() => {
  fetchKeyInfo()
  keyInfoTimer = setInterval(fetchKeyInfo, 30000)
})

onUnmounted(() => {
  if (keyInfoTimer) clearInterval(keyInfoTimer)
})
</script>

<style scoped>
.app-wrapper {
  min-height: 100vh;
}

.app-header {
  background: #fff;
  border-bottom: 1px solid #e4e7ed;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  height: 60px;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 30px;
}

.logo {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 18px;
  font-weight: 600;
  color: #303133;
}

.header-menu {
  border-bottom: none;
}

.header-right {
  display: flex;
  align-items: center;
}

:deep(.el-menu) {
  border-bottom: none;
}
</style>
