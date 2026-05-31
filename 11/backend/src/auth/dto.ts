import { IsString, IsNotEmpty, IsEmail, MinLength, IsOptional } from 'class-validator'

export class LoginDto {
  @IsString()
  @IsNotEmpty()
  username: string

  @IsString()
  @IsNotEmpty()
  password: string
}

export class RegisterDto {
  @IsString()
  @IsNotEmpty()
  username: string

  @IsString()
  @IsNotEmpty()
  nickname: string

  @IsEmail()
  @IsNotEmpty()
  email: string

  @IsString()
  @MinLength(6)
  password: string
}

export class UpdateProfileDto {
  @IsOptional()
  @IsString()
  nickname?: string

  @IsOptional()
  @IsEmail()
  email?: string

  @IsOptional()
  @IsString()
  avatar?: string
}
