<template>
  <div class="docs-page">
    <el-container class="full-height">
      <el-aside width="300px" class="sidebar">
        <div class="sidebar-header">
          <div class="project-name">{{ project?.name || '文档' }}</div>
          <div class="header-btns">
            <el-dropdown trigger="click" @command="handleHeaderCmd">
              <el-button size="small" :icon="MoreFilled" circle />
              <template #dropdown>
                <el-dropdown-menu>
                  <el-dropdown-item command="import" :icon="Upload">批量导入</el-dropdown-item>
                  <el-dropdown-item command="export" :icon="Download" :disabled="selectedIds.length === 0">
                    导出选中 ({{ selectedIds.length }})
                  </el-dropdown-item>
                  <el-dropdown-item command="exportAll" :icon="Download">导出全部</el-dropdown-item>
                  <el-dropdown-item command="batchMode" :icon="Check">批量选择</el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>
            <el-button size="small" :icon="Plus" @click="addCategory">分类</el-button>
            <el-button size="small" type="primary" :icon="Plus" @click="openCreateDoc">新建</el-button>
          </div>
        </div>

        <div v-if="batchMode" class="batch-toolbar">
          <el-checkbox v-model="selectAll" :indeterminate="isIndeterminate" @change="toggleSelectAll">
            全选
          </el-checkbox>
          <el-button size="small" type="danger" :disabled="selectedIds.length === 0" @click="handleBatchDelete">
            删除 ({{ selectedIds.length }})
          </el-button>
          <el-button size="small" @click="batchMode = false">退出</el-button>
        </div>

        <el-input
          v-model="keyword"
          placeholder="搜索文档"
          :prefix-icon="Search"
          clearable
          size="small"
          style="margin: 8px 12px; width: calc(100% - 24px)"
          @input="filterList"
        />
        <div class="tree-wrapper">
          <el-tree
            :data="treeData"
            :props="{ label: 'title', children: 'children' }"
            node-key="id"
            default-expand-all
            highlight-current
            @node-click="handleNodeClick"
          >
            <template #default="{ node, data }">
              <span class="tree-node">
                <el-checkbox
                  v-if="batchMode && data.type === 'doc'"
                  :model-value="selectedIds.includes(data.id)"
                  @change="(v: boolean) => toggleSelect(data.id, v)"
                  @click.stop
                />
                <el-icon v-else-if="data.type === 'category'"><Folder /></el-icon>
                <span v-else class="tag-method" :class="methodColor(data.method)" style="font-size: 10px; padding: 1px 4px; min-width: 40px">
                  {{ data.method || 'GET' }}
                </span>
                <span class="node-label">{{ data.title }}</span>
              </span>
            </template>
          </el-tree>
        </div>
      </el-aside>

      <el-main class="main-area">
        <template v-if="currentDoc">
          <div class="doc-header">
            <div>
              <h2 class="doc-title">
                <span class="tag-method" :class="methodColor(currentDoc.method)">
                  {{ currentDoc.method || 'GET' }}
                </span>
                {{ currentDoc.title }}
              </h2>
              <div class="doc-path">{{ currentDoc.path || '' }}</div>
            </div>
            <div>
              <el-button :icon="ChatDotRound" @click="activeTab = 'comments'">批注 ({{ commentCount }})</el-button>
              <el-button :icon="Connection" @click="gotoDebug">在线调试</el-button>
              <el-button :icon="Refresh" @click="gotoVersion">版本历史</el-button>
              <el-button type="primary" :icon="Edit" @click="gotoEdit">编辑</el-button>
            </div>
          </div>
          <el-tabs v-model="activeTab">
            <el-tab-pane label="接口信息" name="info">
              <div class="doc-content" v-html="renderedContent"></div>
            </el-tab-pane>
            <el-tab-pane label="请求参数" name="params">
              <ParamsTable :params="currentDoc.requestParams" :body="currentDoc.requestBody" />
            </el-tab-pane>
            <el-tab-pane label="响应示例" name="response">
              <JsonViewer :data="currentDoc.response" />
            </el-tab-pane>
            <el-tab-pane :label="`批注 (${commentCount})`" name="comments">
              <DocComments :doc-id="currentDoc.id" @count-change="commentCount = $event" />
            </el-tab-pane>
            <el-tab-pane label="编辑历史" name="history">
              <VersionList :doc-id="currentDoc.id" @rollback="handleRollback" />
            </el-tab-pane>
          </el-tabs>
        </template>
        <el-empty v-else description="请选择左侧文档查看详情" />
      </el-main>
    </el-container>

    <el-dialog v-model="createDialogVisible" title="新建文档" width="500px">
      <el-form ref="formRef" :model="docForm" :rules="docRules" label-width="90px">
        <el-form-item label="文档标题" prop="title">
          <el-input v-model="docForm.title" />
        </el-form-item>
        <el-form-item label="请求方法" prop="method">
          <el-select v-model="docForm.method" style="width: 100%">
            <el-option v-for="m in ['GET', 'POST', 'PUT', 'DELETE', 'PATCH']" :key="m" :label="m" :value="m" />
          </el-select>
        </el-form-item>
        <el-form-item label="请求路径" prop="path">
          <el-input v-model="docForm.path" placeholder="/api/xxx" />
        </el-form-item>
        <el-form-item label="所属分类">
          <el-input v-model="docForm.category" placeholder="默认分类" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="createDialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submitDoc">确定</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="importDialogVisible" title="批量导入文档" width="600px">
      <el-tabs v-model="importTab">
        <el-tab-pane label="上传文件" name="file">
          <el-upload
            class="import-uploader"
            drag
            :auto-upload="false"
            :on-change="handleFileChange"
            :show-file-list="false"
            accept=".json,.yaml,.yml,.md"
          >
            <el-icon class="el-icon--upload"><UploadFilled /></el-icon>
            <div class="el-upload__text">拖拽 JSON / YAML / Markdown 文件到此处</div>
          </el-upload>
          <div v-if="importPreview" class="import-preview">
            <el-divider>预览 ({{ importPreview.length }} 条)</el-divider>
            <div class="preview-list">
              <div v-for="(item, i) in importPreview.slice(0, 5)" :key="i" class="preview-item">
                <span class="tag-method" :class="methodColor(item.method)">{{ item.method }}</span>
                <span>{{ item.title }} {{ item.path }}</span>
              </div>
              <el-divider v-if="importPreview.length > 5">...共 {{ importPreview.length }} 条</el-divider>
            </div>
          </div>
        </el-tab-pane>
        <el-tab-pane label="粘贴内容" name="paste">
          <el-input
            v-model="importPasteContent"
            type="textarea"
            :rows="10"
            placeholder="粘贴 JSON 格式文档数据..."
          />
          <el-select v-model="importFormat" style="width: 140px; margin-top: 8px">
            <el-option label="JSON" value="json" />
            <el-option label="YAML" value="yaml" />
          </el-select>
        </el-tab-pane>
      </el-tabs>
      <template #footer>
        <el-button @click="importDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="importing" @click="doImport">
          开始导入 ({{ importPreview.length || '预览中' }})
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed, reactive } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Plus, Search, Folder, Connection, Refresh, Edit, MoreFilled, Upload, Download, Check, ChatDotRound, UploadFilled } from '@element-plus/icons-vue'
import { marked } from 'marked'
import { projectApi } from '@/api/project'
import { docApi } from '@/api/doc'
import { methodColor } from '@/utils'
import ParamsTable from '@/components/ParamsTable.vue'
import JsonViewer from '@/components/JsonViewer.vue'
import VersionList from '@/components/VersionList.vue'
import DocComments from '@/components/DocComments.vue'

