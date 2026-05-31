<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">上传视频</h1>
    </div>

    <el-row :gutter="20">
      <el-col :span="14">
        <el-card shadow="never">
          <el-form :model="form" label-width="100px" ref="formRef">
            <el-form-item label="视频标题" prop="title" :rules="[{ required: true, message: '请输入视频标题' }]">
              <el-input v-model="form.title" placeholder="请输入视频标题" maxlength="255" show-word-limit />
            </el-form-item>
            <el-form-item label="视频描述">
              <el-input
                v-model="form.description"
                type="textarea"
                :rows="3"
                placeholder="请输入视频描述（可选）"
                maxlength="1000"
                show-word-limit
              />
            </el-form-item>
            <el-form-item label="视频文件" prop="source_file" :rules="[{ required: true, message: '请选择视频文件' }]">
              <div
                class="upload-area"
                :class="{ dragover: isDragOver }"
                @click="triggerFileInput"
                @dragover.prevent="isDragOver = true"
                @dragleave="isDragOver = false"
                @drop.prevent="handleDrop"
              >
                <input
                  ref="fileInput"
                  type="file"
                  :accept="allowedExtensions.join(',')"
                  style="display: none"
                  @change="handleFileSelect"
                />
                <template v-if="!form.source_file">
                  <el-icon class="upload-icon"><UploadFilled /></el-icon>
                  <div class="upload-text">点击或拖拽视频文件到此区域上传</div>
                  <div class="upload-hint">支持 MP4、AVI、MOV、MKV、FLV、WMV、WebM、TS 格式，最大 2GB</div>
                </template>
                <template v-else>
                  <el-icon class="upload-icon" color="#67c23a"><CircleCheckFilled /></el-icon>
                  <div class="upload-text" style="color: #67c23a">已选择文件</div>
                  <div class="upload-hint">{{ form.source_file.name }} ({{ formatFileSize(form.source_file.size) }})</div>
                  <el-button size="small" type="danger" style="margin-top: 12px" @click.stop="clearFile">
                    重新选择
                  </el-button>
                </template>
              </div>
            </el-form-item>
            <el-form-item label="封面图片">
              <div
                class="upload-area cover-upload"
                @click="triggerCoverInput"
              >
                <input
                  ref="coverInput"
                  type="file"
                  accept="image/*"
                  style="display: none"
                  @change="handleCoverSelect"
                />
                <template v-if="!form.cover_image">
                  <el-icon class="upload-icon"><Picture /></el-icon>
                  <div class="upload-text">点击上传封面图片（可选）</div>
                </template>
                <template v-else>
                  <img :src="coverPreview" class="cover-preview" />
                </template>
              </div>
            </el-form-item>

            <el-divider content-position="left">水印设置</el-divider>

            <el-form-item label="启用水印">
              <el-switch v-model="form.enable_watermark" />
            </el-form-item>

            <template v-if="form.enable_watermark">
              <el-form-item label="水印位置">
                <el-radio-group v-model="form.watermark_position">
                  <el-radio value="bottom_right">右下角</el-radio>
                  <el-radio value="bottom_left">左下角</el-radio>
                  <el-radio value="top_right">右上角</el-radio>
                  <el-radio value="top_left">左上角</el-radio>
                </el-radio-group>
              </el-form-item>

              <el-form-item label="水印透明度">
                <el-slider
                  v-model="form.watermark_opacity"
                  :min="0.1"
                  :max="1"
                  :step="0.05"
                  :format-tooltip="(v) => Math.round(v * 100) + '%'"
                />
                <span style="margin-left: 12px; color: #909399">
                  {{ Math.round(form.watermark_opacity * 100) }}%
                </span>
              </el-form-item>

              <el-form-item label="水印字号">
                <el-input-number
                  v-model="form.watermark_font_size"
                  :min="12"
                  :max="72"
                  :step="2"
                />
              </el-form-item>

              <el-form-item label="刷新间隔">
                <el-input-number
                  v-model="form.watermark_refresh_interval"
                  :min="5"
                  :max="300"
                  :step="5"
                />
                <span style="margin-left: 8px; color: #909399">秒（水印中时间戳变化频率）</span>
              </el-form-item>

              <el-form-item label="自定义文本">
                <el-input
                  v-model="form.watermark_custom_text"
                  placeholder="留空则自动生成：用户名 + IP后4位 + 时间戳"
                  maxlength="100"
                  clearable
                />
              </el-form-item>

              <el-form-item label="水印预览">
                <div class="watermark-preview">
                  <div class="preview-video">
                    <div
                      class="preview-watermark"
                      :style="previewWatermarkStyle"
                    >
                      {{ previewWatermarkText }}
                    </div>
                  </div>
                </div>
              </el-form-item>
            </template>

            <el-form-item style="margin-top: 20px">
              <el-button type="primary" :loading="uploading" @click="handleUpload" :disabled="!canUpload">
                {{ uploading ? '上传中...' : '开始上传' }}
              </el-button>
              <el-button @click="resetForm">重置</el-button>
            </el-form-item>
          </el-form>

          <el-progress
            v-if="uploading"
            :percentage="uploadProgress"
            :stroke-width="10"
            style="margin-top: 20px"
          />
        </el-card>
      </el-col>

      <el-col :span="10">
        <el-card shadow="never">
          <template #header>
            <span>转码说明</span>
          </template>
          <el-steps direction="vertical" :active="currentStep" finish-status="success">
            <el-step title="上传视频文件" description="将视频源文件上传到服务器" />
            <el-step title="自动转码" description="自动转码为 720p 和 1080p 两个码率的 HLS 格式" />
            <el-step title="AES-128 加密" description="所有 TS 分片使用 AES-128 加密，密钥每分钟轮换" />
            <el-step title="水印固化" description="动态水印（用户名+IP+时间戳）永久固化在视频中" />
            <el-step title="准备就绪" description="转码完成后即可播放，支持多终端自适应码率" />
          </el-steps>

          <el-divider />

          <div class="tips">
            <h4>注意事项</h4>
            <ul>
              <li>上传后系统将自动进行转码处理，通常需要几分钟</li>
              <li>转码期间视频状态为"转码中"，完成后变为"已就绪"</li>
              <li>视频播放支持 720p 和 1080p 自适应切换</li>
              <li>所有视频分片均使用 AES-128 加密保护</li>
              <li>播放链接具有 5 分钟有效期且绑定客户端 IP</li>
              <li><strong>水印永久固化：</strong>水印在转码时烧录到每一帧，无法通过后期处理去除</li>
            </ul>
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { videoApi } from '@/api'

