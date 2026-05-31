<template>
  <div class="system-config">
    <el-card>
      <div slot="header" class="card-header">
        <span>系统配置</span>
      </div>
      <el-tabs v-model="activeTab" type="border-card">
        <el-tab-pane label="基础配置" name="basic">
          <el-form :model="basicConfig" label-width="120px">
            <el-form-item label="系统名称">
              <el-input v-model="basicConfig.systemName" />
            </el-form-item>
            <el-form-item label="系统版本">
              <el-input v-model="basicConfig.version" />
            </el-form-item>
            <el-form-item label="系统描述">
              <el-input
                v-model="basicConfig.description"
                type="textarea"
                :rows="3" />
            </el-form-item>
            <el-form-item label="开启维护模式">
              <el-switch v-model="basicConfig.maintenanceMode" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="saveConfig">保存配置</el-button>
              <el-button @click="resetConfig">重置</el-button>
            </el-form-item>
          </el-form>
        </el-tab-pane>
        <el-tab-pane label="安全配置" name="security">
          <el-form :model="securityConfig" label-width="120px">
            <el-form-item label="密码最小长度">
              <el-input-number v-model="securityConfig.minPasswordLength" :min="6" :max="20" />
            </el-form-item>
            <el-form-item label="密码有效期(天)">
              <el-input-number v-model="securityConfig.passwordExpiryDays" :min="0" :max="365" />
            </el-form-item>
            <el-form-item label="登录失败锁定次数">
              <el-input-number v-model="securityConfig.loginFailLockCount" :min="3" :max="10" />
            </el-form-item>
            <el-form-item label="Session超时时间(分钟)">
              <el-input-number v-model="securityConfig.sessionTimeout" :min="15" :max="480" />
            </el-form-item>
            <el-form-item label="启用双因素认证">
              <el-switch v-model="securityConfig.enable2FA" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="saveConfig">保存配置</el-button>
            </el-form-item>
          </el-form>
        </el-tab-pane>
        <el-tab-pane label="通知配置" name="notification">
          <el-form :model="notificationConfig" label-width="120px">
            <el-form-item label="邮件服务器">
              <el-input v-model="notificationConfig.smtpServer" />
            </el-form-item>
            <el-form-item label="邮件端口">
              <el-input-number v-model="notificationConfig.smtpPort" :min="1" :max="65535" />
            </el-form-item>
            <el-form-item label="启用邮件通知">
              <el-switch v-model="notificationConfig.enableEmail" />
            </el-form-item>
            <el-form-item label="Webhook地址">
              <el-input v-model="notificationConfig.webhookUrl" placeholder="http://..." />
            </el-form-item>
            <el-form-item label="启用Webhook通知">
              <el-switch v-model="notificationConfig.enableWebhook" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="saveConfig">保存配置</el-button>
              <el-button @click="testNotification">测试通知</el-button>
            </el-form-item>
          </el-form>
        </el-tab-pane>
      </el-tabs>
    </el-card>
  </div>
</template>

<script>
import axios from 'axios';

export default {
  name: 'SystemConfig',
  data() {
    return {
      activeTab: 'basic',
      basicConfig: {
        systemName: '微前端管理平台',
        version: '1.0.0',
        description: '企业级微前端权限粒度化运营后台',
        maintenanceMode: false
      },
      securityConfig: {
        minPasswordLength: 8,
        passwordExpiryDays: 90,
        loginFailLockCount: 5,
        sessionTimeout: 30,
        enable2FA: false
      },
      notificationConfig: {
        smtpServer: 'smtp.example.com',
        smtpPort: 465,
        enableEmail: false,
        webhookUrl: '',
        enableWebhook: false
      }
    };
  },
  methods: {
    async saveConfig() {
      try {
        await axios.post('/api/system-config', {
          basic: this.basicConfig,
          security: this.securityConfig,
          notification: this.notificationConfig
        });
        this.$message.success('配置保存成功');
        this.reportAction('save_config', { tab: this.activeTab });
      } catch (error) {
        this.$message.error('配置保存失败');
      }
    },
    resetConfig() {
      this.$confirm('确定要重置当前标签页的配置吗?', '提示', {
        type: 'warning'
      }).then(() => {
        if (this.activeTab === 'basic') {
          this.basicConfig = {
            systemName: '微前端管理平台',
            version: '1.0.0',
            description: '企业级微前端权限粒度化运营后台',
            maintenanceMode: false
          };
        } else if (this.activeTab === 'security') {
          this.securityConfig = {
            minPasswordLength: 8,
            passwordExpiryDays: 90,
            loginFailLockCount: 5,
            sessionTimeout: 30,
            enable2FA: false
          };
        } else {
          this.notificationConfig = {
            smtpServer: 'smtp.example.com',
            smtpPort: 465,
            enableEmail: false,
            webhookUrl: '',
            enableWebhook: false
          };
        }
      }).catch(() => {});
    },
    testNotification() {
      this.$message.success('通知测试已发送');
      this.reportAction('test_notification', {});
    },
    reportAction(action, detail) {
      if (window.__POWERED_BY_QIANKUN__ && window.__LOGGER__) {
        window.__LOGGER__.reportUserAction(action, 'system-config', detail);
      }
    }
  }
};
</script>

<style scoped>
.system-config {
  padding: 0;
}
.card-header {
  font-weight: bold;
}
</style>
