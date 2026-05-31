import { IsString, IsOptional, IsNotEmpty } from 'class-validator'

export class CreateVersionDto {
  @IsNotEmpty()
  snapshot: any

  @IsOptional()
  @IsString()
  remark?: string
}

export class CompareVersionsDto {
  @IsNotEmpty()
  leftVersionId: number

  @IsNotEmpty()
  rightVersionId: number
}
