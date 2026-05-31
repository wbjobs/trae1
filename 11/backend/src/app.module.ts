import { Module, OnModuleInit, Logger } from '@nestjs/common'
import { TypeOrmModule, InjectRepository } from '@nestjs/typeorm'
import { JwtModule } from '@nestjs/jwt'
import { Repository } from 'typeorm'
import { AuthModule } from './auth/auth.module'
import { UsersModule } from './users/users.module'
import { ProjectsModule } from './projects/projects.module'
import { DocsModule } from './docs/docs.module'
import { VersionsModule } from './versions/versions.module'
import { MembersModule } from './members/members.module'
import { SwaggerParserModule } from './swagger_parser/swagger-parser.module'
import { PermissionsModule } from './permissions/permissions.module'
import { CommentsModule } from './comments/comments.module'
import { NotificationsModule } from './notifications/notifications.module'
import { User } from './users/user.entity'
import { Project } from './projects/project.entity'
import { Doc } from './docs/doc.entity'
import { DocVersion } from './versions/doc-version.entity'
import { Member } from './members/member.entity'
import * as bcrypt from 'bcrypt'

@Module({
  imports: [
    TypeOrmModule.forRoot({
      type: 'mysql',
      host: process.env.DB_HOST || 'localhost',
      port: parseInt(process.env.DB_PORT, 10) || 3306,
      username: process.env.DB_USER || 'root',
      password: process.env.DB_PASS || 'root',
      database: process.env.DB_NAME || 'api_doc',
      entities: [User, Project, Doc, DocVersion, Member],
      synchronize: process.env.DB_SYNC !== 'false',
      logging: process.env.DB_LOG === 'true',
      extra: { connectionLimit: 10 }
    }),
    TypeOrmModule.forFeature([User]),
    JwtModule.register({
      global: true,
      secret: process.env.JWT_SECRET || 'api-doc-secret-key',
      signOptions: { expiresIn: process.env.JWT_EXPIRES || '7d' }
    }),
    AuthModule,
    UsersModule,
    ProjectsModule,
    DocsModule,
    VersionsModule,
    MembersModule,
    SwaggerParserModule,
    PermissionsModule,
    CommentsModule,
    NotificationsModule
  ],
})
export class AppModule implements OnModuleInit {
  private readonly logger = new Logger(AppModule.name)
  constructor(
    @InjectRepository(User)
    private readonly usersRepo: Repository<User>
  ) {}

  async onModuleInit() {
    const adminUsername = process.env.ADMIN_USER || 'admin'
    const exists = await this.usersRepo.findOne({ where: { username: adminUsername } })
    if (!exists) {
      const password = process.env.ADMIN_PASS || 'admin123'
      const hashed = await bcrypt.hash(password, 10)
      await this.usersRepo.save({
        username: adminUsername,
        nickname: '系统管理员',
        email: process.env.ADMIN_EMAIL || 'admin@example.com',
        password: hashed,
        role: 'admin'
      })
      this.logger.log('Default admin account created')
    }
  }
}
