<template>
  <a-layout style="min-height: 100vh">
    <a-layout-header style="background: #001529; padding: 0 24px">
      <h1 style="color: white; margin: 0; line-height: 64px">OAuth2 权限审计系统</h1>
    </a-layout-header>
    <a-layout>
      <a-layout-sider width="200" style="background: #fff">
        <a-menu
          v-model:selectedKeys="selectedKeys"
          mode="inline"
          style="height: 100%"
        >
          <a-menu-item key="dashboard" @click="goToDashboard">
            <template #icon><DashboardOutlined /></template>
            数据看板
          </a-menu-item>
          <a-menu-item key="authorizations" @click="goToAuthorizations">
            <template #icon><SafetyCertificateOutlined /></template>
            授权管理
          </a-menu-item>
          <a-menu-item key="logs" @click="goToLogs">
            <template #icon><FileTextOutlined /></template>
            审计日志
          </a-menu-item>
          <a-menu-item key="risk-events" @click="goToRiskEvents">
            <template #icon><WarningOutlined /></template>
            风险事件
          </a-menu-item>
        </a-menu>
      </a-layout-sider>
      <a-layout-content style="margin: 16px">
        <router-view />
      </a-layout-content>
    </a-layout>
  </a-layout>
</template>

<script setup>
import { ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { DashboardOutlined, SafetyCertificateOutlined, FileTextOutlined, WarningOutlined } from '@ant-design/icons-vue'

const router = useRouter()
const route = useRoute()
const selectedKeys = ref([route.name || 'dashboard'])

const goToDashboard = () => {
  selectedKeys.value = ['dashboard']
  router.push('/')
}

const goToAuthorizations = () => {
  selectedKeys.value = ['authorizations']
  router.push('/authorizations')
}

const goToLogs = () => {
  selectedKeys.value = ['logs']
  router.push('/logs')
}

const goToRiskEvents = () => {
  selectedKeys.value = ['risk-events']
  router.push('/risk-events')
}
</script>

<style>
body {
  margin: 0;
}
</style>
