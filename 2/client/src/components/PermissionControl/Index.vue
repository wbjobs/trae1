<template>
  <div class="permission-control">
    <div class="permission-header">
      <h4>权限管理</h4>
      <el-button
        type="primary"
        size="small"
        :disabled="!canManage"
        @click="showAddDialog = true"
      >
        <el-icon><Plus /></el-icon>
        添加协作者
      </el-button>
    </div>

    <div class="permission-list">
      <div class="permission-item owner">
        <div class="user-info">
          <el-avatar :size="40" :src="owner?.avatar">
            {{ owner?.username?.charAt(0).toUpperCase() }}
          </el-avatar>
          <div class="user-details">
            <span class="username">{{ owner?.username }}</span>
            <span class="email">{{ owner?.email }}</span>
          </div>
        </div>
        <el-tag type="danger" size="small">所有者</el-tag>
      </div>

      <div
        v-for="collaborator in collaborators"
        :key="collaborator.user._id"
        class="permission-item"
      >
        <div class="user-info">
          <el-avatar :size="40" :src="collaborator.user.avatar">
            {{ collaborator.user.username?.charAt(0).toUpperCase() }}
          </el-avatar>
          <div class="user-details">
            <span class="username">{{ collaborator.user.username }}</span>
            <span class="email">{{ collaborator.user.email }}</span>
          </div>
        </div>
        <div class="permission-actions">
          <el-select
            v-model="collaborator.permission"
            size="small"
            :disabled="!canManage"
            @change="handlePermissionChange(collaborator)"
          >
            <el-option label="仅查看" value="view" />
            <el-option label="可批注" value="comment" />
            <el-option label="可编辑" value="edit" />
          </el-select>
          <el-button
            v-if="canManage"
            type="danger"
            text
            size="small"
            @click="handleRemove(collaborator.user._id)"
          >
            <el-icon><Delete /></el-icon>
          </el-button>
        </div>
      </div>

      <el-empty
        v-if="!collaborators.length"
        description="暂无协作者"
        :image-size="80"
      />
    </div>

    <div class="permission-summary">
      <h5>权限说明</h5>
      <div class="permission-description">
        <div class="desc-item">
          <el-tag type="info" size="small">查看</el-tag>
          <span>可查看文档内容和批注</span>
        </div>
        <div class="desc-item">
          <el-tag type="warning" size="small">批注</el-tag>
          <span>可查看和创建批注</span>
        </div>
        <div class="desc-item">
          <el-tag type="success" size="small">编辑</el-tag>
          <span>可编辑文档、创建和解决批注</span>
        </div>
      </div>
    </div>

    <el-dialog
      v-model="showAddDialog"
      title="添加协作者"
      width="480px"
      :close-on-click-modal="false"
    >
      <el-form :model="addForm" label-width="80px">
        <el-form-item label="用户">
          <el-select
            v-model="addForm.userId"
            filterable
            remote
            placeholder="搜索用户"
            :remote-method="searchUsers"
            :loading="searchLoading"
            style="width: 100%"
          >
            <el-option
              v-for="user in searchResults"
              :key="user._id"
              :label="`${user.username} (${user.email})`"
              :value="user._id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="权限">
          <el-select v-model="addForm.permission" style="width: 100%">
            <el-option label="仅查看" value="view" />
            <el-option label="可批注" value="comment" />
            <el-option label="可编辑" value="edit" />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showAddDialog = false">取消</el-button>
        <el-button type="primary" :loading="isAdding" @click="handleAdd">
          添加
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { addCollaborator, removeCollaborator } from '@/utils/api'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useUserStore } from '@/stores/user'

const props = defineProps({
  document: Object,
  permissions: Object
})

const emit = defineEmits(['update', 'add', 'remove'])

const userStore = useUserStore()
const showAddDialog = ref(false)
const isAdding = ref(false)
const searchLoading = ref(false)
const searchResults = ref([])

const addForm = ref({
  userId: '',
  permission: 'view'
})

const owner = computed(() => {
  if (!props.document?.owner) return null
  return typeof props.document.owner === 'string' 
    ? { _id: props.document.owner, username: '未知用户' }
    : props.document.owner
})

const collaborators = computed(() => {
  return props.document?.collaborators || []
})

const canManage = computed(() => {
  return props.document?.owner?._id === userStore.user?._id || 
         props.document?.owner === userStore.user?._id
})

const searchUsers = async (query) => {
  if (!query) {
    searchResults.value = []
    return
  }
  
  searchLoading.value = true
  try {
    searchResults.value = []
  } catch (error) {
    console.error('Search users error:', error)
  } finally {
    searchLoading.value = false
  }
}

const handlePermissionChange = async (collaborator) => {
  try {
    await addCollaborator(props.document._id, {
      userId: collaborator.user._id || collaborator.user,
      permission: collaborator.permission
    })
    ElMessage.success('权限更新成功')
    emit('update', collaborator)
  } catch (error) {
    console.error('Update permission error:', error)
  }
}

const handleRemove = async (userId) => {
  try {
    await ElMessageBox.confirm(
      '确定要移除该协作者吗？',
      '提示',
      {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    
    await removeCollaborator(props.document._id, userId)
    ElMessage.success('移除成功')
    emit('remove', userId)
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Remove collaborator error:', error)
    }
  }
}

const handleAdd = async () => {
  if (!addForm.value.userId) {
    ElMessage.warning('请选择用户')
    return
  }

  isAdding.value = true
  try {
    await addCollaborator(props.document._id, addForm.value)
    ElMessage.success('添加成功')
    emit('add', addForm.value)
    showAddDialog.value = false
    addForm.value = { userId: '', permission: 'view' }
  } catch (error) {
    console.error('Add collaborator error:', error)
  } finally {
    isAdding.value = false
  }
}

watch(() => props.document, () => {
  showAddDialog.value = false
  addForm.value = { userId: '', permission: 'view' }
})
</script>

<style lang="scss" scoped>
.permission-control {
  background: #fff;
  border-radius: 8px;
  padding: 24px;
}

.permission-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 24px;

  h4 {
    margin: 0;
    font-size: 18px;
    font-weight: 600;
    color: #303133;
  }
}

.permission-list {
  margin-bottom: 24px;
}

.permission-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px;
  border-radius: 8px;
  margin-bottom: 12px;
  background: #f5f7fa;
  transition: background 0.2s;

  &:hover {
    background: #ebeef5;
  }

  &.owner {
    background: #fef0f0;
  }

  .user-info {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .user-details {
    display: flex;
    flex-direction: column;

    .username {
      font-weight: 500;
      color: #303133;
    }

    .email {
      font-size: 12px;
      color: #909399;
    }
  }

  .permission-actions {
    display: flex;
    align-items: center;
    gap: 8px;
  }
}

.permission-summary {
  padding-top: 16px;
  border-top: 1px solid #ebeef5;

  h5 {
    margin: 0 0 12px 0;
    font-size: 14px;
    font-weight: 600;
    color: #303133;
  }
}

.permission-description {
  display: flex;
  flex-direction: column;
  gap: 8px;

  .desc-item {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 13px;
    color: #606266;
  }
}

@media (max-width: 768px) {
  .permission-control {
    padding: 16px;
  }

  .permission-item {
    flex-direction: column;
    align-items: flex-start;
    gap: 12px;

    .permission-actions {
      width: 100%;
      justify-content: space-between;
    }
  }

  .permission-header {
    flex-direction: column;
    align-items: flex-start;
    gap: 12px;
  }
}
</style>
