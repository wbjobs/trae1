import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { Dependency } from './dependency.entity';
import { ComponentVersion } from '../versions/version.entity';
import { DependenciesService } from './dependencies.service';
import { DependenciesController } from './dependencies.controller';

@Module({
  imports: [TypeOrmModule.forFeature([Dependency, ComponentVersion])],
  controllers: [DependenciesController],
  providers: [DependenciesService],
  exports: [DependenciesService],
})
export class DependenciesModule {}
