# OpenVPN profiles

Place `server.conf` and the non-secret material here, then enable the
`openvpn` program in `../programs/openvpn.ini`. Private keys, certificates and
the CRL are secrets: keep them in `output/srv/vpn/` (gitignored), which is
mounted read-only as `/etc/oko-services/secrets`, and reference them with
absolute container paths:

```ini
ca   /etc/oko-services/secrets/openvpn/ca.crt
cert /etc/oko-services/secrets/openvpn/server.crt
key  /etc/oko-services/secrets/openvpn/server.key
```

`conf/` is mounted read-only into the container as `/etc/oko-services`.