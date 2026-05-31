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
  Request,
  Res,
  Inject
} from '@nestjs/common'
import { Response } from 'express'
import { DocsService } from './docs.service'
import { CreateDocDto, UpdateDocDto, MoveCategoryDto, BatchRemoveDto, DebugDto, BatchImportDto, ExportDocsDto } from './dto'
import { JwtAuthGuard, RolesGuard } from '../common/guards/jwt-auth.guard'
import { NotificationService } from '../notifications/notification.service'

@Controller()
@UseGuards(JwtAuthGuard)
export class DocsController {
  constructor(
    private readonly docsService: DocsService,
    @Inject(NotificationService)
    private readonly notificationService: NotificationService
  ) {}

  @Get('projects/:projectId/docs')
  list(@Param('projectId') projectId: string, @Query() query: any) {
    return this.docsService.findByProject(+projectId, query)
  }

  @Get('docs/:id')
  findOne(@Param('id') id: string) {
    return this.docsService.findOne(+id)
  }

  @Post('docs')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  async create(@Body() dto: CreateDocDto, @Request() req: any) {
    const doc = await this.docsService.create(dto, req.user)
    this.notificationService.broadcast({
      type: 'doc_created',
      title: '新增接口文档',
      content: `${req.user.nickname} 新增文档「${doc.title}」`,
      projectId: dto.projectId,
      docId: doc.id,
      operator: { id: req.user.id, nickname: req.user.nickname },
      data: { doc }
    })
    return doc
  }

  @Put('docs/:id')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  async update(@Param('id') id: string, @Body() dto: UpdateDocDto, @Request() req: any) {
    const doc = await this.docsService.update(+id, dto, req.user)
    this.notificationService.broadcast({
      type: 'doc_updated',
      title: '接口文档已更新',
      content: `${req.user.nickname} 更新了文档「${doc.title}」`,
      projectId: doc.projectId,
      docId: doc.id,
      operator: { id: req.user.id, nickname: req.user.nickname },
      data: { doc }
    })
    return doc
  }

  @Delete('docs/:id')
  @UseGuards(new RolesGuard(['admin']))
  async remove(@Param('id') id: string, @Request() req: any) {
    const doc = await this.docsService.findOne(+id)
    await this.docsService.remove(+id)
    this.notificationService.broadcast({
      type: 'doc_deleted',
      title: '接口文档已删除',
      content: `${req.user.nickname} 删除了文档「${doc.title}」`,
      projectId: doc.projectId,
      operator: { id: req.user.id, nickname: req.user.nickname }
    })
    return true
  }

  @Post('docs/batch-delete')
  @UseGuards(new RolesGuard(['admin']))
  async batchRemove(@Body() dto: BatchRemoveDto, @Request() req: any) {
    await this.docsService.batchRemove(dto.ids)
    this.notificationService.broadcast({
      type: 'doc_deleted',
      title: '批量删除文档',
      content: `${req.user.nickname} 批量删除了 ${dto.ids.length} 个文档`,
      operator: { id: req.user.id, nickname: req.user.nickname },
      data: { ids: dto.ids }
    })
    return true
  }

  @Post('docs/move-category')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  moveCategory(@Body() dto: MoveCategoryDto) {
    return this.docsService.moveCategory(dto.docId, dto.category)
  }

  @Post('docs/debug')
  debug(@Body() dto: DebugDto) {
    return this.docsService.debug(dto)
  }

  @Post('projects/:projectId/docs/batch-import')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  async batchImport(@Param('projectId') projectId: string, @Body() dto: BatchImportDto, @Request() req: any) {
    const result = await this.docsService.batchImport(+projectId, dto, req.user)
    this.notificationService.broadcast({
      type: 'doc_created',
      title: '批量导入文档',
      content: `${req.user.nickname} 批量导入了 ${result.imported} 个文档`,
      projectId: +projectId,
      operator: { id: req.user.id, nickname: req.user.nickname },
      data: result
    })
    return result
  }

  @Get('projects/:projectId/docs/export')
  @UseGuards(new RolesGuard(['admin', 'editor', 'viewer']))
  async export(
    @Param('projectId') projectId: string,
    @Query() dto: ExportDocsDto,
    @Res() res: Response
  ) {
    const result = await this.docsService.exportDocs(+projectId, dto)
    const format = dto.format || 'json'
    const filename = `api-docs-${projectId}-${Date.now()}`
    if (format === 'json') {
      res.setHeader('Content-Type', 'application/json; charset=utf-8')
      res.setHeader('Content-Disposition', `attachment; filename="${filename}.json"`)
      res.send(JSON.stringify(result, null, 2))
    } else if (format === 'markdown') {
      res.setHeader('Content-Type', 'text/markdown; charset=utf-8')
      res.setHeader('Content-Disposition', `attachment; filename="${filename}.md"`)
      res.send(this.buildMarkdownExport(result))
    } else {
      res.setHeader('Content-Type', 'application/yaml; charset=utf-8')
      res.setHeader('Content-Disposition', `attachment; filename="${filename}.yaml"`)
      res.send(this.buildYamlExport(result))
    }
  }

  private buildMarkdownExport(docs: any[]): string {
    const lines: string[] = []
    lines.push('# 接口文档导出', '')
    lines.push(`> 导出时间: ${new Date().toISOString()}`, '')
    for (const doc of docs) {
      lines.push(`## ${doc.method} ${doc.path}`, '')
      lines.push(`**标题:** ${doc.title}`, '')
      if (doc.description) lines.push(doc.description, '')
      if (doc.content) lines.push(doc.content, '')
      lines.push('---', '')
    }
    return lines.join('\n')
  }

  private buildYamlExport(docs: any[]): string {
    const escape = (s: string) => (s || '').replace(/\n/g, '\\n').replace(/"/g, '\\"')
    const lines: string[] = []
    lines.push(`exported_at: "${new Date().toISOString()}"`)
    lines.push(`count: ${docs.length}`)
    lines.push('docs:')
    for (const doc of docs) {
      lines.push(`  - title: "${escape(doc.title)}"`)
      lines.push(`    method: ${doc.method}`)
      lines.push(`    path: "${escape(doc.path || '')}"`)
      lines.push(`    category: "${escape(doc.category || '')}"`)
      if (doc.description) lines.push(`    description: "${escape(doc.description)}"`)
    }
    return lines.join('\n')
  }
}
