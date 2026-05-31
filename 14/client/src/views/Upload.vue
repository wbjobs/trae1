<template>
  <div class="page-container">
    <div class="page-header">
      <h2 class="page-title">文件上传</h2>
      <el-switch
        v-model="useChunkedUpload"
        active-text="分片上传（大文件）"
        inactive-text="普通上传"
        style="margin-left: 20px;"
      />
    </div>

    <div class="card-container">
      <el-upload
        drag
        :auto-upload="false"
        :show-file-list="true"
        :on-change="handleFileChange"
        :on-remove="handleFileRemove"
        :file-list="fileList"
        multiple
      >
        <el-icon class="el-icon--upload"><upload-filled /></el-icon>
        <div class="el-upload__text">将文件拖到此处，或<em>点击上传</em></div>
        <template #tip>
          <div class="el-upload__tip">
            支持多文件上传，单个文件最大500MB
            <span v-if="useChunkedUpload">（分片上传模式：支持断点续传）</span>
          </div>
        </template>
      </el-upload>

      <div class="config-section">
        <div class="section-title">加密配置</div>
        <el-form label-width="120px">
          <div class="form-row">
            <div class="form-item">
              <el-form-item label="加密算法">
                <el-select v-model="config.algorithm" disabled>
                  <el-option label="AES-256-CBC" value="aes-256-cbc" />
                </el-select>
              </el-form-item>
            </div>
            <div class="form-item">
              <el-form-item label="过期时间">
                <el-select v-model="config.expireDays">
                  <el-option label="永不过期" :value="null" />
                  <el-option label="1天" :value="1" />
                  <el-option label="7天" :value="7" />
                  <el-option label="30天" :value="30" />
                  <el-option label="90天" :value="90" />
                </el-select>
              </el-form-item>
            </div>
          </div>
          <div class="form-row">
            <div class="form-item">
              <el-form-item label="所有者ID">
                <el-input v-model="config.ownerId" placeholder="请输入所有者ID" />
              </el-form-item>
            </div>
            <div class="form-item">
              <el-form-item label="所有者名称">
                <el-input v-model="config.ownerName" placeholder="请输入所有者名称" />
              </el-form-item>
            </div>
          </div>
        </el-form>
      </div>

      <div style="margin-top: 20px; text-align: right;">
        <el-button type="primary" :loading="uploading" @click="handleUpload">
          <el-icon v-if="!uploading"><Upload /></el-icon>
          {{ uploading ? `上传中 ${uploadProgress}%` : '开始上传并加密' }}
        </el-button>
      </div>
    </div>

    <div v-if="uploadResults.length > 0" class="card-container" style="margin-top: 20px;">
      <div class="section-title">上传结果</div>
      <el-table :data="uploadResults" style="width: 100%">
        <el-table-column prop="fileName" label="文件名" />
        <el-table-column prop="fileSize" label="大小">
          <template #default="{ row }">
            {{ formatSize(row.fileSize) }}
          </template>
        </el-table-column>
        <el-table-column prop="status" label="状态">
          <template #default="{ row }">
            <el-tag :type="row.status === 'success' ? 'success' : 'danger'">
              {{ row.status === 'success' ? '成功' : '失败' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="message" label="说明" />
        <el-table-column label="操作" width="200">
          <template #default="{ row }">
            <el-button
              v-if="row.status === 'success'"
              type="primary"
              size="small"
              @click="goToShare(row.fileId)"
            >
              创建分享
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </div>

    <el-dialog v-model="chunkProgressVisible" title="分片上传进度" width="600px" :close-on-click-modal="false">
      <div v-for="(progress, fileId) in chunkProgressMap" :key="fileId" class="chunk-progress-item">
        <div style="margin-bottom: 10px;">
          <span style="font-weight: 500;">{{ progress.fileName }}</span>
          <span style="color: #909399; margin-left: 10px;">
            分片 {{ progress.uploadedChunks }}/{{ progress.totalChunks }}
          </span>
        </div>
        <el-progress :percentage="progress.percentage" :status="progress.status" />
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, reactive, computed } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { UploadFilled, Upload } from '@element-plus/icons-vue'
import { fileApi, chunkApi } from '@/api'
import crypto from 'crypto-js'

const router = useRouter()
const fileList = ref([])
const uploading = ref(false)
const uploadProgress = ref(0)
const uploadResults = ref([])
const useChunkedUpload = ref(true)
const chunkProgressVisible = ref(false)
const chunkProgressMap = reactive({})

const CHUNK_SIZE = 5 * 1024 * 1024

const config = reactive({
  algorithm: 'aes-256-cbc',
  expireDays: 7,
  ownerId: '',
  ownerName: ''
})

const generateFileId = (file) => {
  const content = `${file.name}-${file.size}-${file.lastModified}`
  return crypto.MD5(content).toString()
}

const handleFileChange = (file) => {
  if (fileList.value.indexOf(file) === -1) {
    fileList.value.push(file)
  }
}

const handleFileRemove = (file) => {
  const index = fileList.value.indexOf(file)
  if (index > -1) {
    fileList.value.splice(index, 1)
  }
}

const formatSize = (bytes) => {
  if (!bytes) return '-'
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + ' KB'
  if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + ' MB'
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB'
}

const uploadChunkedFile = async (file) => {
  const fileId = generateFileId(file.raw || file)
  const totalChunks = Math.ceil(file.size / CHUNK_SIZE)
  
  chunkProgressMap[fileId] = {
    fileName: file.name,
    uploadedChunks: 0,
    totalChunks,
    percentage: 0,
    status: ''
  }

  let existingChunks = []
  try {
    const res = await chunkApi.getUploaded(fileId)
    existingChunks = res.data.uploadedChunks || []
    chunkProgressMap[fileId].uploadedChunks = existingChunks.length
    chunkProgressMap[fileId].percentage = Math.round((existingChunks.length / totalChunks) * 100)
  } catch (e) {}

  const uploadedChunks = new Set(existingChunks)

  for (let i = 0; i < totalChunks; i++) {
    if (uploadedChunks.has(i)) continue

    const start = i * CHUNK_SIZE
    const end = Math.min(start + CHUNK_SIZE, file.size)
    const chunk = (file.raw || file).slice(start, end)

    const formData = new FormData()
    formData.append('chunk', chunk, `chunk_${i}`)
    formData.append('fileId', fileId)
    formData.append('chunkIndex', i)
    formData.append('totalChunks', totalChunks)
    formData.append('fileName', file.name)
    formData.append('fileSize', file.size)

    try {
      await chunkApi.upload(formData)
      uploadedChunks.add(i)
      chunkProgressMap[fileId].uploadedChunks = uploadedChunks.size
      chunkProgressMap[fileId].percentage = Math.round((uploadedChunks.size / totalChunks) * 100)
    } catch (error) {
      chunkProgressMap[fileId].status = 'exception'
      throw error
    }
  }

  const mergeRes = await chunkApi.merge({
    fileId,
    fileName: file.name,
    mimeType: file.raw?.type || file.type || 'application/octet-stream',
    expireDays: config.expireDays,
    ownerId: config.ownerId || 'anonymous',
    ownerName: config.ownerName || '匿名用户'
  })

  chunkProgressMap[fileId].status = 'success'
  return mergeRes.data
}

const handleUpload = async () => {
  if (fileList.value.length === 0) {
    ElMessage.warning('请先选择文件')
    return
  }

  uploading.value = true
  uploadProgress.value = 0
  uploadResults.value = []

  if (useChunkedUpload.value) {
    chunkProgressVisible.value = true
    Object.keys(chunkProgressMap).forEach(key => delete chunkProgressMap[key])
  }

  for (let i = 0; i < fileList.value.length; i++) {
    const file = fileList.value[i]
    
    const shouldUseChunked = useChunkedUpload.value && file.size > CHUNK_SIZE

    try {
      let result

      if (shouldUseChunked) {
        result = await uploadChunkedFile(file)
      } else {
        const formData = new FormData()
        formData.append('file', file.raw || file)
        formData.append('expireDays', config.expireDays || '')
        formData.append('ownerId', config.ownerId || 'anonymous')
        formData.append('ownerName', config.ownerName || '匿名用户')

        const res = await fileApi.upload(formData, (progress) => {
          uploadProgress.value = Math.round((i + progress / 100) / fileList.value.length * 100)
        })
        result = res.data
      }

      uploadResults.value.push({
        fileName: file.name,
        fileSize: file.size,
        status: 'success',
        fileId: result.fileId,
        message: '加密上传成功'
      })
    } catch (error) {
      uploadResults.value.push({
        fileName: file.name,
        fileSize: file.size,
        status: 'error',
        message: error.message || '上传失败'
      })
    }

    uploadProgress.value = Math.round(((i + 1) / fileList.value.length) * 100)
  }

  uploading.value = false
  uploadProgress.value = 100
  fileList.value = []
  
  setTimeout(() => {
    chunkProgressVisible.value = false
  }, 2000)
  
  ElMessage.success('上传完成')
}

const goToShare = (fileId) => {
  router.push({ path: '/shares', query: { fileId } })
}
</script>
