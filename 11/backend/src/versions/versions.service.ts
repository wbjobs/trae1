import { Injectable, NotFoundException } from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import { DocVersion } from './doc-version.entity'
import { Doc } from '../docs/doc.entity'

@Injectable()
export class VersionsService {
  constructor(
    @InjectRepository(DocVersion)
    private readonly versionRepo: Repository<DocVersion>,
    @InjectRepository(Doc)
    private readonly docRepo: Repository<Doc>
  ) {}

  async findByDoc(docId: number, query: any = {}) {
    const { page = 1, pageSize = 50 } = query
    const [list, total] = await this.versionRepo.findAndCount({
      where: { docId },
      order: { version: 'DESC' },
      relations: ['author'],
      skip: (page - 1) * pageSize,
      take: pageSize
    })
    return { list, total, page, pageSize }
  }

  async findOne(docId: number, versionId: number) {
    const v = await this.versionRepo.findOne({
      where: { id: versionId, docId },
      relations: ['author']
    })
    if (!v) throw new NotFoundException('版本不存在')
    return v
  }

  async create(docId: number, dto: { snapshot: any; remark?: string }, user: any) {
    const doc = await this.docRepo.findOne({ where: { id: docId } })
    if (!doc) throw new NotFoundException('文档不存在')

    const cleanSnapshot = this.serializeSnapshot(dto.snapshot)
    const snapshotStr = JSON.stringify(cleanSnapshot)

    const saved = await this.versionRepo.manager.transaction(async (em) => {
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
        remark: dto.remark || '',
        authorId: user.id,
        size: Buffer.byteLength(snapshotStr, 'utf8')
      })
      return repo.save(record)
    })
    return saved
  }

  async compare(docId: number, dto: { leftVersionId: number; rightVersionId: number }) {
    const left = await this.findOne(docId, dto.leftVersionId)
    const right = await this.findOne(docId, dto.rightVersionId)
    return {
      left: { ...left.snapshot, _version: left.version },
      right: { ...right.snapshot, _version: right.version }
    }
  }

  async rollback(docId: number, versionId: number, user: any) {
    const version = await this.findOne(docId, versionId)
    const doc = await this.docRepo.findOne({ where: { id: docId } })
    if (!doc) throw new NotFoundException('文档不存在')

    // 先将当前文档保存为历史版本
    const currentSnapshot = this.serializeSnapshot(doc)
    const snapshotStr = JSON.stringify(currentSnapshot)
    await this.versionRepo.manager.transaction(async (em) => {
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
        snapshot: currentSnapshot,
        remark: '回滚前自动保存',
        authorId: user.id,
        size: Buffer.byteLength(snapshotStr, 'utf8')
      })
      await repo.save(record)
    })

    // 回滚：仅更新可编辑字段，保留 id/createdAt
    const snap = version.snapshot || {}
    const {
      id: _id,
      createdAt: _ca,
      updatedAt: _ua,
      ...rest
    } = snap
    await this.docRepo.update(docId, { ...rest, updatedById: user.id })
    return this.docRepo.findOne({ where: { id: docId } })
  }

  private serializeSnapshot(obj: any): any {
    if (obj == null) return obj
    if (typeof obj !== 'object') return obj
    if (obj instanceof Date) return obj.toISOString()
    if (Array.isArray(obj)) return obj.map((i) => this.serializeSnapshot(i))
    const out: any = {}
    for (const [k, v] of Object.entries(obj)) {
      if (v == null) continue
      if (k === 'project' || k === 'createdBy' || k === 'updatedBy') continue
      if (typeof v === 'object' && !(v instanceof Date)) {
        out[k] = this.serializeSnapshot(v)
      } else {
        out[k] = v
      }
    }
    return out
  }
}
