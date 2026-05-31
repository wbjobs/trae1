<template>
  <div>
    <el-timeline>
      <el-timeline-item
        v-for="v in list"
        :key="v.id"
        :timestamp="formatDate(v.createdAt)"
        :type="v.id === currentVersion?.id ? 'primary' : ''"
      >
        <div class="version-item">
          <div class="flex-between">
            <span class="version-title">v{{ v.version }} {{ v.remark || '' }}</span>
            <el-tag size="small" v-if="v.id === currentVersion?.id" type="success">当前</el-tag>
          </div>
          <div class="version-meta">
            <span>作者：{{ v.author?.nickname || v.authorName }}</span>
          </div>
          <div class="version-actions">
            <el-button size="small" text type="danger" @click="rollback(v)" :disabled="v.id === currentVersion?.id">
              回滚到此版本
            </el-button>
          </div>
        </div>
      </el-timeline-item>
    </el-timeline>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { versionApi } from '@/api/version'
import { formatDate } from '@/utils'

const props = defineProps<{ docId: number | string }>()
const emit = defineEmits<(e: 'rollback') => void>()

const list = ref<any[]>([])
const currentVersion = ref<any>(null)

async function fetchList() {
  try {
    const res = await versionApi.getList(props.docId)
    list.value = res.data.list || res.data || []
    currentVersion.value = list.value[0]
  } catch (e) {}
}

async function rollback(v: any) {
  try {
    await ElMessageBox.confirm(`确认回滚到 v${v.version}？`, '警告', { type: 'warning' })
    await versionApi.rollback(props.docId, v.id)
    ElMessage.success('回滚成功')
    emit('rollback')
    fetchList()
  } catch (e) {}
}

onMounted(fetchList)
watch(() => props.docId, fetchList)
</script>

<style scoped>
.version-item {
  padding: 4px 0;
}
.version-title {
  font-weight: 500;
}
.version-meta {
  color: #909399;
  font-size: 12px;
  margin: 4px 0;
}
</style>
