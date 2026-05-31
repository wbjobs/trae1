import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  OneToMany,
} from 'typeorm';
import { Component } from '../components/component.entity';
import { Dependency } from '../dependencies/dependency.entity';

@Entity('component_versions')
export class ComponentVersion {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column()
  componentId: string;

  @ManyToOne(() => Component, (c) => c.versions, { onDelete: 'CASCADE' })
  @JoinColumn({ name: 'componentId' })
  component: Component;

  @Column()
  version: string;

  @Column({ type: 'text', nullable: true })
  changelog?: string;

  @Column({ type: 'text', nullable: true })
  readme?: string;

  @Column({ type: 'text', nullable: true })
  previewSource?: string;

  @Column({ type: 'jsonb', default: '[]' })
  exports: string[];

  @Column({ type: 'jsonb', default: '{}' })
  peerDependencies: Record<string, string>;

  @Column({ default: 'stable' })
  tag: 'alpha' | 'beta' | 'rc' | 'stable' | 'deprecated';

  @Column({ default: false })
  isLatest: boolean;

  @CreateDateColumn()
  createdAt: Date;

  @OneToMany(() => Dependency, (d) => d.version, { cascade: true })
  dependencies: Dependency[];
}
