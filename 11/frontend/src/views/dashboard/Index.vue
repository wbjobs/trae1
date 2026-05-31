<template>
  <div class="page-container">
    <el-row :gutter="20" class="mb-16">
      <el-col :span="6" v-for="item in stats" :key="item.title">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" :style="{ background: item.color }">
            <el-icon :size="24"><component :is="item.icon" /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ item.value }}</div>
            <div class="stat-title">{{ item.title }}</div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20">
      <el-col :span="16">
        <el-card>
          <template #header>
            <div class="card-header">
              <span>最近编辑的文档</span>
              <router-link to="/projects" style="font-size: 12px; color: #409eff">查看全部</router-link>
            </div>
          </template>
          <el-table :data="recentDocs" style="width: 100%">
            <el-table-column prop="title" label="文档标题" />
            <el-table-column prop="method" label="方法" width="100">
              <template #default="{ row }">
                <span class="tag-method" :class="methodColor(row.method)">{{ row.method || 'GET' }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="path" label="路径" />
            <el-table-column prop="updatedAt" label="更新时间" width="180">
              <template #default="{ row }">{{ formatDate(row.updatedAt) }}</template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card>
          <template #header>
            <div class="card-header"><span>快捷操作</span></div>
          </template>
          <div class="quick-actions">
            <router-link to="/projects" class="quick-item">
              <el-icon :size="28"><FolderAdd /></el-icon>
              <span>新建项目</span>
            </router-link>
            <router-link to="/members" class="quick-item">
              <el-icon :size="28"><UserFilled /></el-icon>
              <span>邀请成员</span>
            </router-link>
            <div class="quick-item" @click="showImport = true">
              <el-icon :size="28"><Upload /></el-icon>
              <span>导入Swagger</span>
            </div>
            <div class="quick-item" @click="showDoc = true">
              <el-icon :size="28"><DocumentAdd /></el-icon>
              <span>新建文档</span>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-dialog v-model="showImport" title="导入Swagger" width="500px">
      <el-form :model="importForm" label-width="100px">
        <el-form-item label="项目">
          <el-select v-model="importForm.projectId" placeholder="请选择项目" style="width: 100%">
            <el-option v-for="p in projects" :key="p.id" :label="p.name" :value="p.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="Swagger地址">
          <el-input v-model="importForm.swaggerUrl" placeholder="例如 http://xxx/v2/api-docs" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showImport = false">取消</el-button>
        <el-button type="primary" @click="handleImport">开始导入</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, markRaw } from 'vue'
import { ElMessage } from 'element-plus'
import { Document, Folder, User, DataAnalysis, FolderAdd, UserFilled, Upload, DocumentAdd } from '@element-plus/icons-vue'
import { projectApi } from '@/api/project'
import { formatDate, methodColor } from '@/utils'

const stats = ref([
  { title: '项目总数', value: 0, icon: markRaw(Folder), color: '#409EFF' },
  { title: '文档总数', value: 0, icon: markRaw(Document), color: '#67C23A' },
  { title: '团队成员', value: 0, icon: markRaw(User), color: '#E6A23C' },
  { title: '今日更新', value: 0, icon: markRaw(DataAnalysis), color: '#F56C6C' }
])
const recentDocs = ref<any[]>([])
const projects = ref<any[]>([])
const showImport = ref(false)
const showDoc = ref(false)
const importForm = ref({ projectId: null as any, swaggerUrl: '' })

onMounted(async () => {
  try {
    const res = await projectApi.getList({ pageSize: 50 })
    const list = res.data.list || []
    projects.value = list
    stats.value[0].value = list.length
    stats.value[1].value = list.reduce((s: number, p: any) => s + (p.docCount || 0), 0)
    stats.value[2].value = 12
    stats.value[3].value = 5
    recentDocs.value = [
      { id: 1, title: '用户登录接口', method: 'POST', path: '/api/auth/login', updatedAt: new Date().toISOString() },
      { id: 2, title: '获取用户列表', method: 'GET', path: '/api/users', updatedAt: new Date().toISOString() }
    ]
  } catch (e) {}
})

async function handleImport() {
  if (!importForm.value.projectId || !importForm.value.swaggerUrl) {
    return ElMessage.warning('请选择项目并填写Swagger地址')
  }
  try {
    await projectApi.importSwagger(importForm.value.projectId, {
      swaggerUrl: importForm.value.swaggerUrl
    })
    ElMessage.success('导入成功')
    showImport.value = false
  } catch (e) {}
}
</script>

<style scoped>
.stat-card {
  display: flex;
  align-items: center;
  padding: 20px;
}
.stat-card :deep(.el-card__body) {
  display: flex;
  align-items: center;
  gap: 16px;
  width: 100%;
}
.stat-icon {
  width: 56px;
  height: 56px;
  border-radius: 8px;
  color: #fff;
  display: flex;
  align-items: center;
  justify-content: center;
}
.stat-value {
  font-size: 24px;
  font-weight: 600;
  color: #303133;
}
.stat-title {
  color: #909399;
  font-size: 13px;
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-weight: 600;
}
.quick-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
.quick-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 20px;
  border: 1px solid #ebeef5;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  color: #606266;
}
.quick-item:hover {
  border-color: #409eff;
  color: #409eff;
  transform: translateY(-2px);
}
.quick-item span {
  margin-top: 8px;
  font-size: 13px;
}
</style>
