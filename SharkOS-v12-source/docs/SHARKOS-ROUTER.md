# SharkOS Router (Server edition)

`rtr` turns a SharkOS Server image into a small IPv4 router. One command starts
everything:

```text
rtr start
```

That enables the DHCP server, DNS resolver/forwarder, WAN management, basic
NAT with port forwards and an HTTP management console on **port 69**:

```text
http://192.168.10.1:69/
```

Port 69 is the IANA-assigned TFTP port; it is used here because it was
requested explicitly. On a normal operating system it would require root, but
SharkOS is a single freestanding kernel with no privileged port restrictions.
If another listener (httpd/sshd) already owns the port, `rtr start` reports
that the console could not be opened while the other router services still
start. Use `rtr start <port>` / `rtr http port <port>` to move it.

> **Change the default API token before exposing the box.** The default is
> `sharkos`: `rtr token my-long-secret`. The console is plain HTTP (no TLS),
> so keep it on a trusted LAN or tunnel it.

---

## What is implemented

| Feature | Details |
| --- | --- |
| DHCP server | DISCOVER/OFFER, REQUEST/ACK/NAK, RELEASE, DECLINE, INFORM; 64 leases, 16 static reservations, configurable pool/lease/domain, options 1, 3, 6, 15, 51, 53, 54, 56, 58, 59, 255 |
| DNS server | A and PTR queries, static records, automatic names for DHCP hostnames, `router.<domain>` alias, 64-entry cache, upstream forwarding with 8 outstanding queries and retries, `.in-addr.arpa` reverse lookups, empty NOERROR for AAAA |
| Interfaces | PCI adapter inventory with pfSense-style names (`em0`, `re0`, `vtnet0`...), first NIC auto-assigned as **WAN uplink**, second NIC auto-assigned as **LAN port**, `rtr ports` / `rtr port wan|lan|auto <index>`, roles saved with `rtr save` |
| WAN | Upstream DHCP client (the existing net.c client), static IPv4 configuration, renew, administratively down, status/lease reporting, automatic upstream DNS discovery |
| NAT | Basic NAT44 for TCP/UDP and ICMP echo identifiers, 64 mappings with 120s idle timeout, port forwards (DNAT) with hit counters, ARP reply for the LAN address, ICMP echo replies on the LAN address |
| Firewall | Extended engine: see [SHARKOS-FIREWALL.md](SHARKOS-FIREWALL.md) |
| Management | pfSense-style single-page dashboard (everything visible at once, no tabs), self-contained HTML with no external assets, JSON status API, command API, token auth, firewall drop log, live counters |
| Persistence | `rtr save` / `rtr load` to `/System/Config/router.conf` (LAN, pool, lease, domain, DNS, WAN, token, HTTP port, reservations, records, forwards, policies) |

Defaults: LAN `192.168.10.1/24`, DHCP pool `192.168.10.100-200`, lease
`3600s`, domain `lan`, upstream DNS `1.1.1.1` with `8.8.8.8` secondary (WAN
DHCP overrides these when it supplies a DNS server), WAN mode `dhcp`, NAT on.

---

## Quick start

```text
rtr start                            # DHCP + DNS + WAN + NAT + web UI on :69
rtr status                           # everything at a glance
rtr lan 192.168.50.1 255.255.255.0   # change LAN subnet
rtr dhcp pool 192.168.50.100 192.168.50.200
rtr dhcp lease 7200
rtr dhcp reserve b8:27:eb:4f:2a:11 192.168.50.10 nas
rtr dns add printer.lan 192.168.50.20
rtr wan static 10.0.0.5 255.255.255.0 10.0.0.1 1.1.1.1
rtr forward add tcp 8080 192.168.50.10 80 "nas web"
rtr profile router                   # deny-by-default firewall for router use
rtr token my-long-secret
rtr save
```

`rtr stop` disables all router services (the WAN DHCP client keeps its own
state). `rtr help` prints the full command list, `fw docs` the firewall list.

---

## CLI reference

```
rtr start [port] | stop | status | stats | help
rtr lan <ip> <mask>
rtr dhcp pool <start> <end> | lease <seconds> | enable | disable
rtr dhcp leases | reservations | reserve <mac> <ip> [host] | unreserve <id>
rtr dhcp release <ip>
rtr dns enable | disable | upstream <ip> [ip2] | domain <name>
rtr dns add <name> <ip> | del <name> | records | flush
rtr wan status | dhcp | renew | static <ip> <mask> <gw> [dns] | down
rtr ports
rtr port wan <index> | lan <index> | auto
rtr nat on | off | status | table
rtr forward add <tcp|udp> <wanport> <lanip> <lanport> [comment] | del <id> | list
rtr http port <port> | token <value> | profile router
rtr save [path] | load [path]
```

