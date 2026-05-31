import {
  Controller,
  Get,
  Post,
  Put,
  Delete,
  Param,
  Body,
  Query,
  UseGuards,
  NotFoundException,
  ConflictException
} from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import { Member } from './member.entity'
import { User } from '../users/user.entity'
import { JwtAuthGuard, RolesGuard } from '../common/guards/jwt-auth.guard'

class InviteDto {
  email: string
  role: 'admin' | 'editor' | 'viewer'
  projectId?: number
}

class UpdateRoleDto {
  role: 'admin' | 'editor' | 'viewer'
}

@Controller('members')
@UseGuards(JwtAuthGuard)
export class MembersController {
  constructor(
    @InjectRepository(Member)
    private readonly memberRepo: Repository<Member>,
    @InjectRepository(User)
    private readonly userRepo: Repository<User>
  ) {}

  @Get()
  @UseGuards(new RolesGuard(['admin']))
  async list(@Query() query: any) {
    const { page = 1, pageSize = 50, projectId } = query
    const qb = this.memberRepo
      .createQueryBuilder('m')
      .leftJoinAndSelect('m.user', 'u')
      .leftJoinAndSelect('m.project', 'p')
    if (projectId) qb.andWhere('m.projectId = :pid', { pid: projectId })
    qb.orderBy('m.joinedAt', 'DESC')
    const total = await qb.getCount()
    const raw = await qb
      .skip((page - 1) * pageSize)
      .take(pageSize)
      .getMany()
    const list = raw.map((m) => ({
      id: m.id,
      userId: m.userId,
      projectId: m.projectId,
      role: m.role,
      active: m.active,
      joinedAt: m.joinedAt,
      username: m.user?.username,
      nickname: m.user?.nickname,
      email: m.user?.email,
      avatar: m.user?.avatar
    }))
    return { list, total, page, pageSize }
  }

  @Get('projects/:projectId/members')
  async projectMembers(@Param('projectId') projectId: string) {
    const raw = await this.memberRepo.find({
      where: { projectId: +projectId },
      relations: ['user']
    })
    const list = raw.map((m) => ({
      id: m.id,
      userId: m.userId,
      role: m.role,
      joinedAt: m.joinedAt,
      username: m.user?.username,
      nickname: m.user?.nickname,
      email: m.user?.email
    }))
    return { list }
  }

  @Post('invite')
  @UseGuards(new RolesGuard(['admin']))
  async invite(@Body() dto: InviteDto) {
    const user = await this.userRepo.findOne({ where: { email: dto.email } })
    if (!user) throw new NotFoundException('用户不存在，请先注册')
    const exists = await this.memberRepo.findOne({
      where: { userId: user.id, projectId: dto.projectId || null }
    })
    if (exists) throw new ConflictException('该成员已在列表中')
    const member = this.memberRepo.create({
      userId: user.id,
      projectId: dto.projectId || null,
      role: dto.role,
      active: true
    })
    return this.memberRepo.save(member)
  }

  @Put(':id/role')
  @UseGuards(new RolesGuard(['admin']))
  async updateRole(@Param('id') id: string, @Body() dto: UpdateRoleDto) {
    await this.memberRepo.update(+id, { role: dto.role })
    return true
  }

  @Delete(':id')
  @UseGuards(new RolesGuard(['admin']))
  async remove(@Param('id') id: string) {
    await this.memberRepo.delete(+id)
    return true
  }
}
