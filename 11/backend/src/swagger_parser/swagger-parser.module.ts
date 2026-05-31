import { Module } from '@nestjs/common'
import { TypeOrmModule } from '@nestjs/typeorm'
import { Doc } from '../docs/doc.entity'
import { Project } from '../projects/project.entity'
import { SwaggerParserController } from './swagger-parser.controller'
import { SwaggerParserService } from './swagger-parser.service'

@Module({
  imports: [TypeOrmModule.forFeature([Doc, Project])],
  controllers: [SwaggerParserController],
  providers: [SwaggerParserService],
  exports: [SwaggerParserService]
})
export class SwaggerParserModule {}
