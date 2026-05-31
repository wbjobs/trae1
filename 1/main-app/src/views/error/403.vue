<template>
  <div class="error-page">
    <div class="error-content">
      <h1 class="error-code">403</h1>
      <p class="error-message">抱歉，您没有权限访问此页面</p>
      <el-button type="primary" @click="goBack">返回上一页</el-button>
      <el-button @click="$router.push('/')">返回首页</el-button>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Forbidden',
  methods: {
    goBack() {
      this.$router.back();
    }
  }
};
</script>

<style scoped>
.error-page {
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background-color: #f5f7fa;
}
.error-content {
  text-align: center;
}
.error-code {
  font-size: 120px;
  color: #409EFF;
  margin: 0;
}
.error-message {
  font-size: 18px;
  color: #606266;
  margin: 20px 0 30px;
}
</style>
