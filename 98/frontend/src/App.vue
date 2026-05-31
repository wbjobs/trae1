<template>
  <div class="app-container">
    <el-container>
      <el-header class="app-header">
        <div class="header-left">
          <el-icon :size="28" color="#409EFF"><Connection /></el-icon>
          <h1 class="app-title">BT网络资源监控系统</h1>
        </div>
        <div class="header-right">
          <el-tag v-if="stats" type="success" size="large" effect="dark">
            <el-icon><Coin /></el-icon>
            {{ stats.total_infohashes }} 资源
          </el-tag>
          <el-tag v-if="stats" type="warning" size="large" effect="dark" style="margin-left: 10px;">
            <el-icon><Document /></el-icon>
            {{ stats.parsed_count }} 已解析
          </el-tag>
          <el-tag v-if="stats" type="info" size="large" effect="dark" style="margin-left: 10px;">
            <el-icon><PieChart /></el-icon>
            {{ stats.total_size_readable }}
          </el-tag>
          <el-tag v-if="stats && stats.invalid_count > 0" type="danger" size="large" effect="dark" style="margin-left: 10px;">
            <el-icon><CircleClose /></el-icon>
            {{ stats.invalid_count }} 无效
          </el-tag>
          <el-tooltip v-if="stats && stats.blacklist_count > 0" content="24小时内自动过期" placement="bottom">
            <el-tag type="danger" size="large" effect="plain" style="margin-left: 10px;">
              <el-icon><Lock /></el-icon>
              黑名单 {{ stats.blacklist_count }}
            </el-tag>
          </el-tooltip>
          <el-tooltip v-if="stats && stats.failure_rate > 0" :content="'失败率: ' + (stats.failure_rate * 100).toFixed(1) + '%'" placement="bottom">
            <el-tag :type="stats.failure_rate > 0.5 ? 'danger' : 'info'" size="large" effect="plain" style="margin-left: 10px;">
              <el-icon><Warning /></el-icon>
              {{ (stats.failure_rate * 100).toFixed(1) }}%
            </el-tag>
          </el-tooltip>
          <el-tooltip v-if="isAdmin && stats?.copyright" content="侵权统计" placement="bottom">
            <el-tag type="danger" size="large" effect="plain" style="margin-left: 10px;">
              <el-icon><Key /></el-icon>
              {{ stats.copyright.infringement_total || 0 }} 侵权
            </el-tag>
          </el-tooltip>
          <el-button v-if="!isAdmin" type="primary" size="small" @click="adminLoginVisible = true" style="margin-left: 10px;">
            <el-icon><User /></el-icon>管理员登录
          </el-button>
          <el-button v-else type="success" size="small" @click="logout" style="margin-left: 10px;">
            <el-icon><UserFilled /></el-icon>管理员
          </el-button>
        </div>
      </el-header>

      <el-main>
        <el-tabs v-model="activeTab" @tab-change="handleTabChange">
          <el-tab-pane label="资源列表" name="resources">
            <div class="tab-toolbar">
              <el-select v-model="filterStatus" placeholder="状态筛选" style="width: 160px;" @change="loadResources">
                <el-option label="全部" value="" />
                <el-option label="已解析" value="parsed" />
                <el-option label="待解析" value="pending" />
              </el-select>
            </div>
            <ResourceList
              :items="resources"
              :total="resourceTotal"
              :page="resourcePage"
              :per-page="20"
              @page-change="handleResourcePageChange"
              @view-detail="handleViewDetail"
            />
          </el-tab-pane>

          <el-tab-pane label="热门资源 (24h)" name="hot">
            <HotResources :resources="hotResources" @view-detail="handleViewDetail" />
          </el-tab-pane>

          <el-tab-pane label="搜索" name="search">
            <SearchBar @search="handleSearch" />
            <div v-if="searchResults" class="search-results">
              <el-alert
                :title="'找到 ' + searchResults.total + ' 条结果 (关键词: ' + searchResults.query + ')'"
                type="success"
                :closable="false"
                style="margin-bottom: 15px;"
              />
              <ResourceList
                :items="searchResults.results"
                :total="searchResults.total"
                :page="1"
                :per-page="searchResults.total"
                :hide-pagination="true"
                @view-detail="handleViewDetail"
              />
            </div>
          </el-tab-pane>

          <el-tab-pane label="提交磁力链接" name="submit">
            <MagnetSubmit @submitted="handleMagnetSubmitted" />
          </el-tab-pane>

          <el-tab-pane v-if="isAdmin" label="版权管理" name="copyright">
            <CopyrightPanel />
          </el-tab-pane>
        </el-tabs>
      </el-main>

      <el-footer class="app-footer">
        <span>BT Monitor v3.0.0 | DHT Crawler + Flask API + Vue3 | pHash Copyright Detection</span>
      </el-footer>
    </el-container>

    <ResourceDetail
      v-model:visible="detailVisible"
      :infohash="detailInfohash"
    />

    <el-dialog v-model="adminLoginVisible" title="管理员登录" width="400px" :close-on-click-modal="false">
      <el-form :model="adminForm" label-width="80px">
        <el-form-item label="令牌">
          <el-input v-model="adminForm.token" type="password" placeholder="请输入管理员令牌" show-password />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="adminLoginVisible = false">取消</el-button>
        <el-button type="primary" @click="adminLogin">登录</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, watch } from 'vue'
