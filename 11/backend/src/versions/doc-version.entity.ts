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

@Entity('doc_versions')
@Index(['docId', 'version'], { unique: true })
export class DocVersion {
  @PrimaryGeneratedColumn()
  id: number

  @Column()
  @Index()
  docId: number

  @ManyToOne(() => Doc, { onDelete: 'CASCADE' })
  @JoinColumn({ name: 'docId' })
  doc?: Doc

  @Column({ type: 'int' })
  version: number

  @Column({ type: 'json' })
  snapshot: any

  @Column({ nullable: true, length: 255 })
  remark?: string

  @Column({ nullable: true })
  authorId?: number

  @ManyToOne(() => User, { onDelete: 'SET NULL', nullable: true })
  @JoinColumn({ name: 'authorId' })
  author?: User

  @Column({ type: 'int', default: 0 })
  size: number

  @CreateDateColumn()
  createdAt: Date
}
