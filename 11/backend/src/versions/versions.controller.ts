import {
  Controller,
  Get,
  Post,
  Param,
  Body,
  Query,
  UseGuards,
  Request
} from '@nestjs/common'
import { VersionsService } from './versions.service'
import { CreateVersionDto, CompareVersionsDto } from './dto'
import { JwtAuthGuard, RolesGuard } from '../common/guards/jwt-auth.guard'

@Controller('docs/:docId/versions')
@UseGuards(JwtAuthGuard)
export class VersionsController {
  constructor(private readonly versionsService: VersionsService) {}

  @Get()
  list(@Param('docId') docId: string, @Query() query: any) {
    return this.versionsService.findByDoc(+docId, query)
  }

  @Get(':versionId')
  findOne(@Param('docId') docId: string, @Param('versionId') versionId: string) {
    return this.versionsService.findOne(+docId, +versionId)
  }

  @Post()
  @UseGuards(new RolesGuard(['admin', 'editor']))
  create(@Param('docId') docId: string, @Body() dto: CreateVersionDto, @Request() req: any) {
    return this.versionsService.create(+docId, dto, req.user)
  }

  @Post('compare')
  compare(@Param('docId') docId: string, @Body() dto: CompareVersionsDto) {
    return this.versionsService.compare(+docId, dto)
  }

  @Post(':versionId/rollback')
  @UseGuards(new RolesGuard(['admin', 'editor']))
  rollback(@Param('docId') docId: string, @Param('versionId') versionId: string, @Request() req: any) {
    return this.versionsService.rollback(+docId, +versionId, req.user)
  }
}
