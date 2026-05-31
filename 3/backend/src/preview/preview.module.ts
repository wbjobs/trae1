import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ComponentVersion } from '../versions/version.entity';
import { Dependency } from '../dependencies/dependency.entity';
import { PreviewService } from './preview.service';
import { PreviewController } from './preview.controller';

@Module({
  imports: [TypeOrmModule.forFeature([ComponentVersion, Dependency])],
  controllers: [PreviewController],
  providers: [PreviewService],
  exports: [PreviewService],
})
export class PreviewModule {}