const route = useRoute()
const router = useRouter()
const projectId = computed(() => route.params.projectId)
const project = ref<any>(null)
const docs = ref<any[]>([])
const keyword = ref('')
const treeData = ref<any[]>([])
const currentDoc = ref<any>(null)
const activeTab = ref('info')
const commentCount = ref(0)
const batchMode = ref(false)
const selectedIds = ref<number[]>([])
const selectAll = ref(false)
const isIndeterminate = ref(false)

const createDialogVisible = ref(false)
const docForm = reactive({
  title: '',
  method: 'GET',
  path: '',
  category: '默认分类'
})
const docRules: FormRules = {
  title: [{ required: true, message: '请输入标题', trigger: 'blur' }],
  method: [{ required: true, message: '请选择方法', trigger: 'change' }]
}
const formRef = ref<FormInstance>()

const importDialogVisible = ref(false)
const importTab = ref('file')
const importPreview = ref<any[]>([])
const importPasteContent = ref('')
const importFormat = ref('json')
const importing = ref(false)
let importRaw: any = null

const renderedContent = computed(() => {
  if (!currentDoc.value) return ''
  const c = currentDoc.value.content || currentDoc.value.description || '暂无描述'
  try {
    return marked(c)
  } catch (e) {
    return c
  }
})

