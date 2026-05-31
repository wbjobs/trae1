<template>
  <div class="operation-log">
    <el-card>
      <div slot="header" class="card-header">
        <span>操作日志</span>
      </div>
      <el-row :gutter="20" class="stat-row">
        <el-col :span="6">
          <el-statistic title="今日操作数" :value="stats.todayCount" />
        </el-col>
        <el-col :span="6">
          <el-statistic title="本周操作数" :value="stats.weekCount" />
        </el-col>
        <el-col :span="6">
          <el-statistic title="本月操作数" :value="stats.monthCount" />
        </el-col>
        <el-col :span="6">
          <el-statistic title="异常操作数" :value="stats.errorCount" />
        </el-col>
      </el-row>
      <el-form :inline="true" :model="searchForm" class="search-form">
        <el-form-item label="模块">
          <el-select v-model="searchForm.module" placeholder="请选择" clearable>
            <el-option label="系统配置" value="system-config" />
            <el-option label="数据审计" value="data-audit" />
            <el-option label="角色分级" value="role-management" />
            <el-option label="用户管理" value="user" />
          </el-select>
        </el-form-item>
        <el-form-item label="操作人">
          <el-input v-model="searchForm.username" placeholder="请输入用户名" clearable />
        </el-form-item>
        <el-form-item label="操作类型">
          <el-select v-model="searchForm.action" placeholder="请选择" clearable>
            <el-option label="登录" value="login" />
            <el-option label="新增" value="create" />
            <el-option label="修改" value="update" />
            <el-option label="删除" value="delete" />
            <el-option label="导出" value="export" />
          </el-select>
        </el-form-item>
        <el-form-item label="时间">
          <el-date-picker
            v-model="searchForm.dateRange"
            type="daterange"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="search">查询</el-button>
          <el-button @click="reset">重置</el-button>
          <el-button type="success" @click="exportLogs">导出</el-button>
        </el-form-item>
      </el-form>
      <el-table :data="logs" border stripe>
        <el-table-column prop="id" label="ID" width="80" />
        <el-table-column prop="module" label="模块" width="120">
          <template slot-scope="scope">
            <el-tag size="small">{{ getModuleName(scope.row.module) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="action" label="操作类型" width="100">
          <template slot-scope="scope">
            <el-tag :type="getActionTag(scope.row.action)" size="small">
              {{ getActionName(scope.row.action) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="description" label="操作描述" show-overflow-tooltip />
        <el-table-column prop="username" label="操作人" width="120" />
        <el-table-column prop="ip" label="IP地址" width="140" />
        <el-table-column prop="userAgent" label="浏览器" width="200" show-overflow-tooltip />
        <el-table-column prop="status" label="状态" width="80">
          <template slot-scope="scope">
            <el-tag :type="scope.row.status === 'success' ? 'success' : 'danger'" size="small">
              {{ scope.row.status === 'success' ? '成功' : '失败' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="createdAt" label="操作时间" width="180" />
        <el-table-column label="操作" width="100">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="viewDetail(scope.row)">详情</el-button>
          </template>
        </el-table-column>
      </el-table>
      <el-pagination
        class="pagination"
        :current-page="pagination.page"
        :page-size="pagination.pageSize"
        :total="pagination.total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handlePageChange" />
    </el-card>

    <el-dialog title="日志详情" :visible.sync="detailVisible" width="600px">
      <el-descriptions :column="2" border>
        <el-descriptions-item label="模块">{{ getModuleName(currentDetail.module) }}</el-descriptions-item>
        <el-descriptions-item label="操作类型">{{ getActionName(currentDetail.action) }}</el-descriptions-item>
        <el-descriptions-item label="操作人">{{ currentDetail.username }}</el-descriptions-item>
        <el-descriptions-item label="IP地址">{{ currentDetail.ip }}</el-descriptions-item>
        <el-descriptions-item label="操作时间">{{ currentDetail.createdAt }}</el-descriptions-item>
        <el-descriptions-item label="状态">
          <el-tag :type="currentDetail.status === 'success' ? 'success' : 'danger'">
            {{ currentDetail.status === 'success' ? '成功' : '失败' }}
          </el-tag>
        </el-descriptions-item>
        <el-descriptions-item label="浏览器" :span="2">{{ currentDetail.userAgent }}</el-descriptions-item>
        <el-descriptions-item label="操作描述" :span="2">{{ currentDetail.description }}</el-descriptions-item>
        <el-descriptions-item label="请求参数" :span="2">
          <pre class="detail-pre">{{ formatJson(currentDetail.requestParams) }}</pre>
        </el-descriptions-item>
        <el-descriptions-item label="响应结果" :span="2">
          <pre class="detail-pre">{{ formatJson(currentDetail.responseData) }}</pre>
        </el-descriptions-item>
      </el-descriptions>
    </el-dialog>
  </div>
</template>

<script>
import axios from 'axios';

export default {
  name: 'OperationLog',
  data() {
    return {
      searchForm: {
        module: '',
        username: '',
        action: '',
        dateRange: []
      },
      logs: [],
      stats: {
        todayCount: 0,
        weekCount: 0,
        monthCount: 0,
        errorCount: 0
      },
      pagination: {
        page: 1,
        pageSize: 20,
        total: 0
      },
      detailVisible: false,
      currentDetail: {}
    };
  },
  mounted() {
    this.fetchLogs();
    this.fetchStats();
  },
  methods: {
    async fetchLogs() {
      try {
        const response = await axios.get('/api/operation-logs', {
          params: {
            ...this.searchForm,
            startDate: this.searchForm.dateRange?.[0],
            endDate: this.searchForm.dateRange?.[1],
            page: this.pagination.page,
            pageSize: this.pagination.pageSize
          }
        });
        this.logs = response.data?.logs || [];
        this.pagination.total = response.data?.total || 0;
      } catch (error) {
        this.$message.error('获取操作日志失败');
      }
    },
    async fetchStats() {
      try {
        const response = await axios.get('/api/operation-logs/stats');
        this.stats = response.data || this.stats;
      } catch (error) {
        console.error('获取统计数据失败');
      }
    },
    search() {
      this.pagination.page = 1;
      this.fetchLogs();
      this.reportAction('search_logs', this.searchForm);
    },
    reset() {
      this.searchForm = {
        module: '',
        username: '',
        action: '',
        dateRange: []
      };
      this.pagination.page = 1;
      this.fetchLogs();
    },
    async exportLogs() {
      try {
        this.$message.success('导出任务已提交，请稍后查看');
        this.reportAction('export_logs', {});
      } catch (error) {
        this.$message.error('导出失败');
      }
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size;
      this.fetchLogs();
    },
    handlePageChange(page) {
      this.pagination.page = page;
      this.fetchLogs();
    },
    viewDetail(row) {
      this.currentDetail = row;
      this.detailVisible = true;
    },
    getModuleName(module) {
      const map = {
        'system-config': '系统配置',
        'data-audit': '数据审计',
        'role-management': '角色分级',
        'user': '用户管理'
      };
      return map[module] || module;
    },
    getActionName(action) {
      const map = {
        login: '登录',
        create: '新增',
        update: '修改',
        delete: '删除',
        export: '导出'
      };
      return map[action] || action;
    },
    getActionTag(action) {
      const map = {
        login: 'primary',
        create: 'success',
        update: 'warning',
        delete: 'danger',
        export: 'info'
      };
      return map[action] || 'info';
    },
    formatJson(data) {
      if (!data) return '-';
      try {
        return JSON.stringify(typeof data === 'string' ? JSON.parse(data) : data, null, 2);
      } catch (e) {
        return String(data);
      }
    },
    reportAction(action, detail) {
      if (window.__POWERED_BY_QIANKUN__ && window.__LOGGER__) {
        window.__LOGGER__.reportUserAction(action, 'operation-log', detail);
      }
    }
  }
};
</script>

<style scoped>
.operation-log {
  padding: 0;
}
.card-header {
  font-weight: bold;
}
.stat-row {
  margin-bottom: 20px;
  padding: 20px;
  background-color: #f5f7fa;
  border-radius: 4px;
}
.search-form {
  margin-bottom: 20px;
}
.pagination {
  margin-top: 20px;
  text-align: right;
}
.detail-pre {
  max-height: 200px;
  overflow-y: auto;
  background-color: #f5f7fa;
  padding: 10px;
  border-radius: 4px;
  margin: 0;
}
</style>
