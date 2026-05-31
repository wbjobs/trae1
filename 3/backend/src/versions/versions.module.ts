import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ComponentVersion } from './version.entity';
import { Component } from '../components/component.entity';
import { Dependency } from '../dependencies/dependency.entity';
import { VersionsService } from './versions.service';
import { VersionsController } from './versions.controller';

@Module({
  imports: [TypeOrmModule.forFeature([ComponentVersion, Component, Dependency])],
  controllers: [VersionsController],
  providers: [VersionsService],
  exports: [VersionsService],
})
export class VersionsModule {}
