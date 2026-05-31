<template>
  <div class="page-container">
    <div class="page-header">
      <div class="flex" style="gap: 12px; align-items: center">
        <el-button :icon="ArrowLeft" @click="$router.back()">返回</el-button>
        <span class="page-title">版本历史与对比</span>
      </div>
    </div>

    <el-row :gutter="16">
      <el-col :span="8">
        <el-card>
          <template #header>历史版本列表</template>
          <el-timeline>
            <el-timeline-item
              v-for="v in versions"
              :key="v.id"
              :timestamp="formatDate(v.createdAt)"
              :type="v.id === currentVersion?.id ? 'primary' : ''"
            >
              <div class="version-item">
                <div class="flex-between">
                  <span class="version-title">v{{ v.version }} {{ v.remark || '' }}</span>
                  <el-tag size="small" v-if="v.id === currentVersion?.id" type="success">当前</el-tag>
                </div>
                <div class="version-meta">
                  <span>作者：{{ v.author?.nickname || v.authorName }}</span>
                  <span>大小：{{ v.size }} bytes</span>
                </div>
                <div class="version-actions">
                  <el-button size="small" text @click="viewVersion(v)">查看</el-button>
                  <el-button size="small" text @click="compareWith(v)">对比</el-button>
                  <el-button size="small" text type="danger" @click="rollback(v)" :disabled="v.id === currentVersion?.id">
                    回滚至此版本
                  </el-button>
                </div>
              </div>
            </el-timeline-item>
          </el-timeline>
        </el-card>
      </el-col>

      <el-col :span="16">
        <el-card>
          <template #header>
            <div class="flex-between">
              <span>版本对比</span>
              <el-select
                v-model="leftVersionId"
                placeholder="选择版本A"
                size="small"
                style="width: 180px; margin-right: 8px"
              >
                <el-option v-for="v in versions" :key="v.id" :label="`v${v.version}`" :value="v.id" />
              </el-select>
              <span style="margin: 0 4px">vs</span>
              <el-select
                v-model="rightVersionId"
                placeholder="选择版本B"
                size="small"
                style="width: 180px; margin-left: 8px"
              >
                <el-option v-for="v in versions" :key="v.id" :label="`v${v.version}`" :value="v.id" />
              </el-select>
              <el-button size="small" type="primary" style="margin-left: 12px" @click="doCompare" :disabled="!leftVersionId || !rightVersionId">
                开始对比
              </el-button>
            </div>
          </template>
          <div class="compare-area">
            <template v-if="diffResult">
              <el-tabs v-model="activeField">
                <el-tab-pane v-for="f in fields" :key="f.key" :label="f.label" :name="f.key">
                  <div class="diff-view">
                    <div class="diff-line" v-for="(line, i) in diffResult[f.key]" :key="i" :class="lineClass(line)">
                      <span class="diff-num">{{ line.ln }}</span>
                      <span class="diff-prefix">{{ line.prefix }}</span>
                      <span class="diff-text">{{ line.text }}</span>
                    </div>
                    <el-empty v-if="!diffResult[f.key]?.length" description="无差异" />
                  </div>
                </el-tab-pane>
              </el-tabs>
            </template>
            <el-empty v-else description="请选择两个版本进行对比" />
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { ArrowLeft } from '@element-plus/icons-vue'
import * as Diff from 'diff'
import { versionApi } from '@/api/version'
import { formatDate } from '@/utils'

const route = useRoute()
const docId = computed(() => route.params.docId)
const versions = ref<any[]>([])
const currentVersion = ref<any>(null)
const leftVersionId = ref<number | string>('')
const rightVersionId = ref<number | string>('')
const diffResult = ref<any>(null)
const activeField = ref('content')

const fields = [
  { key: 'content', label: '文档描述' },
  { key: 'title', label: '标题' },
  { key: 'requestParams', label: '请求参数' },
  { key: 'requestBody', label: '请求体' },
  { key: 'response', label: '响应示例' }
]

async function fetchVersions() {
  try {
    const res = await versionApi.getList(docId.value)
    versions.value = res.data.list || res.data || []
    if (versions.value.length) {
      currentVersion.value = versions.value[0]
      leftVersionId.value = versions.value[1]?.id || ''
      rightVersionId.value = versions.value[0]?.id || ''
      if (leftVersionId.value && rightVersionId.value) doCompare()
    }
  } catch (e) {}
}

function viewVersion(v: any) {
  ElMessage.info(`查看版本 v${v.version}`)
}

function compareWith(v: any) {
  rightVersionId.value = v.id
  if (!leftVersionId.value) leftVersionId.value = currentVersion.value?.id
  if (leftVersionId.value && rightVersionId.value) doCompare()
}

async function doCompare() {
  try {
    const res = await versionApi.compare(docId.value, {
      leftVersionId: leftVersionId.value,
      rightVersionId: rightVersionId.value
    })
    const { left, right } = res.data
    const result: any = {}
    fields.forEach((f) => {
      const l = stringify(left?.[f.key])
      const r = stringify(right?.[f.key])
      const diff = Diff.diffLines(l, r)
      let ln = 1
      result[f.key] = diff.flatMap((part) =>
        part.value.split('\n').filter((x: string) => x).map((text: string) => ({
          ln: ln++,
          prefix: part.added ? '+' : part.removed ? '-' : ' ',
          text,
          type: part.added ? 'add' : part.removed ? 'remove' : 'same'
        }))
      )
    })
    diffResult.value = result
  } catch (e) {}
}

function stringify(v: any) {
  if (v == null) return ''
  if (typeof v === 'string') return v
  try {
    return JSON.stringify(v, null, 2)
  } catch (e) {
    return String(v)
  }
}

function lineClass(line: any) {
  return `diff-${line.type}`
}

async function rollback(v: any) {
  try {
    await ElMessageBox.confirm(`确认回滚到 v${v.version}？当前版本将被保存为历史记录`, '警告', { type: 'warning' })
    await versionApi.rollback(docId.value, v.id)
    ElMessage.success('回滚成功')
    fetchVersions()
  } catch (e) {}
}

onMounted(fetchVersions)
</script>

<style scoped>
.version-item {
  padding: 8px 0;
}
.version-title {
  font-weight: 500;
}
.version-meta {
  color: #909399;
  font-size: 12px;
  display: flex;
  gap: 12px;
  margin: 4px 0;
}
.version-actions {
  display: flex;
  gap: 8px;
}
.compare-area {
  min-height: 400px;
}
.diff-view {
  font-family: Menlo, Monaco, Consolas, monospace;
  font-size: 13px;
  line-height: 1.6;
  background: #fafafa;
  padding: 12px;
  border-radius: 4px;
  white-space: pre-wrap;
  word-break: break-all;
}
.diff-line {
  display: flex;
  padding: 2px 4px;
  border-radius: 2px;
}
.diff-num {
  width: 40px;
  color: #909399;
  user-select: none;
}
.diff-prefix {
  width: 20px;
}
.diff-text {
  flex: 1;
}
</style>
