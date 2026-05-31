import { Injectable, Logger } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { ComponentVersion } from '../versions/version.entity';
import { Dependency } from '../dependencies/dependency.entity';
import { Component } from '../components/component.entity';

export interface BundleSize {
  rawBytes: number;
  gzipBytes: number;
  brotliBytes: number;
  rawFormatted: string;
  gzipFormatted: string;
  brotliFormatted: string;
}

export interface BundleReport {
  versionId: string;
  componentName: string;
  version: string;
  exportCount: number;
  dependencyCount: number;
  dependencyNames: string[];
  bundleSize: BundleSize;
  compressionRatio: number;
  treeShakingPotential: number;
  unusedExports: string[];
  optimizationSuggestions: string[];
  perExportSize: Array<{ name: string; estimatedBytes: number }>;
}

@Injectable()
export class BundleService {
  private readonly logger = new Logger(BundleService.name);

  constructor(
    @InjectRepository(ComponentVersion)
    private readonly versionRepo: Repository<ComponentVersion>,
    @InjectRepository(Dependency)
    private readonly depRepo: Repository<Dependency>,
    @InjectRepository(Component)
    private readonly componentRepo: Repository<Component>,
  ) {}

  private formatBytes(bytes: number): string {
    if (bytes === 0) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(1024));
    return `${(bytes / Math.pow(1024, i)).toFixed(2)} ${units[i]}`;
  }

  private estimateGzip(raw: number): number {
    if (raw < 200) return Math.round(raw * 0.85);
    if (raw < 2048) return Math.round(raw * 0.55);
    if (raw < 65536) return Math.round(raw * 0.35);
    return Math.round(raw * 0.28);
  }

  private estimateBrotli(raw: number): number {
    if (raw < 200) return Math.round(raw * 0.8);
    if (raw < 2048) return Math.round(raw * 0.45);
    if (raw < 65536) return Math.round(raw * 0.25);
    return Math.round(raw * 0.18);
  }

  private computeBundleSize(rawBytes: number): BundleSize {
    const gzip = this.estimateGzip(rawBytes);
    const brotli = this.estimateBrotli(rawBytes);
    return {
      rawBytes,
      gzipBytes: gzip,
      brotliBytes: brotli,
      rawFormatted: this.formatBytes(rawBytes),
      gzipFormatted: this.formatBytes(gzip),
      brotliFormatted: this.formatBytes(brotli),
    };
  }

  private estimateRawSize(v: ComponentVersion, depNames: string[]): number {
    const baseSize = 4096;
    const exportSize = (v.exports?.length ?? 0) * 2048;
    const depsSize = depNames.length * 1024;
    const previewSize = (v.previewSource?.length ?? 0) * 2;
    const readmeSize = (v.readme?.length ?? 0) * 1;
    return baseSize + exportSize + depsSize + previewSize + readmeSize;
  }

  async analyze(versionId: string): Promise<BundleReport> {
    const v = await this.versionRepo.findOne({
      where: { id: versionId },
      relations: ['component', 'dependencies'],
    });
    if (!v) throw new Error(`Version #${versionId} not found`);

    const exports = v.exports ?? [];
    const depNames = (v.dependencies ?? []).map((d) => d.dependencyName);
    const rawBytes = this.estimateRawSize(v, depNames);
    const bundleSize = this.computeBundleSize(rawBytes);
    const compressionRatio = rawBytes > 0 ? bundleSize.gzipBytes / rawBytes : 0;

    const perExportSize = exports.map((name) => ({
      name,
      estimatedBytes: Math.max(1024, Math.round(rawBytes / Math.max(exports.length, 1))),
    }));

    const treeShakingPotential = exports.length > 0
      ? Math.min(0.95, 1 - 1 / (exports.length + 1))
      : 0;

    const suggestions: string[] = [];
    if (exports.length > 15) {
      suggestions.push(`导出成员过多 (${exports.length})，建议拆分为多个子包或使用命名空间导出`);
    }
    if (compressionRatio > 0.6) {
      suggestions.push('压缩率偏高，建议检查是否包含未压缩的源码或资源文件');
    }
    if (depNames.length > 20) {
      suggestions.push(`依赖数量较多 (${depNames.length})，建议审视是否可移除冗余依赖`);
    }
    if (rawBytes > 512000) {
      suggestions.push('包体积超过 500KB，建议拆分为按需加载的子模块');
    }
    if (treeShakingPotential > 0.7 && exports.length > 3) {
      suggestions.push('Tree Shaking 潜力高，推荐使用按需引入以减小打包体积');
    }
    if (suggestions.length === 0) {
      suggestions.push('当前包配置良好，无需额外优化');
    }

    const unusedExports = exports.filter((e) =>
      !(v.readme?.includes(e) || v.previewSource?.includes(e)),
    );

    return {
      versionId: v.id,
      componentName: v.component.name,
      version: v.version,
      exportCount: exports.length,
      dependencyCount: depNames.length,
      dependencyNames: depNames,
      bundleSize,
      compressionRatio,
      treeShakingPotential,
      unusedExports,
      optimizationSuggestions: suggestions,
      perExportSize,
    };
  }

  async compare(versionIdA: string, versionIdB: string): Promise<{
    a: BundleReport;
    b: BundleReport;
    rawDiff: number;
    gzipDiff: number;
    brotliDiff: number;
    percentChange: number;
    summary: string;
  }> {
    const [a, b] = await Promise.all([this.analyze(versionIdA), this.analyze(versionIdB)]);
    const rawDiff = b.bundleSize.rawBytes - a.bundleSize.rawBytes;
    const gzipDiff = b.bundleSize.gzipBytes - a.bundleSize.gzipBytes;
    const brotliDiff = b.bundleSize.brotliBytes - a.bundleSize.brotliBytes;
    const percentChange = a.bundleSize.rawBytes > 0
      ? (rawDiff / a.bundleSize.rawBytes) * 100
      : 0;

    let summary = '';
    if (percentChange < -5) summary = `体积减少了 ${Math.abs(percentChange).toFixed(1)}%，优化效果显著`;
    else if (percentChange > 5) summary = `体积增加了 ${percentChange.toFixed(1)}%，建议检查新增内容`;
    else summary = `体积变化不大（${percentChange.toFixed(1)}%）`;

    return { a, b, rawDiff, gzipDiff, brotliDiff, percentChange, summary };
  }
}
