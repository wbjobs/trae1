import { Module } from '@nestjs/common'
import { TypeOrmModule } from '@nestjs/typeorm'
import { Doc } from './doc.entity'
import { DocVersion } from '../versions/doc-version.entity'
import { DocsController } from './docs.controller'
import { DocsService } from './docs.service'
import { NotificationsModule } from '../notifications/notifications.module'

@Module({
  imports: [
    TypeOrmModule.forFeature([Doc, DocVersion]),
    NotificationsModule
  ],
  controllers: [DocsController],
  providers: [DocsService],
  exports: [DocsService]
})
export class DocsModule {}
