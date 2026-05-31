import { Injectable, NotFoundException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { Component } from '../components/component.entity';
import { ComponentVersion } from '../versions/version.entity';
import { Dependency } from '../dependencies/dependency.entity';

export interface DocPayload {
  componentName: string;
  description?: string;
  latestVersion: string;
  versions: Array<{ version: string; tag: string; changelog?: string; createdAt: Date }>;
  exports: string[];
  dependencies: Array<{ name: string; version: string; importPath?: string }>;
  readme?: string;
  installation: { npm: string; yarn: string; pnpm: string };
  importGuide: { esm: string; cjs: string; treeShake: string[] };
}

@Injectable()
export class DocsService {
  constructor(
    @InjectRepository(Component)
    private readonly componentRepo: Repository<Component>,
    @InjectRepository(ComponentVersion)
    private readonly versionRepo: Repository<ComponentVersion>,
    @InjectRepository(Dependency)
    private readonly depRepo: Repository<Dependency>,
  ) {}

  async generate(componentName: string): Promise<DocPayload> {
    const c = await this.componentRepo.findOne({
      where: { name: componentName },
      relations: ['versions'],
    });
    if (!c) throw new NotFoundException(`Component "${componentName}" not found`);
    const latest = c.versions.find((v) => v.isLatest) ?? c.versions[0];
    if (!latest) throw new NotFoundException(`No version for ${componentName}`);

    const deps = await this.depRepo.find({ where: { versionId: latest.id } });
    const pkg = c.name;
    return {
      componentName: c.name,
      description: c.description,
      latestVersion: latest.version,
      versions: c.versions
        .slice()
        .sort((a, b) => b.createdAt.getTime() - a.createdAt.getTime())
        .map((v) => ({
          version: v.version,
          tag: v.tag,
          changelog: v.changelog,
          createdAt: v.createdAt,
        })),
      exports: latest.exports ?? [],
      dependencies: deps.map((d) => ({
        name: d.dependencyName,
        version: d.dependencyVersion,
        importPath: d.importPath,
      })),
      readme: latest.readme,
      installation: {
        npm: `npm install ${pkg}@${latest.version} --save`,
        yarn: `yarn add ${pkg}@${latest.version}`,
        pnpm: `pnpm add ${pkg}@${latest.version}`,
      },
      importGuide: {
        esm: `import { ${(latest.exports ?? []).join(', ')} } from '${pkg}';`,
        cjs: `const { ${(latest.exports ?? []).join(', ')} } = require('${pkg}');`,
        treeShake: (latest.exports ?? []).map(
          (e) => `import ${e} from '${pkg}/es/${e}';`,
        ),
      },
    };
  }

  async generateMarkdown(componentName: string): Promise<string> {
    const doc = await this.generate(componentName);
    const lines: string[] = [];
    lines.push(`# ${doc.componentName}`);
    if (doc.description) lines.push(`\n${doc.description}\n`);
    lines.push(`\n**Latest:** ${doc.latestVersion}\n`);

    lines.push('## Installation\n');
    lines.push('```bash');
    lines.push(doc.installation.npm);
    lines.push('```\n');

    lines.push('## Versions\n');
    lines.push('| Version | Tag | Changelog | Date |');
    lines.push('|---------|-----|-----------|------|');
    for (const v of doc.versions) {
      lines.push(
        `| ${v.version} | ${v.tag} | ${(v.changelog ?? '').replace(/\|/g, '/')} | ${v.createdAt.toISOString().slice(0, 10)} |`,
      );
    }

    lines.push('\n## Exports\n');
    lines.push('```ts');
    lines.push(doc.importGuide.esm);
    lines.push('```\n');

    lines.push('## Dependencies\n');
    if (doc.dependencies.length) {
      lines.push('| Name | Version |');
      lines.push('|------|---------|');
      for (const d of doc.dependencies) {
        lines.push(`| ${d.name} | ${d.version} |`);
      }
    } else {
      lines.push('_No dependencies._');
    }

    if (doc.readme) {
      lines.push('\n---\n');
      lines.push(doc.readme);
    }
    return lines.join('\n');
  }
}
