import {
  Controller,
  Get,
  Post,
  Put,
  Delete,
  Param,
  Body,
  UseGuards,
  Request,
  Inject
} from '@nestjs/common'
import { CommentsService } from './comments.service'
import { CreateCommentDto, UpdateCommentDto } from './dto'
import { JwtAuthGuard } from '../common/guards/jwt-auth.guard'
import { NotificationService } from '../notifications/notification.service'

@Controller('docs/:docId/comments')
@UseGuards(JwtAuthGuard)
export class CommentsController {
  constructor(
    private readonly commentsService: CommentsService,
    @Inject(NotificationService)
    private readonly notificationService: NotificationService
  ) {}

  @Get()
  list(@Param('docId') docId: string) {
    return this.commentsService.findByDoc(+docId)
  }

  @Post()
  async create(@Param('docId') docId: string, @Body() dto: CreateCommentDto, @Request() req: any) {
    const c = await this.commentsService.create({ ...dto, docId: +docId }, req.user)
    if (dto.mentions?.length) {
      for (const uid of dto.mentions) {
        this.notificationService.pushToUser(uid, {
          type: 'comment_added',
          title: '有人在文档中提到了你',
          content: `${req.user.nickname} 在批注中提到了你`,
          docId: +docId,
          operator: { id: req.user.id, nickname: req.user.nickname }
        })
      }
    }
    return c
  }

  @Put(':id')
  update(@Param('id') id: string, @Body() dto: UpdateCommentDto, @Request() req: any) {
    return this.commentsService.update(+id, dto, req.user)
  }

  @Post(':id/upvote')
  upvote(@Param('id') id: string) {
    return this.commentsService.upvote(+id)
  }

  @Delete(':id')
  remove(@Param('id') id: string, @Request() req: any) {
    return this.commentsService.remove(+id, req.user)
  }
}
