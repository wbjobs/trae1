import { Controller, Get, Param, Query } from '@nestjs/common';
import { ApiTags, ApiOperation } from '@nestjs/swagger';
import { BundleService } from './bundle.service';

@ApiTags('bundle')
@Controller('bundle')
export class BundleController {
  constructor(private readonly service: BundleService) {}

  @Get(':versionId')
  @ApiOperation({ summary: '分析组件版本的打包体积与压缩建议' })
  analyze(@Param('versionId') versionId: string) {
    return this.service.analyze(versionId);
  }

  @Get('compare/:versionIdA/:versionIdB')
  @ApiOperation({ summary: '对比两个版本的体积变化' })
  compare(
    @Param('versionIdA') a: string,
    @Param('versionIdB') b: string,
  ) {
    return this.service.compare(a, b);
  }
}
