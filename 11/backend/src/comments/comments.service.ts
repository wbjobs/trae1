import { Injectable, NotFoundException, ForbiddenException } from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import { DocComment } from './comment.entity'
import { CreateCommentDto, UpdateCommentDto } from './dto'

@Injectable()
export class CommentsService {
  constructor(
    @InjectRepository(DocComment)
    private readonly commentRepo: Repository<DocComment>
  ) {}

  async findByDoc(docId: number) {
    const list = await this.commentRepo.find({
      where: { docId },
      relations: ['author'],
      order: { createdAt: 'ASC' }
    })
    return this.buildTree(list)
  }

  private buildTree(list: DocComment[]) {
    const map = new Map<number, any>()
    const roots: any[] = []
    for (const c of list) {
      const node = {
        ...c,
        author: c.author
          ? { id: c.author.id, nickname: c.author.nickname, avatar: c.author.avatar }
          : null,
        children: []
      }
      map.set(c.id, node)
    }
    for (const node of map.values()) {
      if (node.parentId && map.has(node.parentId)) {
        map.get(node.parentId).children.push(node)
      } else {
        roots.push(node)
      }
    }
    return roots
  }

  async findOne(id: number) {
    const c = await this.commentRepo.findOne({ where: { id }, relations: ['author'] })
    if (!c) throw new NotFoundException('批注不存在')
    return c
  }

  async create(dto: CreateCommentDto, user: any) {
    if (dto.parentId) {
      const parent = await this.commentRepo.findOne({ where: { id: dto.parentId } })
      if (!parent) throw new NotFoundException('父批注不存在')
    }
    const c = this.commentRepo.create({
      ...dto,
      authorId: user.id,
      status: 'open'
    })
    return this.commentRepo.save(c)
  }

  async update(id: number, dto: UpdateCommentDto, user: any) {
    const c = await this.findOne(id)
    if (c.authorId !== user.id && user.role !== 'admin') {
      throw new ForbiddenException('无权限修改')
    }
    Object.assign(c, dto)
    return this.commentRepo.save(c)
  }

  async upvote(id: number) {
    await this.commentRepo.increment({ id }, 'upvotes', 1)
    return this.findOne(id)
  }

  async remove(id: number, user: any) {
    const c = await this.findOne(id)
    if (c.authorId !== user.id && user.role !== 'admin') {
      throw new ForbiddenException('无权限删除')
    }
    await this.commentRepo.delete(id)
    return true
  }
}
