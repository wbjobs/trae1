import {
  Controller,
  Post,
  Body,
  UseGuards,
  Request,
  Param
} from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import { Project } from '../projects/project.entity'
import { SwaggerParserService } from './swagger-parser.service'
import { JwtAuthGuard, RolesGuard } from '../common/guards/jwt-auth.guard'

@Controller('swagger')
@UseGuards(JwtAuthGuard)
export class SwaggerParserController {
  constructor(
    private readonly swaggerService: SwaggerParserService,
    @InjectRepository(Project)
    private readonly projectRepo: Repository<Project>
  ) {}

  @Post('projects/:projectId/import')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  async importByUrl(
    @Param('projectId') projectId: string,
    @Body() dto: { swaggerUrl?: string; swaggerContent?: any },
    @Request() req: any
  ) {
    return this.swaggerService.importSwagger(+projectId, dto, req.user)
  }

  @Post('parse')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  async parse(@Body() dto: { swaggerUrl?: string; swaggerContent?: any }) {
    let data = dto.swaggerContent
    if (!data && dto.swaggerUrl) {
      const axios = require('axios')
      const res = await axios.get(dto.swaggerUrl, { timeout: 15000 })
      data = res.data
    }
    if (!data) throw new Error('未提供 Swagger 内容')
    return this.swaggerService.parseSwagger(data)
  }
}
