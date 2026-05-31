<template>
  <div class="page-container debug-page">
    <div class="page-header">
      <div class="flex" style="gap: 12px; align-items: center">
        <el-button :icon="ArrowLeft" @click="$router.back()">返回</el-button>
        <span class="page-title">在线调试</span>
      </div>
    </div>

    <el-row :gutter="16">
      <el-col :span="14">
        <el-card>
          <template #header>
            <div class="flex-between">
              <span>请求配置</span>
              <el-button type="primary" :icon="Promotion" @click="sendRequest" :loading="sending">
                发送请求
              </el-button>
            </div>
          </template>
          <div class="url-bar">
            <el-select v-model="form.method" size="large" style="width: 120px">
              <el-option v-for="m in methods" :key="m" :label="m" :value="m" />
            </el-select>
            <el-input v-model="form.baseUrl" size="large" style="width: 200px" placeholder="Base URL" />
            <el-input v-model="form.path" size="large" style="flex: 1" placeholder="请求路径" />
          </div>

          <el-tabs v-model="activeTab">
            <el-tab-pane label="Query 参数" name="query">
              <KeyValueEditor v-model="form.query" :allow-files="false" />
            </el-tab-pane>
            <el-tab-pane label="Headers" name="headers">
              <KeyValueEditor v-model="form.headers" :allow-files="false" />
            </el-tab-pane>
            <el-tab-pane label="Body" name="body">
              <div v-if="['GET'].includes(form.method)" style="color: #909399">GET 请求不支持 Body</div>
              <div v-else>
                <el-radio-group v-model="form.bodyType" style="margin-bottom: 12px">
                  <el-radio-button label="json">JSON</el-radio-button>
                  <el-radio-button label="form-data">form-data</el-radio-button>
                  <el-radio-button label="x-www-form-urlencoded">x-www</el-radio-button>
                </el-radio-group>
                <KeyValueEditor v-if="form.bodyType === 'json'" v-model="form.bodyText" is-text />
                <KeyValueEditor v-else v-model="form.bodyForm" />
              </div>
            </el-tab-pane>
            <el-tab-pane label="Cookie" name="cookies">
              <KeyValueEditor v-model="form.cookies" />
            </el-tab-pane>
          </el-tabs>
        </el-card>
      </el-col>

      <el-col :span="10">
            <el-card>
              <template #header>
                <div class="flex-between">
                  <span>响应结果</span>
                  <el-tag v-if="response" :type="response.status >= 200 && response.status < 300 ? 'success' : 'danger'">
                    {{ response.status }} {{ response.statusText }}
                  </el-tag>
                  <span v-if="response" style="font-size: 12px; color: #909399">
                    耗时: {{ response.duration }}ms | 大小: {{ response.size }}B
                  </span>
                </div>
              </template>
              <el-tabs v-model="respTab">
                <el-tab-pane label="Body" name="body">
                  <JsonViewer :data="response?.data" v-if="response" />
                  <el-empty v-else description="点击发送请求查看结果" />
                </el-tab-pane>
                <el-tab-pane label="Headers" name="headers">
                    <el-descriptions :column="1" border v-if="response">
                      <el-descriptions-item v-for="(v, k) in response.headers" :key="k" :label="k">{{ v }}</el-descriptions-item>
                    </el-descriptions>
                    <el-empty v-else description="暂无数据" />
                </el-tab-pane>
              </el-tabs>
            </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { ArrowLeft, Promotion } from '@element-plus/icons-vue'
import { docApi } from '@/api/doc'
import KeyValueEditor from '@/components/KeyValueEditor.vue'
import JsonViewer from '@/components/JsonViewer.vue'

const route = useRoute()
const docId = computed(() => route.params.docId)
const methods = ['GET', 'POST', 'PUT', 'DELETE', 'PATCH']

const form = reactive({
  method: 'GET',
  baseUrl: 'http://localhost:3000',
  path: '',
  query: [] as { key: string; value: string; enabled: boolean }[],
  headers: [{ key: 'Content-Type', value: 'application/json', enabled: true }] as any[],
  cookies: [] as any[],
  bodyType: 'json',
  bodyText: '',
  bodyForm: [] as any[]
})

const activeTab = ref('query')
const respTab = ref('body')
const sending = ref(false)
const response = ref<any>(null)

onMounted(async () => {
  try {
    const res = await docApi.getDetail(docId.value)
    const d = res.data
    form.method = d.method || 'GET'
    form.path = d.path || ''
    if (d.requestExample) {
      form.bodyText = JSON.stringify(d.requestExample, null, 2)
    }
  } catch (e) {}
})

async function sendRequest() {
  if (!form.path) {
    return ElMessage.warning('请填写请求路径')
  }
  sending.value = true
  response.value = null
  try {
    const headers: Record<string, string> = Object.fromEntries(
      form.headers.filter((h: any) => h.enabled && h.key).map((h: any) => [h.key, h.value])
    )
    const cookies = form.cookies
      .filter((c: any) => c.enabled && c.key)
      .map((c: any) => `${encodeURIComponent(c.key)}=${encodeURIComponent(c.value)}`)
      .join('; ')
    if (cookies) headers['Cookie'] = cookies

    const params: Record<string, any> = Object.fromEntries(
      form.query.filter((q: any) => q.enabled && q.key).map((q: any) => [q.key, q.value])
    )

    let body: any
    if (form.method !== 'GET') {
      if (form.bodyType === 'json') {
        const text = (form.bodyText || '').trim()
        if (text) {
          try {
            body = JSON.parse(text)
          } catch (e) {
            body = text
            if (!headers['Content-Type']) headers['Content-Type'] = 'text/plain'
          }
        } else {
          body = undefined
        }
        if (!headers['Content-Type']) headers['Content-Type'] = 'application/json'
      } else if (form.bodyType === 'form-data') {
        const fd = new FormData()
        form.bodyForm
          .filter((f: any) => f.enabled && f.key)
          .forEach((f: any) => {
            if (f.file) fd.append(f.key, f.file)
            else fd.append(f.key, f.value || '')
          })
        body = fd
        delete headers['Content-Type']
      } else {
        body = Object.fromEntries(
          form.bodyForm.filter((f: any) => f.enabled && f.key).map((f: any) => [f.key, f.value])
        )
        if (!headers['Content-Type']) headers['Content-Type'] = 'application/x-www-form-urlencoded'
      }
    }
    const res = await docApi.debug({
      method: form.method,
      url: form.baseUrl + form.path,
      headers,
      params,
      body
    })
    response.value = res.data
  } catch (e: any) {
    response.value = e?.response || { status: 0, statusText: '请求失败', data: e?.message, headers: {}, duration: 0, size: 0 }
  } finally {
    sending.value = false
  }
}
</script>

<style scoped>
.debug-page {
  height: 100%;
}
.url-bar {
  display: flex;
  gap: 8px;
  margin-bottom: 16px;
}
</style>
