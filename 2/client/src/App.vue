<template>
  <div id="app" class="app-container">
    <el-config-provider :locale="locale">
      <router-view v-slot="{ Component }">
        <transition name="fade-transform" mode="out-in">
          <component :is="Component" />
        </transition>
      </router-view>
    </el-config-provider>
  </div>
</template>

<script setup>
import zhCn from 'element-plus/dist/locale/zh-cn.mjs'
import { useUserStore } from '@/stores/user'

const userStore = useUserStore()
const locale = zhCn

userStore.initUser()
</script>

<style lang="scss">
.app-container {
  width: 100%;
  min-height: 100vh;
  background-color: var(--bg-color);
}

.fade-transform-enter-active,
.fade-transform-leave-active {
  transition: all 0.3s;
}

.fade-transform-enter-from {
  opacity: 0;
  transform: translateX(-30px);
}

.fade-transform-leave-to {
  opacity: 0;
  transform: translateX(30px);
}
</style>
