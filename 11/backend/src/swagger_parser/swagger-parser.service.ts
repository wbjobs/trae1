import { Injectable, Logger } from '@nestjs/common'
import { InjectRepository } from '@nestjs/typeorm'
import { Repository } from 'typeorm'
import axios from 'axios'
import { Doc } from '../docs/doc.entity'
import { Project } from '../projects/project.entity'

export interface ParsedPath {
  method: string
  path: string
  title: string
  description: string
  parameters: any[]
  requestBody: any
  responses: any
  tags: string[]
  deprecated?: boolean
  security?: any[]
  extensions?: Record<string, any>
}

@Injectable()
export class SwaggerParserService {
  private readonly logger = new Logger(SwaggerParserService.name)

  constructor(
    @InjectRepository(Doc)
    private readonly docRepo: Repository<Doc>,
    @InjectRepository(Project)
    private readonly projectRepo: Repository<Project>
  ) {}

  async importSwagger(
    projectId: number,
    dto: { swaggerUrl?: string; swaggerContent?: any },
    user: any
  ) {
    let swaggerData: any = dto.swaggerContent
    if (!swaggerData && dto.swaggerUrl) {
      try {
        const res = await axios.get(dto.swaggerUrl, { timeout: 15000 })
        swaggerData = res.data
      } catch (e: any) {
        throw new Error(`获取 Swagger 文档失败: ${e.message}`)
      }
    }
    if (!swaggerData) throw new Error('未提供 Swagger 内容')

    await this.projectRepo.update(projectId, { swaggerData })

    const paths = this.parseSwagger(swaggerData)
    const project: any = await this.projectRepo.findOne({ where: { id: projectId } })

    let created = 0
    let updated = 0
    for (const p of paths) {
      const existing = await this.docRepo.findOne({
        where: { projectId, method: p.method.toUpperCase(), path: p.path }
      })
      const payload = {
        projectId,
        title: p.title || `${p.method} ${p.path}`,
        description: p.description,
        method: p.method.toUpperCase(),
        path: p.path,
        content: this.buildMarkdown(p, swaggerData),
        requestParams: this.extractParams(p.parameters),
        requestBody: p.requestBody,
        response: this.extractResponse(p.responses, swaggerData),
        requestExample: this.extractRequestExample(p, swaggerData),
        tags: p.tags?.length ? p.tags : (project?.name ? [project.name] : []),
        category: p.tags?.[0] || '默认分类',
        contentType: this.extractContentType(p),
        status: 'published' as const
      }

      if (existing) {
        Object.assign(existing, payload, { updatedById: user.id })
        await this.docRepo.save(existing)
        updated++
      } else {
        const doc = this.docRepo.create({ ...payload, createdById: user.id, updatedById: user.id })
        await this.docRepo.save(doc)
        created++
      }
    }

    this.logger.log(`项目[${projectId}] Swagger 导入完成: 新建${created}个, 更新${updated}个`)
    return { created, updated, total: paths.length }
  }

  parseSwagger(data: any): ParsedPath[] {
    const result: ParsedPath[] = []
    const paths = data.paths || {}
    const globalParameters = this.resolveGlobalParameters(data)
    const globalSecurity = data.security || []
    const globalTags = data.tags || []
    const externalDocs = data.externalDocs || null

    for (const [path, methods] of Object.entries(paths)) {
      const pathItem = methods as any
      const pathLevelParams = pathItem.parameters || []
      const pathExtensions = this.extractExtensions(pathItem)

      for (const [method, def] of Object.entries(pathItem)) {
        if (['parameters', '$ref', 'summary', 'description', 'servers', 'extensions'].includes(method)) continue
        const d: any = def
        const rawParameters = [...pathLevelParams, ...(d.parameters || [])]
        const parameters = rawParameters.map((p) => this.resolveParameter(p, data))
        const requestBody = this.parseRequestBody(d.requestBody, data)
        const security = d.security || globalSecurity
        const extensions = { ...pathExtensions, ...this.extractExtensions(d) }
        const groupedTags = this.mergeTags(d.tags, globalTags, pathExtensions)
        result.push({
          method: method.toUpperCase(),
          path,
          title: d.summary || d.operationId || `${method} ${path}`,
          description: this.buildDescription(d, externalDocs),
          parameters: [...globalParameters, ...parameters],
          requestBody,
          responses: d.responses || {},
          tags: groupedTags,
          deprecated: d.deprecated,
          security,
          extensions
        })
      }
    }
    return result
  }

