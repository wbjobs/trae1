import {
  Injectable,
  NotFoundException,
  ConflictException,
  BadRequestException,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, DataSource, In } from 'typeorm';
import { valid, gte, lt, major, minor, patch } from 'semver';
import { ComponentVersion } from './version.entity';
import { Dependency } from '../dependencies/dependency.entity';
import { Component } from '../components/component.entity';
import { CreateVersionDto } from './dto/create-version.dto';

@Injectable()
export class VersionsService {
  constructor(
    @InjectRepository(ComponentVersion)
    private readonly versionRepo: Repository<ComponentVersion>,
    @InjectRepository(Component)
    private readonly componentRepo: Repository<Component>,
    @InjectRepository(Dependency)
    private readonly depRepo: Repository<Dependency>,
    private readonly dataSource: DataSource,
  ) {}

  async create(dto: CreateVersionDto): Promise<ComponentVersion> {
    if (!valid(dto.version)) {
      throw new BadRequestException(`Invalid semver: ${dto.version}`);
    }
    const component = await this.componentRepo.findOne({
      where: { id: dto.componentId },
    });
    if (!component) {
      throw new NotFoundException(`Component #${dto.componentId} not found`);
    }
    const existing = await this.versionRepo.findOne({
      where: { componentId: dto.componentId, version: dto.version },
    });
    if (existing) {
      throw new ConflictException(
        `Version ${dto.version} already exists for component ${component.name}`,
      );
    }

    const queryRunner = this.dataSource.createQueryRunner();
    await queryRunner.connect();
    await queryRunner.startTransaction();
    try {
      if (dto.isLatest) {
        await this.versionRepo.update(
          { componentId: dto.componentId, isLatest: true },
          { isLatest: false },
        );
      }
      const version = this.versionRepo.create({
        componentId: dto.componentId,
        version: dto.version,
        changelog: dto.changelog,
        readme: dto.readme,
        previewSource: dto.previewSource,
        exports: dto.exports ?? [],
        peerDependencies: dto.peerDependencies ?? {},
        tag: dto.tag ?? 'stable',
        isLatest: dto.isLatest ?? false,
      });
      const saved = await queryRunner.manager.save(version);

      if (dto.dependencies && dto.dependencies.length) {
        const deps = dto.dependencies.map((d) =>
          this.depRepo.create({
            versionId: saved.id,
            dependencyName: d.dependencyName,
            dependencyVersion: d.dependencyVersion,
            importPath: d.importPath,
          }),
        );
        await queryRunner.manager.save(deps);
      }
      await queryRunner.commitTransaction();
      return this.findOne(saved.id);
    } catch (err) {
      await queryRunner.rollbackTransaction();
      throw err;
    } finally {
      await queryRunner.release();
    }
  }

  async findByComponent(componentId: string): Promise<ComponentVersion[]> {
    return this.versionRepo.find({
      where: { componentId },
      order: { createdAt: 'DESC' },
      relations: ['dependencies'],
    });
  }

  async findOne(id: string): Promise<ComponentVersion> {
    const v = await this.versionRepo.findOne({
      where: { id },
      relations: ['dependencies', 'component'],
    });
    if (!v) throw new NotFoundException(`Version #${id} not found`);
    return v;
  }

  async findLatest(componentId: string): Promise<ComponentVersion> {
    const v = await this.versionRepo.findOne({
      where: { componentId, isLatest: true },
      relations: ['dependencies', 'component'],
    });
    if (!v) throw new NotFoundException(`No latest version for component ${componentId}`);
    return v;
  }

  async suggestNext(componentId: string, bump: 'major' | 'minor' | 'patch'): Promise<string> {
    const latest = await this.findLatest(componentId).catch(() => null);
    const base = latest?.version ?? '0.0.0';
    let m = major(base);
    let n = minor(base);
    let p = patch(base);
    if (bump === 'major') { m += 1; n = 0; p = 0; }
    else if (bump === 'minor') { n += 1; p = 0; }
    else { p += 1; }
    return `${m}.${n}.${p}`;
  }

  async setLatest(id: string): Promise<ComponentVersion> {
    const v = await this.findOne(id);
    await this.versionRepo.update({ componentId: v.componentId, isLatest: true }, { isLatest: false });
    v.isLatest = true;
    return this.versionRepo.save(v);
  }

  async remove(id: string): Promise<void> {
    const { affected } = await this.versionRepo.delete(id);
    if (!affected) throw new NotFoundException(`Version #${id} not found`);
  }

  async rollback(targetVersionId: string): Promise<ComponentVersion> {
    const target = await this.findOne(targetVersionId);
    await this.versionRepo.update(
      { componentId: target.componentId, isLatest: true },
      { isLatest: false },
    );
    target.isLatest = true;
    return this.versionRepo.save(target);
  }

  async rollbackWithClone(targetVersionId: string, bump: 'patch' | 'minor' | 'major' = 'patch'): Promise<ComponentVersion> {
    const target = await this.findOne(targetVersionId);
    const newVersion = await this.suggestNext(target.componentId, bump);

    const queryRunner = this.dataSource.createQueryRunner();
    await queryRunner.connect();
    await queryRunner.startTransaction();
    try {
      await this.versionRepo.update(
        { componentId: target.componentId, isLatest: true },
        { isLatest: false },
      );

      const cloned = this.versionRepo.create({
        componentId: target.componentId,
        version: newVersion,
        changelog: `回滚自 ${target.version}`,
        readme: target.readme,
        previewSource: target.previewSource,
        exports: target.exports,
        peerDependencies: target.peerDependencies,
        tag: 'stable',
        isLatest: true,
      });
      const saved = await queryRunner.manager.save(cloned);

      if (target.dependencies?.length) {
        const deps = target.dependencies.map((d) =>
          this.depRepo.create({
            versionId: saved.id,
            dependencyName: d.dependencyName,
            dependencyVersion: d.dependencyVersion,
            importPath: d.importPath,
          }),
        );
        await queryRunner.manager.save(deps);
      }

      await queryRunner.commitTransaction();
      return this.findOne(saved.id);
    } catch (err) {
      await queryRunner.rollbackTransaction();
      throw err;
    } finally {
      await queryRunner.release();
    }
  }

  async getVersionTree(componentId: string): Promise<ComponentVersion[]> {
    return this.versionRepo
      .createQueryBuilder('v')
      .where({ componentId })
      .orderBy('v.createdAt', 'DESC')
      .getMany();
  }
}
