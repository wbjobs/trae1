<template>
  <div class="task-create">
    <div class="page-header">
      <h2 class="page-title">创建压测任务</h2>
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
        <el-form-item label="任务名称" prop="name">
          <el-input v-model="form.name" placeholder="请输入任务名称" />
        </el-form-item>

        <el-form-item label="选择配置" prop="configId">
          <el-select
            v-model="form.configId"
            placeholder="请选择压测配置"
            filterable
            style="width: 100%"
            @change="handleConfigChange"
          >
            <el-option
              v-for="config in configList"
              :key="config.id"
              :label="`${config.name} - [${config.method}] ${config.url}`"
              :value="config.id"
            />
          </el-select>
        </el-form-item>

        <el-form-item label="任务优先级">
          <el-radio-group v-model="form.priority">
            <el-radio value="LOW">低</el-radio>
            <el-radio value="MEDIUM">中</el-radio>
            <el-radio value="HIGH">高</el-radio>
            <el-radio value="CRITICAL">紧急</el-radio>
          </el-radio-group>
        </el-form-item>

        <div v-if="selectedConfig" class="config-preview">
          <el-descriptions title="配置详情" :column="2" border>
            <el-descriptions-item label="配置名称">{{ selectedConfig.name }}</el-descriptions-item>
            <el-descriptions-item label="请求方法">
              <el-tag :type="getMethodType(selectedConfig.method)" size="small">
                {{ selectedConfig.method }}
              </el-tag>
            </el-descriptions-item>
            <el-descriptions-item label="请求地址" :span="2">{{ selectedConfig.url }}</el-descriptions-item>
            <el-descriptions-item label="线程数">{{ selectedConfig.threadCount }}</el-descriptions-item>
            <el-descriptions-item label="启动时间">{{ selectedConfig.rampUpTime }}秒</el-descriptions-item>
            <el-descriptions-item label="运行设置">
              {{ selectedConfig.useLoopCount ? `循环 ${selectedConfig.loopCount} 次` : `持续 ${selectedConfig.duration} 秒` }}
            </el-descriptions-item>
            <el-descriptions-item label="协议">{{ selectedConfig.protocol || 'http' }}</el-descriptions-item>
          </el-descriptions>
        </div>

        <el-form-item>
          <el-button type="primary" :loading="submitting" @click="handleSubmit">
            创建并启动
          </el-button>
          <el-button @click="$router.back()">取消</el-button>
        </el-form-item>
      </el-form>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { ArrowLeft } from '@element-plus/icons-vue'
import { getConfigList } from '@/api/config'
import { createTask, startTask } from '@/api/task'

const router = useRouter()
const formRef = ref(null)
const loading = ref(false)
const submitting = ref(false)
const configList = ref([])

const form = reactive({
  name: '',
  configId: null,
  priority: 'MEDIUM'
})

const rules = {
  name: [
    { required: true, message: '请输入任务名称', trigger: 'blur' }
  ],
  configId: [
    { required: true, message: '请选择压测配置', trigger: 'change' }
  ]
}

const selectedConfig = computed(() => {
  return configList.value.find(c => c.id === form.configId)
})

const getMethodType = (method) => {
  const typeMap = {
    GET: 'success',
    POST: 'warning',
    PUT: 'primary',
    DELETE: 'danger',
    PATCH: 'info'
  }
  return typeMap[method] || 'info'
}

const loadConfigs = async () => {
  loading.value = true
  try {
    const res = await getConfigList()
    configList.value = res.data || []
  } catch (error) {
    console.error('Failed to load configs:', error)
  } finally {
    loading.value = false
  }
}

const handleConfigChange = () => {
  if (selectedConfig.value && !form.name) {
    form.name = `${selectedConfig.value.name} - 压测任务`
  }
}

const handleSubmit = async () => {
  if (!formRef.value) return

  await formRef.value.validate(async (valid) => {
    if (valid) {
      submitting.value = true
      try {
        const taskRes = await createTask(form)
        if (taskRes.data) {
          await startTask(taskRes.data.id)
          ElMessage.success('任务创建并启动成功')
          router.push(`/tasks/${taskRes.data.id}`)
        }
      } catch (error) {
        console.error('Failed to create task:', error)
        ElMessage.error('任务创建失败')
      } finally {
        submitting.value = false
      }
    }
  })
}

onMounted(() => {
  loadConfigs()
})
</script>

<style lang="scss" scoped>
.task-create {
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

  .config-preview {
    margin: 20px 0;
    padding: 20px;
    background: #f5f7fa;
    border-radius: 8px;
  }
}
</style>
