import { IsString, IsOptional, IsIn, MaxLength } from 'class-validator';

export class CreateComponentDto {
  @IsString()
  @MaxLength(128)
  name: string;

  @IsOptional()
  @IsString()
  description?: string;

  @IsOptional()
  @IsIn(['unpublished', 'published', 'deprecated'])
  status?: 'unpublished' | 'published' | 'deprecated';

  @IsOptional()
  @IsString()
  category?: string;

  @IsOptional()
  @IsString()
  owner?: string;
}

export class UpdateComponentDto {
  @IsOptional()
  @IsString()
  @MaxLength(128)
  name?: string;

  @IsOptional()
  @IsString()
  description?: string;

  @IsOptional()
  @IsIn(['unpublished', 'published', 'deprecated'])
  status?: 'unpublished' | 'published' | 'deprecated';

  @IsOptional()
  @IsString()
  category?: string;

  @IsOptional()
  @IsString()
  owner?: string;
}
