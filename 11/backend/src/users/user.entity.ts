import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  Index
} from 'typeorm'

@Entity('users')
export class User {
  @PrimaryGeneratedColumn()
  id: number

  @Index({ unique: true })
  @Column({ length: 64 })
  username: string

  @Column({ length: 64 })
  nickname: string

  @Index({ unique: true })
  @Column({ length: 128 })
  email: string

  @Column({ length: 128, select: false })
  password: string

  @Column({ type: 'enum', enum: ['admin', 'editor', 'viewer'], default: 'viewer' })
  role: 'admin' | 'editor' | 'viewer'

  @Column({ nullable: true, length: 255 })
  avatar?: string

  @Column({ default: true })
  active: boolean

  @CreateDateColumn()
  createdAt: Date

  @UpdateDateColumn()
  updatedAt: Date
}
