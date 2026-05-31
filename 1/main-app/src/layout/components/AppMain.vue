<template>
  <div class="app-main">
    <section class="main-section">
      <router-view v-if="!$route.meta.app" />
      <div v-else id="subapp-container"></div>
    </section>
  </div>
</template>

<script>
export default {
  name: 'AppMain'
};
</script>

<style scoped>
.app-main {
  height: 100%;
  width: 100%;
}
.main-section {
  height: 100%;
  width: 100%;
  background-color: #fff;
  border-radius: 4px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
}
#subapp-container {
  height: 100%;
  width: 100%;
}
</style>
