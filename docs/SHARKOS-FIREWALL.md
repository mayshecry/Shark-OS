# SharkOS Server firewall

The Server edition has a small IPv4 INPUT packet filter. It validates IPv4 header checksums and basic TCP, UDP, and ICMP lengths, drops fragmented IPv4 packets because reassembly is not implemented, evaluates rules in order, and applies the configured default policy when no rule matches. TCP return traffic is tracked from outbound SYN through a matching SYN-ACK; UDP and ICMP echo replies are matched against outbound traffic. Earlier explicit rules can deny tracked replies. ARP is passed for LAN operation. A DHCP server response from UDP port 67 to client port 68 is automatically allowed only when no explicit rule matches it.

Rules, rate configuration, and policy are kept in RAM and reset at reboot. The default policy is allow. The firewall filters host INPUT traffic; bridged container frames are delivered to their virtual adapters before the host filter. NAT return traffic is translated to its container first.

Commands:

- `fw docs` displays this guide in the Server console.
- `fw status` or `fw list` shows the policy, rate setting, drop count, and rules.
- `fw allow tcp 22` allows TCP destination port 22.
- `fw deny tcp 23` blocks TCP destination port 23.
- `fw deny icmp` blocks incoming ICMP, including ping requests.
- `fw allow icmp` allows incoming ICMP when the default policy is deny.
- `fw allow tcp 22 from 192.168.1.20` allows SSH only from that source address.
- `fw deny udp any` blocks all UDP input.
- `fw policy deny` changes the unmatched-packet policy to deny.
- `fw policy allow` changes the unmatched-packet policy to allow.
- `fw delete 1` deletes rule 1.
- `fw clear` removes rules but does not change the policy or rate setting.
- `fw rate 20` limits new traffic to 20 packets per second per source IPv4 address.
- `fw rate off` disables rate limiting.

Rules are evaluated in insertion order; the first matching rule decides. To block all incoming ICMP while otherwise retaining the current default policy, clear conflicting rules and add a deny rule:

```text
fw clear
fw deny icmp
fw status
```

To use a deny-by-default policy while allowing SSH and ping:

```text
fw clear
fw policy deny
fw allow tcp 22
fw allow icmp
fw status
```

The rate limiter is off by default and accepts values from 1 to 1000 packets per second per source IP. It counts inbound TCP connection SYNs, UDP packets, and ICMP echo requests together per source in one-second windows; tracked replies and DHCP server responses are exempt. `fw status` shows the configured limit and dropped-packet count. It tracks up to 64 source addresses per window and fails closed for additional new sources until the next window.

Rule ports apply to TCP or UDP destination ports; ICMP rules are protocol-wide. The rule table supports 16 ordered rules. TCP tracking matches IPv4 addresses, ports, and handshake flags, but does not validate TCP sequence numbers. This is a basic IPv4 input filter, not a replacement for a complete firewall: IPv6 rule filtering, fragment reassembly, persistence, configurable port forwarding, and filtering of bridged container traffic are not provided.
