import { IsString, IsNotEmpty, IsOptional, MaxLength } from 'class-validator'

export class CreateProjectDto {
  @IsString()
  @IsNotEmpty()
  @MaxLength(128)
  name: string

  @IsOptional()
  @IsString()
  description?: string

  @IsOptional()
  @IsString()
  @MaxLength(512)
  swaggerUrl?: string
}

export class UpdateProjectDto {
  @IsOptional()
  @IsString()
  @MaxLength(128)
  name?: string

  @IsOptional()
  @IsString()
  description?: string

  @IsOptional()
  @IsString()
  @MaxLength(512)
  swaggerUrl?: string
}

export class ImportSwaggerDto {
  @IsOptional()
  @IsString()
  swaggerUrl?: string

  @IsOptional()
  swaggerContent?: any
}
