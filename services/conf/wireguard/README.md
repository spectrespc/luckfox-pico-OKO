# WireGuard profiles

Place `wg0.conf` here and enable the `wg` program in `../programs/wg.ini`.
Real private keys are secrets: keep them in `output/srv/vpn/` (gitignored),
which is mounted read-only as `/etc/oko-services/secrets`, and reference them
from the profile with an absolute path inside the container, for example:

```ini
[Interface]
PrivateKey = /etc/oko-services/secrets/wireguard-privatekey
```

`conf/` is mounted read-only into the container, so any file here is visible as
`/etc/oko-services/wireguard/<name>`.