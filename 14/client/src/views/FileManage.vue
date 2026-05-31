<template>
  <div class="page-container">
    <div class="page-header">
      <h2 class="page-title">文件管理</h2>
      <div>
        <el-input
          v-model="searchKeyword"
          placeholder="搜索文件名"
          style="width: 200px; margin-right: 10px;"
          clearable
          @clear="loadFiles"
          @keyup.enter="loadFiles"
        >
          <template #prefix>
            <el-icon><Search /></el-icon>
          </template>
        </el-input>
        <el-button type="primary" @click="loadFiles">
          <el-icon><Refresh /></el-icon>
          刷新
        </el-button>
      </div>
    </div>

    <div class="card-container">
      <el-table :data="files" v-loading="loading" style="width: 100%">
        <el-table-column prop="originalName" label="文件名" min-width="200">
          <template #default="{ row }">
            <div style="display: flex; align-items: center; gap: 8px;">
              <el-icon :size="20"><Document /></el-icon>
              <span>{{ row.originalName }}</span>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="fileSize" label="大小" width="120">
          <template #default="{ row }">
            {{ formatSize(row.fileSize) }}
          </template>
        </el-table-column>
        <el-table-column prop="mimeType" label="类型" width="150" />
        <el-table-column prop="ownerName" label="所有者" width="120" />
        <el-table-column prop="expireAt" label="过期时间" width="180">
          <template #default="{ row }">
            <span v-if="row.expireAt">{{ formatDate(row.expireAt) }}</span>
            <span v-else style="color: #909399;">永不过期</span>
          </template>
        </el-table-column>
        <el-table-column prop="createdAt" label="上传时间" width="180">
          <template #default="{ row }">
            {{ formatDate(row.createdAt) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="280" fixed="right">
          <template #default="{ row }">
            <el-button type="primary" size="small" @click="handlePreview(row)>
              <el-icon><View /></el-icon>
              预览
            </el-button>
            <el-button type="success" size="small" @click="handleDownload(row)">
              <el-icon><Download /></el-icon>
              下载
            </el-button>
            <el-button type="warning" size="small" @click="handleShare(row)">
              <el-icon><Share /></el-icon>
              分享
            </el-button>
            <el-button type="danger" size="small" @click="handleDelete(row)">
              <el-icon><Delete /></el-icon>
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div style="margin-top: 20px; text-align: right;">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :page-sizes="[10, 20, 50, 100]"
          :total="total"
          layout="total, sizes, prev, pager, next, jumper"
          @size-change="loadFiles"
          @current-change="loadFiles"
        />
      </div>
    </div>

    <el-dialog v-model="shareDialogVisible" title="创建分享链接" width="500px">
      <el-form :model="shareForm" label-width="100px">
        <el-form-item label="权限级别">
          <el-select v-model="shareForm.permissionLevel">
            <el-option label="仅查看" value="read" />
            <el-option label="可下载" value="download" />
            <el-option label="完全控制" value="admin" />
          </el-select>
        </el-form-item>
        <el-form-item label="有效期">
          <el-select v-model="shareForm.expireDays">
            <el-option label="永久有效" :value="null" />
            <el-option label="1天" :value="1" />
            <el-option label="7天" :value="7" />
            <el-option label="30天" :value="30" />
          </el-select>
        </el-form-item>
        <el-form-item label="访问密码">
          <el-input v-model="shareForm.password" placeholder="留空表示无密码" show-password />
        </el-form-item>
        <el-form-item label="访问次数">
          <el-input-number v-model="shareForm.accessLimit" :min="0" :max="999999" />
          <span style="color: #909399; margin-left: 10px; font-size: 12px;">0表示无限制</span>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="shareDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="sharing" @click="createShare">创建分享</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="shareResultVisible" title="分享成功" width="500px">
      <el-result icon="success" title="分享链接已创建">
        <template #extra>
          <div style="text-align: left;">
            <p style="margin-bottom: 10px;">
              <strong>分享码：</strong>
              <span style="font-size: 18px; color: #409eff; letter-spacing: 2px;">
                {{ shareResult.shareCode }}
              </span>
              <el-button link @click="copyShareCode">
                <el-icon><CopyDocument /></el-icon>
                复制
              </el-button>
            </p>
            <p v-if="shareResult.hasPassword" style="color: #e6a23c;">
              <el-icon><Warning /></el-icon>
              该分享需要密码访问，请妥善保管密码
            </p>
            <p v-if="shareResult.expireAt">
              过期时间：{{ formatDate(shareResult.expireAt) }}
            </p>
          </div>
        </template>
      </el-result>
      <template #footer>
        <el-button type="primary" @click="shareResultVisible = false">确定</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="previewVisible" title="文件预览" width="800px">
      <div style="margin-bottom: 10px;">
        <el-switch
          v-model="previewWatermark"
          active-text="显示水印"
          inactive-text="无水印"
          @change="handlePreviewWatermarkChange"
        />
      </div>
      <iframe
      v-if="previewUrl"
      :src="previewUrl"
      frameborder="0"
      style="width: 100%; height: 500px;"
    ></iframe>
      <template #footer>
        <el-button @click="previewVisible = false">关闭</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  Document, Download, Share, Delete, View, Search, Refresh, CopyDocument, Warning } from '@element-plus/icons-vue'
import { fileApi, shareApi } from '@/api'
import dayjs from 'dayjs'

const router = useRouter()
const route = useRoute()

const files = ref([])
const loading = ref(false)
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const searchKeyword = ref('')

const shareDialogVisible = ref(false)
const shareResultVisible = ref(false)
const sharing = ref(false)
const currentFile = ref(null)
const shareResult = ref({})
const previewVisible = ref(false)
const previewUrl = ref('')
const previewWatermark = ref(true)

const shareForm = ref({
  permissionLevel: 'read',
  expireDays: 7,
  password: '',
  accessLimit: 0
})

const formatSize = (bytes) => {
  if (!bytes) return '-'
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + ' KB'
  if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + ' MB'
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB'
}

const formatDate = (date) => {
  return dayjs(date).format('YYYY-MM-DD HH:mm:ss')
}

const loadFiles = async () => {
  loading.value = true
  try {
    const res = await fileApi.list({
      page: page.value,
      pageSize: pageSize.value,
      keyword: searchKeyword.value
    })
    files.value = res.data.list
    total.value = res.data.total
  } catch (error) {
    console.error(error)
  } finally {
    loading.value = false
  }
}

const currentPreviewRow = ref(null)

const handlePreview = (row) => {
  currentPreviewRow.value = row
  previewUrl.value = fileApi.preview(row.id) + (previewWatermark.value ? '?watermark=true' : '')
  previewVisible.value = true
}

const handlePreviewWatermarkChange = () => {
  if (currentPreviewRow.value) {
    previewUrl.value = fileApi.preview(currentPreviewRow.value.id) + (previewWatermark.value ? '?watermark=true' : '')
  }
}

const handleDownload = (row) => {
  window.open(fileApi.download(row.id))
}

const handleShare = (row) => {
  currentFile.value = row
  shareForm.value = {
    permissionLevel: 'read',
    expireDays: 7,
    password: '',
    accessLimit: 0
  }
  shareDialogVisible.value = true
}

const createShare = async () => {
  sharing.value = true
  try {
    const payload = {
      fileId: currentFile.value.id,
      permissionLevel: shareForm.value.permissionLevel,
      expireDays: shareForm.value.expireDays,
      password: shareForm.value.password || null,
      accessLimit: shareForm.value.accessLimit > 0 ? shareForm.value.accessLimit : null,
      createdBy: 'anonymous'
    }
    const res = await shareApi.create(payload)
    shareResult.value = res.data
    shareDialogVisible.value = false
    shareResultVisible.value = true
  } catch (error) {
    console.error(error)
  } finally {
    sharing.value = false
  }
}

const copyShareCode = () => {
  navigator.clipboard.writeText(shareResult.value.shareCode)
  ElMessage.success('分享码已复制')
}

const handleDelete = async (row) => {
  try {
    await ElMessageBox.confirm(
      `确定要删除文件 "${row.originalName}" 吗？`,
      '删除确认',
      {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    await fileApi.delete(row.id, { ownerId: 'anonymous' })
    ElMessage.success('删除成功')
    loadFiles()
  } catch (error) {
    if (error !== 'cancel') {
      console.error(error)
    }
  }
}

onMounted(() => {
  loadFiles()
})
</script>
