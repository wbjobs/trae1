import { Injectable, NotFoundException, ForbiddenException } from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import axios from 'axios'
import { Doc } from './doc.entity'
import { CreateDocDto, UpdateDocDto, DebugDto, BatchImportDto, ExportDocsDto } from './dto'
import { DocVersion } from '../versions/doc-version.entity'

@Injectable()
export class DocsService {
  constructor(
    @InjectRepository(Doc)
    private readonly docRepo: Repository<Doc>,
    @InjectRepository(DocVersion)
    private readonly versionRepo: Repository<DocVersion>
  ) {}

  async findByProject(projectId: number, query: any = {}) {
    const { page = 1, pageSize = 200, keyword, category, method } = query
    const qb = this.docRepo.createQueryBuilder('d').where('d.projectId = :projectId', { projectId })
    if (keyword) qb.andWhere('d.title LIKE :kw', { kw: `%${keyword}%` })
    if (category) qb.andWhere('d.category = :category', { category })
    if (method) qb.andWhere('d.method = :method', { method })
    qb.orderBy('d.updatedAt', 'DESC')
    const total = await qb.getCount()
    const list = await qb
      .skip((page - 1) * pageSize)
      .take(pageSize)
      .getMany()
    return { list, total, page, pageSize }
  }

  async findOne(id: number) {
    const doc = await this.docRepo.findOne({ where: { id } })
    if (!doc) throw new NotFoundException('文档不存在')
    return doc
  }

  async create(dto: CreateDocDto, user: any) {
    const doc = this.docRepo.create({ ...dto, createdById: user.id, updatedById: user.id })
    const saved = await this.docRepo.save(doc)
    await this.saveVersion(saved.id, saved, user, '初始版本')
    return saved
  }

  async update(id: number, dto: UpdateDocDto, user: any) {
    const doc = await this.findOne(id)
    Object.assign(doc, dto, { updatedById: user.id })
    const saved = await this.docRepo.save(doc)
    return saved
  }

  async remove(id: number) {
    await this.findOne(id)
    await this.docRepo.delete(id)
    return true
  }

  async batchRemove(ids: number[]) {
    await this.docRepo.delete(ids)
    return true
  }

  async moveCategory(docId: number, category: string) {
    await this.findOne(docId)
    await this.docRepo.update(docId, { category })
    return true
  }

  async batchImport(projectId: number, dto: BatchImportDto, user: any) {
    let docsToImport: any[] = []

    if (dto.docs && dto.docs.length) {
      docsToImport = dto.docs
    } else if (dto.raw) {
      docsToImport = this.parseRawImport(dto.raw, dto.format)
    }

    let imported = 0
    let updated = 0
    const errors: string[] = []

    for (const item of docsToImport) {
      try {
        const title = item.title || item.name || '未命名文档'
        const existing = item.method && item.path
          ? await this.docRepo.findOne({ where: { projectId, method: String(item.method).toUpperCase(), path: item.path } })
          : null

        const payload = {
          projectId,
          title,
          description: item.description || '',
          method: (item.method || 'GET').toUpperCase(),
          path: item.path || '',
          category: item.category || '导入',
          tags: item.tags || [],
          content: item.content || '',
          requestParams: item.requestParams,
          requestBody: item.requestBody,
          response: item.response,
          requestExample: item.requestExample,
          contentType: item.contentType || 'application/json',
          status: 'published' as const
        }

        if (existing) {
          Object.assign(existing, payload, { updatedById: user.id })
          await this.docRepo.save(existing)
          updated++
        } else {
          const doc = this.docRepo.create({ ...payload, createdById: user.id, updatedById: user.id })
          await this.docRepo.save(doc)
          imported++
        }
      } catch (e: any) {
        errors.push(`${item.title || item.path}: ${e.message}`)
      }
    }

    return { imported, updated, errors, total: docsToImport.length }
  }

  private parseRawImport(raw: any, format?: string): any[] {
    if (Array.isArray(raw)) return raw
    if (format === 'yaml') {
      // 简单 YAML 解析（仅处理嵌套 key-value）
      const result: any[] = []
      const lines = (raw || '').split('\n')
      let current: any = null
      for (const line of lines) {
        if (line.startsWith('  - ')) {
          if (current) result.push(current)
          current = {}
          const content = line.slice(4)
          const match = content.match(/^(\w+):\s*"?(.*?)"?\s*$/)
          if (match) current[match[1]] = match[2]
        } else if (current && line.startsWith('    ')) {
          const content = line.trim()
          const match = content.match(/^(\w+):\s*"?(.*?)"?\s*$/)
          if (match) current[match[1]] = match[2]
        }
      }
      if (current) result.push(current)
      return result
    }
    if (typeof raw === 'string') {
      try { return JSON.parse(raw) } catch (e) { return [] }
    }
    return []
  }

