<template>
  <div class="doc-comments">
    <div class="comments-header">
      <el-avatar :size="32" :src="currentUser?.avatar">
        {{ currentUser?.nickname?.[0] || 'U' }}
      </el-avatar>
      <el-input
        v-model="newContent"
        type="textarea"
        :rows="2"
        placeholder="输入批注内容，使用 @ 提及团队成员..."
        resize="none"
      />
      <el-button type="primary" :disabled="!newContent.trim()" :loading="submitting" @click="submitNew">
        发布
      </el-button>
    </div>

    <div v-if="!comments.length" class="empty-tip">
      <el-empty description="暂无批注，快来添加第一条吧" :image-size="80" />
    </div>

    <div class="comments-list">
      <div v-for="item in comments" :key="item.id" class="comment-item" :class="{ resolved: item.status === 'resolved' }">
        <div class="comment-row">
          <el-avatar :size="32" :src="item.author?.avatar">
            {{ item.author?.nickname?.[0] || 'U' }}
          </el-avatar>
          <div class="comment-main">
            <div class="comment-meta">
              <span class="comment-author">{{ item.author?.nickname || '未知用户' }}</span>
              <span class="comment-time">{{ formatTime(item.createdAt) }}</span>
              <el-tag v-if="item.status === 'resolved'" size="small" type="success" effect="plain">已解决</el-tag>
              <el-tag v-else-if="item.status === 'closed'" size="small" effect="plain">已关闭</el-tag>
              <el-tag v-if="item.anchor?.field" size="small" type="info" effect="plain" style="margin-left:4px">
                @ {{ item.anchor.field }}
              </el-tag>
            </div>
            <div class="comment-content" v-html="renderContent(item.content)"></div>
            <div class="comment-actions">
              <el-button size="small" text @click="toggleReply(item)">回复</el-button>
              <el-button size="small" text @click="doUpvote(item)">
                <el-icon><Star /></el-icon>
                {{ item.upvotes || 0 }}
              </el-button>
              <el-button
                v-if="canModify(item)"
                size="small"
                text
                type="success"
                @click="toggleResolved(item)"
              >
                {{ item.status === 'resolved' ? '取消解决' : '标记解决' }}
              </el-button>
              <el-button
                v-if="canModify(item)"
                size="small"
                text
                type="danger"
                @click="doDelete(item)"
              >
                删除
              </el-button>
            </div>

            <div v-if="replyingTo === item.id" class="reply-input">
              <el-input
                v-model="replyContent"
                type="textarea"
                :rows="1"
                placeholder="回复..."
                resize="none"
                @keydown.enter.ctrl="submitReply(item)"
              />
              <div>
                <el-button size="small" @click="replyingTo = null">取消</el-button>
                <el-button size="small" type="primary" :loading="submitting" @click="submitReply(item)">回复</el-button>
              </div>
            </div>

            <div v-if="item.children?.length" class="children">
              <div v-for="child in item.children" :key="child.id" class="comment-item child">
                <div class="comment-row">
                  <el-avatar :size="24" :src="child.author?.avatar">
                    {{ child.author?.nickname?.[0] || 'U' }}
                  </el-avatar>
                  <div class="comment-main">
                    <div class="comment-meta">
                      <span class="comment-author">{{ child.author?.nickname || '未知' }}</span>
                      <span class="comment-time">{{ formatTime(child.createdAt) }}</span>
                    </div>
                    <div class="comment-content" v-html="renderContent(child.content)"></div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Star } from '@element-plus/icons-vue'
import { marked } from 'marked'
import { docApi } from '@/api/doc'
import { useUserStore } from '@/stores/user'

const props = defineProps<{ docId: number | string }>()
const emit = defineEmits<{ (e: 'count-change', n: number): void }>()

const userStore = useUserStore()
const currentUser = computed(() => userStore.userInfo)

const comments = ref<any[]>([])
const newContent = ref('')
const replyContent = ref('')
const replyingTo = ref<number | null>(null)
const submitting = ref(false)

async function fetchComments() {
  try {
    const res = await docApi.getComments(props.docId)
    comments.value = res.data || []
    emit('count-change', countComments(comments.value))
  } catch (e) {}
}

function countComments(list: any[]): number {
  let n = 0
  for (const c of list) {
    n++
    if (c.children?.length) n += countComments(c.children)
  }
  return n
}

