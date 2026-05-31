import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { Component } from '../components/component.entity';
import { ComponentVersion } from '../versions/version.entity';
import { Dependency } from '../dependencies/dependency.entity';
import { DocsService } from './docs.service';
import { DocsController } from './docs.controller';

@Module({
  imports: [TypeOrmModule.forFeature([Component, ComponentVersion, Dependency])],
  controllers: [DocsController],
  providers: [DocsService],
  exports: [DocsService],
})
export class DocsModule {}
