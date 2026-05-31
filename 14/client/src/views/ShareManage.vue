<template>
  <div class="page-container">
    <div class="page-header">
      <h2 class="page-title">分享管理</h2>
      <el-button type="primary" @click="loadShares">
        <el-icon><Refresh /></el-icon>
        刷新
      </el-button>
    </div>

    <div class="card-container">
      <el-table :data="shares" v-loading="loading" style="width: 100%">
        <el-table-column prop="shareCode" label="分享码" width="150">
          <template #default="{ row }">
            <span style="font-weight: 600; color: #409eff; letter-spacing: 2px;">
              {{ row.shareCode }}
            </span>
            <el-button link @click="copyShareCode(row.shareCode)">
              <el-icon><CopyDocument /></el-icon>
            </el-button>
          </template>
        </el-table-column>
        <el-table-column prop="fileName" label="文件名" min-width="200">
          <template #default="{ row }">
            {{ row.file?.originalName || '-' }}
          </template>
        </el-table-column>
        <el-table-column prop="permissionLevel" label="权限" width="100">
          <template #default="{ row }">
            <span :class="['permission-badge', `permission-${row.permissionLevel}`]">
              {{ getPermissionLabel(row.permissionLevel) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="accessCount" label="访问次数" width="100">
          <template #default="{ row }">
            {{ row.accessCount }}{{ row.accessLimit ? `/${row.accessLimit}` : '' }}
          </template>
        </el-table-column>
        <el-table-column prop="expireAt" label="过期时间" width="180">
          <template #default="{ row }">
            <span v-if="row.expireAt">{{ formatDate(row.expireAt) }}</span>
            <span v-else style="color: #909399;">永久</span>
          </template>
        </el-table-column>
        <el-table-column prop="status" label="状态" width="100">
          <template #default="{ row }">
            <span :class="['status-tag', `status-${row.status}`]">
              {{ getStatusLabel(row.status) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="createdAt" label="创建时间" width="180">
          <template #default="{ row }">
            {{ formatDate(row.createdAt) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="150" fixed="right">
          <template #default="{ row }">
            <el-button
              v-if="row.status === 'active'"
              type="danger"
              size="small"
              @click="handleRevoke(row)"
            >
              <el-icon><Close /></el-icon>
              撤销
            </el-button>
            <span v-else style="color: #c0c4cc;">已失效</span>
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
          @size-change="loadShares"
          @current-change="loadShares"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Refresh, CopyDocument, Close } from '@element-plus/icons-vue'
import { shareApi } from '@/api'
import dayjs from 'dayjs'

const shares = ref([])
const loading = ref(false)
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)

const getPermissionLabel = (level) => {
  const map = {
    read: '仅查看',
    download: '可下载',
    admin: '完全控制'
  }
  return map[level] || level
}

const getStatusLabel = (status) => {
  const map = {
    active: '有效',
    expired: '已过期',
    revoked: '已撤销'
  }
  return map[status] || status
}

const formatDate = (date) => {
  return dayjs(date).format('YYYY-MM-DD HH:mm:ss')
}

const loadShares = async () => {
  loading.value = true
  try {
    const res = await shareApi.list({
      page: page.value,
      pageSize: pageSize.value
    })
    shares.value = res.data.list
    total.value = res.data.total
  } catch (error) {
    console.error(error)
  } finally {
    loading.value = false
  }
}

const copyShareCode = (code) => {
  navigator.clipboard.writeText(code)
  ElMessage.success('分享码已复制')
}

const handleRevoke = async (row) => {
  try {
    await ElMessageBox.confirm(
      `确定要撤销分享码 "${row.shareCode}" 吗？`,
      '撤销确认',
      {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    await shareApi.revoke(row.id, { createdBy: 'anonymous' })
    ElMessage.success('撤销成功')
    loadShares()
  } catch (error) {
    if (error !== 'cancel') {
      console.error(error)
    }
  }
}

onMounted(() => {
  loadShares()
})
</script>
