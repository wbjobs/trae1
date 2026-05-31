import { defineStore } from 'pinia'
import { projectApi } from '@/api/project'

interface Project {
  id: number
  name: string
  description: string
  swaggerUrl?: string
  createdAt: string
  updatedAt: string
}

export const useProjectStore = defineStore('project', {
  state: () => ({
    projectList: [] as Project[],
    currentProject: null as Project | null,
    loading: false
  }),
  actions: {
    async fetchProjects() {
      this.loading = true
      try {
        const res = await projectApi.getList()
        this.projectList = res.data.list
        return res.data.list
      } finally {
        this.loading = false
      }
    },
    setCurrent(project: Project) {
      this.currentProject = project
    }
  }
})
