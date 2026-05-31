<template>
  <div class="dashboard">
    <el-row :gutter="20">
      <el-col :span="6">
        <el-card class="stat-card" shadow="hover">
          <div class="stat-content">
            <div class="stat-info">
              <p class="stat-label">用户总数</p>
              <p class="stat-value">{{ stats.userCount }}</p>
            </div>
            <i class="el-icon-user stat-icon" />
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card class="stat-card" shadow="hover">
          <div class="stat-content">
            <div class="stat-info">
              <p class="stat-label">角色数量</p>
              <p class="stat-value">{{ stats.roleCount }}</p>
            </div>
            <i class="el-icon-s-custom stat-icon" />
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card class="stat-card" shadow="hover">
          <div class="stat-content">
            <div class="stat-info">
              <p class="stat-label">今日操作</p>
              <p class="stat-value">{{ stats.todayOperations }}</p>
            </div>
            <i class="el-icon-s-operation stat-icon" />
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card class="stat-card" shadow="hover">
          <div class="stat-content">
            <div class="stat-info">
              <p class="stat-label">系统状态</p>
              <p class="stat-value status-text">
                <i :class="stats.systemStatus === 'normal' ? 'el-icon-success' : 'el-icon-warning'" />
                {{ stats.systemStatus === 'normal' ? '正常' : '异常' }}
              </p>
            </div>
            <i class="el-icon-monitor stat-icon" />
          </div>
        </el-card>
      </el-col>
    </el-row>
    <el-row :gutter="20" style="margin-top: 20px;">
      <el-col :span="12">
        <el-card shadow="hover">
          <div slot="header" class="card-header">
            <span>快速入口</span>
          </div>
          <div class="quick-links">
            <div class="link-item" @click="$router.push('/system-config')">
              <i class="el-icon-setting" />
              <span>系统配置</span>
            </div>
            <div class="link-item" @click="$router.push('/data-audit')">
              <i class="el-icon-document" />
              <span>数据审计</span>
            </div>
            <div class="link-item" @click="$router.push('/operation-log')">
              <i class="el-icon-tickets" />
              <span>操作日志</span>
            </div>
            <div class="link-item" @click="$router.push('/role-management')">
              <i class="el-icon-s-custom" />
              <span>角色分级</span>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="12">
        <el-card shadow="hover">
          <div slot="header" class="card-header">
            <span>最近活动</span>
          </div>
          <el-timeline>
            <el-timeline-item
              v-for="(activity, index) in recentActivities"
              :key="index"
              :timestamp="activity.time"
              placement="top">
              {{ activity.content }}
            </el-timeline-item>
          </el-timeline>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script>
export default {
  name: 'Dashboard',
  data() {
    return {
      stats: {
        userCount: 128,
        roleCount: 6,
        todayOperations: 342,
        systemStatus: 'normal'
      },
      recentActivities: [
        { time: '10:30', content: '管理员更新了系统配置' },
        { time: '09:45', content: '新增角色: 数据审计员' },
        { time: '09:15', content: '用户张三登录系统' },
        { time: '昨天', content: '完成了数据库备份' }
      ]
    };
  }
};
</script>

<style scoped>
.dashboard {
  padding: 0;
}
.stat-card {
  margin-bottom: 20px;
}
.stat-content {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.stat-info {
  flex: 1;
}
.stat-label {
  color: #999;
  font-size: 14px;
  margin: 0;
}
.stat-value {
  font-size: 28px;
  font-weight: bold;
  color: #333;
  margin: 10px 0 0;
}
.status-text {
  font-size: 18px;
}
.stat-icon {
  font-size: 48px;
  color: #409EFF;
  opacity: 0.8;
}
.card-header {
  font-weight: bold;
}
.quick-links {
  display: flex;
  flex-wrap: wrap;
  gap: 20px;
}
.link-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 20px;
  cursor: pointer;
  border-radius: 4px;
  transition: background-color 0.3s;
}
.link-item:hover {
  background-color: #f5f7fa;
}
.link-item i {
  font-size: 32px;
  color: #409EFF;
  margin-bottom: 10px;
}
.link-item span {
  font-size: 14px;
  color: #606266;
}
</style>
