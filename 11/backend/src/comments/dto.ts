import { IsString, IsNotEmpty, IsOptional, IsEnum, IsInt, IsArray, MinLength } from 'class-validator'

export class CreateCommentDto {
  @IsInt()
  @IsNotEmpty()
  docId: number

  @IsOptional()
  @IsInt()
  parentId?: number

  @IsString()
  @IsNotEmpty()
  @MinLength(1)
  content: string

  @IsOptional()
  anchor?: {
    field?: string
    lineNumber?: number
    selection?: string
  }

  @IsOptional()
  @IsArray()
  mentions?: number[]
}

export class UpdateCommentDto {
  @IsOptional()
  @IsString()
  content?: string

  @IsOptional()
  @IsEnum(['open', 'resolved', 'closed'])
  status?: 'open' | 'resolved' | 'closed'
}
