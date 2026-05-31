import { Controller, Get, Param, Query, Res } from '@nestjs/common';
import { ApiTags, ApiOperation } from '@nestjs/swagger';
import { Response } from 'express';
import { DocsService } from './docs.service';

@ApiTags('docs')
@Controller('docs')
export class DocsController {
  constructor(private readonly service: DocsService) {}

  @Get(':componentName')
  @ApiOperation({ summary: '获取组件结构化文档数据' })
  generate(@Param('componentName') name: string) {
    return this.service.generate(name);
  }

  @Get(':componentName/markdown')
  @ApiOperation({ summary: '下载组件 Markdown 文档' })
  async markdown(@Param('componentName') name: string, @Res() res: Response) {
    const md = await this.service.generateMarkdown(name);
    res.setHeader('Content-Type', 'text/markdown; charset=utf-8');
    res.setHeader('Content-Disposition', `attachment; filename="${name}.md"`);
    res.send(md);
  }
}
