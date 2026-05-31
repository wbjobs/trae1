import { Module } from '@nestjs/common'
import { TypeOrmModule } from '@nestjs/typeorm'
import { Member } from './member.entity'
import { User } from '../users/user.entity'
import { MembersController } from './members.controller'

@Module({
  imports: [TypeOrmModule.forFeature([Member, User])],
  controllers: [MembersController]
})
export class MembersModule {}
