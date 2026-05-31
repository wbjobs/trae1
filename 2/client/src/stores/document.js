import { defineStore } from 'pinia'
import { getDocuments, getDocument, createDocument, updateDocument, deleteDocument, getDocumentVersions, restoreVersion } from '@/utils/api'

export const useDocumentStore = defineStore('document', {
  state: () => ({
    documents: [],
    currentDocument: null,
    versions: [],
    isLoading: false,
    pagination: {
      page: 1,
      limit: 20,
      total: 0,
      totalPages: 0
    }
  }),

  getters: {
    documentList: (state) => state.documents,
    hasCurrentDocument: (state) => !!state.currentDocument
  },

  actions: {
    async fetchDocuments(options = {}) {
      this.isLoading = true
      try {
        const response = await getDocuments({
          page: this.pagination.page,
          limit: this.pagination.limit,
          ...options
        })
        this.documents = response.data.documents
        this.pagination = {
          page: response.data.page,
          limit: response.data.limit,
          total: response.data.total,
          totalPages: response.data.totalPages
        }
        return response
      } finally {
        this.isLoading = false
      }
    },

    async fetchDocument(id) {
      this.isLoading = true
      try {
        const response = await getDocument(id)
        this.currentDocument = response.data
        return response
      } finally {
        this.isLoading = false
      }
    },

    async createDocument(data) {
      this.isLoading = true
      try {
        const response = await createDocument(data)
        return response
      } finally {
        this.isLoading = false
      }
    },

    async updateDocument(id, data, version = null) {
      this.isLoading = true
      try {
        const response = await updateDocument(id, data, version)
        if (this.currentDocument?._id === id) {
          this.currentDocument = response.data
        }
        return response
      } finally {
        this.isLoading = false
      }
    },

    async deleteDocument(id, version = null) {
      this.isLoading = true
      try {
        const response = await deleteDocument(id, version)
        this.documents = this.documents.filter(doc => doc._id !== id)
        return response
      } finally {
        this.isLoading = false
      }
    },

    async fetchVersions(documentId) {
      this.isLoading = true
      try {
        const response = await getDocumentVersions(documentId)
        this.versions = response.data.versions
        return response
      } finally {
        this.isLoading = false
      }
    },

    async restoreVersion(versionId) {
      this.isLoading = true
      try {
        const response = await restoreVersion(versionId)
        this.currentDocument = response.data
        return response
      } finally {
        this.isLoading = false
      }
    },

    clearCurrentDocument() {
      this.currentDocument = null
    }
  }
})
