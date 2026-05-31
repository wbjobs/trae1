<template>
  <div class="subapp-container">
    <div id="subapp-container"></div>
  </div>
</template>

<script>
import logger from '@/logger';

export default {
  name: 'SubappContainer',
  mounted() {
    logger.reportPageView(this.$route.path);
    logger.reportUserAction('view_subapp', this.$route.meta.app, {
      path: this.$route.path
    });
  }
};
</script>

<style scoped>
.subapp-container {
  height: 100%;
  width: 100%;
}
#subapp-container {
  height: 100%;
  width: 100%;
}
</style>