import { ElMessage } from 'element-plus'
import ResourceList from './components/ResourceList.vue'
import SearchBar from './components/SearchBar.vue'
import MagnetSubmit from './components/MagnetSubmit.vue'
import HotResources from './components/HotResources.vue'
import ResourceDetail from './components/ResourceDetail.vue'
import CopyrightPanel from './components/CopyrightPanel.vue'
import { fetchStats, fetchResources, fetchHot } from './api/index.js'

const isAdmin = ref(localStorage.getItem('is_admin') === 'true')
const adminLoginVisible = ref(false)
const adminForm = reactive({ token: '' })

const activeTab = ref('resources')
const stats = ref(null)

const resources = ref([])
const resourceTotal = ref(0)
const resourcePage = ref(1)
const filterStatus = ref('')

const hotResources = ref([])

const searchResults = ref(null)

const detailVisible = ref(false)
const detailInfohash = ref('')

function adminLogin() {
  if (!adminForm.token.trim()) {
    ElMessage.warning('请输入管理员令牌')
    return
  }
  localStorage.setItem('admin_token', adminForm.token)
  localStorage.setItem('is_admin', 'true')
  isAdmin.value = true
  adminLoginVisible.value = false
  ElMessage.success('已登录为管理员')
  loadStats()
}

function logout() {
  localStorage.removeItem('admin_token')
  localStorage.removeItem('is_admin')
  isAdmin.value = false
  activeTab.value = 'resources'
  ElMessage.info('已退出管理员登录')
}

async function loadStats() {
  try {
    stats.value = await fetchStats()
  } catch (e) {
    console.error('Failed to load stats:', e)
  }
}

async function loadResources() {
  try {
    const data = await fetchResources(resourcePage.value, 20, filterStatus.value)
    resources.value = data.items
    resourceTotal.value = data.total
  } catch (e) {
    console.error('Failed to load resources:', e)
  }
}

async function loadHot() {
  try {
    const data = await fetchHot(24, 100)
    hotResources.value = data.resources
  } catch (e) {
    console.error('Failed to load hot resources:', e)
  }
}

function handleTabChange(tab) {
  if (tab === 'resources') loadResources()
  if (tab === 'hot') loadHot()
}

function handleResourcePageChange(page) {
  resourcePage.value = page
  loadResources()
}

function handleViewDetail(infohash) {
  detailInfohash.value = infohash
  detailVisible.value = true
}

function handleSearch(results) {
  searchResults.value = results
}

function handleMagnetSubmitted(data) {
  ElMessage.success('磁力链接已提交: ' + data.infohash)
  loadStats()
  loadResources()
}

onMounted(() => {
  loadStats()
  loadResources()
  setInterval(() => {
    loadStats()
    if (activeTab.value === 'resources') loadResources()
    if (activeTab.value === 'hot') loadHot()
  }, 30000)
})
</script>

<style scoped>
.app-container {
  min-height: 100vh;
}

.app-header {
  background: #fff;
  border-bottom: 1px solid #e4e7ed;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.app-title {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
  color: #303133;
}

.header-right {
  display: flex;
  align-items: center;
}

.el-main {
  background: #f5f7fa;
  padding: 20px;
}

.tab-toolbar {
  margin-bottom: 15px;
}

.app-footer {
  text-align: center;
  color: #909399;
  font-size: 12px;
  border-top: 1px solid #e4e7ed;
  background: #fff;
  padding: 12px 0;
}

.search-results {
  margin-top: 15px;
}
</style>
