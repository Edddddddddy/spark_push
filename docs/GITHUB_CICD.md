# GitHub And CI/CD Guide

## Target Repository

Recommended repository:

- `https://github.com/Edddddddddy/spark_push`

This project is currently inside a larger parent Git repository, so the correct migration is to make `06/` a new standalone repository first. That avoids pushing sibling training directories by mistake.

## Local Git Migration

Run these commands inside the `06` directory:

```bash
git init -b main
git remote add origin https://github.com/Edddddddddy/spark_push.git
git add .
git commit -m "chore: bootstrap spark_push repository"
git push -u origin main
```

The old GitLab remote belongs to the parent repository, not to this new standalone repo, so it does not need to be reused here.

## Suggested GitHub Settings

### Branch protection

Protect `main` with these rules:

- require pull request before merge
- require at least 1 review
- require status checks to pass
- block force-push
- block direct deletion

### Environments

Create a GitHub Environment named `production`.

Recommended settings:

- required reviewers: enabled
- deployment branch rule: only `main`

## Secrets And Variables

Configure these in the `production` environment.

### Variables

- `DEPLOY_HOST=47.250.130.135`
- `DEPLOY_USER=eddy`
- `DEPLOY_PATH=/opt/spark-push`
- `GHCR_PULL_USERNAME=Edddddddddy`

### Secrets

- `PROD_SSH_PRIVATE_KEY`
- `GHCR_PULL_TOKEN`

`GHCR_PULL_TOKEN` should be a GitHub Personal Access Token with at least `read:packages`.

## Workflow Design

### CI

Triggered on:

- push to `main`
- pull request

What it does:

- validate Docker Compose
- build the application image
- start the stack in CI
- verify `/health`

### CD

Triggered on:

- push to `main`
- push tag like `v1.0.0`
- manual run from GitHub Actions

What it does:

- build and push image to GHCR
- sync deployment manifests to the server
- log in to GHCR on the server
- pull the new image
- restart the release stack

## Release Deployment Model

There are now two deployment modes:

- `deploy/docker-compose.prod.yml`: source-build deployment, useful for local and manual server build
- `deploy/docker-compose.release.yml`: image-based deployment, used by GitHub CD

The release stack pulls:

```text
ghcr.io/<owner>/<repo>:sha-<commit>
```

That means every production deployment can be traced back to one exact commit.

## First-Time Server Preparation

The server already has Docker and Compose. For GitHub CD, also make sure:

1. `rsync` is installed
2. `/opt/spark-push` exists
3. `deploy/.env.prod` exists
4. `deploy/systemd/spark-push-release.service` is installed when you want image-based auto-restart on reboot

Example:

```bash
sudo cp deploy/systemd/spark-push-release.service /etc/systemd/system/spark-push.service
sudo systemctl daemon-reload
sudo systemctl enable --now spark-push
```

## Daily Usage

### Start a feature

```bash
git checkout main
git pull
git checkout -b feature/your-change
```

### Push for review

```bash
git add .
git commit -m "feat: your change"
git push -u origin feature/your-change
```

Then open a Pull Request on GitHub.

### Merge and deploy

1. CI passes
2. PR is reviewed
3. Merge into `main`
4. GitHub Actions runs CD
5. Approve the `production` environment if protection is enabled

### Tag a release

```bash
git checkout main
git pull
git tag v1.0.0
git push origin v1.0.0
```

This gives you a human-readable release tag in GHCR in addition to the immutable SHA tag.
