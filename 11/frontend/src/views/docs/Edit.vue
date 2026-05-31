<template>
  <div class="doc-edit-page">
    <div class="edit-header">
      <div class="flex" style="gap: 12px; align-items: center">
        <el-button :icon="ArrowLeft" @click="$router.back()">返回</el-button>
        <span class="page-title">编辑文档</span>
      </div>
      <div class="flex" style="gap: 8px">
        <el-button :icon="View" @click="showPreview = !showPreview">预览</el-button>
        <el-button :icon="Refresh" @click="gotoHistory">历史版本</el-button>
        <el-button type="primary" :icon="Check" @click="save(true)">保存并发布</el-button>
        <el-button :icon="DocumentAdd" @click="save(false)">保存为草稿</el-button>
      </div>
    </div>

    <el-row :gutter="16" class="edit-body">
      <el-col :span="showPreview ? 12 : 24">
        <el-card shadow="never" class="editor-card">
          <div class="form-row">
            <el-form :model="form" label-width="100px" inline>
              <el-form-item label="文档标题">
                <el-input v-model="form.title" placeholder="请输入文档标题" />
              </el-form-item>
              <el-form-item label="请求方法">
                <el-select v-model="form.method" style="width: 120px">
                  <el-option v-for="m in methods" :key="m" :label="m" :value="m" />
                </el-select>
              </el-form-item>
              <el-form-item label="请求路径">
                <el-input v-model="form.path" placeholder="/api/xxx" style="width: 240px" />
              </el-form-item>
              <el-form-item label="分类">
                <el-input v-model="form.category" style="width: 160px" placeholder="默认分类" />
              </el-form-item>
              <el-form-item label="标签">
                <el-input
                  v-model="tagsInput"
                  placeholder="回车添加标签"
                  style="width: 240px"
                  @keyup.enter="addTag"
                />
              </el-form-item>
            </el-form>
            <div class="tags">
              <el-tag
                v-for="(tag, i) in form.tags"
                :key="i"
                closable
                @close="form.tags.splice(i, 1)"
                style="margin-right: 6px"
              >
                {{ tag }}
              </el-tag>
            </div>
          </div>
          <el-tabs v-model="activeTab" class="editor-tabs">
            <el-tab-pane label="Markdown 描述" name="md">
              <div ref="mdEditorRef" class="md-editor"></div>
            </el-tab-pane>
            <el-tab-pane label="请求参数" name="params">
              <ParamsEditor
                v-model:params="form.requestParams"
                v-model:body="form.requestBody"
                v-model:contentType="form.contentType"
              />
            </el-tab-pane>
            <el-tab-pane label="响应示例" name="response">
              <JsonEditor v-model="form.response" title="响应 JSON 示例" />
            </el-tab-pane>
            <el-tab-pane label="请求示例" name="requestBody">
              <JsonEditor v-model="form.requestExample" title="请求体 JSON 示例" />
            </el-tab-pane>
          </el-tabs>
        </el-card>
      </el-col>
      <el-col :span="12" v-if="showPreview">
        <el-card shadow="never">
          <template #header>预览</template>
          <div class="preview">
            <h2>
              <span class="tag-method" :class="methodColor(form.method)">
                {{ form.method }}
              </span>
              {{ form.title }}
            </h2>
            <div class="preview-path">{{ form.path }}</div>
            <div class="doc-content" v-html="renderedContent"></div>
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, nextTick, computed, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { ArrowLeft, View, Refresh, Check, DocumentAdd } from '@element-plus/icons-vue'
import * as monaco from 'monaco-editor'
import { marked } from 'marked'
import { docApi } from '@/api/doc'
import { versionApi } from '@/api/version'
import { methodColor } from '@/utils'
import ParamsEditor from '@/components/ParamsEditor.vue'
import JsonEditor from '@/components/JsonEditor.vue'

const route = useRoute()
const router = useRouter()
const docId = computed(() => route.params.docId)

