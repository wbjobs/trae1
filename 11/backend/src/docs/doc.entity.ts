import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  ManyToOne,
  JoinColumn,
  Index
} from 'typeorm'
import { Project } from '../projects/project.entity'
import { User } from '../users/user.entity'

@Entity('docs')
export class Doc {
  @PrimaryGeneratedColumn()
  id: number

  @Column()
  @Index()
  projectId: number

  @ManyToOne(() => Project, { onDelete: 'CASCADE' })
  @JoinColumn({ name: 'projectId' })
  project?: Project

  @Column({ length: 255 })
  title: string

  @Column({ type: 'text', nullable: true })
  description?: string

  @Column({ length: 16, default: 'GET' })
  method?: string

  @Column({ length: 512, nullable: true })
  path?: string

  @Column({ length: 64, nullable: true, default: '默认分类' })
  category?: string

  @Column({ type: 'json', nullable: true })
  tags?: string[]

  @Column({ type: 'text', nullable: true })
  content?: string

  @Column({ length: 64, default: 'application/json' })
  contentType?: string

  @Column({ type: 'json', nullable: true })
  requestParams?: any

  @Column({ type: 'json', nullable: true })
  requestBody?: any

  @Column({ type: 'json', nullable: true })
  response?: any

  @Column({ type: 'json', nullable: true })
  requestExample?: any

  @Column({ type: 'enum', enum: ['draft', 'published'], default: 'draft' })
  status?: 'draft' | 'published'

  @Column({ nullable: true })
  createdById?: number

  @ManyToOne(() => User, { onDelete: 'SET NULL', nullable: true })
  @JoinColumn({ name: 'createdById' })
  createdBy?: User

  @Column({ nullable: true })
  updatedById?: number

  @ManyToOne(() => User, { onDelete: 'SET NULL', nullable: true })
  @JoinColumn({ name: 'updatedById' })
  updatedBy?: User

  @CreateDateColumn()
  createdAt: Date

  @UpdateDateColumn()
  updatedAt: Date
}
