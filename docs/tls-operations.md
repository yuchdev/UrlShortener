# TLS Setup and Rotation Runbook

## Provisioning requirements

- Certificate chain PEM and private key PEM must match.
- Private key should be readable only by service user.
- Avoid embedding key material in environment variables.

## Permissions and ownership

```bash
chown urlshortener:urlshortener /etc/urlshortener/tls/*
chmod 640 /etc/urlshortener/tls/server.crt
chmod 600 /etc/urlshortener/tls/server.key
```

## Verification commands

```bash
openssl x509 -in /etc/urlshortener/tls/server.crt -noout -dates -subject -issuer
openssl rsa -in /etc/urlshortener/tls/server.key -check -noout
openssl s_client -connect example.com:443 -servername example.com </dev/null
```

## Zero/low-downtime rotation

1. Stage new cert/key files beside active pair.
2. Validate key/cert match and expiry.
3. Atomically swap symlink or file reference.
4. Send `SIGHUP` to trigger reload.
5. Confirm logs show reload success and metrics increment.
6. Validate external handshake and certificate chain.

## SIGHUP validation checklist

- `tls_reload_success_total` increments.
- no spike in `tls_handshake_failure_total`.
- readiness remains healthy.

## Rollback

1. Restore prior cert/key references.
2. Send `SIGHUP` again.
3. Verify handshake and readiness.
4. Open incident ticket for root-cause analysis.
