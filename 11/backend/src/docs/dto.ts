import { IsString, IsNotEmpty, IsOptional, IsEnum, IsArray } from 'class-validator'

export class CreateDocDto {
  @IsNotEmpty()
  projectId: number

  @IsString()
  @IsNotEmpty()
  title: string

  @IsOptional()
  @IsString()
  description?: string

  @IsOptional()
  @IsString()
  method?: string

  @IsOptional()
  @IsString()
  path?: string

  @IsOptional()
  @IsString()
  category?: string

  @IsOptional()
  @IsArray()
  tags?: string[]

  @IsOptional()
  @IsString()
  content?: string

  @IsOptional()
  @IsString()
  contentType?: string

  @IsOptional()
  requestParams?: any

  @IsOptional()
  requestBody?: any

  @IsOptional()
  response?: any

  @IsOptional()
  requestExample?: any

  @IsOptional()
  @IsEnum(['draft', 'published'])
  status?: 'draft' | 'published'
}

export class UpdateDocDto {
  @IsOptional()
  @IsString()
  title?: string

  @IsOptional()
  description?: string

  @IsOptional()
  method?: string

  @IsOptional()
  path?: string

  @IsOptional()
  category?: string

  @IsOptional()
  tags?: string[]

  @IsOptional()
  content?: string

  @IsOptional()
  contentType?: string

  @IsOptional()
  requestParams?: any

  @IsOptional()
  requestBody?: any

  @IsOptional()
  response?: any

  @IsOptional()
  requestExample?: any

  @IsOptional()
  status?: 'draft' | 'published'
}

export class MoveCategoryDto {
  @IsNotEmpty()
  docId: number

  @IsString()
  @IsNotEmpty()
  category: string
}

export class BatchRemoveDto {
  @IsArray()
  ids: number[]
}

export class DebugDto {
  @IsString()
  @IsNotEmpty()
  method: string

  @IsString()
  @IsNotEmpty()
  url: string

  @IsOptional()
  headers?: Record<string, string>

  @IsOptional()
  params?: Record<string, any>

  @IsOptional()
  body?: any
}

export class BatchImportDto {
  @IsOptional()
  @IsArray()
  docs?: any[]

  @IsOptional()
  @IsString()
  format?: 'json' | 'yaml' | 'markdown'

  @IsOptional()
  raw?: any
}

export class ExportDocsDto {
  @IsOptional()
  @IsString()
  @IsEnum(['json', 'yaml', 'markdown'])
  format?: 'json' | 'yaml' | 'markdown'

  @IsOptional()
  @IsArray()
  ids?: number[]

  @IsOptional()
  @IsString()
  category?: string

  @IsOptional()
  @IsString()
  keyword?: string
}