  private extractExtensions(obj: any): Record<string, any> {
    if (!obj || typeof obj !== 'object') return {}
    const exts: Record<string, any> = {}
    for (const [k, v] of Object.entries(obj)) {
      if (k.startsWith('x-')) exts[k] = v
    }
    return exts
  }

  private mergeTags(
    opTags: string[] | undefined,
    globalTags: any[],
    pathExtensions: Record<string, any>
  ): string[] {
    const set = new Set<string>(opTags || [])
    if (pathExtensions['x-group']) set.add(pathExtensions['x-group'])
    for (const t of globalTags) {
      if (typeof t === 'object' && t.name) set.add(t.name)
      else if (typeof t === 'string') set.add(t)
    }
    return Array.from(set)
  }

  private buildDescription(d: any, externalDocs: any): string {
    const parts: string[] = []
    if (d.description) parts.push(d.description)
    if (d.operationId) parts.push(`*Operation ID: \`${d.operationId}\`*`)
    if (externalDocs) parts.push(`*参考: [${externalDocs.description || externalDocs.url}](${externalDocs.url})*`)
    if (d.deprecated) parts.unshift('> **⚠️ 已废弃**')
    return parts.join('\n\n')
  }

  private resolveGlobalParameters(data: any): any[] {
    const raw = data.parameters || data.components?.parameters || {}
    if (Array.isArray(raw)) {
      return raw.map((p) => this.resolveParameter(p, data))
    }
    if (raw && typeof raw === 'object') {
      return Object.values(raw).map((p: any) => this.resolveParameter(p, data))
    }
    return []
  }

  private resolveParameter(param: any, data: any): any {
    if (!param) return param
    if (param.$ref) {
      const name = param.$ref.split('/').pop()
      const defs = data?.components?.parameters || data?.parameters || {}
      const ref = Array.isArray(defs) ? defs.find((x: any) => x.name === name) : defs[name]
      if (ref) return this.resolveParameter(ref, data)
      return param
    }
    const schema = param.schema ? this.resolveSchema(param.schema, data) : null
    return {
      name: param.name,
      in: param.in,
      type: schema?.type || param.type || 'string',
      required: param.required || param.in === 'path',
      description: param.description || schema?.description || '',
      example: param.example ?? schema?.example ?? '',
      schema
    }
  }

  private parseRequestBody(rb: any, data: any) {
    if (!rb) return null
    const content = rb.content || {}
    const candidate =
      content['application/json'] ||
      content['multipart/form-data'] ||
      content['application/x-www-form-urlencoded'] ||
      content['*/*']
    if (!candidate) return null
    return {
      description: rb.description,
      required: rb.required,
      schema: this.resolveSchema(candidate.schema, data),
      example: candidate.example || this.genExample(candidate.schema, data),
      encoding: candidate.encoding
    }
  }

  private extractContentType(p: ParsedPath) {
    const rb: any = p.requestBody
    if (!rb) return 'application/json'
    if (rb.schema?.type === 'object') return 'application/json'
    return 'application/json'
  }

  private extractRequestExample(p: ParsedPath, data: any) {
    const rb: any = p.requestBody
    if (!rb) return null
    if (rb.example) return rb.example
    if (rb.schema) return this.genExample(rb.schema, data)
    return null
  }

  private extractResponse(responses: any, data: any) {
    if (!responses || typeof responses !== 'object') return null
    const codes = ['200', '201', '202', '204']
    let success: any = null
    for (const c of codes) {
      if (responses[c]) {
        success = responses[c]
        break
      }
    }
    if (!success) success = responses[Object.keys(responses)[0]]
    if (!success) return null
    const content = success.content || {}
    const json =
      content['application/json'] ||
      content['application/xml'] ||
      content['*/*']
    if (!json) return { description: success.description }
    const schema = this.resolveSchema(json.schema, data)
    return {
      description: success.description,
      schema,
      example: json.example || this.genExample(json.schema, data)
    }
  }

  private genExample(schema: any, data: any, depth = 0): any {
    if (!schema || depth > 6) return undefined
    if (schema.$ref) {
      const name = schema.$ref.split('/').pop()
      const defs = data?.components?.schemas || data?.definitions || {}
      const ref = defs[name]
      return ref ? this.genExample(ref, data, depth + 1) : undefined
    }
    if (schema.example !== undefined) return schema.example
    if (schema.enum?.length) return schema.enum[0]
    if (schema.type === 'object' || schema.properties) {
      const obj: any = {}
      for (const [k, v] of Object.entries(schema.properties || {})) {
        obj[k] = this.genExample(v as any, data, depth + 1)
      }
      return obj
    }
    if (schema.type === 'array') {
      return [this.genExample(schema.items, data, depth + 1)]
    }
    switch (schema.type) {
      case 'string':
        return schema.format === 'date-time' ? new Date().toISOString() : 'string'
      case 'number':
      case 'integer':
        return 0
      case 'boolean':
        return false
      default:
        return schema.type ? `${schema.type}_value` : null
    }
  }

