<template>
  <div class="create-page">
    <div class="create-container">
      <div class="create-header">
        <el-button text :icon="ArrowLeft" @click="goBack">
          返回
        </el-button>
        <h2>创建新文档</h2>
      </div>

      <div class="create-form">
        <el-form
          ref="createForm"
          :model="form"
          :rules="rules"
          label-width="100px"
        >
          <el-form-item label="文档标题" prop="title">
            <el-input
              v-model="form.title"
              placeholder="请输入文档标题"
              maxlength="200"
              show-word-limit
              size="large"
            />
          </el-form-item>

          <el-form-item label="文档类型" prop="type">
            <el-radio-group v-model="form.type" size="large">
              <el-radio-button value="markdown">Markdown</el-radio-button>
              <el-radio-button value="txt">纯文本</el-radio-button>
            </el-radio-group>
          </el-form-item>

          <el-form-item label="文档标签">
            <el-select
              v-model="form.tags"
              multiple
              filterable
              allow-create
              default-first-option
              placeholder="输入标签后按回车添加"
              style="width: 100%"
            >
            </el-select>
          </el-form-item>

          <el-form-item label="文档内容" prop="content">
            <el-input
              v-model="form.content"
              type="textarea"
              :rows="15"
              placeholder="请输入文档内容..."
              maxlength="100000"
              show-word-limit
            />
          </el-form-item>

          <el-form-item>
            <el-button
              type="primary"
              size="large"
              :loading="isCreating"
              @click="handleCreate"
            >
              创建文档
            </el-button>
            <el-button size="large" @click="goBack">
              取消
            </el-button>
          </el-form-item>
        </el-form>
      </div>

      <div class="create-tips">
        <h4>提示</h4>
        <ul>
          <li>支持 Markdown 格式，可以使用标题、列表、代码块等语法</li>
          <li>创建后可以邀请他人协作批注</li>
          <li>所有修改都会自动保存历史版本</li>
        </ul>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { useDocumentStore } from '@/stores/document'
import { ElMessage } from 'element-plus'
import { ArrowLeft } from '@element-plus/icons-vue'

const router = useRouter()
const documentStore = useDocumentStore()

const createForm = ref(null)
const isCreating = ref(false)

const form = reactive({
  title: '',
  type: 'markdown',
  content: '',
  tags: []
})

const rules = {
  title: [
    { required: true, message: '请输入文档标题', trigger: 'blur' },
    { min: 1, max: 200, message: '标题长度为1-200个字符', trigger: 'blur' }
  ],
  type: [
    { required: true, message: '请选择文档类型', trigger: 'change' }
  ],
  content: [
    { required: true, message: '请输入文档内容', trigger: 'blur' },
    { min: 1, message: '内容不能为空', trigger: 'blur' }
  ]
}

const goBack = () => {
  router.back()
}

const handleCreate = async () => {
  try {
    await createForm.value.validate()
    
    isCreating.value = true
    const response = await documentStore.createDocument(form)
    
    ElMessage.success('文档创建成功')
    router.push(`/documents/${response.data._id}`)
  } catch (error) {
    if (error !== false) {
      console.error('Create document error:', error)
    }
  } finally {
    isCreating.value = false
  }
}
</script>

<style lang="scss" scoped>
.create-page {
  min-height: 100vh;
  background: #f5f7fa;
  padding: 24px;
}

.create-container {
  max-width: 900px;
  margin: 0 auto;
}

.create-header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 24px;

  h2 {
    margin: 0;
    font-size: 24px;
    font-weight: 600;
    color: #303133;
  }
}

.create-form {
  background: #fff;
  border-radius: 12px;
  padding: 32px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
}

.create-tips {
  margin-top: 24px;
  padding: 20px;
  background: #ecf5ff;
  border-radius: 8px;
  border: 1px solid #d9ecff;

  h4 {
    margin: 0 0 12px 0;
    color: #409EFF;
    font-size: 16px;
  }

  ul {
    margin: 0;
    padding-left: 20px;
    color: #606266;
    font-size: 14px;

    li {
      margin-bottom: 8px;

      &:last-child {
        margin-bottom: 0;
      }
    }
  }
}

@media (max-width: 768px) {
  .create-page {
    padding: 16px;
  }

  .create-header {
    h2 {
      font-size: 20px;
    }
  }

  .create-form {
    padding: 16px;
  }
}
</style>