  async exportDocs(projectId: number, dto: ExportDocsDto) {
    const qb = this.docRepo.createQueryBuilder('d').where('d.projectId = :projectId', { projectId })
    if (dto.ids?.length) qb.andWhere('d.id IN (:...ids)', { ids: dto.ids })
    if (dto.category) qb.andWhere('d.category = :category', { category: dto.category })
    if (dto.keyword) qb.andWhere('d.title LIKE :kw', { kw: `%${dto.keyword}%` })
    qb.orderBy('d.category', 'ASC').addOrderBy('d.createdAt', 'ASC')
    const docs = await qb.getMany()
    return docs.map((d) => ({
      id: d.id,
      title: d.title,
      method: d.method,
      path: d.path,
      category: d.category,
      description: d.description,
      content: d.content,
      tags: d.tags,
      requestParams: d.requestParams,
      requestBody: d.requestBody,
      response: d.response,
      requestExample: d.requestExample,
      contentType: d.contentType,
      status: d.status,
      createdAt: d.createdAt,
      updatedAt: d.updatedAt
    }))
  }

  async saveVersion(docId: number, snapshot: any, user: any, remark = '') {
    const cleanSnapshot = this.serializeSnapshot(snapshot)
    const snapshotStr = JSON.stringify(cleanSnapshot)
    // 版本号在 doc_versions 表中对 (docId, version) 有唯一索引，使用事务避免并发重复
    const version = await this.versionRepo.manager.transaction(async (em) => {
      const repo = em.getRepository(DocVersion)
      const max = await repo
        .createQueryBuilder('v')
        .where('v.docId = :docId', { docId })
        .setLock('pessimistic_write')
        .select('MAX(v.version)', 'max')
        .getRawOne()
      const next = (max?.max || 0) + 1
      const record = repo.create({
        docId,
        version: next,
        snapshot: cleanSnapshot,
        remark,
        authorId: user.id,
        size: Buffer.byteLength(snapshotStr, 'utf8')
      })
      return repo.save(record)
    })
    return version
  }

  private serializeSnapshot(obj: any): any {
    if (obj == null) return obj
    if (typeof obj !== 'object') return obj
    if (obj instanceof Date) return obj.toISOString()
    if (Array.isArray(obj)) return obj.map((i) => this.serializeSnapshot(i))
    const out: any = {}
    for (const [k, v] of Object.entries(obj)) {
      if (v == null) continue
      // 跳过关联字段（循环引用来源）
      if (k === 'project' || k === 'createdBy' || k === 'updatedBy') continue
      if (typeof v === 'object' && !(v instanceof Date)) {
        out[k] = this.serializeSnapshot(v)
      } else {
        out[k] = v
      }
    }
    return out
  }

  async debug(dto: DebugDto) {
    const start = Date.now()
    try {
      const res = await axios({
        method: dto.method as any,
        url: dto.url,
        headers: dto.headers,
        params: dto.params,
        data: dto.body,
        timeout: 30000,
        validateStatus: () => true,
        // 对于 form-data 等由 axios 自动处理 Content-Type
        transformRequest: [
          (data, headers) => {
            if (data == null) return data
            if (typeof data === 'string') return data
            if (typeof FormData !== 'undefined' && data instanceof FormData) return data
            const ct = headers?.['Content-Type'] || headers?.['content-type']
            if (ct && ct.includes('application/x-www-form-urlencoded')) {
              return new URLSearchParams(data as any).toString()
            }
            if (ct && ct.includes('application/json')) {
              return JSON.stringify(data)
            }
            return JSON.stringify(data)
          }
        ]
      })
      return {
        status: res.status,
        statusText: res.statusText,
        data: res.data,
        headers: res.headers,
        duration: Date.now() - start,
        size: Buffer.byteLength(JSON.stringify(res.data ?? ''), 'utf8')
      }
    } catch (e: any) {
      return {
        status: 0,
        statusText: e.message || '请求失败',
        data: e?.response?.data || null,
        headers: e?.response?.headers || {},
        duration: Date.now() - start,
        size: 0
      }
    }
  }
}
