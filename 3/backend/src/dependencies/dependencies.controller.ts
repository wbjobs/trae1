import { Controller, Get, Post, Param, Body, Query } from '@nestjs/common';
import { ApiTags, ApiOperation } from '@nestjs/swagger';
import { DependenciesService } from './dependencies.service';
import { AnalyzeDto } from './dto/analyze.dto';

@ApiTags('dependencies')
@Controller('dependencies')
export class DependenciesController {
  constructor(private readonly service: DependenciesService) {}

  @Get('version/:versionId')
  @ApiOperation({ summary: '查询某版本声明的所有依赖' })
  listByVersion(@Param('versionId') versionId: string) {
    return this.service.listByVersion(versionId);
  }

  @Post('analyze')
  @ApiOperation({ summary: '依赖冲突检测：传入一组版本 ID' })
  analyze(@Body() dto: AnalyzeDto) {
    return this.service.analyze(dto.versionIds);
  }

  @Get()
  @ApiOperation({ summary: '列出最近的依赖声明' })
  listAll(@Query('limit') limit?: number) {
    return this.service.listAll(Number(limit) || 200);
  }
}
