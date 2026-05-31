import {
  Controller,
  Get,
  Query,
  UseGuards,
  Request
} from '@nestjs/common'
import { JwtAuthGuard } from '../common/guards/jwt-auth.guard'

@Controller('permissions')
@UseGuards(JwtAuthGuard)
export class PermissionsController {
  private readonly rolePermissions: Record<string, string[]> = {
    admin: ['*:*'],
    editor: ['doc:view', 'doc:create', 'doc:update', 'doc:debug', 'project:view', 'version:view', 'version:rollback'],
    viewer: ['doc:view', 'project:view', 'version:view']
  }

  @Get('check')
  check(@Query() query: { resource: string; action: string }, @Request() req: any) {
    const perms = this.rolePermissions[req.user.role] || []
    const key = `${query.resource}:${query.action}`
    const allowed = perms.includes('*:*') || perms.includes(key)
    return { allowed, permissions: perms }
  }

  @Get('my')
  my(@Request() req: any) {
    return {
      role: req.user.role,
      permissions: this.rolePermissions[req.user.role] || []
    }
  }
}
