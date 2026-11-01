# SharkOS Server Containers

## Agreed scope

- Server edition only.
- Containers share the running SharkOS Server kernel; they do not boot an ISO and do not use VT-x or AMD-V.
- Each container is a SharkOS Server instance with its own root filesystem, shell context, and HTTP/SSH service configuration.
- Each container needs a distinct virtual MAC address and a selectable network mode: bridged through the physical NIC with LAN DHCP, or NAT on a private subnet with outbound translation.
- The first version is intended for trusted services. It is operationally isolated, not a security boundary against malicious guest code.
- Arbitrary ELF applications inside containers are out of initial scope.

## Current implementation constraints

- `src/server/main.c` owns a single main loop and polls the host network, HTTP server, and SSH server directly.
- The filesystem still exposes global `root` and `current_dir` pointers plus a shared node pool. The current prototype swaps the root and working-directory pointers around local console commands; it does not support concurrent shell tasks.
- `src/net/net.c` implements one NIC and one global DHCP/IP/TCP stack.
- HTTP and SSH state is singleton/global rather than scoped to an instance.
- The server build has no general process scheduler or per-process address spaces.

A metadata-only `start` command would not start an isolated SharkOS instance. The runtime must first give services and shell sessions explicit container contexts, then poll those contexts from the server supervisor.

## Runtime shape

The host server supervisor owns a fixed-size table of container instances. Each instance has a name, image reference, lifecycle state, unique MAC, network/DHCP state, root filesystem view, and service settings. The initial runtime is cooperative: the host loop polls running container contexts. It should not claim CPU, RAM, or security isolation that it does not enforce.

The container image is a non-bootable SharkOS Server root filesystem plus service defaults. The base image can be shared read-only while each instance has its own writable overlay. The image is not an ISO and does not contain another bootable kernel.

Bridge mode forwards Ethernet frames between each virtual MAC and the physical NIC, preserving the container source MAC and using the LAN's DHCP server. NAT mode assigns an address from 10.203.0.0/24, serves DHCP locally, and translates outbound IPv4 ICMP, TCP, and UDP traffic through the host address with tracked return mappings. NAT allows outbound flows only; unsolicited inbound traffic and port forwarding are not implemented.

## Current prototype status

The server-only builds now include a RAM-resident container registry and a minimal SharkOS Server filesystem template. The local console supports `container create <name>`, `start`, `stop`, `list`, `inspect`, `enter`, `exit`, and `remove`. Entered shells use separate root and working-directory pointers while sharing the host kernel. Up to four container definitions are supported.

Each running container attaches a virtual Ethernet interface with a unique locally administered MAC. `container start <name> -br` selects bridge mode and requests an address from the LAN DHCP server; `container start <name> -nat` selects NAT mode, assigns a private address, and answers DHCP locally. The most recently selected mode is retained across stop/start, and starting without a flag uses that mode. The physical NIC receive path is configured for promiscuous reception on supported drivers. Bridge mode queues matching and broadcast frames; NAT mode applies IPv4 ICMP, TCP, and UDP address/port translation with tracked return traffic. NAT defaults to outbound-only, without unsolicited inbound connections or port forwarding. The `ping <ip> [count]` command in an entered container uses that container's virtual interface for ARP resolution, ICMP echo requests, and reply matching rather than the host interface.

The prototype is not persistent across reboot. Per-container TCP/IP service contexts, HTTP/SSH contexts, and shell sessions using each virtual interface are still unfinished, so this network mode work does not yet make those services reachable through the container adapters. The host-side regression test simulates bridged and NAT virtual-adapter ICMP echo request/reply traffic to 1.1.1.1, plus bridged DHCP/ARP and NAT DHCP, gateway ARP, UDP translation, and return mapping. This verifies frame construction and translation, not reachability through a real NIC, router, or ISP.

## Intended command interface

- `container create <name> --image <server-image>`
- `container start <name> [-br|-nat]`
- `container stop <name>`
- `container restart <name>`
- `container list`
- `container inspect <name>`
- `container enter <name>`
- `container remove <name>`

`start` must only report full service/network readiness after the rootfs, virtual NIC, network context, and selected services have actually initialized. `enter` attaches the host console to that container's shell; concurrent shells require independent shell input/output state.

## Implementation order

1. Define a Server Container Image format and create/validate a base image from SharkOS Server's filesystem and service defaults.
2. Add persistent container definitions and the management command parser, with truthful lifecycle states.
3. Refactor filesystem access, shell state, HTTP server state, and SSH server state into per-container contexts; run each context under the server supervisor.
4. Add virtual MAC registration and per-container network protocol state, then bridge frames through the physical NIC and verify that each instance gets its own DHCP lease.
5. Add boot-time restoration, resource accounting, and lifecycle error reporting after the basic end-to-end path works.

## Validation target

A useful first end-to-end test is to create two containers from the same SharkOS Server image, start both, confirm separate MAC/IP DHCP leases on the same LAN, serve different web roots from both, enter each shell, then stop and restart either container without changing the other.