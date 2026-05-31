import { Injectable, NotFoundException } from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import { Project } from './project.entity'
import { CreateProjectDto, UpdateProjectDto } from './dto'
import { Doc } from '../docs/doc.entity'

@Injectable()
export class ProjectsService {
  constructor(
    @InjectRepository(Project)
    private readonly projectRepo: Repository<Project>,
    @InjectRepository(Doc)
    private readonly docRepo: Repository<Doc>
  ) {}

  async findAll(query: { page?: number; pageSize?: number; keyword?: string; ownerId?: number }) {
    const { page = 1, pageSize = 20, keyword, ownerId } = query
    const qb = this.projectRepo.createQueryBuilder('p')
    if (keyword) {
      qb.andWhere('(p.name LIKE :kw OR p.description LIKE :kw)', { kw: `%${keyword}%` })
    }
    if (ownerId) qb.andWhere('p.ownerId = :ownerId', { ownerId })
    qb.orderBy('p.createdAt', 'DESC')
    const total = await qb.getCount()
    const list = await qb
      .skip((page - 1) * pageSize)
      .take(pageSize)
      .getMany()

    const result = []
    for (const p of list) {
      const docCount = await this.docRepo.count({ where: { projectId: p.id } })
      result.push({ ...p, docCount })
    }
    return { list: result, total, page, pageSize }
  }

  async findOne(id: number) {
    const project = await this.projectRepo.findOne({ where: { id } })
    if (!project) throw new NotFoundException('项目不存在')
    const docCount = await this.docRepo.count({ where: { projectId: id } })
    return { ...project, docCount }
  }

  async create(dto: CreateProjectDto, user: any) {
    const project = this.projectRepo.create({ ...dto, ownerId: user.id })
    return this.projectRepo.save(project)
  }

  async update(id: number, dto: UpdateProjectDto) {
    await this.findOne(id)
    await this.projectRepo.update(id, dto)
    return this.findOne(id)
  }

  async remove(id: number) {
    await this.findOne(id)
    await this.projectRepo.delete(id)
    return true
  }
}
