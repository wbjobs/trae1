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
  Inject,
  NotFoundException,
  ConflictException
} from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import { ProjectsService } from './projects.service'
import { CreateProjectDto, UpdateProjectDto, ImportSwaggerDto } from './dto'
import { JwtAuthGuard, RolesGuard } from '../common/guards/jwt-auth.guard'
import { SwaggerParserService } from '../swagger_parser/swagger-parser.service'

@Controller('projects')
@UseGuards(JwtAuthGuard)
export class ProjectsController {
  constructor(
    private readonly projectsService: ProjectsService,
    @Inject(SwaggerParserService)
    private readonly swaggerService: SwaggerParserService
  ) {}

  @Get()
  list(@Query() query: any) {
    return this.projectsService.findAll(query)
  }

  @Get(':id')
  findOne(@Param('id') id: string) {
    return this.projectsService.findOne(+id)
  }

  @Post()
  @UseGuards(new RolesGuard(['admin', 'editor']))
  create(@Body() dto: CreateProjectDto, @Request() req: any) {
    return this.projectsService.create(dto, req.user)
  }

  @Put(':id')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  update(@Param('id') id: string, @Body() dto: UpdateProjectDto) {
    return this.projectsService.update(+id, dto)
  }

  @Delete(':id')
  @UseGuards(new RolesGuard(['admin']))
  remove(@Param('id') id: string) {
    return this.projectsService.remove(+id)
  }

  @Post(':id/import-swagger')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  async importSwagger(@Param('id') id: string, @Body() dto: ImportSwaggerDto, @Request() req: any) {
    await this.projectsService.findOne(+id)
    if (dto.swaggerUrl) {
      await this.projectsService.update(+id, { swaggerUrl: dto.swaggerUrl })
    }
    return this.swaggerService.importSwagger(+id, dto, req.user)
  }

  @Post(':id/sync-swagger')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  async syncSwagger(@Param('id') id: string, @Request() req: any) {
    const project: any = await this.projectsService.findOne(+id)
    if (!project.swaggerUrl) {
      throw new Error('项目未配置 Swagger 地址')
    }
    return this.swaggerService.importSwagger(
      +id,
      { swaggerUrl: project.swaggerUrl },
      req.user
    )
  }
}
