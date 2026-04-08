# Contributing

## Branch Strategy

- `main`: production-ready branch
- `feature/*`: new features
- `fix/*`: bug fixes
- `chore/*`: infrastructure and maintenance work

## Recommended Flow

1. Pull the latest `main`
2. Create a short-lived branch
3. Commit small, reviewable changes
4. Open a Pull Request into `main`
5. Wait for CI to pass
6. Merge after review
7. Let CD deploy to the server through the `production` environment

## Commit Style

Recommended prefixes:

- `feat:`
- `fix:`
- `refactor:`
- `docs:`
- `chore:`
- `ci:`

Examples:

- `feat: add grpc stream retry backoff`
- `fix: avoid duplicate room route cleanup`
- `ci: add docker smoke test workflow`

## Pull Request Checklist

- Code builds locally
- Main path is smoke-tested
- Config changes are documented
- Rollback impact is understood
- User-facing or deploy-facing changes are mentioned in the PR

