import { Module } from '@nestjs/common'
import { TypeOrmModule } from '@nestjs/typeorm'
import { Project } from './project.entity'
import { Doc } from '../docs/doc.entity'
import { ProjectsController } from './projects.controller'
import { ProjectsService } from './projects.service'
import { SwaggerParserModule } from '../swagger_parser/swagger-parser.module'

@Module({
  imports: [
    TypeOrmModule.forFeature([Project, Doc]),
    SwaggerParserModule
  ],
  controllers: [ProjectsController],
  providers: [ProjectsService],
  exports: [ProjectsService]
})
export class ProjectsModule {}
