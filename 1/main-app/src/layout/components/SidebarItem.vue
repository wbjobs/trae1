<template>
  <el-submenu
    v-if="hasChildren"
    :index="resolvePath(item.path)"
    popper-append-to-body>
    <template slot="title">
      <i :class="item.meta.icon" />
      <span>{{ item.meta.title }}</span>
    </template>
    <sidebar-item
      v-for="child in item.children"
      :key="child.path"
      :item="child"
      :base-path="resolvePath(child.path)" />
  </el-submenu>
  <el-menu-item
    v-else
    :index="resolvePath(item.path)">
    <i :class="item.meta.icon" />
    <span slot="title">{{ item.meta.title }}</span>
  </el-menu-item>
</template>

<script>
export default {
  name: 'SidebarItem',
  props: {
    item: {
      type: Object,
      required: true
    },
    basePath: {
      type: String,
      default: ''
    }
  },
  computed: {
    hasChildren() {
      return this.item.children && this.item.children.length > 0;
    }
  },
  methods: {
    resolvePath(routePath) {
      if (this.basePath && routePath.startsWith('/')) {
        return this.basePath + routePath;
      }
      return this.basePath + '/' + routePath;
    }
  }
};
</script>