`rtr` and `fw` work in the desktop shell, the Server console, over SSH
(`server_execute_remote`) and from the web console.

---

## Web console and API

Open `http://<lan-ip>:69/`. The dashboard is a **single page** in pfSense
style: dark top bar and side menu, light card widgets, and everything visible
at once — system information, interface assignments, WAN uplink, LAN/DHCP,
leases and reservations, DNS, firewall rules/policies/counters, NAT forwards
and mappings, traffic counters, log, quick actions, quick configuration and
the CLI console. The side menu only scrolls to a widget; nothing is hidden
behind tabs.

The **Interface Assignments** widget shows the uplink adapter (name, driver,
MAC, PCI slot, IP, link) and the LAN port. With two or more NICs the LAN port
is the second adapter; with one NIC the LAN shares the uplink and the widget
says so. Enter the API token in the header; it is kept in `sessionStorage` and
sent as `X-Shark-Token`.

The HTML file is [`docs/router_ui.html`](router_ui.html). It contains all CSS
and JS inline, so it also works offline as a static preview (it falls back to
demo data when no router is reachable). Regenerate the embedded C header with
`make router-ui` after editing it.

| Method | Path | Auth | Result |
| --- | --- | --- | --- |
| GET | `/` | no | dashboard HTML |
| GET | `/api/status` | yes | full JSON state (interfaces, LAN, WAN, leases, DNS, NAT, firewall, stats, log) |
| GET | `/api/fw` | yes | firewall rules, policies, services, counters |
| GET | `/api/log` | yes | recent firewall drops as a JSON array |
| POST | `/api/cli` | yes | executes any `rtr`/`fw` command, returns `{"ok":true,"output":"..."}` |
| POST | `/api/token` | yes | replaces the API token |

For API calls, send either `X-Shark-Token: <token>` or
`Authorization: Bearer <token>`. The CLI body can be plain text or
`{"cmd":"rtr status"}`.

```bash
TOKEN=my-long-secret
curl -H "X-Shark-Token: $TOKEN" http://192.168.10.1:69/api/status
curl -H "X-Shark-Token: $TOKEN" --data 'rtr dhcp leases' \
     http://192.168.10.1:69/api/cli
curl -H "X-Shark-Token: $TOKEN" --data 'fw add deny tcp dport 23 log' \
     http://192.168.10.1:69/api/cli
```

Every mutating endpoint requires the token. GET `/` is deliberately public so
the UI can load before authentication; all data endpoints are protected.

---

## Interfaces: uplink adapter and LAN port

`net_port_scan()` walks PCI configuration space at boot and records every
recognised network controller (Realtek RTL8139/8168/8169/8029, AMD PCnet,
Intel e1000, virtio-net, NE2000) with a pfSense-style name, driver name, PCI
slot, MAC and link state. Only one of them is the adapter the kernel driver
actually transmits on (`active`).

`router_init()` then auto-assigns roles:

| NICs | WAN (uplink) | LAN |
| --- | --- | --- |
| 0 or 1 | adapter 0 | shares the uplink (`lan_shared = 1`) |
| 2 or more | adapter 0 | adapter 1 (`lan_shared = 0`) |

```text
rtr ports                        # list adapters, roles, MACs, PCI slots
rtr port lan 1                   # make adapter 1 the LAN port
rtr port wan 0                   # make adapter 0 the uplink
rtr port lan 0                   # force shared single-NIC mode
rtr port auto                    # re-detect and auto-assign again
```

Assignments are stored by `rtr save` as a `ports <wan> <lan> <shared>` line
and restored by `rtr load`. `/api/status` exposes them under `interfaces`
(`count`, `mode`, `wan`, `lan`, `lan_shared`, `uplink`, `lan_port`, `ports[]`).

> The current kernel drives one adapter. A second NIC is detected and
> assigned the LAN role (shown on the dashboard and used by the config), but
> frames still egress the single active driver. WAN and LAN therefore share
> the same wire until the drivers support multiple simultaneously active
> adapters. See Limitations below.

---

## Firewall integration

`rtr start` registers implicit service ports (UDP/67 DHCP, UDP/53 DNS,
TCP/<port> management) so those services keep working even under a
deny-by-default policy. Explicit firewall rules are always evaluated first,
so `fw deny udp 53` still blocks DNS.

