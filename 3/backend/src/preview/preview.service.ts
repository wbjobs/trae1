import { Injectable, NotFoundException, Logger } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { ComponentVersion } from '../versions/version.entity';
import { Dependency } from '../dependencies/dependency.entity';

export interface PreviewPayload {
  versionId: string;
  componentName: string;
  version: string;
  previewSource: string;
  exports: string[];
  importConfig: ImportConfig;
}

export interface ImportConfig {
  esm: string;
  cjs: string;
  unpkg: string;
  sideEffects: string[];
  treeShakePaths: string[];
}

@Injectable()
export class PreviewService {
  private readonly logger = new Logger(PreviewService.name);

  constructor(
    @InjectRepository(ComponentVersion)
    private readonly versionRepo: Repository<ComponentVersion>,
    @InjectRepository(Dependency)
    private readonly depRepo: Repository<Dependency>,
  ) {}

  async getPreview(versionId: string): Promise<PreviewPayload> {
    const v = await this.versionRepo.findOne({
      where: { id: versionId },
      relations: ['component'],
    });
    if (!v) throw new NotFoundException(`Version #${versionId} not found`);
    return {
      versionId: v.id,
      componentName: v.component.name,
      version: v.version,
      previewSource: v.previewSource ?? '',
      exports: v.exports ?? [],
      importConfig: this.buildImportConfig(v),
    };
  }

  private async resolveImportPaths(v: ComponentVersion): Promise<Map<string, string>> {
    const deps = await this.depRepo.find({ where: { versionId: v.id } });
    const map = new Map<string, string>();
    for (const dep of deps) {
      if (dep.importPath) {
        map.set(dep.dependencyName, dep.importPath);
      }
    }
    return map;
  }

  private sanitizeExportName(name: string): string {
    return name
      .replace(/[^a-zA-Z0-9_-]/g, '-')
      .replace(/^-+|-+$/g, '')
      .toLowerCase();
  }

  buildImportConfig(v: ComponentVersion): ImportConfig {
    const pkg = v.component.name;
    const exports = v.exports ?? [];
    const version = v.version;

    const esm = exports.length
      ? `import { ${exports.join(', ')} } from '${pkg}';`
      : `import ${pkg} from '${pkg}';`;

    const cjs = exports.length
      ? `const { ${exports.join(', ')} } = require('${pkg}');`
      : `const ${pkg} = require('${pkg}');`;

    const unpkg = `https://unpkg.com/${pkg}@${version}/dist/index.esm.js`;

    const sideEffects = exports.map(
      (e) => `${pkg}/es/${this.sanitizeExportName(e)}.js`,
    );

    const treeShakePaths = exports.map(
      (e) => `import ${e} from '${pkg}/es/${this.sanitizeExportName(e)}';`,
    );

    return { esm, cjs, unpkg, sideEffects, treeShakePaths };
  }

  validateImportPath(path: string): boolean {
    if (!path || typeof path !== 'string') return false;
    if (path.includes('\0') || path.includes('\n') || path.includes('\r')) return false;
    if (path.startsWith('..') || path.startsWith('/')) return false;
    if (path.length > 256) return false;
    return true;
  }
}