  private resolveSchema(schema: any, data: any, depth = 0): any {
    if (!schema || depth > 8) return schema
    if (schema.$ref) {
      const name = schema.$ref.split('/').pop()
      const defs = data?.components?.schemas || data?.definitions || {}
      const ref = defs[name]
      if (ref) {
        const resolved = this.resolveSchema(ref, data, depth + 1)
        return { ...resolved, $ref: schema.$ref }
      }
      return schema
    }
    if (schema.allOf?.length) {
      const merged: any = { type: 'object', properties: {}, required: [] }
      for (const sub of schema.allOf) {
        const r = this.resolveSchema(sub, data, depth + 1)
        if (r?.properties) Object.assign(merged.properties, r.properties)
        if (r?.required) merged.required.push(...r.required)
      }
      return merged
    }
    if (schema.oneOf?.length || schema.anyOf?.length) {
      const arr = schema.oneOf || schema.anyOf
      return this.resolveSchema(arr[0], data, depth + 1)
    }
    if (schema.type === 'object' || schema.properties) {
      const result: any = {
        type: 'object',
        properties: {},
        required: schema.required || [],
        description: schema.description
      }
      for (const [k, v] of Object.entries(schema.properties || {})) {
        result.properties[k] = this.resolveSchema(v as any, data, depth + 1)
      }
      return result
    }
    if (schema.type === 'array') {
      return {
        type: 'array',
        items: this.resolveSchema(schema.items, data, depth + 1),
        description: schema.description
      }
    }
    return {
      type: schema.type,
      description: schema.description,
      example: schema.example,
      enum: schema.enum,
      format: schema.format
    }
  }

  private extractParams(parameters: any[] = []) {
    const query: any[] = []
    const path: any[] = []
    const header: any[] = []
    for (const p of parameters) {
      const item = {
        name: p.name,
        type: p.type || 'string',
        required: !!p.required,
        description: p.description || '',
        example: p.example ?? ''
      }
      if (p.in === 'query') query.push(item)
      else if (p.in === 'path') path.push(item)
      else if (p.in === 'header') header.push(item)
    }
    return { query, path, header }
  }

  private buildMarkdown(p: ParsedPath, data: any) {
    const lines: string[] = []
    lines.push(`# ${p.title}`)
    if (p.description) lines.push('', p.description)
    lines.push('', `## 基本信息`, '', `| 项目 | 值 |`, `| --- | --- |`)
    lines.push(`| 方法 | ${p.method} |`)
    lines.push(`| 路径 | \`${p.path}\` |`)
    if (p.tags?.length) lines.push(`| 标签 | ${p.tags.join(', ')} |`)
    if (p.parameters?.length) {
      lines.push('', `## 请求参数`, '', `| 名称 | 位置 | 类型 | 必填 | 描述 | 示例 |`, `| --- | --- | --- | --- | --- | --- |`)
      for (const param of p.parameters) {
        lines.push(
          `| ${param.name} | ${param.in} | ${param.type || ''} | ${
            param.required ? '是' : '否'
          } | ${param.description || ''} | ${param.example || ''} |`
        )
      }
    }
    if (p.requestBody?.schema?.properties) {
      lines.push('', `## 请求体`, '', `| 字段 | 类型 | 必填 | 描述 |`, `| --- | --- | --- | --- |`)
      const req = p.requestBody.schema.required || []
      for (const [k, v] of Object.entries(p.requestBody.schema.properties || {})) {
        const vv: any = v
        lines.push(`| ${k} | ${vv.type || ''} | ${req.includes(k) ? '是' : '否'} | ${vv.description || ''} |`)
      }
      if (p.requestBody.example) {
        lines.push('', `### 请求示例`, '', '```json', JSON.stringify(p.requestBody.example, null, 2), '```')
      }
    }
    const resp = this.extractResponse(p.responses, data)
    if (resp?.schema?.properties || resp?.example) {
      lines.push('', `## 响应示例`, '', '```json')
      lines.push(JSON.stringify(resp.example || this.genExample(resp.schema, data) || {}, null, 2))
      lines.push('```')
    }
    return lines.join('\n')
  }
}
