import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  Index
} from 'typeorm'
import { User } from '../users/user.entity'
import { Project } from '../projects/project.entity'

@Entity('members')
@Index(['userId', 'projectId'], { unique: true })
export class Member {
  @PrimaryGeneratedColumn()
  id: number

  @Column()
  userId: number

  @ManyToOne(() => User, { onDelete: 'CASCADE' })
  @JoinColumn({ name: 'userId' })
  user?: User

  @Column({ nullable: true })
  projectId?: number

  @ManyToOne(() => Project, { onDelete: 'CASCADE', nullable: true })
  @JoinColumn({ name: 'projectId' })
  project?: Project

  @Column({ type: 'enum', enum: ['admin', 'editor', 'viewer'], default: 'viewer' })
  role: 'admin' | 'editor' | 'viewer'

  @Column({ default: true })
  active: boolean

  @CreateDateColumn()
  joinedAt: Date
}
