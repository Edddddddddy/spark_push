# Spark Push 开发与上线流程

这份文档只讲现在这套仓库和服务器的实际流程，目标是让你能按同一套步骤持续开发、提测、上线。

## 1. 分支约定

- `main`
  生产分支。只放准备上线的代码。推送到这个分支会触发 CD。
- `develop`
  日常集成分支。功能开发、缺陷修复先合到这里。
- `feature/*`
  新功能分支，例如 `feature/chatroom-admin`
- `fix/*`
  缺陷修复分支，例如 `fix/default-page-single-chat`
- `release/*`
  版本整理分支，例如 `release/v1.0.0`

推荐规则：

1. 新需求或新 bug，不要直接在 `main` 上改。
2. 从 `develop` 切出 `feature/*` 或 `fix/*`。
3. 开发完成后先合回 `develop`。
4. `develop` 验证通过后，再合到 `main` 上线。

## 2. 一次完整修复的标准流程

这次默认页单聊修复，就是按下面这条线走：

1. 切到 `develop`
2. 从 `develop` 新建修复分支
3. 本地改代码并自查
4. 提交并推送修复分支
5. 合并到 `develop`
6. `develop` 验证通过后，再合并到 `main`
7. GitHub Actions 在 `main` 上自动执行 CI/CD
8. 线上验证

## 3. 常用命令

### 3.1 开始开发

```bash
git switch develop
git pull origin develop
git switch -c fix/your-bug-name
```

### 3.2 开发完成后提交

```bash
git status
git add .
git commit -m "fix: describe the bug fix"
git push -u origin fix/your-bug-name
```

### 3.3 合并到 develop

```bash
git switch develop
git pull origin develop
git merge --no-ff fix/your-bug-name
git push origin develop
```

### 3.4 develop 验证通过后合并到 main

```bash
git switch main
git pull origin main
git merge --no-ff develop
git push origin main
```

### 3.5 打版本标签

```bash
git tag v1.0.0
git push origin v1.0.0
```

## 4. CI/CD 怎么工作

当前仓库已经接入 GitHub Actions。

### CI

文件：

- `.github/workflows/ci.yml`

作用：

- 编译检查
- 基础校验
- 保证主干代码至少能构建

触发时机：

- push
- pull request

### CD

文件：

- `.github/workflows/cd.yml`

作用：

1. 构建镜像
2. 推送到 GHCR
3. 通过 SSH 登录服务器
4. 在服务器执行发布脚本
5. 用镜像版本更新线上服务

触发时机：

- push 到 `main`

## 5. 线上部署怎么执行

### 5.1 服务器信息

- 地址：`47.250.130.135`
- 用户：`eddy`
- 部署目录：`/opt/spark-push`

### 5.2 线上发布核心脚本

服务器执行的是：

```bash
cd /opt/spark-push
APP_IMAGE_REPOSITORY=ghcr.io/eddddddddddy/spark_push APP_IMAGE_TAG=sha-<commit> bash deploy/scripts/release_server.sh
```

这个脚本会：

1. 拉取对应 commit 的镜像
2. 用 `deploy/docker-compose.release.yml` 更新服务
3. 重启线上容器

### 5.3 手工查看线上状态

```bash
cd /opt/spark-push
docker compose --env-file deploy/.env.prod -f deploy/docker-compose.release.yml ps
docker compose --env-file deploy/.env.prod -f deploy/docker-compose.release.yml logs --tail=200 nginx
docker compose --env-file deploy/.env.prod -f deploy/docker-compose.release.yml logs --tail=200 app
curl http://47.250.130.135/health
```

## 6. 这套流程里做了哪些企业化步骤

这个项目现在不是“本地改完手传服务器”，而是已经具备一套基本企业化流程：

1. 独立 GitHub 仓库管理
2. 主干分支和开发分支分离
3. bug 修复走独立分支
4. CI 自动校验
5. CD 自动部署
6. 服务器使用固定部署目录
7. 使用 Docker Compose 管理服务
8. 镜像版本和 commit 绑定，方便回滚
9. 部署凭据通过 GitHub Secrets 管理
10. 文档、Issue 模板、PR 模板、CODEOWNERS 已补齐

## 7. 以后你自己开发时怎么做

最简版记住这 4 句就够了：

1. 日常开发从 `develop` 拉分支
2. 修完先合 `develop`
3. 验证没问题再合 `main`
4. 推到 `main` 后，GitHub Actions 自动上线

## 8. 回滚思路

如果某次上线有问题，不要直接在服务器手改代码，优先回滚到上一个稳定 commit。

方法一：回滚 main 到前一个提交，再重新触发 CD

```bash
git switch main
git log --oneline
git revert <bad_commit_sha>
git push origin main
```

方法二：手工指定旧镜像版本重新部署

```bash
cd /opt/spark-push
APP_IMAGE_REPOSITORY=ghcr.io/eddddddddddy/spark_push APP_IMAGE_TAG=sha-<old_commit_sha> bash deploy/scripts/release_server.sh
```

## 9. 本次默认页单聊修复的实际示例

本次问题的根因不是后端，而是默认页前端：

1. 默认页把单聊 `session_id` 拼成了错误格式
2. 发送前没有严格校验 `to_user_id`
3. WebSocket 收到单聊消息时没有按当前会话过滤

修复后遵循的就是标准流程：

```bash
git switch develop
git switch -c fix/default-page-single-chat
# 修改代码
git add .
git commit -m "fix: repair default page single chat flow"
git push -u origin fix/default-page-single-chat

git switch develop
git merge --no-ff fix/default-page-single-chat
git push origin develop

git switch main
git merge --no-ff develop
git push origin main
```

这就是后面最值得复用的一套流程。