function renderContent(content: string): string {
  try { return marked(content) } catch (e) { return content }
}

function formatTime(t: any): string {
  if (!t) return ''
  const d = new Date(t)
  const now = new Date()
  const diff = (now.getTime() - d.getTime()) / 1000
  if (diff < 60) return '刚刚'
  if (diff < 3600) return `${Math.floor(diff / 60)} 分钟前`
  if (diff < 86400) return `${Math.floor(diff / 3600)} 小时前`
  if (diff < 604800) return `${Math.floor(diff / 86400)} 天前`
  return d.toLocaleDateString()
}

function canModify(item: any): boolean {
  if (!currentUser.value) return false
  return currentUser.value.role === 'admin' || currentUser.value.id === item.authorId
}

async function submitNew() {
  if (!newContent.value.trim()) return
  submitting.value = true
  try {
    await docApi.createComment(props.docId, { content: newContent.value.trim() })
    newContent.value = ''
    await fetchComments()
    ElMessage.success('批注已发布')
  } catch (e) {
    ElMessage.error('发布失败')
  } finally {
    submitting.value = false
  }
}

function toggleReply(item: any) {
  replyingTo.value = replyingTo.value === item.id ? null : item.id
  replyContent.value = ''
}

async function submitReply(item: any) {
  if (!replyContent.value.trim()) return
  submitting.value = true
  try {
    await docApi.createComment(props.docId, {
      content: replyContent.value.trim(),
      parentId: item.id
    })
    replyContent.value = ''
    replyingTo.value = null
    await fetchComments()
    ElMessage.success('回复成功')
  } catch (e) {
    ElMessage.error('回复失败')
  } finally {
    submitting.value = false
  }
}

async function doUpvote(item: any) {
  try {
    await docApi.upvoteComment(props.docId, item.id)
    item.upvotes = (item.upvotes || 0) + 1
  } catch (e) {}
}

async function toggleResolved(item: any) {
  const newStatus = item.status === 'resolved' ? 'open' : 'resolved'
  try {
    await docApi.updateComment(props.docId, item.id, { status: newStatus })
    item.status = newStatus
  } catch (e) {
    ElMessage.error('操作失败')
  }
}

async function doDelete(item: any) {
  try {
    await ElMessageBox.confirm('确定删除该批注？', '提示', { type: 'warning' })
    await docApi.deleteComment(props.docId, item.id)
    ElMessage.success('已删除')
    await fetchComments()
  } catch (e) {}
}

onMounted(() => {
  fetchComments()
})

watch(() => props.docId, () => {
  fetchComments()
})
</script>

<style scoped>
.doc-comments {
  padding: 8px 0;
}
.comments-header {
  display: flex;
  gap: 12px;
  align-items: flex-start;
  padding: 16px;
  background: #fafafa;
  border-radius: 8px;
  margin-bottom: 16px;
}
.comments-header :deep(.el-input) {
  flex: 1;
}
.comments-list {
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.comment-item {
  padding: 12px 16px;
  background: #fff;
  border-left: 3px solid #e4e7ed;
  border-radius: 4px;
  transition: border-color 0.2s;
}
.comment-item.resolved {
  border-left-color: #67c23a;
  opacity: 0.7;
}
.comment-item.child {
  padding: 8px 12px;
  background: #f9f9f9;
  margin-left: 20px;
  border-left: 2px solid #dcdfe6;
}
.comment-row {
  display: flex;
  gap: 12px;
}
.comment-main {
  flex: 1;
  min-width: 0;
}
.comment-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  margin-bottom: 4px;
}
.comment-author {
  font-weight: 600;
  color: #303133;
}
.comment-time {
  color: #909399;
  font-size: 12px;
}
.comment-content {
  font-size: 14px;
  line-height: 1.7;
  color: #303133;
  word-wrap: break-word;
}
.comment-content :deep(p) { margin: 0; }
.comment-actions {
  display: flex;
  gap: 4px;
  margin-top: 8px;
}
.reply-input {
  margin-top: 8px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.reply-input > div:last-child {
  display: flex;
  justify-content: flex-end;
  gap: 4px;
}
.children {
  margin-top: 10px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.empty-tip {
  padding: 32px 0;
}
</style>
