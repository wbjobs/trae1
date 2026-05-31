<template>
  <div class="config-form">
    <div class="page-header">
      <h2 class="page-title">{{ isEdit ? '编辑配置' : '新建配置' }}</h2>
      <el-button :icon="ArrowLeft" @click="$router.back()">返回</el-button>
    </div>

    <div class="card-container">
      <el-form
        ref="formRef"
        :model="form"
        :rules="rules"
        label-width="120px"
        v-loading="loading"
      >
        <el-row :gutter="20">
          <el-col :span="12">
            <el-form-item label="配置名称" prop="name">
              <el-input v-model="form.name" placeholder="请输入配置名称" />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="请求方法" prop="method">
              <el-select v-model="form.method" placeholder="请选择请求方法">
                <el-option label="GET" value="GET" />
                <el-option label="POST" value="POST" />
                <el-option label="PUT" value="PUT" />
                <el-option label="DELETE" value="DELETE" />
                <el-option label="PATCH" value="PATCH" />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <el-form-item label="请求URL" prop="url">
          <el-input v-model="form.url" placeholder="请输入完整的请求URL" />
        </el-form-item>

        <el-form-item label="请求描述" prop="description">
          <el-input
            v-model="form.description"
            type="textarea"
            :rows="2"
            placeholder="请输入配置描述（可选）"
          />
        </el-form-item>

        <el-divider content-position="left">请求参数</el-divider>

        <el-form-item label="请求头">
          <el-input
            v-model="form.headers"
            type="textarea"
            :rows="3"
            placeholder='请输入JSON格式的请求头，如: {"Content-Type": "application/json"}'
          />
        </el-form-item>

        <el-form-item label="请求体">
          <el-input
            v-model="form.requestBody"
            type="textarea"
            :rows="4"
            placeholder="请输入请求体内容（POST/PUT请求使用）"
          />
        </el-form-item>

        <el-divider content-position="left">压测参数</el-divider>

        <el-row :gutter="20">
          <el-col :span="8">
            <el-form-item label="线程数" prop="threadCount">
              <el-input-number
                v-model="form.threadCount"
                :min="1"
                :max="10000"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
          <el-col :span="8">
            <el-form-item label="启动时间(秒)" prop="rampUpTime">
              <el-input-number
                v-model="form.rampUpTime"
                :min="1"
                :max="3600"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-form-item label="运行模式" prop="useLoopCount">
          <el-radio-group v-model="form.useLoopCount">
            <el-radio :label="true">循环次数</el-radio>
            <el-radio :label="false">持续时间</el-radio>
          </el-radio-group>
        </el-form-item>

        <el-row :gutter="20">
          <el-col :span="8" v-if="form.useLoopCount">
            <el-form-item label="循环次数" prop="loopCount">
              <el-input-number
                v-model="form.loopCount"
                :min="1"
                :max="1000000"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
          <el-col :span="8" v-else>
            <el-form-item label="持续时间(秒)" prop="duration">
              <el-input-number
                v-model="form.duration"
                :min="1"
                :max="86400"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-divider content-position="left">高级设置</el-divider>

        <el-row :gutter="20">
          <el-col :span="8">
            <el-form-item label="协议">
              <el-select v-model="form.protocol" placeholder="请选择协议">
                <el-option label="HTTP" value="http" />
                <el-option label="HTTPS" value="https" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="8">
            <el-form-item label="端口号">
              <el-input-number
                v-model="form.port"
                :min="1"
                :max="65535"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
          <el-col :span="8">
            <el-form-item label="域名">
              <el-input v-model="form.domain" placeholder="请输入域名（可选）" />
            </el-form-item>
          </el-col>
        </el-row>

        <el-form-item label="请求路径">
          <el-input v-model="form.path" placeholder="请输入请求路径（可选，如: /api/test）" />
        </el-form-item>

        <el-divider content-position="left">超时设置</el-divider>

        <el-row :gutter="20">
          <el-col :span="8">
            <el-form-item label="连接超时(ms)">
              <el-input-number
                v-model="form.connectionTimeout"
                :min="1000"
                :max="120000"
                :step="1000"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
          <el-col :span="8">
            <el-form-item label="响应超时(ms)">
              <el-input-number
                v-model="form.responseTimeout"
                :min="1000"
                :max="300000"
                :step="1000"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-divider content-position="left">异常场景模拟</el-divider>

        <el-alert
          title="异常场景模拟用于测试系统在各种异常情况下的表现"
          type="info"
          :closable="false"
          show-icon
        />

        <el-row :gutter="20" class="mt-10">
          <el-col :span="8">
            <el-form-item label="模拟延迟">
              <el-switch v-model="form.simulateDelay" />
            </el-form-item>
          </el-col>
          <el-col :span="8" v-if="form.simulateDelay">
            <el-form-item label="延迟范围(ms)">
              <el-input-number
                v-model="form.delayMinMs"
                :min="0"
                :max="10000"
                placeholder="最小"
                style="width: 45%"
              />
              <span style="margin: 0 10px;">-</span>
              <el-input-number
                v-model="form.delayMaxMs"
                :min="0"
                :max="10000"
                placeholder="最大"
                style="width: 45%"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-row :gutter="20">
          <el-col :span="8">
            <el-form-item label="模拟错误">
              <el-switch v-model="form.simulateError" />
            </el-form-item>
          </el-col>
          <el-col :span="8" v-if="form.simulateError">
            <el-form-item label="错误概率(%)">
              <el-input-number
                v-model="form.errorProbability"
                :min="0"
                :max="100"
                :step="5"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-form-item v-if="form.simulateError" label="错误状态码">
          <el-input
            v-model="form.errorStatusCodes"
            placeholder="请输入要模拟的错误状态码，用逗号分隔，如: 500,502,503"
          />
        </el-form-item>

        <el-form-item>
          <el-button type="primary" :loading="submitting" @click="handleSubmit">
            {{ isEdit ? '保存修改' : '创建配置' }}
          </el-button>
          <el-button @click="$router.back()">取消</el-button>
        </el-form-item>
      </el-form>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { ArrowLeft } from '@element-plus/icons-vue'
