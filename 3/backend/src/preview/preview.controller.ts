import { Controller, Get, Param } from '@nestjs/common';
import { ApiTags, ApiOperation } from '@nestjs/swagger';
import { PreviewService } from './preview.service';

@ApiTags('preview')
@Controller('preview')
export class PreviewController {
  constructor(private readonly service: PreviewService) {}

  @Get(':versionId')
  @ApiOperation({ summary: '获取组件版本的预览与按需引入配置' })
  getPreview(@Param('versionId') versionId: string) {
    return this.service.getPreview(versionId);
  }
}
