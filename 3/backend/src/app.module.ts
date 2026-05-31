import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ComponentsModule } from './components/components.module';
import { VersionsModule } from './versions/versions.module';
import { DependenciesModule } from './dependencies/dependencies.module';
import { PreviewModule } from './preview/preview.module';
import { DocsModule } from './docs/docs.module';
import { BundleModule } from './bundle/bundle.module';

@Module({
  imports: [
    TypeOrmModule.forRoot({
      type: 'postgres',
      host: process.env.DB_HOST ?? 'localhost',
      port: Number(process.env.DB_PORT ?? 5432),
      username: process.env.DB_USER ?? 'postgres',
      password: process.env.DB_PASSWORD ?? 'postgres',
      database: process.env.DB_DATABASE ?? 'component_library',
      autoLoadEntities: true,
      synchronize: true,
      logging: false,
    }),
    ComponentsModule,
    VersionsModule,
    DependenciesModule,
    PreviewModule,
    DocsModule,
    BundleModule,
  ],
})
export class AppModule {}
