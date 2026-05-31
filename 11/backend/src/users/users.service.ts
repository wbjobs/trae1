import { Injectable, NotFoundException, ConflictException } from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import * as bcrypt from 'bcrypt'
import { User } from './user.entity'

@Injectable()
export class UsersService {
  constructor(
    @InjectRepository(User)
    private readonly usersRepo: Repository<User>
  ) {}

  async findAll(query: { page?: number; pageSize?: number; keyword?: string; role?: string }) {
    const { page = 1, pageSize = 20, keyword, role } = query
    const qb = this.usersRepo.createQueryBuilder('u')
    if (keyword) {
      qb.andWhere('(u.username LIKE :kw OR u.nickname LIKE :kw OR u.email LIKE :kw)', {
        kw: `%${keyword}%`
      })
    }
    if (role) qb.andWhere('u.role = :role', { role })
    qb.orderBy('u.createdAt', 'DESC')
    const total = await qb.getCount()
    const list = await qb
      .skip((page - 1) * pageSize)
      .take(pageSize)
      .getMany()
    return { list, total, page, pageSize }
  }

  async findOne(id: number) {
    const user = await this.usersRepo.findOne({ where: { id } })
    if (!user) throw new NotFoundException('用户不存在')
    return user
  }

  async update(id: number, dto: Partial<User>) {
    const user = await this.findOne(id)
    if (dto.email && dto.email !== user.email) {
      const exists = await this.usersRepo.findOne({ where: { email: dto.email } })
      if (exists) throw new ConflictException('邮箱已存在')
    }
    Object.assign(user, dto)
    return this.usersRepo.save(user)
  }

  async updatePassword(id: number, password: string) {
    const user = await this.findOne(id)
    user.password = await bcrypt.hash(password, 10)
    return this.usersRepo.save(user)
  }

  async remove(id: number) {
    await this.findOne(id)
    await this.usersRepo.delete(id)
    return true
  }
}
