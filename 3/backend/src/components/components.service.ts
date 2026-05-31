import { Injectable, NotFoundException, ConflictException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { Component } from './component.entity';
import { CreateComponentDto, UpdateComponentDto } from './dto/create-component.dto';

@Injectable()
export class ComponentsService {
  constructor(
    @InjectRepository(Component)
    private readonly repo: Repository<Component>,
  ) {}

  private computeScore(c: Partial<Component>): number {
    const d = c.downloadCount ?? 0;
    const p = c.previewCount ?? 0;
    const r = c.referenceCount ?? 0;
    return d * 3 + p * 1 + r * 2;
  }

  async create(dto: CreateComponentDto): Promise<Component> {
    const exists = await this.repo.findOne({ where: { name: dto.name } });
    if (exists) throw new ConflictException(`Component "${dto.name}" already exists`);
    const comp = this.repo.create(dto);
    return this.repo.save(comp);
  }

  async findAll(category?: string, sortBy = 'createdAt'): Promise<Component[]> {
    const order: Record<string, 'ASC' | 'DESC'> = {};
    if (sortBy === 'popularity') {
      order.popularityScore = 'DESC';
    } else {
      order.createdAt = 'DESC';
    }
    return this.repo.find({
      where: category ? { category } : undefined,
      order,
      relations: ['versions'],
    });
  }

  async findOne(id: string): Promise<Component> {
    const c = await this.repo.findOne({
      where: { id },
      relations: ['versions'],
    });
    if (!c) throw new NotFoundException(`Component #${id} not found`);
    return c;
  }

  async findByName(name: string): Promise<Component> {
    const c = await this.repo.findOne({ where: { name }, relations: ['versions'] });
    if (!c) throw new NotFoundException(`Component "${name}" not found`);
    return c;
  }

  async update(id: string, dto: UpdateComponentDto): Promise<Component> {
    const c = await this.findOne(id);
    if (dto.name && dto.name !== c.name) {
      const dup = await this.repo.findOne({ where: { name: dto.name } });
      if (dup) throw new ConflictException(`Component "${dto.name}" already exists`);
    }
    Object.assign(c, dto);
    return this.repo.save(c);
  }

  async remove(id: string): Promise<void> {
    const { affected } = await this.repo.delete(id);
    if (!affected) throw new NotFoundException(`Component #${id} not found`);
  }

  async incrementStat(id: string, field: 'download' | 'preview' | 'reference'): Promise<Component> {
    const c = await this.findOne(id);
    if (field === 'download') c.downloadCount += 1;
    if (field === 'preview') c.previewCount += 1;
    if (field === 'reference') c.referenceCount += 1;
    c.popularityScore = this.computeScore(c);
    return this.repo.save(c);
  }

  async getTopN(n = 10): Promise<Component[]> {
    return this.repo.find({
      order: { popularityScore: 'DESC' },
      take: n,
      relations: ['versions'],
    });
  }
}
