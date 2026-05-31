import {
  Controller,
  Get,
  Put,
  Delete,
  Param,
  Body,
  Query,
  UseGuards,
  Request
} from '@nestjs/common'
import { UsersService } from './users.service'
import { JwtAuthGuard, RolesGuard } from '../common/guards/jwt-auth.guard'
import { UpdateProfileDto } from '../auth/dto'

@Controller('users')
@UseGuards(JwtAuthGuard)
export class UsersController {
  constructor(private readonly usersService: UsersService) {}

  @Get()
  @UseGuards(new RolesGuard(['admin']))
  async list(@Query() query: any) {
    return this.usersService.findAll(query)
  }

  @Get('me')
  me(@Request() req: any) {
    return req.user
  }

  @Get(':id')
  async findOne(@Param('id') id: string) {
    return this.usersService.findOne(+id)
  }

  @Put(':id')
  @UseGuards(new RolesGuard(['admin']))
  async update(@Param('id') id: string, @Body() dto: UpdateProfileDto) {
    return this.usersService.update(+id, dto)
  }

  @Delete(':id')
  @UseGuards(new RolesGuard(['admin']))
  async remove(@Param('id') id: string) {
    return this.usersService.remove(+id)
  }
}
