import { IsArray, IsString } from 'class-validator';

export class AnalyzeDto {
  @IsArray()
  @IsString({ each: true })
  versionIds: string[];
}
