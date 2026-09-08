# Phase 21 Network Stack Design

## Scope

Phase 21 introduces a real network path for RixuriOS rather than command-level stubs. The path is layered as packet buffers, Ethernet framing, ARP, IPv4, ICMP, UDP, TCP, routing/interfaces, sockets and userspace utilities. The RTL8125 driver is the first physical Ethernet target; loopback remains the deterministic host-test backend.

## Non-negotiable rule

`ping` and `curl` must use the same socket and packet path as ordinary applications. A timeout, unavailable interface or failed route must return an error. No utility may print success without receiving and validating a corresponding packet or protocol response.

## Initial layers

The packet layer owns bounded buffers, ownership, length checks and Internet checksums. Ethernet validates destination/source addresses and EtherType. ARP maintains a bounded neighbor cache with request/reply handling and expiry. IPv4 validates version, header length, total length, checksum, TTL and protocol. The routing table initially supports a directly connected route, a default route and loopback.

ICMP provides echo request/reply and error messages. UDP provides datagram endpoints and checksum validation. TCP begins with an explicit state machine and bounded retransmission timers; unsupported states fail closed. The socket layer exposes loopback and NIC-backed endpoints through one ABI, with blocking and nonblocking behavior represented explicitly.

## Driver boundary

The RTL8125 driver is isolated behind an Ethernet-device interface. PCI discovery, BAR validation, MMIO setup, DMA ring allocation, interrupt or polling completion, link state and reset/recovery are separate steps. No physical-hardware claim is made until a real RTL8125 or compatible device passes the transmit/receive and recovery tests.

## Userspace programs

`ping` sends ICMP echo requests through the socket layer, validates identifier, sequence, source and checksum, and reports timeout or protocol errors. `curl` initially supports HTTP over TCP for an explicit URL form, DNS resolution through the resolver, redirects only when bounded by policy, and non-zero failure status for DNS, connection, TLS-unsupported or HTTP transport errors. HTTPS is not claimed until the TLS layer exists.

## Acceptance gates

The deterministic gate first validates loopback packet delivery, checksums, ARP cache behavior, IPv4 routing, ICMP echo, UDP delivery, TCP connect/close and socket blocking semantics. The QEMU gate then exercises the same path through a configured virtual or physical Ethernet device. The RTL8125 gate requires PCI probe, MMIO validation, DMA TX/RX, link state and reset/recovery evidence. The userspace gate requires `ping` success only after a validated echo reply and `curl` success only after a validated HTTP response.

## Current status

At the start of Phase 21 the repository has PCI, DMA and storage infrastructure but no network, socket, NIC driver, `ping` or `curl` implementation. The first implementation milestone is therefore the packet/device abstraction plus host-testable checksum and loopback foundations; later milestones must not bypass that path.


## Continuation status — 2026-09-08

The deterministic loopback milestone now includes real UDP and TCP wire-segment helpers. UDP uses IPv4 pseudo-header checksums and strict length/port validation. TCP uses minimum-header segments with sequence/acknowledgment validation, explicit flags and checksum verification. The TCP loopback connection path performs SYN, SYN/ACK and ACK processing through those helpers before exposing an established socket. The loopback HTTP response is queued only after a validated ACK/PSH GET segment and a validated ACK/PSH response segment; it is not a direct string-triggered socket shortcut.

Host tests and real QEMU serial-to-TTY tests pass for the bounded loopback path. The tested userspace commands are `/usr/bin/ping` against `127.0.0.1` and `/usr/bin/curl` against the loopback HTTP service. This is deterministic protocol evidence, not NIC or Internet evidence.

The next required implementation slice is a network-device boundary that connects the same Ethernet/IP/socket path to a completed E1000 QEMU backend, including RX/TX completion polling, MAC/interface state, ARP requests/replies and IPv4 delivery. Only after that path is validated should DNS, DHCP, routing configuration and physical RTL8125 TX/RX work be claimed. The current E1000 code proves PCI discovery, BAR mapping and ring setup only; the current RTL8125 code is a probe/register/descriptor foundation. External-network and physical-hardware results remain `NOT TESTED` or `BLOCKED` until real packets and recovery behavior are observed.
