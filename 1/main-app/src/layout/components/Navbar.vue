<template>
  <div class="navbar">
    <div class="left-section">
      <i class="hamburger" :class="collapsed ? 'el-icon-s-unfold' : 'el-icon-s-fold'" @click="toggleSidebar" />
    </div>
    <div class="right-section">
      <el-dropdown trigger="click" @command="handleCommand">
        <span class="user-info">
          <el-avatar :size="32" :src="avatarUrl" />
          <span class="username">{{ username }}</span>
          <i class="el-icon-caret-bottom" />
        </span>
        <el-dropdown-menu slot="dropdown">
          <el-dropdown-item command="profile">个人中心</el-dropdown-item>
          <el-dropdown-item command="logout" divided>退出登录</el-dropdown-item>
        </el-dropdown-menu>
      </el-dropdown>
    </div>
  </div>
</template>

<script>
import { mapGetters } from 'vuex';

export default {
  name: 'Navbar',
  computed: {
    ...mapGetters(['userInfo']),
    collapsed() {
      return !this.$store.state.app.sidebar.opened;
    },
    username() {
      return this.userInfo?.username || '未登录';
    },
    avatarUrl() {
      return this.userInfo?.avatar || 'https://cube.elemecdn.com/0/88/03b0d39583f48206768a7534e55bcpng.png';
    }
  },
  methods: {
    toggleSidebar() {
      this.$store.dispatch('app/toggleSideBar');
    },
    handleCommand(command) {
      if (command === 'logout') {
        this.handleLogout();
      } else if (command === 'profile') {
        this.$router.push('/profile');
      }
    },
    handleLogout() {
      this.$confirm('确定要退出登录吗?', '提示', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      })
        .then(() => {
          this.$store.dispatch('user/logout').then(() => {
            this.$router.push('/login');
          });
        })
        .catch(() => {});
    }
  }
};
</script>

<style scoped>
.navbar {
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  background-color: #fff;
  box-shadow: 0 1px 4px rgba(0, 21, 41, 0.08);
}
.left-section {
  display: flex;
  align-items: center;
}
.hamburger {
  font-size: 20px;
  cursor: pointer;
  color: #606266;
}
.right-section {
  display: flex;
  align-items: center;
}
.user-info {
  display: flex;
  align-items: center;
  cursor: pointer;
  padding: 0 10px;
}
.username {
  margin: 0 8px;
  color: #606266;
  font-size: 14px;
}
</style>
