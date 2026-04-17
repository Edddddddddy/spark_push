# Enterprise deployment model

This project uses GitHub Actions to build one immutable image per commit and deploys release manifests to a fixed server layout.

The deploy job pulls the GHCR image on the GitHub runner and streams it to the server with `docker save | gzip | ssh docker load`. The server then runs the release with `SKIP_PULL=true` and `APP_PULL_POLICY=never`, which avoids slow direct GHCR pulls from the runtime host.

## Environments

| Git branch or action | GitHub Environment | Runtime environment | Default URL |
| --- | --- | --- | --- |
| `develop` | `development` | `dev` | `http://47.116.57.154:8081/` |
| `release/**` | `testing` | `test` | `http://47.116.57.154:8082/` |
| `main` or `v*` tag | `production` | `prod` | `http://47.116.57.154/` |
| Manual dispatch | selected input | selected input | environment variable `PUBLIC_URL` |

## Server layout

```text
/opt/apps/spark_push/
  dev/
    current -> releases/<release-id>
    releases/
  test/
    current -> releases/<release-id>
    releases/
  prod/
    current -> releases/<release-id>
    releases/
  shared/
    dev/
      env/app.env
      compose/docker-compose.override.yml
      certs/
      logs/
      backups/
    test/
      env/app.env
      compose/docker-compose.override.yml
      certs/
      logs/
      backups/
    prod/
      env/app.env
      compose/docker-compose.override.yml
      certs/
      logs/
      backups/
```

`releases/<release-id>` contains the release manifests from the matching commit. `current` is updated only after `deploy/scripts/release_server.sh` starts the stack and the health check passes.

## GitHub Environment settings

Create three GitHub Environments: `development`, `testing`, and `production`.

Use the same variable names in each environment:

```text
DEPLOY_HOST=47.116.57.154
DEPLOY_USER=eddy
APP_ROOT=/opt/apps/spark_push
GHCR_PULL_USERNAME=<github-user-or-bot>
PUBLIC_URL=<environment-url>
```

Recommended `PUBLIC_URL` values:

```text
development: http://47.116.57.154:8081/
testing:     http://47.116.57.154:8082/
production:  http://47.116.57.154/
```

Use these secrets in each environment:

```text
SSH_PRIVATE_KEY=<private key whose public key is in /home/eddy/.ssh/authorized_keys>
GHCR_PULL_TOKEN=<optional token with read:packages for cross-repo or bot-based pulls>
```

When `GHCR_PULL_TOKEN` is not set, the workflow uses the job `GITHUB_TOKEN` for same-repository GHCR pulls.

`production` should require manual approval in the Environment protection rules.

## Server operations

Status:

```bash
/opt/apps/spark_push/bin/spark_push_ctl.sh dev ps
/opt/apps/spark_push/bin/spark_push_ctl.sh test ps
/opt/apps/spark_push/bin/spark_push_ctl.sh prod ps
```

Logs:

```bash
/opt/apps/spark_push/bin/spark_push_ctl.sh dev logs
```

Rollback to a previous release:

```bash
/opt/apps/spark_push/bin/spark_push_ctl.sh prod rollback <release-id>
```

Because the current server has about 2G memory, do not run dev, test, prod, and Jenkins at the same time unless the machine is upgraded.