async function fetchProject() {
  try {
    const res = await projectApi.getDetail(projectId.value)
    project.value = res.data
  } catch (e) {}
}

async function fetchDocs() {
  try {
    const res = await docApi.getList(projectId.value)
    docs.value = res.data.list || res.data || []
    buildTree()
  } catch (e) {}
}

function buildTree() {
  const map: Record<string, any> = {}
  const roots: any[] = []
  docs.value.forEach((d) => {
    const cat = d.category || '默认分类'
    if (!map[cat]) {
      map[cat] = { id: `cat_${cat}`, title: cat, type: 'category', children: [] }
      roots.push(map[cat])
    }
    map[cat].children.push({ ...d, type: 'doc' })
  })
  treeData.value = roots
}

function filterList() {
  const kw = keyword.value.toLowerCase()
  const filtered = kw ? docs.value.filter((d) => d.title?.toLowerCase().includes(kw)) : docs.value
  const map: Record<string, any> = {}
  const roots: any[] = []
  filtered.forEach((d) => {
    const cat = d.category || '默认分类'
    if (!map[cat]) {
      map[cat] = { id: `cat_${cat}`, title: cat, type: 'category', children: [] }
      roots.push(map[cat])
    }
    map[cat].children.push({ ...d, type: 'doc' })
  })
  treeData.value = roots
}

function handleNodeClick(data: any) {
  if (data.type === 'doc') {
    currentDoc.value = data
    commentCount.value = 0
  }
}

function toggleSelect(id: number, checked: boolean) {
  if (checked) {
    if (!selectedIds.value.includes(id)) selectedIds.value.push(id)
  } else {
    selectedIds.value = selectedIds.value.filter((x) => x !== id)
  }
  updateSelectState()
}

function toggleSelectAll(val: boolean) {
  if (val) {
    selectedIds.value = docs.value.map((d) => d.id)
  } else {
    selectedIds.value = []
  }
  updateSelectState()
}

function updateSelectState() {
  const total = docs.value.length
  const selected = selectedIds.value.length
  selectAll.value = selected === total
  isIndeterminate.value = selected > 0 && selected < total
}

async function handleBatchDelete() {
  try {
    await ElMessageBox.confirm(`确定删除选中的 ${selectedIds.value.length} 个文档？`, '提示', { type: 'warning' })
    await docApi.batchDelete(selectedIds.value)
    ElMessage.success('批量删除成功')
    selectedIds.value = []
    batchMode.value = false
    fetchDocs()
  } catch (e) {}
}

function handleHeaderCmd(cmd: string) {
  if (cmd === 'import') {
    importPreview.value = []
    importPasteContent.value = ''
    importRaw = null
    importDialogVisible.value = true
  } else if (cmd === 'export') {
    doExport(selectedIds.value)
  } else if (cmd === 'exportAll') {
    doExport([])
  } else if (cmd === 'batchMode') {
    batchMode.value = !batchMode.value
    selectedIds.value = []
  }
}

async function handleFileChange(file: any) {
  const f = file.raw
  if (!f) return
  const text = await f.text()
  parseImportContent(text, f.name.endsWith('.yaml') || f.name.endsWith('.yml') ? 'yaml' : 'json')
}

function parseImportContent(text: string, format: string) {
  try {
    let data: any
    if (format === 'yaml') {
      data = parseSimpleYaml(text)
    } else {
      data = JSON.parse(text)
    }
    importRaw = Array.isArray(data) ? data : (data.docs || [])
    importPreview.value = importRaw
  } catch (e: any) {
    ElMessage.error('解析失败: ' + e.message)
    importPreview.value = []
  }
}

function parseSimpleYaml(text: string): any[] {
  const result: any[] = []
  const lines = text.split('\n')
  let current: any = null
  for (const line of lines) {
    if (line.startsWith('  - ')) {
      if (current) result.push(current)
      current = {}
      const content = line.slice(4)
      const match = content.match(/^(\w+):\s*"?(.*?)"?\s*$/)
      if (match) current[match[1]] = match[2]
    } else if (current && line.startsWith('    ')) {
      const content = line.trim()
      const match = content.match(/^(\w+):\s*"?(.*?)"?\s*$/)
      if (match) current[match[1]] = match[2]
    }
  }
  if (current) result.push(current)
  return result
}

