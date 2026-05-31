<template>
  <div class="resource-list">
    <el-table
      :data="items"
      stripe
      style="width: 100%"
      empty-text="暂无数据"
      v-loading="loading"
    >
      <el-table-column prop="name" label="名称" min-width="200">
        <template #default="{ row }">
          <span class="resource-name" :title="row.name || row.infohash">
            {{ row.name || '未知资源' }}
          </span>
        </template>
      </el-table-column>

      <el-table-column prop="infohash" label="InfoHash" width="180">
        <template #default="{ row }">
          <el-tooltip :content="row.infohash" placement="top">
            <span class="infohash-cell">{{ row.infohash.substring(0, 16) }}...</span>
          </el-tooltip>
        </template>
      </el-table-column>

      <el-table-column prop="total_size" label="大小" width="110" align="center">
        <template #default="{ row }">
          <span v-if="row.total_size">{{ formatSize(row.total_size) }}</span>
          <span v-else style="color: #c0c4cc;">-</span>
        </template>
      </el-table-column>

      <el-table-column prop="status" label="状态" width="80" align="center">
        <template #default="{ row }">
          <el-tag :type="getStatusType(row.status)" size="small" effect="light">
            {{ getStatusLabel(row.status) }}
          </el-tag>
        </template>
      </el-table-column>

      <el-table-column label="健康度" width="130" align="center">
        <template #default="{ row }">
          <div class="health-indicator">
            <el-icon color="#67c23a" size="14"><ArrowUp /></el-icon>
            <span class="seeders">{{ row.seeders || 0 }}</span>
            <el-divider direction="vertical" />
            <el-icon color="#e6a23c" size="14"><ArrowDown /></el-icon>
            <span class="leechers">{{ row.leechers || 0 }}</span>
          </div>
        </template>
      </el-table-column>

      <el-table-column prop="download_count" label="下载量" width="100" align="center">
        <template #default="{ row }">
          <span class="download-count">{{ row.download_count || 0 }}</span>
        </template>
      </el-table-column>

      <el-table-column prop="first_seen" label="首次发现" width="100" align="center">
        <template #default="{ row }">
          <span v-if="row.first_seen">{{ formatTime(row.first_seen) }}</span>
          <span v-else style="color: #c0c4cc;">-</span>
        </template>
      </el-table-column>

      <el-table-column label="操作" width="100" align="center" fixed="right">
        <template #default="{ row }">
          <el-button type="primary" link size="small" @click="$emit('view-detail', row.infohash)">
            详情
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-pagination
      v-if="!hidePagination && total > 0"
      class="pagination"
      :current-page="page"
      :page-size="perPage"
      :total="total"
      layout="prev, pager, next"
      @current-change="val => $emit('page-change', val)"
    />
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  items: { type: Array, default: () => [] },
  total: { type: Number, default: 0 },
  page: { type: Number, default: 1 },
  perPage: { type: Number, default: 20 },
  hidePagination: { type: Boolean, default: false },
  loading: { type: Boolean, default: false }
})

defineEmits(['page-change', 'view-detail'])

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

function formatTime(timestamp) {
  if (!timestamp) return '-'
  const d = new Date(timestamp * 1000)
  const diff = Date.now() / 1000 - timestamp
  if (diff < 60) return '刚刚'
  if (diff < 3600) return Math.floor(diff / 60) + '分钟前'
  if (diff < 86400) return Math.floor(diff / 3600) + '小时前'
  return d.toLocaleDateString('zh-CN')
}

function getStatusType(status) {
  switch (status) {
    case 'parsed': return 'success'
    case 'pending': return 'warning'
    default: return 'info'
  }
}

function getStatusLabel(status) {
  switch (status) {
    case 'parsed': return '已解析'
    case 'pending': return '待解析'
    default: return status || '未知'
  }
}
</script>

<style scoped>
.resource-name {
  display: block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  cursor: default;
}

.infohash-cell {
  font-family: 'Consolas', 'Monaco', monospace;
  font-size: 12px;
  color: #606266;
  cursor: default;
}

.health-indicator {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 2px;
  font-size: 13px;
}

.health-indicator .seeders {
  color: #67c23a;
  font-weight: 600;
}

.health-indicator .leechers {
  color: #e6a23c;
  font-weight: 600;
}

.download-count {
  color: #409eff;
  font-weight: 600;
}

.pagination {
  margin-top: 15px;
  justify-content: center;
  display: flex;
}
</style>
