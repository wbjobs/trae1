<template>
  <div class="copyright-panel">
    <el-tabs v-model="activeTab" type="border-card">
      <el-tab-pane label="侵权监控" name="monitor">
        <div class="copyright-stats">
          <el-row :gutter="20">
            <el-col :span="6">
              <el-card shadow="hover" class="stat-card">
                <div class="stat-icon" style="background:#e74c3c">
                  <el-icon :size="28"><Warning /></el-icon>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ stats.infringement_total || 0 }}</div>
                  <div class="stat-label">侵权记录总数</div>
                </div>
              </el-card>
            </el-col>
            <el-col :span="6">
              <el-card shadow="hover" class="stat-card">
                <div class="stat-icon" style="background:#e67e22">
                  <el-icon :size="28"><CircleCheck /></el-icon>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ stats.infringement_confirmed || 0 }}</div>
                  <div class="stat-label">已确认侵权</div>
                </div>
              </el-card>
            </el-col>
            <el-col :span="6">
              <el-card shadow="hover" class="stat-card">
                <div class="stat-icon" style="background:#f39c12">
                  <el-icon :size="28"><Question /></el-icon>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ stats.infringement_suspected || 0 }}</div>
                  <div class="stat-label">疑似侵权</div>
                </div>
              </el-card>
            </el-col>
            <el-col :span="6">
              <el-card shadow="hover" class="stat-card">
                <div class="stat-icon" style="background:#27ae60">
                  <el-icon :size="28"><Document /></el-icon>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ stats.dmca_total || 0 }}</div>
                  <div class="stat-label">DMCA通知</div>
                </div>
              </el-card>
            </el-col>
          </el-row>
        </div>

        <el-card class="filter-card" shadow="never">
          <el-form :inline="true" :model="filter">
            <el-form-item label="状态">
              <el-select v-model="filter.status" placeholder="全部" clearable @change="loadInfringements">
                <el-option label="疑似" value="suspected" />
                <el-option label="已确认" value="confirmed" />
                <el-option label="误报" value="false_positive" />
                <el-option label="已解决" value="resolved" />
              </el-select>
            </el-form-item>
            <el-form-item label="版权方">
              <el-select v-model="filter.holder" placeholder="全部" clearable @change="loadInfringements">
                <el-option v-for="h in holders" :key="h" :label="h" :value="h" />
              </el-select>
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="loadInfringements">
                <el-icon><Refresh /></el-icon>刷新
              </el-button>
            </el-form-item>
          </el-form>
        </el-card>

        <el-table :data="infringements" stripe style="width: 100%" v-loading="loading">
          <el-table-column prop="id" label="ID" width="80" />
          <el-table-column prop="matched_title" label="匹配作品" width="200">
            <template #default="{ row }">
              <el-tooltip :content="row.matched_title" placement="top">
                <span class="truncate-text">{{ row.matched_title }}</span>
              </el-tooltip>
            </template>
          </el-table-column>
          <el-table-column prop="copyright_holder" label="版权方" width="150" />
          <el-table-column prop="file_name" label="文件名" width="250">
            <template #default="{ row }">
              <el-tooltip :content="row.file_name" placement="top">
                <span class="truncate-text">{{ row.file_name }}</span>
              </el-tooltip>
            </template>
          </el-table-column>
          <el-table-column prop="similarity" label="相似度" width="110">
            <template #default="{ row }">
              <el-tag :type="row.similarity >= 0.98 ? 'danger' : 'warning'" size="small">
                {{ (row.similarity * 100).toFixed(1) }}%
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="status" label="状态" width="110">
            <template #default="{ row }">
              <el-tag :type="statusType(row.status)" size="small">
                {{ statusLabel(row.status) }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="source_ip" label="来源IP" width="140" />
          <el-table-column label="时间" width="170">
            <template #default="{ row }">
              {{ formatTime(row.detected_at) }}
            </template>
          </el-table-column>
          <el-table-column label="操作" width="260" fixed="right">
            <template #default="{ row }">
              <el-button size="small" type="primary" @click="showDetail(row)">
                <el-icon><View /></el-icon>详情
              </el-button>
              <el-button size="small" type="warning" @click="changeStatus(row)">
                <el-icon><Edit /></el-icon>状态
              </el-button>
              <el-button size="small" type="danger" @click="generateDMCA(row)" :disabled="row.status === 'false_positive'">
                <el-icon><Document /></el-icon>DMCA
              </el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-tab-pane>

      <el-tab-pane label="版权方统计" name="holders">
        <el-table :data="holderStats" stripe style="width: 100%" v-loading="loading">
          <el-table-column prop="copyright_holder" label="版权方" width="250" />
          <el-table-column prop="total_infringements" label="侵权总数" width="120" sortable />
          <el-table-column prop="unique_resources" label="唯一资源" width="120" />
          <el-table-column prop="confirmed" label="已确认" width="120" />
          <el-table-column prop="dmca_sent" label="已发DMCA" width="120" />
          <el-table-column label="最近检测" width="180">
            <template #default="{ row }">
              {{ formatTime(row.last_detected) }}
            </template>
          </el-table-column>
          <el-table-column label="操作" width="180">
            <template #default="{ row }">
              <el-button size="small" type="danger" @click="batchDMCA(row.copyright_holder)">
                <el-icon><Document /></el-icon>批量DMCA
              </el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-tab-pane>

      <el-tab-pane label="指纹库管理" name="fingerprints">
        <el-card class="filter-card" shadow="never">
          <el-row :gutter="20">
            <el-col :span="8">
              <div class="fp-stat">
                <el-icon :size="32"><Key /></el-icon>
                <div>
                  <div class="big-number">{{ stats.fingerprint_count || 0 }}</div>
                  <div class="stat-label">指纹总数</div>
                </div>
              </div>
            </el-col>
            <el-col :span="8">
              <div class="fp-stat">
                <el-icon :size="32"><User /></el-icon>
                <div>
                  <div class="big-number">{{ stats.copyright_holders || 0 }}</div>
                  <div class="stat-label">版权方数量</div>
                </div>
              </div>
            </el-col>
            <el-col :span="8">
              <div class="fp-stat">
                <el-icon :size="32"><DataAnalysis /></el-icon>
                <div>
                  <div class="big-number">{{ (stats.similarity_threshold * 100).toFixed(0) }}%</div>
                  <div class="stat-label">判定阈值</div>
                </div>
              </div>
            </el-col>
          </el-row>
        </el-card>

        <el-table :data="fingerprints" stripe style="width: 100%" v-loading="fpLoading">
          <el-table-column prop="id" label="ID" width="80" />
          <el-table-column prop="title" label="作品名" width="250" />
          <el-table-column prop="copyright_holder" label="版权方" width="200" />
          <el-table-column prop="filename" label="文件名" width="200">
            <template #default="{ row }">
              <el-tooltip :content="row.filename" placement="top">
                <span class="truncate-text">{{ row.filename }}</span>
              </el-tooltip>
            </template>
          </el-table-column>
          <el-table-column label="文件大小" width="130">
            <template #default="{ row }">
              {{ formatSize(row.file_size || 0) }}
            </template>
          </el-table-column>
          <el-table-column prop="phash" label="感知哈希" width="200">
            <template #default="{ row }">
              <span class="mono-text">{{ row.phash ? row.phash.substring(0, 24) + '...' : '-' }}</span>
            </template>
          </el-table-column>
          <el-table-column label="添加时间" width="170">
            <template #default="{ row }">
              {{ formatTime(row.added_at) }}
            </template>
          </el-table-column>
        </el-table>

        <el-card class="import-card" shadow="never">
          <div class="import-header">
            <span>导入版权指纹库 (JSON格式)</span>
          </div>
          <el-input
            v-model="importJson"
            type="textarea"
            :rows="6"
            placeholder='粘贴 JSON 指纹数组，如 [{"phash": "...", "title": "...", "copyright_holder": "..."}]'
          />
          <div class="import-actions">
            <el-button type="primary" @click="importFingerprints" :loading="importing">
              <el-icon><Upload /></el-icon>导入指纹
            </el-button>
          </div>
        </el-card>
      </el-tab-pane>

      <el-tab-pane label="DMCA通知" name="dmca">
        <el-table :data="dmcaNotices" stripe style="width: 100%" v-loading="dmcaLoading">
          <el-table-column prop="id" label="ID" width="80" />
          <el-table-column prop="subject" label="主题" width="300">
            <template #default="{ row }">
              <el-tooltip :content="row.subject" placement="top">
                <span class="truncate-text">{{ row.subject }}</span>
              </el-tooltip>
            </template>
          </el-table-column>
          <el-table-column prop="recipient_email" label="收件人" width="200" />
          <el-table-column prop="infohash" label="Infohash" width="200">
            <template #default="{ row }">
              <span class="mono-text">{{ row.infohash ? row.infohash.substring(0, 16) + '...' : '-' }}</span>
            </template>
          </el-table-column>
          <el-table-column prop="status" label="状态" width="100">
            <template #default="{ row }">
              <el-tag :type="row.status === 'sent' ? 'success' : 'info'" size="small">
                {{ row.status === 'sent' ? '已发送' : '已生成' }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column label="生成时间" width="170">
            <template #default="{ row }">
              {{ formatTime(row.generated_at) }}
            </template>
          </el-table-column>
          <el-table-column label="操作" width="150" fixed="right">
            <template #default="{ row }">
              <el-button size="small" @click="showDMCA(row)">
                <el-icon><View /></el-icon>查看
              </el-button>
              <el-button size="small" type="success" @click="markSent(row)" :disabled="row.status === 'sent'">
                <el-icon><Check /></el-icon>标记已发
              </el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-tab-pane>
    </el-tabs>

    <el-dialog v-model="detailVisible" title="侵权详情" width="700px">
      <div v-if="currentRecord" class="detail-content">
        <el-descriptions :column="2" border>
          <el-descriptions-item label="记录ID">{{ currentRecord.id }}</el-descriptions-item>
          <el-descriptions-item label="相似度">
            <el-tag :type="currentRecord.similarity >= 0.98 ? 'danger' : 'warning'">
              {{ (currentRecord.similarity * 100).toFixed(1) }}%
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="匹配作品">{{ currentRecord.matched_title }}</el-descriptions-item>
          <el-descriptions-item label="版权方">{{ currentRecord.copyright_holder }}</el-descriptions-item>
          <el-descriptions-item label="Infohash" :span="2">
            <span class="mono-text">{{ currentRecord.infohash }}</span>
          </el-descriptions-item>
          <el-descriptions-item label="文件名" :span="2">{{ currentRecord.file_name }}</el-descriptions-item>
          <el-descriptions-item label="文件大小">{{ formatSize(currentRecord.file_size || 0) }}</el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="statusType(currentRecord.status)">
              {{ statusLabel(currentRecord.status) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="来源IP">{{ currentRecord.source_ip || '未知' }}</el-descriptions-item>
          <el-descriptions-item label="检测时间">{{ formatTime(currentRecord.detected_at) }}</el-descriptions-item>
          <el-descriptions-item label="感知哈希" :span="2">
            <span class="mono-text">{{ currentRecord.phash }}</span>
          </el-descriptions-item>
        </el-descriptions>
        <div v-if="currentRecord.evidence" class="evidence-section">
          <h4>证据数据</h4>
          <pre class="evidence-json">{{ JSON.stringify(currentRecord.evidence, null, 2) }}</pre>
        </div>
      </div>
    </el-dialog>

    <el-dialog v-model="dmcaVisible" title="DMCA下架通知" width="700px">
      <div v-if="currentDMCA" class="dmca-content">
        <el-input type="textarea" :rows="20" v-model="currentDMCA.notice_text" readonly />
      </div>
      <template #footer>
        <el-button @click="dmcaVisible = false">关闭</el-button>
        <el-button type="primary" @click="copyDMCA">
          <el-icon><CopyDocument /></el-icon>复制文本
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import api from '../api'

const activeTab = ref('monitor')
const loading = ref(false)
const fpLoading = ref(false)
const dmcaLoading = ref(false)
const importing = ref(false)

const stats = ref({})
const infringements = ref([])
const holderStats = ref([])
const fingerprints = ref([])
const dmcaNotices = ref([])
const holders = ref([])

const filter = ref({ status: '', holder: '' })
const importJson = ref('')

const detailVisible = ref(false)
const currentRecord = ref(null)
const dmcaVisible = ref(false)
const currentDMCA = ref(null)

const adminToken = ref(localStorage.getItem('admin_token') || '')

const formatSize = (bytes) => {
  if (!bytes) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let i = 0
  let size = bytes
  while (size >= 1024 && i < units.length - 1) { size /= 1024; i++ }
  return `${size.toFixed(2)} ${units[i]}`
}

const formatTime = (ts) => {
  if (!ts) return '-'
  const d = new Date(ts * 1000)
  return d.toLocaleString('zh-CN')
}

const statusType = (s) => ({
  suspected: 'warning',
  confirmed: 'danger',
  false_positive: 'info',
  resolved: 'success'
}[s] || '')

const statusLabel = (s) => ({
  suspected: '疑似',
  confirmed: '已确认',
  false_positive: '误报',
  resolved: '已解决'
}[s] || s)

const loadStats = async () => {
  try {
    const res = await api.get('/copyright/stats', {
      headers: { 'X-Admin-Token': adminToken.value }
    })
    stats.value = res.data
    holders.value = (res.data.by_holder || []).map(h => h.copyright_holder)
  } catch (e) {
    if (e.response?.status === 403) {
      ElMessage.warning('需要管理员权限访问版权管理')
    }
  }
}

const loadInfringements = async () => {
  loading.value = true
  try {
    const params = {}
    if (filter.value.status) params.status = filter.value.status
    if (filter.value.holder) params.holder = filter.value.holder
    const res = await api.get('/copyright/infringements', {
      params,
      headers: { 'X-Admin-Token': adminToken.value }
    })
    infringements.value = res.data.records || []
  } catch (e) {
    if (e.response?.status === 403) {
      ElMessage.warning('需要管理员权限')
    }
  } finally {
    loading.value = false
  }
}

const loadHolderStats = async () => {
  try {
    const res = await api.get('/copyright/stats', {
      headers: { 'X-Admin-Token': adminToken.value }
    })
    holderStats.value = res.data.by_holder || []
  } catch (e) {}
}

const loadFingerprints = async () => {
  fpLoading.value = true
  try {
    const res = await api.get('/copyright/fingerprints', {
      headers: { 'X-Admin-Token': adminToken.value }
    })
    fingerprints.value = res.data.fingerprints || []
  } catch (e) {} finally {
    fpLoading.value = false
  }
}

const loadDMCA = async () => {
  dmcaLoading.value = true
  try {
    const res = await api.get('/copyright/dmca', {
      headers: { 'X-Admin-Token': adminToken.value }
    })
    dmcaNotices.value = res.data.notices || []
  } catch (e) {} finally {
    dmcaLoading.value = false
  }
}

const showDetail = (row) => {
  currentRecord.value = row
  detailVisible.value = true
}

const changeStatus = async (row) => {
  try {
    const { value } = await ElMessageBox.prompt(
      '请选择新状态',
      '更新侵权状态',
      {
        inputPattern: /^(confirmed|false_positive|suspected|resolved)$/,
        inputErrorMessage: '状态必须是: confirmed, false_positive, suspected, resolved',
        inputValue: row.status
      }
    )
    await api.put(`/copyright/infringements/${row.id}/status`,
      { status: value },
      { headers: { 'X-Admin-Token': adminToken.value } }
    )
    ElMessage.success('状态已更新')
    loadInfringements()
    loadHolderStats()
    loadStats()
  } catch (e) {}
}

const generateDMCA = async (row) => {
  try {
    const { value: email } = await ElMessageBox.prompt(
      '请输入收件人邮箱',
      '生成DMCA通知',
      { inputValue: 'abuse@example.com' }
    )
    const res = await api.post('/copyright/dmca/generate',
      {
        record_id: row.id,
        recipient_email: email
      },
      { headers: { 'X-Admin-Token': adminToken.value } }
    )
    currentDMCA.value = res.data
    dmcaVisible.value = true
    loadDMCA()
    loadStats()
  } catch (e) {}
}

const batchDMCA = async (holder) => {
  try {
    await ElMessageBox.confirm(
      `确认为版权方 "${holder}" 的所有侵权记录批量生成 DMCA 通知？`,
      '批量生成DMCA',
      { type: 'warning' }
    )
    const res = await api.post('/copyright/dmca/batch',
      { copyright_holder: holder },
      { headers: { 'X-Admin-Token': adminToken.value } }
    )
    ElMessage.success(`已生成 ${res.data.total_notices} 份 DMCA 通知`)
    loadDMCA()
    loadStats()
  } catch (e) {}
}

const showDMCA = (row) => {
  currentDMCA.value = row
  dmcaVisible.value = true
}

const markSent = async (row) => {
  try {
    await api.post(`/copyright/dmca/${row.id}/send`, {},
      { headers: { 'X-Admin-Token': adminToken.value } }
    )
    ElMessage.success('已标记为已发送')
    loadDMCA()
  } catch (e) {}
}

const copyDMCA = async () => {
  if (currentDMCA.value?.notice_text) {
    await navigator.clipboard.writeText(currentDMCA.value.notice_text)
    ElMessage.success('已复制到剪贴板')
  }
}

const importFingerprints = async () => {
  if (!importJson.value.trim()) {
    ElMessage.warning('请输入指纹 JSON 数据')
    return
  }
  importing.value = true
  try {
    const fps = JSON.parse(importJson.value)
    if (!Array.isArray(fps)) {
      ElMessage.error('JSON 数据必须是数组')
      return
    }
    const res = await api.post('/copyright/fingerprints/import',
      { fingerprints: fps },
      { headers: { 'X-Admin-Token': adminToken.value } }
    )
    ElMessage.success(`成功导入 ${res.data.imported} 个指纹，当前共 ${res.data.total} 个`)
    importJson.value = ''
    loadStats()
    loadFingerprints()
  } catch (e) {
    ElMessage.error('导入失败: ' + (e.response?.data?.error || e.message))
  } finally {
    importing.value = false
  }
}

onMounted(() => {
  loadStats()
  loadInfringements()
  loadHolderStats()
  loadFingerprints()
  loadDMCA()
})
</script>

<style scoped>
.copyright-panel { padding: 10px; }

.copyright-stats { margin-bottom: 20px; }

.stat-card { display: flex; align-items: center; padding: 0; }
.stat-card :deep(.el-card__body) {
  display: flex; align-items: center; padding: 15px 20px; width: 100%;
}
.stat-icon {
  width: 48px; height: 48px; border-radius: 10px;
  display: flex; align-items: center; justify-content: center;
  color: white; margin-right: 15px;
}
.stat-info .stat-value { font-size: 22px; font-weight: bold; color: #303133; }
.stat-info .stat-label { font-size: 12px; color: #909399; margin-top: 2px; }

.filter-card { margin-bottom: 15px; }

.truncate-text {
  display: inline-block; max-width: 230px; overflow: hidden;
  text-overflow: ellipsis; white-space: nowrap; vertical-align: middle;
}
.mono-text { font-family: 'Consolas', 'Monaco', monospace; font-size: 12px; }

.fp-stat {
  display: flex; align-items: center; gap: 15px;
  padding: 20px;
}
.fp-stat .el-icon { color: #409eff; }
.big-number { font-size: 28px; font-weight: bold; color: #303133; }

.import-card { margin-top: 20px; }
.import-header { font-weight: bold; margin-bottom: 10px; color: #303133; }
.import-actions { margin-top: 10px; }

.detail-content { max-height: 500px; overflow-y: auto; }
.evidence-section { margin-top: 20px; }
.evidence-section h4 { margin-bottom: 10px; color: #606266; }
.evidence-json {
  background: #f5f7fa; padding: 15px; border-radius: 6px;
  font-family: 'Consolas', 'Monaco', monospace; font-size: 12px;
  max-height: 300px; overflow-y: auto;
}

.dmca-content :deep(.el-textarea__inner) {
  font-family: 'Consolas', 'Monaco', monospace; font-size: 13px;
}
</style>
