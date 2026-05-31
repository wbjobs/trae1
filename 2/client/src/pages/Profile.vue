<template>
  <div class="profile-page">
    <div class="profile-container">
      <div class="profile-header">
        <el-avatar :size="80" :src="user?.avatar">
          {{ user?.username?.charAt(0).toUpperCase() }}
        </el-avatar>
        <div class="header-info">
          <h2>{{ user?.username }}</h2>
          <p>{{ user?.email }}</p>
          <el-tag :type="getRoleTagType(user?.role)">
            {{ getRoleLabel(user?.role) }}
          </el-tag>
        </div>
      </div>

      <div class="profile-content">
        <el-tabs v-model="activeTab">
          <el-tab-pane label="基本信息" name="basic">
            <el-form :model="form" label-width="100px" class="profile-form">
              <el-form-item label="用户名">
                <el-input v-model="form.username" />
              </el-form-item>
              <el-form-item label="邮箱">
                <el-input v-model="form.email" />
              </el-form-item>
              <el-form-item label="头像URL">
                <el-input v-model="form.avatar" placeholder="输入头像图片链接" />
              </el-form-item>
              <el-form-item>
                <el-button
                  type="primary"
                  :loading="isUpdating"
                  @click="handleUpdateProfile"
                >
                  保存修改
                </el-button>
              </el-form-item>
            </el-form>
          </el-tab-pane>

          <el-tab-pane label="修改密码" name="password">
            <el-form
              ref="passwordForm"
              :model="passwordForm"
              :rules="passwordRules"
              label-width="100px"
              class="profile-form"
            >
              <el-form-item label="原密码" prop="oldPassword">
                <el-input
                  v-model="passwordForm.oldPassword"
                  type="password"
                  show-password
                />
              </el-form-item>
              <el-form-item label="新密码" prop="newPassword">
                <el-input
                  v-model="passwordForm.newPassword"
                  type="password"
                  show-password
                />
              </el-form-item>
              <el-form-item label="确认密码" prop="confirmPassword">
                <el-input
                  v-model="passwordForm.confirmPassword"
                  type="password"
                  show-password
                />
              </el-form-item>
              <el-form-item>
                <el-button
                  type="primary"
                  :loading="isChangingPassword"
                  @click="handleChangePassword"
                >
                  修改密码
                </el-button>
              </el-form-item>
            </el-form>
          </el-tab-pane>

          <el-tab-pane label="账号信息" name="account">
            <div class="account-info">
              <div class="info-item">
                <span class="label">用户ID</span>
                <span class="value">{{ user?._id }}</span>
              </div>
              <div class="info-item">
                <span class="label">注册时间</span>
                <span class="value">{{ formatTime(user?.createdAt) }}</span>
              </div>
              <div class="info-item">
                <span class="label">最后登录</span>
                <span class="value">{{ formatTime(user?.lastLoginAt) }}</span>
              </div>
              <div class="info-item">
                <span class="label">账号状态</span>
                <el-tag :type="user?.status === 'active' ? 'success' : 'danger'">
                  {{ user?.status === 'active' ? '正常' : '禁用' }}
                </el-tag>
              </div>
            </div>
          </el-tab-pane>
        </el-tabs>
      </div>

      <div class="profile-footer">
        <el-button type="danger" @click="handleLogout">
          退出登录
        </el-button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, computed } from 'vue'
import { useRouter } from 'vue-router'
import { useUserStore } from '@/stores/user'
import { changePassword } from '@/utils/api'
import { ElMessage } from 'element-plus'
import dayjs from '@/utils/dayjs'

const router = useRouter()
const userStore = useUserStore()

const user = computed(() => userStore.user)
const activeTab = ref('basic')
const isUpdating = ref(false)
const isChangingPassword = ref(false)
const passwordForm = ref(null)

const form = reactive({
  username: '',
  email: '',
  avatar: ''
})

