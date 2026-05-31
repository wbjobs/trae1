import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { ComponentVersion } from '../versions/version.entity';

@Entity('dependencies')
export class Dependency {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column()
  @Index()
  versionId: string;

  @ManyToOne(() => ComponentVersion, (v) => v.dependencies, { onDelete: 'CASCADE' })
  @JoinColumn({ name: 'versionId' })
  version: ComponentVersion;

  @Column()
  dependencyName: string;

  @Column()
  dependencyVersion: string;

  @Column({ type: 'text', nullable: true })
  importPath?: string;
}