import { getConfig, createConfig, updateConfig } from '@/api/config'

const route = useRoute()
const router = useRouter()

const formRef = ref(null)
const loading = ref(false)
const submitting = ref(false)

const isEdit = computed(() => !!route.params.id)

const form = reactive({
  id: null,
  name: '',
  description: '',
  method: 'GET',
  url: '',
  headers: '',
  requestBody: '',
  threadCount: 10,
  rampUpTime: 10,
  loopCount: 10,
  duration: 60,
  useLoopCount: true,
  protocol: 'http',
  port: 80,
  domain: '',
  path: '',
  connectionTimeout: 10000,
  responseTimeout: 30000,
  simulateDelay: false,
  delayMinMs: 100,
  delayMaxMs: 500,
  simulateError: false,
  errorProbability: 10,
  errorStatusCodes: '500,502,503'
})

const rules = {
  name: [
    { required: true, message: '请输入配置名称', trigger: 'blur' },
    { min: 2, max: 100, message: '配置名称长度在 2 到 100 个字符', trigger: 'blur' }
  ],
  method: [
    { required: true, message: '请选择请求方法', trigger: 'change' }
  ],
  url: [
    { required: true, message: '请输入请求URL', trigger: 'blur' },
    { type: 'url', message: '请输入有效的URL', trigger: 'blur' }
  ],
  threadCount: [
    { required: true, message: '请输入线程数', trigger: 'blur' }
  ],
  rampUpTime: [
    { required: true, message: '请输入启动时间', trigger: 'blur' }
  ],
  loopCount: [
    { required: true, message: '请输入循环次数', trigger: 'blur' }
  ],
  duration: [
    { required: true, message: '请输入持续时间', trigger: 'blur' }
  ]
}

const loadConfig = async (id) => {
  loading.value = true
  try {
    const res = await getConfig(id)
    if (res.data) {
      Object.assign(form, res.data)
    }
  } catch (error) {
    console.error('Failed to load config:', error)
    ElMessage.error('加载配置失败')
  } finally {
    loading.value = false
  }
}

const handleSubmit = async () => {
  if (!formRef.value) return

  await formRef.value.validate(async (valid) => {
    if (valid) {
      submitting.value = true
      try {
        if (isEdit.value) {
          await updateConfig(form.id, form)
          ElMessage.success('配置更新成功')
        } else {
          await createConfig(form)
          ElMessage.success('配置创建成功')
        }
        router.push('/configs')
      } catch (error) {
        console.error('Failed to save config:', error)
      } finally {
        submitting.value = false
      }
    }
  })
}

onMounted(() => {
  if (isEdit.value) {
    loadConfig(route.params.id)
  }
})
</script>

<style lang="scss" scoped>
.config-form {
  .page-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;
  }

  .page-title {
    margin: 0;
    font-size: 20px;
    font-weight: 600;
  }

  .card-container {
    background: #fff;
    border-radius: 8px;
    padding: 30px;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
  }

  .mt-10 {
    margin-top: 10px;
  }
}
</style>
