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
        <el-button type="primary" @click="openEditor">编辑 / 新建</el-button>
      </el-col>
    </el-row>

    <el-row :gutter="20">
      <el-col :span="14">
        <el-card shadow="never">
          <template #header>
            <div style="display:flex;justify-content:space-between">
              <span>当前内容
                <el-tag v-if="current" type="success" size="small" style="margin-left:8px">
                  v{{ current.version }} · {{ current.updatedBy }} · {{ formatTs(current.updatedAt) }}
                </el-tag>
              </span>
              <el-button size="small" @click="loadCurrent">刷新</el-button>
            </div>
          </template>
          <pre v-if="current" style="background:#f6f8fa;padding:12px;white-space:pre-wrap;border-radius:4px">{{ current.content }}</pre>
          <el-empty v-else description="暂无配置" />
        </el-card>
      </el-col>
      <el-col :span="10">
        <el-card shadow="never">
          <template #header>
            <div style="display:flex;justify-content:space-between">
              <span>变更历史</span>
              <el-button size="small" @click="loadHistory">刷新</el-button>
            </div>
          </template>
          <el-table :data="history" size="small" stripe>
            <el-table-column label="版本" prop="version" width="70"/>
            <el-table-column label="类型" width="90">
              <template #default="scope">
                <el-tag size="small" :type="tagType(scope.row.changeType)">
                  {{ scope.row.changeType }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column label="操作人" prop="operator" width="90"/>
            <el-table-column label="时间">
              <template #default="scope">{{ formatTs(scope.row.changedAt) }}</template>
            </el-table-column>
            <el-table-column label="操作" width="130">
              <template #default="scope">
                <el-button size="small" @click="showVersion(scope.row)">查看</el-button>
                <el-button size="small" type="warning"
                  :disabled="scope.row.version === (current ? current.version : -1)"
                  @click="doRollback(scope.row)">回滚</el-button>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>

    <el-dialog v-model="editorVisible" title="编辑 YAML 配置" width="700px">
      <el-form :model="editor" label-width="80px">
        <el-form-item label="应用">
          <el-input v-model="editor.application"/>
        </el-form-item>
        <el-form-item label="环境">
          <el-select v-model="editor.profile" style="width:100%">
            <el-option v-for="p in ['dev','staging','prod']" :key="p" :label="p" :value="p"/>
          </el-select>
        </el-form-item>
        <el-form-item label="配置键">
          <el-input v-model="editor.key"/>
        </el-form-item>
        <el-form-item label="操作人">
          <el-input v-model="editor.operator"/>
        </el-form-item>
        <el-form-item label="YAML">
          <el-input v-model="editor.content" type="textarea" :rows="16"
            placeholder="YAML 配置内容..." style="font-family:monospace"/>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="editorVisible=false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveConfig">保存</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="historyDetailVisible" title="历史版本详情" width="700px">
      <div v-if="historyDetail">
        <el-descriptions :column="2" border size="small">
          <el-descriptions-item label="版本">{{ historyDetail.version }}</el-descriptions-item>
          <el-descriptions-item label="类型">{{ historyDetail.changeType }}</el-descriptions-item>
          <el-descriptions-item label="操作人">{{ historyDetail.operator }}</el-descriptions-item>
          <el-descriptions-item label="时间">{{ formatTs(historyDetail.changedAt) }}</el-descriptions-item>
          <el-descriptions-item label="差异" :span="2">{{ historyDetail.diff }}</el-descriptions-item>
        </el-descriptions>
        <pre style="background:#f6f8fa;padding:12px;margin-top:12px;border-radius:4px">{{ historyDetail.content }}</pre>
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { appsApi, configApi } from '../api'

const apps = ref([])
const app = ref('')
const profile = ref('dev')
const key = ref('')
const keys = ref([])
const current = ref(null)
const history = ref([])
const editorVisible = ref(false)
const historyDetailVisible = ref(false)
const historyDetail = ref(null)
const saving = ref(false)

const editor = ref({
  application: '', profile: 'dev', key: '', operator: 'web', content: ''
})

function formatTs(t) {
  if (!t) return '-'
  return new Date(t).toLocaleString()
}

function tagType(t) {
  switch (t) {
    case 'CREATE': return 'success'
    case 'UPDATE': return ''
    case 'ROLLBACK': return 'warning'
    case 'DELETE': return 'danger'
    default: return 'info'
  }
}

function onAppChange() { keys.value = []; key.value = ''; loadKeys(); loadCurrent(); loadHistory() }
function onProfileChange() { keys.value = []; key.value = ''; loadKeys(); loadCurrent(); loadHistory() }
function onKeyChange() { loadCurrent(); loadHistory() }

async function loadApps() {
  const r = await appsApi.list()
  apps.value = r.data?.data || []
  if (apps.value.length && !app.value) app.value = apps.value[0]
  if (app.value) loadKeys()
}

async function loadKeys() {
  if (!app.value) { keys.value = []; return }
  const r = await configApi.listKeys(app.value, profile.value)
  keys.value = (r.data?.data || []).map(k => k.key)
  if (keys.value.length && !key.value) key.value = keys.value[0]
  loadCurrent(); loadHistory()
}

async function loadCurrent() {
  if (!app.value) { current.value = null; return }
  const r = await configApi.get(app.value, profile.value, key.value)
  current.value = r.data?.data || null
}

async function loadHistory() {
  if (!app.value) { history.value = []; return }
  const r = await configApi.history(app.value, profile.value, key.value, 50)
  history.value = r.data?.data || []
}

function openEditor() {
  editor.value = {
    application: app.value || '',
    profile: profile.value,
    key: key.value || '',
    operator: 'web',
    content: current.value?.content || ''
  }
  editorVisible.value = true
}

async function saveConfig() {
  if (!editor.value.application || !editor.value.profile) {
    ElMessage.warning('应用和环境必填')
    return
  }
  saving.value = true
  try {
    const r = await configApi.publish(editor.value)
    if (r.data?.success) {
      ElMessage.success('保存成功 v' + r.data.data.version)
      editorVisible.value = false
      if (!apps.value.includes(editor.value.application)) apps.value.push(editor.value.application)
      app.value = editor.value.application
      profile.value = editor.value.profile
      key.value = editor.value.key
      loadKeys()
    } else {
      ElMessage.error(r.data?.message || '保存失败')
    }
  } catch (e) {
    ElMessage.error(e.response?.data?.message || e.message)
  } finally {
    saving.value = false
  }
}

function showVersion(h) {
  historyDetail.value = h
  historyDetailVisible.value = true
}

async function doRollback(h) {
  try {
    await ElMessageBox.confirm(`确认回滚到 v${h.version}?`, '回滚确认', { type: 'warning' })
    const r = await configApi.rollback({
      application: app.value,
      profile: profile.value,
      key: key.value,
      targetVersion: h.version,
      operator: 'web'
    })
    if (r.data?.success) {
      ElMessage.success('已回滚，新版本 v' + r.data.data.version)
      loadCurrent(); loadHistory()
    } else {
      ElMessage.error(r.data?.message)
    }
  } catch (e) {
    if (e !== 'cancel') ElMessage.error(e.message)
  }
}

onMounted(loadApps)
</script>
