# Why httpd stops answering, and what changed

## Symptom

After some time (hours, or one network hiccup) the built-in web server stops
responding. No error, no log line, no crash: the page just spins until the
browser gives up. A restart of `httpd` brings it back.

## Where the web server actually lives

`src/net/httpd.c` is only a thin layer over the kernel's TCP server, which
lives in `src/net/net.c` (`tcp_server_on_segment`, `net_tcp_server_accept`,
`net_tcp_server_close`). Everything that decides whether a connection is
answered is in that TCP server, and that is where the bug was.

The chain per request is:

```
net_poll -> NIC poll -> net_on_frame -> tcp_server_on_segment   (state machine)
        -> httpd_poll -> net_tcp_server_accept                  (hand out a slot)
                      -> net_tcp_server_recv/send               (serve it)
                      -> net_tcp_server_close                   (release it)
```

The server has a fixed table of `TCP_SERVER_CONNECTIONS` (8) slots. One slot is
consumed by every connection that is accepted and by every connection that is
still in the handshake. When the table is full, the SYN for a new connection is
dropped **without a RST**, so the client retries until it times out. There is no
reasoning about *why* the table is full, which is the root of the problem.

## The three leaks that filled the table

Before this change, a slot was only ever released in three situations: the
client sent a FIN, the client sent a RST, or the server itself called
`net_tcp_server_close()`. Nothing else ever freed a slot. Three paths violate
that assumption and each one is enough to wedge the server on its own:

1. **Half-open connections were immortal.** A SYN is answered and the slot moves
   to state 1 waiting for the final ACK. If that ACK never arrives (a scanner, a
   health checker, a NAT'd guest whose reply got lost during a link flap, a
   browser tab closed mid-handshake) the slot stayed in state 1 *forever*.
   Eight of those and no connection is ever answered again.

2. **An accepted request that never arrives was immortal.** The slot moves to
   established, httpd accepts it, no bytes ever arrive and the client never
   sends a FIN (because it already closed its side, or it is a half-open
   socket). Nothing is reaped, because there was no timeout anywhere.

3. **A lost re-arm could keep the whole port deaf.** `httpd_rearm()` runs after
   every link recovery and re-armed a port only if `net_tcp_listen()` returned
   success. That function short-circuits on "already listening", so a re-arm
   during a link flap could return success while the listener table was in an
   inconsistent state, leaving a port that looks served but never answers.

The reproduction harness in `tests/tcp_server_stall_repro.c` shows all of it
without needing a machine: scenario 2 in its output is a table of eight
connections in TIME_WAIT where the next SYN is dropped and the browser reports a
timeout, and scenario 3 shows eight half-open handshakes doing the same.

## A second, independent stall

Two HTTP servers can end up holding the same port. `src/net/router_http.c` runs
its own listener for the router console and `src/net/httpd.c` runs the file
server, and both refuse to bind a port somebody else has - but the *check*
(`net_tcp_is_listening`) is exactly the call that returns success in the
inconsistent state described above. When that collision happens, one of the two
silently loses its listener and its clients are dropped forever.

The web server now re-checks its own listener on every poll and re-binds it if it
disappeared, so a lost listener repairs itself instead of needing a restart.

## What was changed

### `src/net/net.c` - connection lifecycle

* Every connection slot records `last_rx` / `last_tx` (ticks of the last inbound
  and outbound segment).
* `net_tcp_server_reap()` runs from `net_poll()` on every tick and reclaims:
  * state-1 slots with no ACK after `TCP_SERVER_SYN_TIMEOUT_MS` (4 s),
  * established slots where the peer closed and the buffer is drained,
  * established slots that sent nothing and received nothing for
    `TCP_SERVER_IDLE_TIMEOUT_MS` (30 s), after sending a RST,
  * slots whose keepalive probe went unanswered `TCP_SERVER_KEEPALIVE_TRIES`
    (3) times.
* Keepalive probes (an ACK with the last sent sequence number) go out after
  `TCP_SERVER_KEEPALIVE_MS` (3 s) of quiet, which keeps NAT and firewall state
  for a busy page alive without spamming an idle one.
* A SYN that arrives when the table is full evicts a stale half-open slot
  instead of being dropped.
* `net_tcp_server_send()` now has one absolute deadline
  (`TCP_SERVER_SEND_TIMEOUT_MS`, 8 s) instead of a per-phase 3 s that could be
  restarted by every retry, and it resets the peer and frees the slot when it
  gives up, so a client that stops reading can no longer pin a working thread.
* The receive buffer per connection went from 16 KiB to 4 KiB
  (`TCP_SERVER_RX_SIZE`). The old size reserved 128 KiB of kernel memory for
  eight connections on machines that boot with a fraction of that; with the
  advertised window now derived from a separate write cursor, 4 KiB per
  connection is more than enough for request headers and still lets file bodies
  stream.
* The advertised window in `tcp_server_emit()` is now truthful: it reports
  `rx_len - rx_rd` of free space. The old code force-collapsed the ring buffer
  whenever it was full, which could overwrite unread bytes.
* `net_on_frame()` hands non-local unicast frames to the virtual NIC layer as
  well as to the router forward hook. Without that, NAT'd containers on an
  emulated gateway MAC never received the replies that vnic.c itself emitted.
* Nicer detail: `net_poll()` now dispatches to the single active driver through
  the device registry (see the driver section below) instead of four `if`s.

### `src/net/httpd.c`

* Each client slot carries `opened`; a request header that does not arrive
  within `HTTPD_HEADER_TIMEOUT_MS` (5 s) is closed. Slow-loris style clients
  can no longer hold one of the eight client slots.
* The response body is streamed in 1 KiB chunks instead of one
  `net_tcp_server_send()` call, so the send deadlock above cannot swallow a
  whole response, and the caller notices a partial send when it happens.
* A request that overflows the 4 KiB header buffer now gets a real
  `431 Request Header Fields Too Large` instead of a silent close.
* `httpd_relisten()` re-binds the port if the listener disappeared, so a lost
  listener (link flap, port collision with the router console) repairs itself.
* `httpd_stats()` exposes accepted / dropped / slow counters for the shell.

### Tests

* `tests/tcp_server_stall_repro.c` (`make test-tcp-stall`) reproduces the old
  behaviour: it prints how the table fills and how the next SYN gets dropped,
  with no SYN-ACK.
* `tests/tcp_server_recovery_test.c` (`make test-tcp-recovery`) runs the same
  scenarios against the new bookkeeping and asserts that the table drains, that
  keepalives keep a live connection alive, and that 500 sequential requests
  never exhaust it.

## What to watch for if it ever happens again

`netstat` in the shell prints the listener table. `net_tcp_server_stale_count(port)`
reports slots that the reaper considers abandoned; if that number stays above
zero for more than one poll interval, the reaper is not running (which means
`net_poll()` is not being called from the main loop of the image you booted).
