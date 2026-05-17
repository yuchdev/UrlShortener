# Deployment Guide

## Build artifacts

Expected binaries:
- `url_shortener` server executable
- migration SQL files (if SQL backend)
- runtime config file(s)

Build (example):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Runtime configuration and flags

Primary runtime config can come from CLI flags and YAML/config files. Ensure backend and TLS settings are consistent with environment.

## systemd example

```ini
[Unit]
Description=UrlShortener Service
After=network.target

[Service]
Type=simple
User=urlshortener
Group=urlshortener
ExecStart=/opt/urlshortener/url_shortener --config /etc/urlshortener/config.yaml
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=3
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```

## Network/firewall requirements

- Inbound to service port (`80/443` or configured custom port)
- Egress to storage backends (PostgreSQL/Redis) if externalized
- Allow health checks from load balancer/orchestrator

## Startup/shutdown and rolling restart

1. Validate config in staging.
2. Start one instance and verify `/healthz`, `/readyz`, `/metrics`.
3. Roll by small batch; ensure readiness before draining old nodes.
4. For shutdown, stop intake first, then allow in-flight requests to drain.

## Health-check integration

- Use `/healthz` for process liveness.
- Use `/readyz` for traffic eligibility.
- Alert on sustained readiness failures, not isolated probe blips.
