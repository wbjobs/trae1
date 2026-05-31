<template>
  <div class="page-container">
    <div class="page-header">
      <div class="page-title">成员管理</div>
      <div>
        <el-select v-model="projectId" placeholder="选择项目" style="width: 200px; margin-right: 12px" @change="fetchList">
          <el-option v-for="p in projects" :key="p.id" :label="p.name" :value="p.id" />
        </el-select>
        <el-button type="primary" :icon="Plus" @click="showInvite = true">邀请成员</el-button>
      </div>
    </div>

    <el-card>
      <el-table :data="list" v-loading="loading">
        <el-table-column label="成员" min-width="220">
          <template #default="{ row }">
            <div class="flex" style="gap: 12px; align-items: center">
              <el-avatar :size="36" :src="row.avatar">{{ row.nickname?.[0] }}</el-avatar>
              <div>
                <div style="font-weight: 500">{{ row.nickname }}</div>
                <div style="color: #909399; font-size: 12px">{{ row.email }}</div>
              </div>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="username" label="账号" width="140" />
        <el-table-column label="角色" width="160">
          <template #default="{ row }">
            <el-select v-model="row.role" size="small" @change="(v) => changeRole(row, v)">
              <el-option label="管理员" value="admin" />
              <el-option label="编辑者" value="editor" />
              <el-option label="只读" value="viewer" />
            </el-select>
          </template>
        </el-table-column>
        <el-table-column label="权限" min-width="240">
          <template #default="{ row }">
            <div>
              <el-tag v-for="p in permissionMap[row.role] || []" :key="p" style="margin: 2px 4px 2px 0">
                {{ p }}
              </el-tag>
            </div>
          </template>
        </el-table-column>
        <el-table-column label="加入时间" prop="joinedAt" width="180">
          <template #default="{ row }">{{ formatDate(row.joinedAt) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="140">
          <template #default="{ row }">
            <el-button size="small" text type="danger" @click="removeMember(row)">移除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog v-model="showInvite" title="邀请成员" width="500px">
      <el-form :model="inviteForm" label-width="90px">
        <el-form-item label="邮箱">
          <el-input v-model="inviteForm.email" placeholder="example@company.com" />
        </el-form-item>
        <el-form-item label="角色">
          <el-select v-model="inviteForm.role" style="width: 100%">
            <el-option label="管理员" value="admin" />
            <el-option label="编辑者" value="editor" />
            <el-option label="只读" value="viewer" />
          </el-select>
        </el-form-item>
        <el-form-item label="所属项目">
          <el-select v-model="inviteForm.projectId" style="width: 100%">
            <el-option label="平台成员" :value="null" />
            <el-option v-for="p in projects" :key="p.id" :label="p.name" :value="p.id" />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showInvite = false">取消</el-button>
        <el-button type="primary" @click="submitInvite">发送邀请</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus } from '@element-plus/icons-vue'
import { memberApi } from '@/api/member'
import { projectApi } from '@/api/project'
import { formatDate } from '@/utils'

const list = ref<any[]>([])
const projects = ref<any[]>([])
const loading = ref(false)
const showInvite = ref(false)
const projectId = ref<number | string | null>(null)
const inviteForm = reactive({ email: '', role: 'editor', projectId: null as any })

const permissionMap: Record<string, string[]> = {
  admin: ['全部权限'],
  editor: ['查看', '编辑', '调试'],
  viewer: ['查看']
}

async function fetchList() {
  loading.value = true
  try {
    const res = projectId.value
      ? await memberApi.getProjectMembers(projectId.value)
      : await memberApi.getList()
    list.value = res.data.list || res.data || []
  } catch (e) {} finally {
    loading.value = false
  }
}

async function fetchProjects() {
  try {
    const res = await projectApi.getList({ pageSize: 200 })
    projects.value = res.data.list || []
  } catch (e) {}
}

async function changeRole(row: any, role: string) {
  try {
    await memberApi.updateRole(row.id, { role })
    ElMessage.success('角色已更新')
  } catch (e) {
    fetchList()
  }
}

async function removeMember(row: any) {
  try {
    await ElMessageBox.confirm(`确认移除成员「${row.nickname}」？`, '警告', { type: 'warning' })
    await memberApi.remove(row.id)
    ElMessage.success('已移除')
    fetchList()
  } catch (e) {}
}

async function submitInvite() {
  if (!inviteForm.email) return ElMessage.warning('请填写邮箱')
  try {
    await memberApi.invite(inviteForm)
    ElMessage.success('邀请已发送')
    showInvite.value = false
    fetchList()
  } catch (e) {}
}

onMounted(() => {
  fetchProjects()
  fetchList()
})
</script>
