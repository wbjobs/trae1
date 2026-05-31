import { Injectable, Logger } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, In } from 'typeorm';
import { intersects, Range, minVersion, maxSatisfying, valid, gt, lt, coerce } from 'semver';
import { Dependency } from './dependency.entity';
import { ComponentVersion } from '../versions/version.entity';

export interface ConflictInfo {
  dependencyName: string;
  conflicts: Array<{
    versionId: string;
    componentName: string;
    componentVersion: string;
    requiredVersion: string;
  }>;
  suggestion?: string;
}

interface CacheEntry<T> {
  value: T;
  expiresAt: number;
}

@Injectable()
export class DependenciesService {
  private readonly logger = new Logger(DependenciesService.name);
  private readonly rangeCache = new Map<string, CacheEntry<Range>>();
  private readonly validCache = new Map<string, CacheEntry<boolean>>();
  private readonly CACHE_TTL = 5 * 60 * 1000;

  constructor(
    @InjectRepository(Dependency)
    private readonly depRepo: Repository<Dependency>,
    @InjectRepository(ComponentVersion)
    private readonly versionRepo: Repository<ComponentVersion>,
  ) {}

  private cachedRange(raw: string): Range | null {
    const now = Date.now();
    const cached = this.rangeCache.get(raw);
    if (cached && cached.expiresAt > now) return cached.value;
    try {
      const r = new Range(raw);
      const result = r.range === '<0.0.0-0' || r.range === '' ? null : r;
      this.rangeCache.set(raw, { value: result, expiresAt: now + this.CACHE_TTL });
      return result;
    } catch {
      this.rangeCache.set(raw, { value: null, expiresAt: now + this.CACHE_TTL });
      return null;
    }
  }

  private cachedValid(raw: string): boolean {
    const now = Date.now();
    const cached = this.validCache.get(raw);
    if (cached && cached.expiresAt > now) return cached.value;
    const result = !!valid(raw);
    this.validCache.set(raw, { value: result, expiresAt: now + this.CACHE_TTL });
    return result;
  }

  clearCache(): void {
    this.rangeCache.clear();
    this.validCache.clear();
    this.logger.debug('Dependency resolution cache cleared');
  }

  async listByVersion(versionId: string): Promise<Dependency[]> {
    return this.depRepo.find({ where: { versionId } });
  }

  async analyze(versionIds: string[]): Promise<ConflictInfo[]> {
    if (!versionIds?.length) return [];

    const deps = await this.depRepo.find({
      where: { versionId: In(versionIds) },
      relations: ['version', 'version.component'],
    });

    const byName = new Map<string, Dependency[]>();
    for (const d of deps) {
      const arr = byName.get(d.dependencyName) ?? [];
      arr.push(d);
      byName.set(d.dependencyName, arr);
    }

    const conflicts: ConflictInfo[] = [];
    const names = Array.from(byName.keys());

    for (const name of names) {
      const list = byName.get(name)!;
      if (list.length < 2) continue;

      const ranges = list.map((d) => d.dependencyVersion);
      const hasConflict = !this.hasCommonVersionParallel(ranges);
      if (!hasConflict) continue;

      conflicts.push({
        dependencyName: name,
        conflicts: list.map((d) => ({
          versionId: d.versionId,
          componentName: d.version?.component?.name ?? 'unknown',
          componentVersion: d.version?.version ?? 'unknown',
          requiredVersion: d.dependencyVersion,
        })),
        suggestion: this.suggestResolution(ranges),
      });
    }
    return conflicts;
  }

  private isValidRange(raw: string): boolean {
    if (!raw) return false;
    if (this.cachedValid(raw)) return true;
    return this.cachedRange(raw) !== null;
  }

  private hasCommonVersionParallel(ranges: string[]): boolean {
    const validRanges: Range[] = [];
    for (const r of ranges) {
      const parsed = this.cachedRange(r);
      if (parsed) validRanges.push(parsed);
    }
    if (validRanges.length < 2) return true;

    try {
      for (let i = 0; i < validRanges.length; i++) {
        for (let j = i + 1; j < validRanges.length; j++) {
          if (!intersects(validRanges[i], validRanges[j])) {
            return false;
          }
        }
      }
      return true;
    } catch (e) {
      this.logger.warn(`Range intersect error: ${e}`);
      return false;
    }
  }

  private suggestResolution(ranges: string[]): string | undefined {
    try {
      const mins: string[] = [];
      for (const r of ranges) {
        try {
          const mv = minVersion(r);
          if (mv?.version) mins.push(mv.version);
        } catch { /* ignore */ }
      }
      if (mins.length) {
        const sorted = [...mins].sort((a, b) => (gt(a, b) ? 1 : -1)).reverse();
        const highest = sorted[0];

        let allSatisfy = true;
        for (const r of ranges) {
          const range = this.cachedRange(r);
          if (!range) continue;
          try {
            const satisfied = maxSatisfying([highest], range);
            if (!satisfied) {
              allSatisfy = false;
              break;
            }
          } catch { /* ignore */ }
        }

        if (allSatisfy) {
          return `建议对齐到 >= ${highest}`;
        }
        return `建议对齐到 >= ${highest}（需验证全部范围兼容）`;
      }
    } catch {
      /* ignore */
    }
    return undefined;
  }

  async listAll(limit = 200): Promise<Dependency[]> {
    return this.depRepo
      .createQueryBuilder('dep')
      .leftJoinAndSelect('dep.version', 'version')
      .leftJoinAndSelect('version.component', 'component')
      .orderBy('version.createdAt', 'DESC')
      .take(limit)
      .getMany();
  }

  async batchAnalyze(versionIdGroups: string[][]): Promise<ConflictInfo[][]> {
    return Promise.all(versionIdGroups.map((group) => this.analyze(group)));
  }
}
