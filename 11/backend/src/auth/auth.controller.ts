import {
  Controller,
  Post,
  Body,
  Get,
  UseGuards,
  Request,
  ConflictException,
  UnauthorizedException
} from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import { JwtService } from '@nestjs/jwt'
import * as bcrypt from 'bcrypt'
import { User } from '../users/user.entity'
import { LoginDto, RegisterDto } from './dto'
import { JwtAuthGuard } from '../common/guards/jwt-auth.guard'

@Controller('auth')
export class AuthController {
  constructor(
    @InjectRepository(User)
    private readonly usersRepo: Repository<User>,
    private readonly jwtService: JwtService
  ) {}

  @Post('login')
  async login(@Body() dto: LoginDto) {
    const user = await this.usersRepo
      .createQueryBuilder('u')
      .addSelect('u.password')
      .where('u.username = :username', { username: dto.username })
      .getOne()
    if (!user) throw new UnauthorizedException('用户名或密码错误')
    const ok = await bcrypt.compare(dto.password, user.password)
    if (!ok) throw new UnauthorizedException('用户名或密码错误')
    if (!user.active) throw new UnauthorizedException('账号已禁用')
    const token = this.jwtService.sign({ sub: user.id, role: user.role })
    const { password, ...info } = user
    return { token, user: info }
  }

  @Post('register')
  async register(@Body() dto: RegisterDto) {
    const exists = await this.usersRepo.findOne({
      where: [{ username: dto.username }, { email: dto.email }]
    })
    if (exists) throw new ConflictException('用户名或邮箱已存在')
    const hashed = await bcrypt.hash(dto.password, 10)
    const user = this.usersRepo.create({
      username: dto.username,
      nickname: dto.nickname,
      email: dto.email,
      password: hashed,
      role: 'viewer'
    })
    await this.usersRepo.save(user)
    return { id: user.id }
  }

  @Get('profile')
  @UseGuards(JwtAuthGuard)
  profile(@Request() req: any) {
    const { password, ...info } = req.user
    return info
  }
}
