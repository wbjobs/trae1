export interface Component {
  id: string;
  name: string;
  description?: string;
  status: 'unpublished' | 'published' | 'deprecated';
  category?: string;
  owner?: string;
  downloadCount: number;
  previewCount: number;
  referenceCount: number;
  popularityScore: number;
  createdAt: string;
  updatedAt: string;
  versions?: ComponentVersion[];
}

export interface ComponentVersion {
  id: string;
  componentId: string;
  component?: Component;
  version: string;
  changelog?: string;
  readme?: string;
  previewSource?: string;
  exports: string[];
  peerDependencies: Record<string, string>;
  tag: 'alpha' | 'beta' | 'rc' | 'stable' | 'deprecated';
  isLatest: boolean;
  createdAt: string;
  dependencies?: Dependency[];
}

export interface Dependency {
  id: string;
  versionId: string;
  dependencyName: string;
  dependencyVersion: string;
  importPath?: string;
}

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

export interface ImportConfig {
  esm: string;
  cjs: string;
  unpkg: string;
  sideEffects: string[];
  treeShakePaths: string[];
}

export interface PreviewPayload {
  versionId: string;
  componentName: string;
  version: string;
  previewSource: string;
  exports: string[];
  importConfig: ImportConfig;
}

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

export interface BundleCompare {
  a: BundleReport;
  b: BundleReport;
  rawDiff: number;
  gzipDiff: number;
  brotliDiff: number;
  percentChange: number;
  summary: string;
}

export interface DocPayload {
  componentName: string;
  description?: string;
  latestVersion: string;
  versions: Array<{ version: string; tag: string; changelog?: string; createdAt: string }>;
  exports: string[];
  dependencies: Array<{ name: string; version: string; importPath?: string }>;
  readme?: string;
  installation: { npm: string; yarn: string; pnpm: string };
  importGuide: { esm: string; cjs: string; treeShake: string[] };
}

export interface CreateComponentInput {
  name: string;
  description?: string;
  category?: string;
  owner?: string;
  status?: 'unpublished' | 'published' | 'deprecated';
}

export interface CreateVersionInput {
  componentId: string;
  version: string;
  changelog?: string;
  readme?: string;
  previewSource?: string;
  exports?: string[];
  peerDependencies?: Record<string, string>;
  tag?: 'alpha' | 'beta' | 'rc' | 'stable' | 'deprecated';
  isLatest?: boolean;
  dependencies?: Array<{
    dependencyName: string;
    dependencyVersion: string;
    importPath?: string;
  }>;
}
