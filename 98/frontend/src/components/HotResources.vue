<template>
  <div class="hot-resources">
    <div class="hot-header">
      <el-icon :size="24" color="#e6a23c"><TrendCharts /></el-icon>
      <h3>热门资源排行 (最近24小时下载量)</h3>
    </div>

    <el-table
      :data="resources"
      stripe
      style="width: 100%"
      empty-text="暂无数据"
    >
      <el-table-column type="index" label="排名" width="70" align="center">
        <template #default="{ $index }">
          <el-tag
            v-if="$index < 3"
            :type="['danger', 'warning', 'success'][$index]"
            size="large"
            effect="dark"
          >
            #{{ $index + 1 }}
          </el-tag>
          <span v-else class="rank-other">#{{ $index + 1 }}</span>
        </template>
      </el-table-column>

      <el-table-column prop="name" label="名称" min-width="250">
        <template #default="{ row }">
          <span class="hot-name" :title="row.name || row.infohash">
            {{ row.name || '未知资源' }}
          </span>
        </template>
      </el-table-column>

      <el-table-column prop="infohash" label="InfoHash" width="160">
        <template #default="{ row }">
          <el-tooltip :content="row.infohash" placement="top">
            <span class="infohash-cell">{{ row.infohash.substring(0, 16) }}...</span>
          </el-tooltip>
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

      <el-table-column prop="recent_downloads" label="24h下载量" width="130" align="center">
        <template #default="{ row }">
          <el-tag type="danger" size="small" effect="dark">
            {{ row.recent_downloads || 0 }}
          </el-tag>
        </template>
      </el-table-column>

      <el-table-column prop="total_size" label="大小" width="110" align="center">
        <template #default="{ row }">
          <span v-if="row.total_size">{{ formatSize(row.total_size) }}</span>
          <span v-else style="color: #c0c4cc;">-</span>
        </template>
      </el-table-column>

      <el-table-column label="操作" width="80" align="center" fixed="right">
        <template #default="{ row }">
          <el-button type="primary" link size="small" @click="$emit('view-detail', row.infohash)">
            详情
          </el-button>
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script setup>
defineProps({
  resources: { type: Array, default: () => [] }
})

defineEmits(['view-detail'])

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
.hot-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 20px;
}

.hot-header h3 {
  margin: 0;
  font-size: 18px;
  color: #303133;
}

.hot-name {
  display: block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.infohash-cell {
  font-family: 'Consolas', 'Monaco', monospace;
  font-size: 12px;
  color: #606266;
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

.rank-other {
  color: #909399;
  font-weight: 600;
}
</style>
