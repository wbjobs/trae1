<template>
  <div>
    <el-row :gutter="20" style="margin-bottom:16px">
      <el-col :span="6">
        <el-select v-model="app" placeholder="选择应用" filterable allow-create
          style="width:100%" @change="onAppChange">
          <el-option v-for="a in apps" :key="a" :label="a" :value="a"/>
        </el-select>
      </el-col>
      <el-col :span="5">
        <el-select v-model="profile" placeholder="环境" style="width:100%" @change="onProfileChange">
          <el-option v-for="p in ['dev','staging','prod']" :key="p" :label="p" :value="p"/>
        </el-select>
      </el-col>
      <el-col :span="10">
        <el-select v-model="key" placeholder="选择配置键 (可新建)" filterable allow-create
          style="width:100%" @change="onKeyChange">
          <el-option v-for="k in keys" :key="k" :label="k" :value="k"/>
        </el-select>
      </el-col>
      <el-col :span="3">
        <el-button type="primary" @click="openGrayDialog">灰度发布</el-button>
      </el-col>
    </el-row>

    <el-row :gutter="20">
      <el-col :span="12">
        <el-card shadow="never">
          <template #header>
            <div style="display:flex;justify-content:space-between;align-items:center">
              <span>灰度批次状态</span>
              <el-tag v-if="activeBatch" type="warning">ACTIVE</el-tag>
              <el-tag v-else type="info">IDLE</el-tag>
            </div>
          </template>
          <div v-if="activeBatch" style="font-size:14px">
            <el-descriptions :column="2" border size="small">
              <el-descriptions-item label="批次ID">{{ activeBatch.batchId }}</el-descriptions-item>
              <el-descriptions-item label="目标版本">v{{ activeBatch.targetVersion }}</el-descriptions-item>
              <el-descriptions-item label="灰度比例">{{ activeBatch.percent }}%</el-descriptions-item>
              <el-descriptions-item label="观察窗口">{{ activeBatch.observeWindowSec }}s</el-descriptions-item>
              <el-descriptions-item label="发起者">{{ activeBatch.operator }}</el-descriptions-item>
              <el-descriptions-item label="启动时间">{{ formatTs(activeBatch.createdAt) }}</el-descriptions-item>
              <el-descriptions-item label="错误阈值">{{ activeBatch.errorThresholdPct }}%</el-descriptions-item>
              <el-descriptions-item label="差异">{{ activeBatch.diff }}</el-descriptions-item>
            </el-descriptions>
            <el-progress :percentage="grayProgress" :stroke-width="10" style="margin-top:16px"
              :status="errorOverThreshold ? 'exception' : 'success'"/>
            <div style="margin-top:16px;display:flex;gap:8px;justify-content:flex-end">
              <el-button type="success" :loading="acting" @click="promote">立即全量</el-button>
              <el-button type="danger" :loading="acting" @click="cancel">取消灰度</el-button>
            </div>
          </div>
          <el-empty v-else description="当前无活跃灰度批次，点击右上角「灰度发布」启动"/>
        </el-card>
      </el-col>

      <el-col :span="12">
        <el-card shadow="never">
          <template #header>
            <div style="display:flex;justify-content:space-between">
              <span>实时指标</span>
              <el-button size="small" @click="refreshStatus">刷新</el-button>
            </div>
          </template>
          <el-row :gutter="12" v-if="status">
            <el-col :span="8">
              <el-statistic title="总实例" :value="status.totalClients"/>
            </el-col>
            <el-col :span="8">
              <el-statistic title="灰度实例" :value="status.grayClients"/>
            </el-col>
            <el-col :span="8">
              <el-statistic title="已升级">
                <template #default>
                  <span :style="{color: status.upgradedClients === status.grayClients ? '#67C23A' : '#E6A23C'}">
                    {{ status.upgradedClients }} / {{ status.grayClients }}
                  </span>
                </template>
              </el-statistic>
            </el-col>
            <el-col :span="8">
              <el-statistic title="健康" :value="status.healthyClients"/>
            </el-col>
            <el-col :span="8">
              <el-statistic title="异常">
                <template #default>
                  <span :style="{color: status.errorClients > 0 ? '#F56C6C' : ''}">
                    {{ status.errorClients }}
                  </span>
                </template>
              </el-statistic>
            </el-col>
            <el-col :span="8">
              <el-statistic title="异常率" :precision="2" :value="status.errorRatePct" suffix="%"/>
            </el-col>
            <el-col :span="24" style="margin-top:8px">
              <el-progress :percentage="Math.min(100, status.errorRatePct)"
                :color="status.errorRatePct >= (activeBatch?.errorThresholdPct || 20) ? '#F56C6C' : '#67C23A'"
                :stroke-width="8"/>
            </el-col>
            <el-col :span="24">
              <el-progress :percentage="observeProgress" :stroke-width="8"
                status="success" :format="() => `观察剩余 ${status.remainingSec}s`"/>
            </el-col>
          </el-row>
          <el-empty v-else description="暂无指标"/>
        </el-card>
      </el-col>
    </el-row>

    <el-card shadow="never" style="margin-top:16px">
      <template #header>
        <div style="display:flex;justify-content:space-between">
          <span>客户端列表 ({{ clients.length }})</span>
          <el-button size="small" @click="loadClients">刷新</el-button>
        </div>
      </template>
      <el-table :data="clients" size="small" stripe max-height="400">
        <el-table-column label="实例ID" prop="instanceId" width="120" show-overflow-tooltip/>
        <el-table-column label="IP" prop="clientIp" width="140"/>
        <el-table-column label="应用" prop="application" width="140"/>
        <el-table-column label="环境" prop="profile" width="90"/>
        <el-table-column label="版本" width="90">
          <template #default="scope">v{{ scope.row.version }}</template>
        </el-table-column>
        <el-table-column label="状态" width="90">
          <template #default="scope">
            <el-tag size="small" :type="scope.row.healthy ? 'success' : 'danger'">
              {{ scope.row.healthy ? '正常' : '异常' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="灰度" width="80">
          <template #default="scope">
            <el-tag v-if="scope.row.grayscale" size="small" type="warning">GRAY</el-tag>
            <span v-else style="color:#909399">-</span>
          </template>
        </el-table-column>
        <el-table-column label="错误信息" prop="errorMessage" show-overflow-tooltip/>
        <el-table-column label="最后心跳" width="180">
          <template #default="scope">{{ formatTs(scope.row.lastSeenAt) }}</template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog v-model="grayDialogVisible" title="启动灰度发布" width="700px">
      <el-form :model="grayForm" label-width="100px">
        <el-form-item label="应用">
          <el-input v-model="grayForm.application"/>
        </el-form-item>
        <el-form-item label="环境">
          <el-select v-model="grayForm.profile" style="width:100%">
            <el-option v-for="p in ['dev','staging','prod']" :key="p" :label="p" :value="p"/>
          </el-select>
        </el-form-item>
        <el-form-item label="配置键">
          <el-input v-model="grayForm.key"/>
        </el-form-item>
        <el-form-item label="灰度比例(%)">
          <el-input-number v-model="grayForm.percent" :min="1" :max="99"/>
        </el-form-item>
        <el-form-item label="观察窗口(秒)">
          <el-input-number v-model="grayForm.observeWindowSec" :min="30" :max="86400"/>
        </el-form-item>
        <el-form-item label="错误阈值(%)">
          <el-input-number v-model="grayForm.errorThresholdPct" :min="1" :max="100"/>
        </el-form-item>
        <el-form-item label="操作人">
          <el-input v-model="grayForm.operator"/>
        </el-form-item>
        <el-form-item label="YAML 内容">
          <el-input v-model="grayForm.content" type="textarea" :rows="10"
            placeholder="YAML 配置内容..." style="font-family:monospace"/>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="grayDialogVisible=false">取消</el-button>
        <el-button type="primary" :loading="submitting" @click="submitGray">启动灰度</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch as vueWatch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { appsApi, configApi, grayApi } from '../api'

const apps = ref([])
const app = ref('')
const profile = ref('dev')
const key = ref('')
const keys = ref([])
const activeBatch = ref(null)
const status = ref(null)
const clients = ref([])
const grayDialogVisible = ref(false)
const submitting = ref(false)
const acting = ref(false)
const pollTimer = ref(null)

const grayForm = ref({
  application: '', profile: 'dev', key: '',
  percent: 10, observeWindowSec: 900, errorThresholdPct: 20,
  operator: 'web', content: ''
})

const grayProgress = computed(() => {
  if (!status.value || !status.value.grayClients) return 0
  return Math.round(status.value.upgradedClients / status.value.grayClients * 100)
})
const observeProgress = computed(() => {
  if (!activeBatch.value || !status.value) return 0
  const total = activeBatch.value.observeWindowSec
  const remain = status.value.remainingSec
  if (remain >= total) return 0
  return Math.round((total - remain) / total * 100)
})
const errorOverThreshold = computed(() =>
  activeBatch.value && status.value
    ? status.value.errorRatePct >= activeBatch.value.errorThresholdPct
    : false)

function formatTs(t) { return t ? new Date(t).toLocaleString() : '-' }
function onAppChange() { keys.value=[]; key.value=''; loadKeys(); loadAll() }
function onProfileChange() { keys.value=[]; key.value=''; loadKeys(); loadAll() }
function onKeyChange() { loadAll() }

async function loadApps() {
  const r = await appsApi.list()
  apps.value = r.data?.data || []
  if (apps.value.length && !app.value) app.value = apps.value[0]
  if (app.value) loadKeys()
}
async function loadKeys() {
  if (!app.value) return
  const r = await configApi.listKeys(app.value, profile.value)
  keys.value = (r.data?.data || []).map(k => k.key)
  if (keys.value.length && !key.value) key.value = keys.value[0]
  loadAll()
}
async function loadActive() {
  if (!app.value) { activeBatch.value=null; return }
  const r = await grayApi.active(app.value, profile.value, key.value)
  activeBatch.value = r.data?.data || null
}
async function loadStatus() {
  if (!activeBatch.value) { status.value=null; return }
  const r = await grayApi.status(activeBatch.value.batchId)
  status.value = r.data?.data || null
}
async function loadClients() {
  if (!app.value) { clients.value=[]; return }
  const r = await grayApi.clients(app.value, profile.value)
  clients.value = r.data?.data || []
}
async function loadAll() {
  await loadActive()
  await loadStatus()
  await loadClients()
}
async function refreshStatus() { await loadStatus(); await loadClients() }

function openGrayDialog() {
  grayForm.value = {
    application: app.value || '', profile: profile.value, key: key.value || '',
    percent: 10, observeWindowSec: 900, errorThresholdPct: 20,
    operator: 'web', content: ''
  }
  grayDialogVisible.value = true
}
async function submitGray() {
  submitting.value = true
  try {
    const r = await grayApi.start(grayForm.value)
    if (r.data?.success) {
      ElMessage.success('灰度发布已启动')
      grayDialogVisible.value = false
      loadAll()
    } else ElMessage.error(r.data?.message)
  } catch (e) { ElMessage.error(e.response?.data?.message || e.message) }
  finally { submitting.value = false }
}
async function promote() {
  try {
    await ElMessageBox.confirm('立即全量推送到所有实例?', '确认', {type:'warning'})
    acting.value = true
    const r = await grayApi.promote(activeBatch.value.batchId)
    if (r.data?.success) { ElMessage.success('已全量推送'); loadAll() }
    else ElMessage.error(r.data?.message)
  } catch (e) { if (e !== 'cancel') ElMessage.error(e.message) }
  finally { acting.value = false }
}
async function cancel() {
  try {
    await ElMessageBox.confirm('取消灰度，灰度实例将回滚到旧版本?', '确认', {type:'warning'})
    acting.value = true
    const r = await grayApi.cancel(activeBatch.value.batchId)
    if (r.data?.success) { ElMessage.success('已取消'); loadAll() }
    else ElMessage.error(r.data?.message)
  } catch (e) { if (e !== 'cancel') ElMessage.error(e.message) }
  finally { acting.value = false }
}

onMounted(async () => {
  await loadApps()
  pollTimer.value = setInterval(refreshStatus, 5000)
})
</script>
