import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  OneToMany,
} from 'typeorm';
import { ComponentVersion } from '../versions/version.entity';

@Entity('components')
export class Component {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ unique: true })
  name: string;

  @Column({ type: 'text', nullable: true })
  description?: string;

  @Column({ default: 'unpublished' })
  status: 'unpublished' | 'published' | 'deprecated';

  @Column({ type: 'text', nullable: true })
  category?: string;

  @Column({ type: 'text', nullable: true })
  owner?: string;

  @Column({ default: 0 })
  downloadCount: number;

  @Column({ default: 0 })
  previewCount: number;

  @Column({ default: 0 })
  referenceCount: number;

  @Column({ type: 'double precision', default: 0 })
  popularityScore: number;

  @CreateDateColumn()
  createdAt: Date;

  @UpdateDateColumn()
  updatedAt: Date;

  @OneToMany(() => ComponentVersion, (v) => v.component, { cascade: true })
  versions: ComponentVersion[];
}
