<template>
  <div class="page-container">
    <div class="page-header">
      <div class="page-title">项目管理</div>
      <div>
        <el-input
          v-model="keyword"
          placeholder="搜索项目"
          style="width: 240px; margin-right: 12px"
          clearable
          :prefix-icon="Search"
          @keyup.enter="fetchList"
        />
        <el-button type="primary" :icon="Plus" @click="openCreate">新建项目</el-button>
      </div>
    </div>

    <el-row :gutter="16">
      <el-col :span="8" v-for="item in list" :key="item.id">
        <el-card shadow="hover" class="project-card">
          <div class="project-header" @click="gotoDocs(item)">
            <div class="project-icon" :style="{ background: colors[item.id % colors.length] }">
              {{ item.name[0] }}
            </div>
            <div class="project-info">
              <div class="project-name">{{ item.name }}</div>
              <div class="project-desc">{{ item.description || '暂无描述' }}</div>
            </div>
          </div>
          <div class="project-footer">
            <span>文档数：{{ item.docCount || 0 }}</span>
            <span>更新于 {{ formatDate(item.updatedAt) }}</span>
            <div class="project-actions">
              <el-button size="small" text @click.stop="handleSync(item)" :disabled="!item.swaggerUrl">
                <el-icon><Refresh /></el-icon>同步
              </el-button>
              <el-button size="small" text @click.stop="openImport(item)">
                <el-icon><Upload /></el-icon>导入
              </el-button>
              <el-button size="small" text type="danger" @click.stop="handleDelete(item)">
                <el-icon><Delete /></el-icon>删除
              </el-button>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="8" v-if="!list.length">
        <el-empty description="暂无项目，点击右上角新建" />
      </el-col>
    </el-row>

    <el-dialog v-model="dialogVisible" :title="isEdit ? '编辑项目' : '新建项目'" width="500px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px">
        <el-form-item label="项目名称" prop="name">
          <el-input v-model="form.name" />
        </el-form-item>
        <el-form-item label="项目描述" prop="description">
          <el-input v-model="form.description" type="textarea" :rows="3" />
        </el-form-item>
        <el-form-item label="Swagger地址">
          <el-input v-model="form.swaggerUrl" placeholder="可选，填写后可自动同步接口" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="loading" @click="handleSubmit">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Search, Plus, Refresh, Upload, Delete } from '@element-plus/icons-vue'
import { projectApi, type ProjectData } from '@/api/project'
import { formatDate } from '@/utils'

const router = useRouter()
const keyword = ref('')
const list = ref<any[]>([])
const dialogVisible = ref(false)
const isEdit = ref(false)
const loading = ref(false)
const formRef = ref<FormInstance>()
const editingId = ref<number | string | null>(null)
const colors = ['#409EFF', '#67C23A', '#E6A23C', '#F56C6C', '#909399', '#9B59B6']
const importProject = ref<any>(null)

const form = reactive<ProjectData>({
  name: '',
  description: '',
  swaggerUrl: ''
})

const rules: FormRules = {
  name: [{ required: true, message: '请输入项目名称', trigger: 'blur' }]
}

async function fetchList() {
  const res = await projectApi.getList({ pageSize: 50, keyword: keyword.value })
  list.value = res.data.list || []
}

function openCreate() {
  isEdit.value = false
  editingId.value = null
  Object.assign(form, { name: '', description: '', swaggerUrl: '' })
  dialogVisible.value = true
}

function gotoDocs(item: any) {
  router.push(`/projects/${item.id}/docs`)
}

async function handleSubmit() {
  const valid = await formRef.value?.validate().catch(() => false)
  if (!valid) return
  loading.value = true
  try {
    if (isEdit.value && editingId.value) {
      await projectApi.update(editingId.value, form)
      ElMessage.success('更新成功')
    } else {
      await projectApi.create(form)
      ElMessage.success('创建成功')
    }
    dialogVisible.value = false
    fetchList()
  } finally {
    loading.value = false
  }
}

async function handleDelete(item: any) {
  try {
    await ElMessageBox.confirm(`确定删除项目「${item.name}」？该操作不可恢复`, '警告', {
      type: 'warning'
    })
    await projectApi.remove(item.id)
    ElMessage.success('已删除')
    fetchList()
  } catch (e) {}
}

async function handleSync(item: any) {
  try {
    await projectApi.syncSwagger(item.id)
    ElMessage.success('同步成功')
    fetchList()
  } catch (e) {}
}

async function openImport(item: any) {
  try {
    const { value } = await ElMessageBox.prompt('请输入 Swagger/OpenAPI 地址', '导入Swagger', {
      inputPlaceholder: 'http://xxx/v2/api-docs 或 https://xxx/openapi.json',
      inputValue: item.swaggerUrl || ''
    })
    if (value) {
      await projectApi.importSwagger(item.id, { swaggerUrl: value })
      ElMessage.success('导入成功')
      fetchList()
    }
  } catch (e) {}
}

onMounted(fetchList)
</script>

<style scoped>
.project-card {
  margin-bottom: 16px;
  cursor: pointer;
  transition: all 0.2s;
}
.project-card:hover {
  transform: translateY(-2px);
}
.project-header {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 12px;
}
.project-icon {
  width: 48px;
  height: 48px;
  border-radius: 8px;
  color: #fff;
  font-size: 20px;
  font-weight: bold;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
.project-info {
  flex: 1;
  min-width: 0;
}
.project-name {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
  margin-bottom: 4px;
}
.project-desc {
  color: #909399;
  font-size: 13px;
  overflow: hidden;
  text-overflow: ellipsis;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}
.project-footer {
  display: flex;
  align-items: center;
  gap: 12px;
  color: #909399;
  font-size: 12px;
  padding-top: 12px;
  border-top: 1px solid #f0f0f0;
}
.project-actions {
  margin-left: auto;
  display: flex;
  gap: 4px;
}
</style>
