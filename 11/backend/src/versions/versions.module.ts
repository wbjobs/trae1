import { Module } from '@nestjs/common'
import { TypeOrmModule } from '@nestjs/typeorm'
import { DocVersion } from './doc-version.entity'
import { Doc } from '../docs/doc.entity'
import { VersionsController } from './versions.controller'
import { VersionsService } from './versions.service'

@Module({
  imports: [TypeOrmModule.forFeature([DocVersion, Doc])],
  controllers: [VersionsController],
  providers: [VersionsService],
  exports: [VersionsService]
})
export class VersionsModule {}
