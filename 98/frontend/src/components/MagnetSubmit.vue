<template>
  <div class="magnet-submit">
    <el-card class="submit-card">
      <template #header>
        <div class="card-header">
          <el-icon :size="20" color="#409EFF"><Link /></el-icon>
          <span>提交磁力链接</span>
        </div>
      </template>

      <el-form :model="form" label-width="0" @submit.prevent="handleSubmit">
        <el-form-item>
          <el-input
            v-model="form.magnetUri"
            type="textarea"
            :rows="3"
            placeholder="粘贴磁力链接，例如: magnet:?xt=urn:btih:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
            resize="none"
          />
        </el-form-item>

        <el-form-item>
          <el-checkbox v-model="form.fetchMetadata">
            立即尝试获取种子元数据（可能需要较长时间）
          </el-checkbox>
        </el-form-item>

        <el-form-item>
          <el-button
            type="primary"
            size="large"
            :loading="loading"
            @click="handleSubmit"
          >
            提交解析
          </el-button>
          <el-button size="large" @click="form.magnetUri = ''">
            清空
          </el-button>
        </el-form-item>
      </el-form>

      <div class="result-area" v-if="result">
        <el-divider />
        <h4>解析结果</h4>
        <el-descriptions :column="1" border size="small">
          <el-descriptions-item label="InfoHash">
            <el-tag type="primary" size="small">{{ result.infohash }}</el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="显示名称">
            {{ result.parsed?.display_name || '-' }}
          </el-descriptions-item>
          <el-descriptions-item label="数据获取">
            <el-tag :type="result.metadata_fetched ? 'success' : 'info'" size="small">
              {{ result.metadata_fetched ? '已获取' : '后台解析中' }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item v-if="result.metadata" label="文件名">
            {{ result.metadata.name }}
          </el-descriptions-item>
          <el-descriptions-item v-if="result.metadata" label="总大小">
            {{ formatSize(result.metadata.total_size) }}
          </el-descriptions-item>
          <el-descriptions-item v-if="result.metadata" label="文件数量">
            {{ result.metadata.file_count }}
          </el-descriptions-item>
        </el-descriptions>
      </div>

      <div class="help-text">
        <el-divider />
        <el-alert
          title="磁力链接格式"
          type="info"
          :closable="false"
          show-icon
        >
          <template #default>
            <p>标准磁力链接格式: <code>magnet:?xt=urn:btih:&lt;40位十六进制哈希&gt;</code></p>
            <p>支持包含 <code>dn</code>（显示名称）、<code>tr</code>（tracker）等参数</p>
            <p>提交后系统会自动尝试下载元数据并解析文件列表</p>
          </template>
        </el-alert>
      </div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue'
import { ElMessage } from 'element-plus'
import { submitMagnet } from '../api/index.js'

const emit = defineEmits(['submitted'])

const form = reactive({
  magnetUri: '',
  fetchMetadata: false
})

const loading = ref(false)
const result = ref(null)

async function handleSubmit() {
  if (!form.magnetUri.trim()) {
    ElMessage.warning('请输入磁力链接')
    return
  }

  loading.value = true
  result.value = null
  try {
    const data = await submitMagnet(form.magnetUri.trim(), form.fetchMetadata)
    result.value = data
    ElMessage.success('提交成功！')
    emit('submitted', data)
  } catch (e) {
    ElMessage.error('提交失败: ' + (e.response?.data?.error || e.message))
  } finally {
    loading.value = false
  }
}

function formatSize(bytes) {
  if (!bytes) return '-'
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  let size = bytes
  let idx = 0
  while (size >= 1024 && idx < units.length - 1) {
    size /= 1024
    idx++
  }
  return size.toFixed(2) + ' ' + units[idx]
}
</script>

<style scoped>
.submit-card {
  max-width: 800px;
}

.card-header {
  display: flex;
  align-items: center;
  gap: 8px;
}

.result-area {
  margin-top: 10px;
}

.help-text {
  margin-top: 15px;
}

code {
  background: #f5f7fa;
  padding: 2px 6px;
  border-radius: 3px;
  font-size: 13px;
  color: #e6a23c;
}
</style>
