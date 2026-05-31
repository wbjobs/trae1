<template>
  <div class="search-bar">
    <el-form :inline="true" @submit.prevent="doSearch">
      <el-input
        v-model="query"
        placeholder="输入关键词搜索（文件名、资源名等）"
        clearable
        style="width: 400px;"
        size="large"
        :prefix-icon="Search"
        @keyup.enter="doSearch"
      />
      <el-button
        type="primary"
        size="large"
        :loading="loading"
        @click="doSearch"
      >
        搜索
      </el-button>
      <el-button
        size="large"
        @click="query = ''; $emit('search', null)"
      >
        清除
      </el-button>
    </el-form>
    <div class="search-tips">
      <el-tag type="info" size="small">支持模糊匹配</el-tag>
      <el-tag type="info" size="small">FTS5全文索引</el-tag>
      <el-tag type="info" size="small">中文/英文</el-tag>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { Search } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import { searchResources } from '../api/index.js'

const emit = defineEmits(['search'])

const query = ref('')
const loading = ref(false)

async function doSearch() {
  if (!query.value.trim()) {
    ElMessage.warning('请输入搜索关键词')
    return
  }

  loading.value = true
  try {
    const results = await searchResources(query.value.trim())
    emit('search', results)
    if (results.total === 0) {
      ElMessage.info('未找到匹配的资源')
    }
  } catch (e) {
    ElMessage.error('搜索失败: ' + (e.response?.data?.error || e.message))
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.search-bar {
  margin-bottom: 20px;
}

.search-tips {
  margin-top: 10px;
  display: flex;
  gap: 8px;
}
</style>
