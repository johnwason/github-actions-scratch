#!/usr/bin/env bash
# No-network smoke test. Fails unless the container has only a loopback interface,
# no routes, and name resolution does not work. Sends no packets.
set -u
fail=0

ifaces="$(ls /sys/class/net)"
if [[ "$ifaces" != "lo" ]]; then
  echo "no-network check: unexpected interfaces: $(echo $ifaces)" >&2; fail=1
fi

# /proc/net/route has a header line only when there are no IPv4 routes.
if [[ "$(wc -l < /proc/net/route)" -gt 1 ]]; then
  echo "no-network check: IPv4 routes present" >&2; fail=1
fi
if [[ -s /proc/net/ipv6_route ]] && grep -qv ' lo$' /proc/net/ipv6_route; then
  echo "no-network check: non-loopback IPv6 routes present" >&2; fail=1
fi

if getent ahosts github.com >/dev/null 2>&1; then
  echo "no-network check: DNS lookup of github.com succeeded" >&2; fail=1
fi

if [[ $fail -ne 0 ]]; then
  echo "no-network check: FAILED (container must run with --network=none)" >&2
  exit 1
fi
echo "no-network check: OK (interfaces: lo only; no routes; DNS lookup fails)"
