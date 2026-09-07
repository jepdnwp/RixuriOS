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