async function doImport() {
  if (importTab.value === 'paste') {
    parseImportContent(importPasteContent.value, importFormat.value)
  }
  if (!importPreview.value.length) {
    ElMessage.warning('没有可导入的数据')
    return
  }
  importing.value = true
  try {
    const res = await docApi.batchImport(projectId.value, { docs: importPreview.value })
    ElMessage.success(`导入成功: 新建${res.data.imported}个, 更新${res.data.updated}个`)
    importDialogVisible.value = false
    fetchDocs()
  } catch (e) {
    ElMessage.error('导入失败')
  } finally {
    importing.value = false
  }
}

async function doExport(ids: number[]) {
  try {
    const { value: format } = await ElMessageBox.prompt('选择导出格式', '导出文档', {
      inputPattern: /(json|yaml|markdown)/,
      inputPlaceholder: 'json / yaml / markdown',
      inputValue: 'json'
    })
    const url = docApi.buildExportUrl(projectId.value, format, ids)
    window.open(url, '_blank')
  } catch (e) {}
}

function openCreateDoc() {
  Object.assign(docForm, { title: '', method: 'GET', path: '', category: '默认分类' })
  createDialogVisible.value = true
}

async function submitDoc() {
  const valid = await formRef.value?.validate().catch(() => false)
  if (!valid) return
  try {
    await docApi.create({ projectId: projectId.value, ...docForm })
    ElMessage.success('创建成功')
    createDialogVisible.value = false
    fetchDocs()
  } catch (e) {}
}

function addCategory() {
  ElMessage.info('请在新建文档时填写分类名')
}

function gotoEdit() {
  router.push(`/docs/${currentDoc.value.id}/edit`)
}
function gotoDebug() {
  router.push(`/docs/${currentDoc.value.id}/debug`)
}
function gotoVersion() {
  router.push(`/docs/${currentDoc.value.id}/version`)
}
function handleRollback() {
  fetchDocs()
}

onMounted(() => {
  fetchProject()
  fetchDocs()
})
</script>

<style scoped>
.docs-page,
.full-height {
  height: 100%;
}
.sidebar {
  background: #fff;
  border-right: 1px solid #ebeef5;
  display: flex;
  flex-direction: column;
}
.sidebar-header {
  padding: 16px;
  border-bottom: 1px solid #ebeef5;
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.project-name {
  font-weight: 600;
  font-size: 16px;
}
.header-btns {
  display: flex;
  gap: 4px;
}
.batch-toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  background: #ecf5ff;
  border-bottom: 1px solid #ebeef5;
  font-size: 13px;
}
.tree-wrapper {
  flex: 1;
  overflow-y: auto;
  padding: 4px 12px;
}
.tree-node {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
}
.node-label {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.main-area {
  background: #fff;
  padding: 24px 32px;
}
.doc-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 20px;
  padding-bottom: 16px;
  border-bottom: 1px solid #ebeef5;
}
.doc-title {
  font-size: 22px;
  margin: 0;
  display: flex;
  align-items: center;
  gap: 10px;
}
.doc-path {
  margin-top: 6px;
  color: #909399;
  font-family: Menlo, Monaco, Consolas, monospace;
}
.doc-content {
  line-height: 1.8;
  color: #303133;
}
.doc-content :deep(h1),
.doc-content :deep(h2),
.doc-content :deep(h3) {
  margin: 16px 0 8px 0;
}
.doc-content :deep(pre) {
  background: #f5f7fa;
  padding: 12px;
  border-radius: 4px;
  overflow-x: auto;
}
.doc-content :deep(code) {
  background: #f5f7fa;
  padding: 2px 4px;
  border-radius: 3px;
  font-size: 12px;
}
.doc-content :deep(pre code) {
  background: transparent;
  padding: 0;
}
.import-uploader {
  width: 100%;
}
.import-preview {
  margin-top: 12px;
}
.preview-list {
  max-height: 200px;
  overflow-y: auto;
}
.preview-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0;
  font-size: 13px;
}
</style>
