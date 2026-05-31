import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  ManyToOne,
  JoinColumn
} from 'typeorm'
import { User } from '../users/user.entity'

@Entity('projects')
export class Project {
  @PrimaryGeneratedColumn()
  id: number

  @Column({ length: 128 })
  name: string

  @Column({ type: 'text', nullable: true })
  description?: string

  @Column({ nullable: true, length: 512 })
  swaggerUrl?: string

  @Column({ type: 'json', nullable: true })
  swaggerData?: any

  @ManyToOne(() => User, { onDelete: 'SET NULL', nullable: true })
  @JoinColumn({ name: 'ownerId' })
  owner?: User

  @Column({ nullable: true })
  ownerId?: number

  @CreateDateColumn()
  createdAt: Date

  @UpdateDateColumn()
  updatedAt: Date
}
