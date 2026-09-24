## Summary

<!-- Describe what this PR changes and why. -->

## PR title (important)

Use a **conventional commit** title — it becomes the release version on merge:

| Title prefix | Release bump | Example |
|--------------|--------------|---------|
| `fix:` | patch (0.3.0 → 0.3.1) | `fix: correct session expiry in Redis` |
| `feat:` | minor (0.3.0 → 0.4.0) | `feat: add group chat media upload` |
| `feat!:` or `BREAKING CHANGE:` in body | major (0.3.0 → 1.0.0) | `feat!: change auth token format` |
| `chore:`, `ci:`, `docs:` | no release tag | `chore: update docker-compose` |

> Use **Squash merge** so the PR title becomes the commit on `main`.

## Checklist

- [ ] PR title follows the table above
- [ ] Docker image builds (`docker build .` or `docker compose -f docker-compose.dev.yml build`)
- [ ] Database migrations added/updated if schema changed (`http-service/http-server/src/storage/postgres/migrations/`)
- [ ] Swagger updated if API changed (`docs/swagger/swagger_v2.json`)
- [ ] `.env.example` updated if new environment variables were added
- [ ] Source is formatted (`make lin-format`)

## Related issues

<!-- Closes #123 -->

<!-- parmigiano-chttp-docker -->
<!-- /parmigiano-chttp-docker -->
