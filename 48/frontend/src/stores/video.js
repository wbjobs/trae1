import { defineStore } from 'pinia'
import { videoApi } from '@/api'

export const useVideoStore = defineStore('video', {
  state: () => ({
    videos: [],
    currentVideo: null,
    loading: false,
    stats: null,
    statsList: [],
    playbackRecord: null,
    playbackTimer: null,
    watchedDuration: 0,
    currentResolution: '',
    currentBitrate: 0,
    keyIndex: null,
    minKeyIndex: null,
    maxKeyIndex: null,
  }),

  actions: {
    async fetchVideos(params = {}) {
      this.loading = true
      try {
        const res = await videoApi.list(params)
        this.videos = res.data.results || res.data
        return this.videos
      } finally {
        this.loading = false
      }
    },

    async fetchVideo(id) {
      this.loading = true
      try {
        const res = await videoApi.get(id)
        this.currentVideo = res.data
        return this.currentVideo
      } finally {
        this.loading = false
      }
    },

    async uploadVideo(formData, onProgress) {
      const res = await videoApi.upload(formData, onProgress)
      return res.data
    },

    async deleteVideo(id) {
      await videoApi.delete(id)
      this.videos = this.videos.filter((v) => v.id !== id)
    },

    async retryTranscode(id) {
      const res = await videoApi.retryTranscode(id)
      return res.data
    },

    async fetchStats(videoId = null) {
      const res = await videoApi.getStats(videoId)
      this.stats = res.data
      return this.stats
    },

    async fetchStatsList() {
      const res = await videoApi.getStatsList()
      this.statsList = res.data
      return this.statsList
    },

    setKeyInfo(keyIndex, minKeyIndex, maxKeyIndex) {
      this.keyIndex = keyIndex
      this.minKeyIndex = minKeyIndex
      this.maxKeyIndex = maxKeyIndex
    },

    startPlaybackTracking(recordId, videoId) {
      this.stopPlaybackTracking()
      this.playbackRecord = { id: recordId, videoId }
      this.watchedDuration = 0
      this.playbackTimer = setInterval(() => {
        this.watchedDuration += 1
        if (this.watchedDuration % 5 === 0) {
          this.sendPlaybackUpdate()
        }
      }, 1000)
    },

    stopPlaybackTracking() {
      if (this.playbackTimer) {
        clearInterval(this.playbackTimer)
        this.playbackTimer = null
      }
    },

    async sendPlaybackUpdate() {
      if (!this.playbackRecord) return
      try {
        await videoApi.playbackUpdate(this.playbackRecord.videoId, {
          record_id: this.playbackRecord.id,
          watched_duration: this.watchedDuration,
          resolution: this.currentResolution,
          avg_bitrate: this.currentBitrate,
        })
      } catch (e) {
        console.error('Playback update error:', e)
      }
    },

    async endPlayback(isCompleted = false) {
      if (!this.playbackRecord) return
      try {
        await videoApi.playbackEnd(this.playbackRecord.videoId, {
          record_id: this.playbackRecord.id,
          watched_duration: this.watchedDuration,
          is_completed: isCompleted,
          avg_bitrate: this.currentBitrate,
        })
      } catch (e) {
        console.error('Playback end error:', e)
      }
      this.stopPlaybackTracking()
      this.playbackRecord = null
    },
  },
})
