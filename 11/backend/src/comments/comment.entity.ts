import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  Index
} from 'typeorm'
import { Doc } from '../docs/doc.entity'
import { User } from '../users/user.entity'

@Entity('doc_comments')
export class DocComment {
  @PrimaryGeneratedColumn()
  id: number

  @Column()
  @Index()
  docId: number

  @ManyToOne(() => Doc, { onDelete: 'CASCADE' })
  @JoinColumn({ name: 'docId' })
  doc?: Doc

  @Column({ nullable: true })
  @Index()
  parentId?: number

  @Column({ type: 'text' })
  content: string

  @Column({ type: 'json', nullable: true })
  anchor?: {
    field?: string
    lineNumber?: number
    selection?: string
  }

  @Column({ type: 'enum', enum: ['open', 'resolved', 'closed'], default: 'open' })
  status: 'open' | 'resolved' | 'closed'

  @Column({ default: 0 })
  upvotes: number

  @Column({ nullable: true })
  authorId?: number

  @ManyToOne(() => User, { onDelete: 'SET NULL', nullable: true })
  @JoinColumn({ name: 'authorId' })
  author?: User

  @Column({ type: 'json', nullable: true })
  mentions?: number[]

  @CreateDateColumn()
  createdAt: Date
}
