<template>
  <div class="data-audit">
    <el-card>
      <div slot="header" class="card-header">
        <span>数据审计</span>
      </div>
      <el-form :inline="true" :model="searchForm" class="search-form">
        <el-form-item label="操作类型">
          <el-select v-model="searchForm.operationType" placeholder="请选择" clearable>
            <el-option label="新增" value="create" />
            <el-option label="修改" value="update" />
            <el-option label="删除" value="delete" />
            <el-option label="查询" value="query" />
          </el-select>
        </el-form-item>
        <el-form-item label="操作人">
          <el-input v-model="searchForm.operator" placeholder="请输入操作人" clearable />
        </el-form-item>
        <el-form-item label="时间范围">
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
        </el-form-item>
      </el-form>
      <el-table :data="auditLogs" border stripe>
        <el-table-column prop="id" label="ID" width="80" />
        <el-table-column prop="operationType" label="操作类型" width="100">
          <template slot-scope="scope">
            <el-tag :type="getOperationTypeTag(scope.row.operationType)">
              {{ getOperationTypeText(scope.row.operationType) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="tableName" label="数据表" width="150" />
        <el-table-column prop="recordId" label="记录ID" width="120" />
        <el-table-column prop="oldValue" label="修改前数据" show-overflow-tooltip />
        <el-table-column prop="newValue" label="修改后数据" show-overflow-tooltip />
        <el-table-column prop="operator" label="操作人" width="120" />
        <el-table-column prop="operationTime" label="操作时间" width="180" />
        <el-table-column prop="ip" label="IP地址" width="140" />
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
  </div>
</template>

<script>
import axios from 'axios';

export default {
  name: 'DataAudit',
  data() {
    return {
      searchForm: {
        operationType: '',
        operator: '',
        dateRange: []
      },
      auditLogs: [],
      pagination: {
        page: 1,
        pageSize: 20,
        total: 0
      }
    };
  },
  mounted() {
    this.fetchAuditLogs();
  },
  methods: {
    async fetchAuditLogs() {
      try {
        const response = await axios.get('/api/audit-logs', {
          params: {
            ...this.searchForm,
            startDate: this.searchForm.dateRange?.[0],
            endDate: this.searchForm.dateRange?.[1],
            page: this.pagination.page,
            pageSize: this.pagination.pageSize
          }
        });
        this.auditLogs = response.data?.logs || [];
        this.pagination.total = response.data?.total || 0;
      } catch (error) {
        this.$message.error('获取审计日志失败');
      }
    },
    search() {
      this.pagination.page = 1;
      this.fetchAuditLogs();
      this.reportAction('search_audit_logs', this.searchForm);
    },
    reset() {
      this.searchForm = {
        operationType: '',
        operator: '',
        dateRange: []
      };
      this.pagination.page = 1;
      this.fetchAuditLogs();
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size;
      this.fetchAuditLogs();
    },
    handlePageChange(page) {
      this.pagination.page = page;
      this.fetchAuditLogs();
    },
    getOperationTypeTag(type) {
      const map = {
        create: 'success',
        update: 'warning',
        delete: 'danger',
        query: 'info'
      };
      return map[type] || 'info';
    },
    getOperationTypeText(type) {
      const map = {
        create: '新增',
        update: '修改',
        delete: '删除',
        query: '查询'
      };
      return map[type] || type;
    },
    reportAction(action, detail) {
      if (window.__POWERED_BY_QIANKUN__ && window.__LOGGER__) {
        window.__LOGGER__.reportUserAction(action, 'data-audit', detail);
      }
    }
  }
};
</script>

<style scoped>
.data-audit {
  padding: 0;
}
.card-header {
  font-weight: bold;
}
.search-form {
  margin-bottom: 20px;
}
.pagination {
  margin-top: 20px;
  text-align: right;
}
</style>
