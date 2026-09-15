# Authentication flow

1. POST /api/v2/auth/confirm/email with email to request a code.
2. POST /api/v2/auth/verify with email and code. Codes are consumed atomically on success.
   - Existing account without a password: 200 with the session in message.
   - Existing account with a password: 202 with message=password-required, confirmation_token and expires_in=300.
   - New account: 202 with message=registration-required, confirmation_token and expires_in=300.
3. Pass confirmation_token in the JSON body of /auth/login (with email and password) or /auth/create (with email, name, username and optional password).

The proof is a secret bearer token bound to the normalized email. Keep it in client memory and do not log it. It is consumed atomically before creating an account/session. A wrong password does not consume it. On expiry or a failure after consumption, request and verify a new email code. Old email_confirmed Redis flags are ignored.

Existing clients must handle 202 for registration and pass confirmation_token; email alone cannot authorize login or registration.

## Regression tests

Run from the repository root:

```sh
docker build --target builder -t parmigiano-http:auth-test-builder .
docker build -f tests/Dockerfile -t parmigiano-http:auth-tests .
```

Tests use the actual HTTP parser, handlers, Argon2, encryption and an isolated Redis instance inside the build container. PostgreSQL account lookups/writes are stubbed; no production databases or email services are used. Test sources use .c.in because the application's Makefile includes all .c files recursively.
