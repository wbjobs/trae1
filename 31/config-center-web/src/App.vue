<template>
  <el-container style="height: 100vh">
    <el-header style="background:#409EFF;color:#fff;display:flex;align-items:center;">
      <h2 style="margin:0;color:#fff">⚙ 分布式配置中心</h2>
      <span style="margin-left:auto">Config Center Admin</span>
    </el-header>
    <el-container>
      <el-aside width="220px" style="background:#2c3e50">
        <el-menu
          :default-active="route"
          background-color="#2c3e50"
          text-color="#fff"
          active-text-color="#409EFF"
          router>
          <el-menu-item index="/">
            <el-icon><Menu /></el-icon>
            <span>配置管理</span>
          </el-menu-item>
          <el-menu-item index="/gray">
            <el-icon><Promotion /></el-icon>
            <span>灰度发布</span>
          </el-menu-item>
        </el-menu>
      </el-aside>
      <el-main>
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup>
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { Menu, Promotion } from '@element-plus/icons-vue'

const route = computed(() => useRoute().path)
</script>

<style>
html, body, #app { margin:0; padding:0; height:100%; }
</style>