const router = useRouter()
const formRef = ref(null)
const fileInput = ref(null)
const coverInput = ref(null)
const isDragOver = ref(false)
const uploading = ref(false)
const uploadProgress = ref(0)
const coverPreview = ref('')

const allowedExtensions = ['.mp4', '.avi', '.mov', '.mkv', '.flv', '.wmv', '.webm', '.ts']

const form = ref({
  title: '',
  description: '',
  source_file: null,
  cover_image: null,
  enable_watermark: true,
  watermark_position: 'bottom_right',
  watermark_opacity: 0.3,
  watermark_font_size: 24,
  watermark_refresh_interval: 30,
  watermark_custom_text: '',
})

const canUpload = computed(() => {
  return form.value.title && form.value.source_file && !uploading.value
})

const currentStep = computed(() => {
  if (uploading.value) return 1
  return 0
})

const positionMap = {
  bottom_right: { bottom: '10px', right: '10px' },
  bottom_left: { bottom: '10px', left: '10px' },
  top_right: { top: '10px', right: '10px' },
  top_left: { top: '10px', left: '10px' },
}

const previewWatermarkStyle = computed(() => {
  const pos = positionMap[form.value.watermark_position] || positionMap['bottom_right']
  return {
    ...pos,
    opacity: form.value.watermark_opacity,
    fontSize: form.value.watermark_font_size + 'px',
  }
})

const previewWatermarkText = computed(() => {
  if (form.value.watermark_custom_text) {
    return form.value.watermark_custom_text + ' 2024-01-01 12:00'
  }
  return '当前用户 IP1234 2024-01-01 12:00'
})

function triggerFileInput() {
  fileInput.value?.click()
}

