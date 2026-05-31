<template>
  <div class="page-container">
    <div class="page-header">
      <h2 class="page-title">访问分享</h2>
    </div>

    <div class="card-container" style="max-width: 600px; margin: 0 auto;">
      <el-form
        v-if="!fileInfo"
        :model="accessForm"
        label-width="80px"
        class="access-form"
      >
        <el-form-item label="分享码">
          <el-input
            v-model="accessForm.shareCode"
            placeholder="请输入分享码"
            size="large"
            maxlength="20"
          >
            <template #prefix>
              <el-icon><Key /></el-icon>
            </template>
          </el-input>
        </el-form-item>
        <el-form-item label="密码">
          <el-input
            v-model="accessForm.password"
            placeholder="如有密码请输入"
            show-password
            size="large"
          />
        </el-form-item>
        <el-form-item>
          <el-button
            type="primary"
            size="large"
            style="width: 100%;"
            :loading="accessing"
            @click="handleAccess"
          >
            访问
          </el-button>
        </el-form-item>
      </el-form>

      <div v-if="fileInfo" class="file-info-card">
        <el-result
          icon="success"
          title="访问成功"
          :sub-title="fileInfo.fileName"
        >
          <template #extra>
            <div class="file-details">
              <el-descriptions :column="1" border>
                <el-descriptions-item label="文件名">
                  {{ fileInfo.fileName }}
                </el-descriptions-item>
                <el-descriptions-item label="大小">
                  {{ formatSize(fileInfo.fileSize) }}
                </el-descriptions-item>
                <el-descriptions-item label="所有者">
                  {{ fileInfo.ownerName }}
                </el-descriptions-item>
                <el-descriptions-item label="权限">
                  <span :class="['permission-badge', `permission-${fileInfo.permissionLevel}`]">
                    {{ getPermissionLabel(fileInfo.permissionLevel) }}
                  </span>
                </el-descriptions-item>
              </el-descriptions>

              <div style="margin-top: 20px; text-align: center;">
                <el-button
                  type="primary"
                  size="large"
                  @click="handlePreview"
                >
                  <el-icon><View /></el-icon>
                  预览文件
                </el-button>
                <el-button
                  v-if="fileInfo.canDownload"
                  type="success"
                  size="large"
                  @click="handleDownload"
                >
                  <el-icon><Download /></el-icon>
                  下载文件
                </el-button>
              </div>
            </div>
          </template>
        </el-result>
        <div style="text-align: center; margin-top: 20px;">
          <el-button @click="resetAccess">
            <el-icon><Refresh /></el-icon>
            访问其他分享
          </el-button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { Key, View, Download, Refresh } from '@element-plus/icons-vue'
import { shareApi } from '@/api'

const accessForm = ref({
  shareCode: '',
  password: ''
})

const accessing = ref(false)
const fileInfo = ref(null)
const currentFileId = ref(null)

const getPermissionLabel = (level) => {
  const map = {
    read: '仅查看',
    download: '可下载',
    admin: '完全控制'
  }
  return map[level] || level
}

const formatSize = (bytes) => {
  if (!bytes) return '-'
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + ' KB'
  if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + ' MB'
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB'
}

const handleAccess = async () => {
  if (!accessForm.value.shareCode) {
    ElMessage.warning('请输入分享码')
    return
  }

  accessing.value = true
  try {
    const res = await shareApi.access(accessForm.value.shareCode.toUpperCase(), {
      password: accessForm.value.password || null
    })
    fileInfo.value = res.data
    currentFileId.value = res.data.fileId
  } catch (error) {
    console.error(error)
  } finally {
    accessing.value = false
  }
}

const handlePreview = () => {
  window.open(`/api/files/${currentFileId.value}/preview`, '_blank')
}

const handleDownload = () => {
  window.open(`/api/files/${currentFileId.value}/download`, '_blank')
}

const resetAccess = () => {
  fileInfo.value = null
  accessForm.value = {
    shareCode: '',
    password: ''
  }
}
</script>
