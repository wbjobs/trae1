import {
  IsString,
  IsOptional,
  IsArray,
  IsObject,
  IsIn,
  IsBoolean,
  ValidateNested,
  ArrayMinSize,
  MaxLength,
} from 'class-validator';
import { Type } from 'class-transformer';

export class DependencyDto {
  @IsString()
  dependencyName: string;

  @IsString()
  dependencyVersion: string;

  @IsOptional()
  @IsString()
  importPath?: string;
}

export class CreateVersionDto {
  @IsString()
  @MaxLength(64)
  componentId: string;

  @IsString()
  @MaxLength(64)
  version: string;

  @IsOptional()
  @IsString()
  changelog?: string;

  @IsOptional()
  @IsString()
  readme?: string;

  @IsOptional()
  @IsString()
  previewSource?: string;

  @IsOptional()
  @IsArray()
  @IsString({ each: true })
  exports?: string[];

  @IsOptional()
  @IsObject()
  peerDependencies?: Record<string, string>;

  @IsOptional()
  @IsIn(['alpha', 'beta', 'rc', 'stable', 'deprecated'])
  tag?: 'alpha' | 'beta' | 'rc' | 'stable' | 'deprecated';

  @IsOptional()
  @IsBoolean()
  isLatest?: boolean;

  @IsOptional()
  @IsArray()
  @ValidateNested({ each: true })
  @Type(() => DependencyDto)
  dependencies?: DependencyDto[];
}