function triggerCoverInput() {
  coverInput.value?.click()
}

function handleFileSelect(e) {
  const file = e.target.files[0]
  if (!file) return
  validateAndSetFile(file)
}

function handleDrop(e) {
  isDragOver.value = false
  const file = e.dataTransfer.files[0]
  if (!file) return
  validateAndSetFile(file)
}

function validateAndSetFile(file) {
  const ext = '.' + file.name.split('.').pop().toLowerCase()
  if (!allowedExtensions.includes(ext)) {
    ElMessage.error(`不支持的文件格式: ${ext}`)
    return
  }
  if (file.size > 2 * 1024 * 1024 * 1024) {
    ElMessage.error('文件大小超过 2GB 限制')
    return
  }
  form.value.source_file = file
}

function handleCoverSelect(e) {
  const file = e.target.files[0]
  if (!file) return
  if (!file.type.startsWith('image/')) {
    ElMessage.error('请选择图片文件')
    return
  }
  form.value.cover_image = file
  coverPreview.value = URL.createObjectURL(file)
}

function clearFile() {
  form.value.source_file = null
  if (fileInput.value) fileInput.value.value = ''
}

function formatFileSize(bytes) {
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB'
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB'
}

async function handleUpload() {
  if (!form.value.title) {
    ElMessage.warning('请输入视频标题')
    return
  }
  if (!form.value.source_file) {
    ElMessage.warning('请选择视频文件')
    return
  }

  const formData = new FormData()
  formData.append('title', form.value.title)
  formData.append('description', form.value.description)
  formData.append('source_file', form.value.source_file)
  if (form.value.cover_image) {
    formData.append('cover_image', form.value.cover_image)
  }
  formData.append('enable_watermark', form.value.enable_watermark)
  if (form.value.enable_watermark) {
    formData.append('watermark_position', form.value.watermark_position)
    formData.append('watermark_opacity', form.value.watermark_opacity)
    formData.append('watermark_font_size', form.value.watermark_font_size)
    formData.append('watermark_refresh_interval', form.value.watermark_refresh_interval)
    if (form.value.watermark_custom_text) {
      formData.append('watermark_custom_text', form.value.watermark_custom_text)
    }
  }

  uploading.value = true
  uploadProgress.value = 0

  try {
    const res = await videoApi.upload(formData, (progress) => {
      uploadProgress.value = progress
    })
    ElMessage.success('视频上传成功，正在转码...')
    router.push('/admin/videos')
  } catch (e) {
    ElMessage.error('上传失败: ' + (e.response?.data?.detail || e.message))
  } finally {
    uploading.value = false
  }
}

function resetForm() {
  form.value = {
    title: '',
    description: '',
    source_file: null,
    cover_image: null,
    enable_watermark: true,
    watermark_position: 'bottom_right',
    watermark_opacity: 0.3,
    watermark_font_size: 24,
    watermark_refresh_interval: 30,
    watermark_custom_text: '',
  }
  coverPreview.value = ''
  uploadProgress.value = 0
  if (fileInput.value) fileInput.value.value = ''
  if (coverInput.value) coverInput.value.value = ''
}
</script>

<style scoped>
.cover-upload {
  height: 120px;
  padding: 20px;
}

.cover-preview {
  max-height: 100px;
  max-width: 100%;
  object-fit: contain;
  border-radius: 4px;
}

.tips {
  color: #606266;
}

.tips h4 {
  margin-bottom: 8px;
  color: #303133;
}

.tips ul {
  padding-left: 20px;
  line-height: 1.8;
  font-size: 13px;
}

.watermark-preview {
  width: 100%;
  display: flex;
  justify-content: center;
}

.preview-video {
  position: relative;
  width: 400px;
  height: 225px;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border-radius: 6px;
  overflow: hidden;
}

.preview-watermark {
  position: absolute;
  color: white;
  font-family: monospace;
  text-shadow: 1px 1px 2px rgba(0, 0, 0, 0.8);
  white-space: nowrap;
  pointer-events: none;
  padding: 2px 6px;
  background: rgba(0, 0, 0, 0.3);
  border-radius: 2px;
}
</style>
