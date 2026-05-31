import {
  Controller,
  Get,
  Post,
  Body,
  Param,
  Put,
  Delete,
  Query,
} from '@nestjs/common';
import { ApiTags, ApiOperation } from '@nestjs/swagger';
import { ComponentsService } from './components.service';
import { CreateComponentDto, UpdateComponentDto } from './dto/create-component.dto';

@ApiTags('components')
@Controller('components')
export class ComponentsController {
  constructor(private readonly service: ComponentsService) {}

  @Post()
  @ApiOperation({ summary: '创建组件' })
  create(@Body() dto: CreateComponentDto) {
    return this.service.create(dto);
  }

  @Get()
  @ApiOperation({ summary: '获取组件列表，可选按 category/sortBy 过滤排序' })
  findAll(
    @Query('category') category?: string,
    @Query('sortBy') sortBy?: string,
  ) {
    return this.service.findAll(category, sortBy);
  }

  @Get('stats/top')
  @ApiOperation({ summary: '获取热度 Top N 组件' })
  getTop(@Query('n') n?: string) {
    return this.service.getTopN(Number(n) || 10);
  }

  @Get(':id')
  @ApiOperation({ summary: '根据 ID 获取组件详情' })
  findOne(@Param('id') id: string) {
    return this.service.findOne(id);
  }

  @Put(':id')
  @ApiOperation({ summary: '更新组件信息' })
  update(@Param('id') id: string, @Body() dto: UpdateComponentDto) {
    return this.service.update(id, dto);
  }

  @Delete(':id')
  @ApiOperation({ summary: '删除组件' })
  remove(@Param('id') id: string) {
    return this.service.remove(id);
  }

  @Post(':id/stats/download')
  @ApiOperation({ summary: '记录一次下载' })
  incDownload(@Param('id') id: string) {
    return this.service.incrementStat(id, 'download');
  }

  @Post(':id/stats/preview')
  @ApiOperation({ summary: '记录一次预览' })
  incPreview(@Param('id') id: string) {
    return this.service.incrementStat(id, 'preview');
  }

  @Post(':id/stats/reference')
  @ApiOperation({ summary: '记录一次引用' })
  incReference(@Param('id') id: string) {
    return this.service.incrementStat(id, 'reference');
  }
}
