# SharkOS firewall

The firewall is an ordered, first-match, stateful IPv4 packet filter shared by
the desktop, lite and Server images. It validates IPv4 header checksums and
basic TCP/UDP/ICMP lengths, drops fragmented IPv4 packets (reassembly is not
implemented), and applies a default policy when no rule matches. TCP return
traffic is tracked from outbound SYN through a matching SYN-ACK; UDP and ICMP
echo replies are matched against outbound flows. Earlier explicit rules can
deny tracked replies. ARP is passed for LAN operation. A DHCP server response
from UDP port 67 to client port 68 is allowed only when no explicit rule
matches.

The engine now filters three directions independently — **INPUT** (to this
host), **OUTPUT** (from this host) and **FORWARD** (the router's NAT path) —
and supports 64 ordered rules. Rules, policies, rate limits, services and
counters live in RAM and reset at reboot. The default policy for all three
directions is `allow`.

## Classic commands (unchanged)

```text
fw docs                              show the CLI guide
fw status / fw list                  policy, rate, rules, services, counters
fw status -v                         also show recent drops
fw allow tcp 22                      allow TCP destination port 22
fw deny tcp 23                       block TCP destination port 23
fw deny icmp                         block incoming ICMP (including ping)
fw allow tcp 22 from 192.168.1.20    allow SSH only from that source
fw deny udp any                      block all UDP input
fw policy deny / fw policy allow     unmatched INPUT policy
fw delete 1                          delete rule 1
fw clear                             remove rules, keep policy/rate
fw rate 20 / fw rate off             new-traffic limit per source IP
```

Deny-by-default with SSH and ping:

```text
fw clear
fw policy deny
fw allow tcp 22
fw allow icmp
fw status
```

## Extended rules

```text
fw add <allow|deny> <tcp|udp|icmp|any> [options]
```

| Option | Meaning |
| --- | --- |
| `dir in\|out\|forward\|any` | direction (default `any`) |
| `from <ip[/prefix]>` | source CIDR, e.g. `10.0.0.0/8` |
| `to <ip[/prefix]>` | destination CIDR |
| `dport <port\|a-b>` | destination port or inclusive range |
| `sport <port\|a-b>` | source port or inclusive range |
| `state new\|established\|any` | connection state |
| `icmp <type>` | match a specific ICMP type (`8` = echo request) |
| `rate <1-1000>` | per-rule packets/second/source |
| `log` | log every match |
| `comment "text"` | 31-character label |

Examples:

```text
fw add deny tcp dir in from 203.0.113.0/24 dport 22 log comment "no ssh"
fw add allow udp dir forward to 10.0.0.0/8 dport 5000-5100
fw add allow any state established
fw add deny icmp icmp 8 rate 5 log comment "ping flood"
```

Rules are evaluated in insertion order; the first matching enabled rule wins.
`fw move <id> up|down` reorders, `fw enable <id>` / `fw disable <id>` toggles
without deleting, `fw hits <id>` shows the hit counter.

## Policies, profiles and services

```text
fw policy [in|out|forward|any] allow|deny
fw profile open|server|lockdown|router
fw service add|del <tcp|udp> <port>
fw service list
fw counters [reset]
fw reset                        factory defaults
```

- `open` — allow everything.
- `server` — deny new INPUT, allow established traffic and ICMP, allow OUTPUT
  and FORWARD.
- `lockdown` — deny all three directions except established flows.
- `router` — same idea plus LAN->WAN forwarding and LAN->router input; it uses
  the LAN subnet configured with `rtr lan`. It also pins explicit deny rules
  on WAN-side DHCP, DNS and console traffic (the console rule follows
  `rtr http port <port>`). See [SHARKOS-ROUTER.md](SHARKOS-ROUTER.md).

**Implicit service ports** are local services that stay reachable under a
deny-by-default policy (DHCP 67/udp, DNS 53/udp, the router console, port
forwards). They are checked *after* explicit rules, so a deny rule always
overrides them. `rtr start` registers its own ports automatically.

## Logging and counters

```text
fw log [count]        newest drop/allow events (newest first)
fw log clear
fw log on|off         toggle drop logging
fw logdrops on|off
fw counters reset
```

`fw status` reports per-direction allow/drop counters, the global rate-limit
drop count and per-rule hit counts. The log is a 24-entry ring buffer, with
drops rate-limited to one entry per second so a flood cannot erase history.

The global rate limiter accepts 1–1000 packets/second/source and counts new
inbound and forwarded TCP SYNs, UDP packets and ICMP echo requests together in
one-second windows; tracked replies and DHCP server responses are exempt. It
tracks up to 64 sources per window and fails closed for extra sources.

## Guarantees and limits

- 64 rules, 12 implicit service ports, 48 tracked connections (120s idle TTL),
  64 rate-limit sources, 24 log entries.
- ICMP rules are protocol-wide unless `icmp <type>` is set; ports apply to
  TCP/UDP only.
- TCP tracking matches addresses, ports and handshake flags, not sequence
  numbers.
- Bridged container frames are delivered to virtual adapters before the host
  INPUT filter; NAT return traffic is translated to its container first.
- No IPv6 filtering, no fragment reassembly, no persistence of rules, no
  eBPF/nftables-style expressions. This is still a basic IPv4 filter.
