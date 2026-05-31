import {
  Controller,
  Get,
  Post,
  Body,
  Param,
  Delete,
  Put,
  Query,
} from '@nestjs/common';
import { ApiTags, ApiOperation } from '@nestjs/swagger';
import { VersionsService } from './versions.service';
import { CreateVersionDto } from './dto/create-version.dto';

@ApiTags('versions')
@Controller('versions')
export class VersionsController {
  constructor(private readonly service: VersionsService) {}

  @Post()
  @ApiOperation({ summary: '发布新版本（组件迭代）' })
  create(@Body() dto: CreateVersionDto) {
    return this.service.create(dto);
  }

  @Get('component/:componentId')
  @ApiOperation({ summary: '获取组件所有版本' })
  findByComponent(@Param('componentId') componentId: string) {
    return this.service.findByComponent(componentId);
  }

  @Get('component/:componentId/latest')
  @ApiOperation({ summary: '获取组件最新版本' })
  findLatest(@Param('componentId') componentId: string) {
    return this.service.findLatest(componentId);
  }

  @Get('component/:componentId/suggest')
  @ApiOperation({ summary: '建议下一个版本号' })
  suggestNext(
    @Param('componentId') componentId: string,
    @Query('bump') bump: 'major' | 'minor' | 'patch' = 'patch',
  ) {
    return this.service.suggestNext(componentId, bump);
  }

  @Get(':id')
  @ApiOperation({ summary: '根据 ID 获取版本详情' })
  findOne(@Param('id') id: string) {
    return this.service.findOne(id);
  }

  @Put(':id/latest')
  @ApiOperation({ summary: '将该版本标记为最新版本' })
  setLatest(@Param('id') id: string) {
    return this.service.setLatest(id);
  }

  @Post(':id/rollback')
  @ApiOperation({ summary: '软回滚：将该版本标记为最新，保留原版本号' })
  rollback(@Param('id') id: string) {
    return this.service.rollback(id);
  }

  @Post(':id/rollback-clone')
  @ApiOperation({ summary: '硬回滚：克隆该版本内容，递增版本号并设为最新' })
  rollbackClone(
    @Param('id') id: string,
    @Query('bump') bump: 'major' | 'minor' | 'patch' = 'patch',
  ) {
    return this.service.rollbackWithClone(id, bump);
  }

  @Delete(':id')
  @ApiOperation({ summary: '删除版本' })
  remove(@Param('id') id: string) {
    return this.service.remove(id);
  }
}