const passwordForm = reactive({
  oldPassword: '',
  newPassword: '',
  confirmPassword: ''
})

const validateConfirmPassword = (rule, value, callback) => {
  if (value !== passwordForm.newPassword) {
    callback(new Error('两次输入的密码不一致'))
  } else {
    callback()
  }
}

const passwordRules = {
  oldPassword: [
    { required: true, message: '请输入原密码', trigger: 'blur' }
  ],
  newPassword: [
    { required: true, message: '请输入新密码', trigger: 'blur' },
    { min: 6, message: '密码至少6位', trigger: 'blur' }
  ],
  confirmPassword: [
    { required: true, message: '请确认新密码', trigger: 'blur' },
    { validator: validateConfirmPassword, trigger: 'blur' }
  ]
}

const getRoleLabel = (role) => {
  const labels = {
    admin: '管理员',
    editor: '编辑者',
    viewer: '查看者'
  }
  return labels[role] || role
}

const getRoleTagType = (role) => {
  const types = {
    admin: 'danger',
    editor: 'primary',
    viewer: 'info'
  }
  return types[role] || 'info'
}

const formatTime = (time) => {
  return time ? dayjs(time).format('YYYY-MM-DD HH:mm:ss') : '-'
}

const loadUserInfo = () => {
  if (user.value) {
    form.username = user.value.username
    form.email = user.value.email
    form.avatar = user.value.avatar || ''
  }
}

const handleUpdateProfile = async () => {
  isUpdating.value = true
  try {
    await userStore.updateProfile(form)
    ElMessage.success('更新成功')
  } catch (error) {
    console.error('Update profile error:', error)
  } finally {
    isUpdating.value = false
  }
}

const handleChangePassword = async () => {
  try {
    await passwordForm.value.validate()
    
    isChangingPassword.value = true
    await changePassword(passwordForm)
    ElMessage.success('密码修改成功')
    
    passwordForm.oldPassword = ''
    passwordForm.newPassword = ''
    passwordForm.confirmPassword = ''
  } catch (error) {
    if (error !== false) {
      console.error('Change password error:', error)
    }
  } finally {
    isChangingPassword.value = false
  }
}

const handleLogout = () => {
  userStore.logout()
  router.push('/login')
  ElMessage.success('已退出登录')
}

onMounted(() => {
  loadUserInfo()
})
</script>

<style lang="scss" scoped>
.profile-page {
  min-height: 100vh;
  background: #f5f7fa;
  padding: 24px;
}

.profile-container {
  max-width: 800px;
  margin: 0 auto;
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
  overflow: hidden;
}

.profile-header {
  display: flex;
  align-items: center;
  gap: 24px;
  padding: 32px;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: #fff;

  .header-info {
    h2 {
      margin: 0 0 8px 0;
      font-size: 24px;
      font-weight: 600;
    }

    p {
      margin: 0 0 8px 0;
      opacity: 0.9;
    }
  }
}

.profile-content {
  padding: 24px 32px;
}

.profile-form {
  max-width: 500px;
}

.account-info {
  display: flex;
  flex-direction: column;
  gap: 16px;

  .info-item {
    display: flex;
    align-items: center;
    padding: 12px 16px;
    background: #f5f7fa;
    border-radius: 8px;

    .label {
      width: 120px;
      color: #606266;
      font-size: 14px;
    }

    .value {
      flex: 1;
      color: #303133;
      font-size: 14px;
      word-break: break-all;
    }
  }
}

.profile-footer {
  padding: 24px 32px;
  border-top: 1px solid #ebeef5;
  text-align: right;
}

@media (max-width: 768px) {
  .profile-page {
    padding: 16px;
  }

  .profile-header {
    flex-direction: column;
    text-align: center;
    padding: 24px;

    .header-info {
      h2 {
        font-size: 20px;
      }
    }
  }

  .profile-content {
    padding: 16px;
  }

  .profile-footer {
    padding: 16px;
    text-align: center;
  }
}
</style>