const methods = ['GET', 'POST', 'PUT', 'DELETE', 'PATCH']
const showPreview = ref(false)
const activeTab = ref('md')
const tagsInput = ref('')
const mdEditorRef = ref<HTMLElement>()
let editor: monaco.editor.IStandaloneCodeEditor | null = null

const form = reactive({
  id: null as any,
  title: '',
  method: 'GET',
  path: '',
  category: '默认分类',
  tags: [] as string[],
  content: '',
  requestParams: { query: [], path: [], header: [] },
  requestBody: { type: 'object', properties: {}, required: [] },
  contentType: 'application/json',
  response: { code: 200, message: 'ok', data: {} },
  requestExample: {}
})

const renderedContent = computed(() => {
  try {
    return marked(form.content || '')
  } catch (e) {
    return form.content
  }
})

function addTag() {
  const v = tagsInput.value.trim()
  if (v && !form.tags.includes(v)) {
    form.tags.push(v)
  }
  tagsInput.value = ''
}

async function loadDoc() {
  try {
    const res = await docApi.getDetail(docId.value)
    const d = res.data
    Object.assign(form, {
      id: d.id,
      title: d.title || '',
      method: d.method || 'GET',
      path: d.path || '',
      category: d.category || '默认分类',
      tags: d.tags || [],
      content: d.content || '',
      requestParams: d.requestParams || { query: [], path: [], header: [] },
      requestBody: d.requestBody || { type: 'object', properties: {}, required: [] },
      contentType: d.contentType || 'application/json',
      response: d.response || { code: 200, message: 'ok', data: {} },
      requestExample: d.requestExample || {}
    })
    await nextTick()
    if (editor) editor.setValue(form.content)
  } catch (e) {}
}

function initEditor() {
  if (!mdEditorRef.value) return
  editor = monaco.editor.create(mdEditorRef.value, {
    value: form.content,
    language: 'markdown',
    theme: 'vs',
    automaticLayout: true,
    minimap: { enabled: false },
    fontSize: 14
  })
  editor.onDidChangeModelContent(() => {
    form.content = editor?.getValue() || ''
  })
}

async function save(publish: boolean) {
  if (!form.title) {
    return ElMessage.warning('请填写文档标题')
  }
  try {
    const payload = { ...form, status: publish ? 'published' : 'draft' }
    await docApi.update(docId.value, payload)
    await versionApi.create(docId.value, {
      snapshot: payload,
      remark: publish ? '保存并发布' : '保存为草稿'
    })
    ElMessage.success('保存成功')
  } catch (e) {}
}

function gotoHistory() {
  router.push(`/docs/${docId.value}/version`)
}

onMounted(async () => {
  await loadDoc()
  initEditor()
})
</script>

<style scoped>
.doc-edit-page {
  height: 100%;
  background: #f5f7fa;
  padding: 16px;
}
.edit-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}
.edit-body {
  height: calc(100% - 70px);
}
.editor-card {
  height: 100%;
}
.editor-card :deep(.el-card__body) {
  height: 100%;
  display: flex;
  flex-direction: column;
  padding: 16px;
}
.form-row {
  margin-bottom: 12px;
}
.tags {
  margin: 8px 0 0 100px;
}
.editor-tabs {
  flex: 1;
  display: flex;
  flex-direction: column;
}
.editor-tabs :deep(.el-tabs__content) {
  flex: 1;
  overflow: auto;
}
.md-editor {
  height: calc(100vh - 320px);
  border: 1px solid #ebeef5;
  border-radius: 4px;
}
.preview {
  line-height: 1.8;
}
.preview h2 {
  display: flex;
  gap: 8px;
  align-items: center;
}
.preview-path {
  color: #909399;
  font-family: Menlo, Monaco, monospace;
  margin: 8px 0 16px;
}
.doc-content :deep(pre) {
  background: #f5f7fa;
  padding: 12px;
  border-radius: 4px;
  overflow-x: auto;
}
</style>