`rtr profile router` (or `fw profile router`) installs a deny-by-default
router policy:

1. allow established traffic in any direction;
2. allow LAN -> WAN forwarding;
3. allow LAN -> router input (ping, DHCP, DNS, web console, ICMP replies);
4. explicitly deny WAN-side DHCP, DNS and console requests;
5. implicitly allow the registered service ports for LAN clients;
6. deny everything else, including new connections from the WAN side.

The console deny rule follows `rtr http port <port>` automatically, so
changing the management port does not reopen it to the WAN side.

Port forwards automatically add their WAN port as an implicit service port.
`fw status -v` shows rules, hit counters, services, counters and recent drops.

---

## How it is wired into the kernel

The router does not need a second thread or a second NIC. `net.c` gained five
optional callbacks and two helpers; `router_init()` registers them:

| Hook | Called from | Used for |
| --- | --- | --- |
| `net_set_service_hook` | TCP/UDP/ICMP receive path | DHCP server, DNS server/forwarder, inbound NAT and DNAT |
| `net_set_arp_hook` | ARP request handling | ARP replies for the LAN/WAN addresses |
| `net_set_local_ip_hook` | `ip_is_for_us()` | accepting the LAN address and static WAN address |
| `net_set_forward_hook` | IPv4 frames not addressed to us | outbound/inbound NAT |
| `net_set_poll_hook` | `net_poll()` | lease/DNS/NAT expiry, WAN refresh, HTTP polling |

`net_port_scan()` / `net_port_get()` provide the adapter inventory that backs
the interface assignments widget. `net_send_udp()` builds a complete
ethernet/IPv4/UDP datagram with valid IP and UDP checksums; `net_send_raw()` now also consults
`firewall_output_allowed()`. `net_dhcp_stop()` / `net_set_static_config()`
make the WAN side manageable without fighting the existing DHCP client.

`net_poll()` is already called by the Server console, the desktop loop, the
lite shell and the TCP server internals, so router state advances everywhere.
`router_poll()` and `router_http_poll()` have re-entrancy guards because
`net_tcp_server_send()` calls `net_poll()` while waiting for ACKs.

Source files:

- `include/router.h`, `src/net/router.c` — core, DHCP, DNS, WAN, NAT, JSON
- `src/net/router_http.c` — HTTP server and API
- `src/net/router_cli.c` — shared `rtr`/`fw` CLI and config persistence
- `src/net/router_ui_html.h` — generated from `docs/router_ui.html`
- `src/net/firewall.c`, `include/firewall.h` — extended firewall
- `src/shell/commands.c`, `src/server/main.c`, `src/shell/main.c`,
  `src/shell/lite.c` — command registration and boot-time init
- `tests/router_test.c`, `tests/firewall_test.c` — host-side tests

---

## Build and test

```bash
make server              # sharkos-server.iso (needs xorriso + limine assets)
make sharkos-server.bin  # 64-bit server kernel only
make server32            # 32-bit server ISO
make test-firewall       # extended firewall regression tests
make test-router         # DHCP/DNS/WAN/NAT/CLI/JSON regression tests
make router-ui           # regenerate the embedded dashboard header
```

`make test-router` injects real DHCP DISCOVER/REQUEST/RELEASE frames, DNS A
queries plus an upstream reply, NAT/ICMP frames and CLI commands into the
router, exercises the interface inventory and role assignment, and validates
the generated JSON with Python.

---

## Limitations (intentionally "basic")

- One *active* NIC: the kernel initializes and transmits on a single adapter.
  Extra PCI NICs are detected, named and auto-assigned the LAN role, but are
  not yet driven independently. WAN and LAN share the wire; LAN clients use
  the LAN address as their gateway, the WAN address is the upstream identity.
  There is no VLAN, bridge or routing protocol. `rtr ports` shows the truth
  (`driver active` vs `detected`).
- IPv4 only. Fragmented IPv4 packets are dropped; no IPv6, no reassembly.
- NAT handles TCP, UDP and ICMP echo. No FTP/SIP ALG, no PPPoE, no full conntrack
  (TCP sequence numbers are not validated).
- DNS handles A/PTR, serves AAAA as empty NOERROR, no DNSSEC/TCP DNS/EDNS0.
- DHCP relay (`giaddr`) is not supported.
- Leases, DNS cache, NAT mappings, firewall rules and counters live in RAM.
  `rtr save`/`rtr load` persist router configuration and firewall *policies*,
  not firewall rules; re-apply rules after reboot if needed.
- The console is HTTP, not HTTPS. Change the default token and keep it on a
  trusted network.
