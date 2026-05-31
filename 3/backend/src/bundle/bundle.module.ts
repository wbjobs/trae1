import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ComponentVersion } from '../versions/version.entity';
import { Dependency } from '../dependencies/dependency.entity';
import { Component } from '../components/component.entity';
import { BundleService } from './bundle.service';
import { BundleController } from './bundle.controller';

@Module({
  imports: [TypeOrmModule.forFeature([ComponentVersion, Dependency, Component])],
  controllers: [BundleController],
  providers: [BundleService],
  exports: [BundleService],
})
export class BundleModule {}
