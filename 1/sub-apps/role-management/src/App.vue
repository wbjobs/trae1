<template>
  <div class="role-management">
    <el-card>
      <div slot="header" class="card-header">
        <span>角色分级管理</span>
        <el-button type="primary" size="small" @click="showAddDialog">新增角色</el-button>
      </div>
      <el-table :data="roles" border stripe>
        <el-table-column prop="id" label="ID" width="80" />
        <el-table-column prop="name" label="角色名称" width="150" />
        <el-table-column prop="code" label="角色编码" width="150" />
        <el-table-column prop="level" label="等级" width="100">
          <template slot-scope="scope">
            <el-tag :type="getLevelTag(scope.row.level)">{{ getLevelName(scope.row.level) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="description" label="描述" show-overflow-tooltip />
        <el-table-column prop="userCount" label="用户数" width="100" />
        <el-table-column prop="createdAt" label="创建时间" width="180" />
        <el-table-column label="操作" width="200">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="editRole(scope.row)">编辑</el-button>
            <el-button type="text" size="small" @click="assignPermissions(scope.row)">分配权限</el-button>
            <el-button
              type="text"
              size="small"
              style="color: #F56C6C;"
              @click="deleteRole(scope.row)">
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog :title="dialogTitle" :visible.sync="dialogVisible" width="600px">
      <el-form :model="roleForm" :rules="roleRules" ref="roleForm" label-width="100px">
        <el-form-item label="角色名称" prop="name">
          <el-input v-model="roleForm.name" />
        </el-form-item>
        <el-form-item label="角色编码" prop="code">
          <el-input v-model="roleForm.code" :disabled="isEdit" />
        </el-form-item>
        <el-form-item label="角色等级" prop="level">
          <el-select v-model="roleForm.level">
            <el-option label="一级(最高)" :value="1" />
            <el-option label="二级" :value="2" />
            <el-option label="三级" :value="3" />
            <el-option label="四级" :value="4" />
            <el-option label="五级(最低)" :value="5" />
          </el-select>
        </el-form-item>
        <el-form-item label="描述">
          <el-input v-model="roleForm.description" type="textarea" :rows="3" />
        </el-form-item>
      </el-form>
      <div slot="footer">
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" @click="saveRole">确定</el-button>
      </div>
    </el-dialog>

    <el-dialog title="分配权限" :visible.sync="permissionDialogVisible" width="700px">
      <el-tree
        ref="permissionTree"
        :data="permissionTree"
        show-checkbox
        node-key="id"
        default-expand-all
        :default-checked-keys="checkedPermissions" />
      <div slot="footer">
        <el-button @click="permissionDialogVisible = false">取消</el-button>
        <el-button type="primary" @click="savePermissions">保存</el-button>
      </div>
    </el-dialog>
  </div>
</template>

<script>
import axios from 'axios';

export default {
  name: 'RoleManagement',
  data() {
    return {
      roles: [],
      dialogVisible: false,
      permissionDialogVisible: false,
      dialogTitle: '新增角色',
      isEdit: false,
      roleForm: {
        id: null,
        name: '',
        code: '',
        level: 3,
        description: ''
      },
      roleRules: {
        name: [
          { required: true, message: '请输入角色名称', trigger: 'blur' }
        ],
        code: [
          { required: true, message: '请输入角色编码', trigger: 'blur' }
        ],
        level: [
          { required: true, message: '请选择角色等级', trigger: 'change' }
        ]
      },
      permissionTree: [],
      checkedPermissions: [],
      currentRoleId: null
    };
  },
  mounted() {
    this.fetchRoles();
    this.fetchPermissionTree();
  },
  methods: {
    async fetchRoles() {
      try {
        const response = await axios.get('/api/roles');
        this.roles = response.data?.roles || [];
      } catch (error) {
        this.$message.error('获取角色列表失败');
      }
    },
    async fetchPermissionTree() {
      try {
        const response = await axios.get('/api/permissions/tree');
        this.permissionTree = response.data || [];
      } catch (error) {
        console.error('获取权限树失败');
      }
    },
    showAddDialog() {
      this.dialogTitle = '新增角色';
      this.isEdit = false;
      this.roleForm = {
        id: null,
        name: '',
        code: '',
        level: 3,
        description: ''
      };
      this.dialogVisible = true;
    },
    editRole(role) {
      this.dialogTitle = '编辑角色';
      this.isEdit = true;
      this.roleForm = { ...role };
      this.dialogVisible = true;
    },
    async saveRole() {
      this.$refs.roleForm.validate(async valid => {
        if (!valid) return;

        try {
          if (this.isEdit) {
            await axios.put(`/api/roles/${this.roleForm.id}`, this.roleForm);
            this.$message.success('角色更新成功');
          } else {
            await axios.post('/api/roles', this.roleForm);
            this.$message.success('角色创建成功');
          }
          this.dialogVisible = false;
          this.fetchRoles();
          this.reportAction(this.isEdit ? 'update_role' : 'create_role', this.roleForm);
        } catch (error) {
          this.$message.error('角色保存失败');
        }
      });
    },
    async deleteRole(role) {
      this.$confirm(`确定要删除角色 "${role.name}" 吗?`, '提示', {
        type: 'warning'
      }).then(async () => {
        try {
          await axios.delete(`/api/roles/${role.id}`);
          this.$message.success('角色删除成功');
          this.fetchRoles();
          this.reportAction('delete_role', { roleId: role.id });
        } catch (error) {
          this.$message.error('角色删除失败');
        }
      }).catch(() => {});
    },
    async assignPermissions(role) {
      this.currentRoleId = role.id;
      try {
        const response = await axios.get(`/api/roles/${role.id}/permissions`);
        this.checkedPermissions = (response.data || []).map(p => p.id);
        this.permissionDialogVisible = true;
      } catch (error) {
        this.$message.error('获取角色权限失败');
      }
    },
    async savePermissions() {
      const checkedKeys = this.$refs.permissionTree.getCheckedKeys();
      try {
        await axios.post(`/api/roles/${this.currentRoleId}/permissions`, {
          permissionIds: checkedKeys
        });
        this.$message.success('权限分配成功');
        this.permissionDialogVisible = false;
        this.reportAction('assign_permissions', {
          roleId: this.currentRoleId,
          permissionIds: checkedKeys
        });
      } catch (error) {
        this.$message.error('权限分配失败');
      }
    },
    getLevelTag(level) {
      const map = {
        1: 'danger',
        2: 'warning',
        3: '',
        4: 'success',
        5: 'info'
      };
      return map[level] || 'info';
    },
    getLevelName(level) {
      const map = {
        1: '一级',
        2: '二级',
        3: '三级',
        4: '四级',
        5: '五级'
      };
      return map[level] || '未知';
    },
    reportAction(action, detail) {
      if (window.__POWERED_BY_QIANKUN__ && window.__LOGGER__) {
        window.__LOGGER__.reportUserAction(action, 'role-management', detail);
      }
    }
  }
};
</script>

<style scoped>
.role-management {
  padding: 0;
}
.card-header {
  font-weight: bold;
  display: flex;
  justify-content: space-between;
  align-items: center;
}
</style>
