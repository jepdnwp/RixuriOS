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


## E1000 and ARP continuation — 2026-09-08

The E1000 boundary now uses the exact legacy 16-byte descriptor ABI. Descriptor rings and buffers are allocated below 4 GiB, TX descriptors are submitted with EOP/IFCS/RS and completed by polling TDH, and RX descriptors are consumed from the device-owned ring with DD/EOP and RDT return. QEMU reports link-up and the emulated MAC. ARP request/reply wire serialization and parsing is also available beside the cache, with strict Ethernet/IPv4/length/opcode checks.

The next gate is attaching this device boundary to the per-process socket path: Ethernet frame transmit/receive dispatch, static/DHCP interface configuration, ARP resolution on wire, IPv4 delivery, UDP DNS exchange and external TCP. Until that integration is complete, `curl google.com` must continue to fail closed rather than report fabricated HTML.


## External ARP and RX DMA gate — 2026-09-08

The device-to-stack path now emits a valid ARP request through E1000 and receives a QEMU user-net ARP reply on the virtual wire. Packet capture confirms request `10.0.2.15 -> 10.0.2.3` and reply `10.0.2.3 -> 10.0.2.15`. The remaining gate is delivery of the received frame into the E1000 RX descriptor ring; the current software observation remains `DD=0`, `RDH=0`, `RDT=63`. DNS parsing and external TCP are intentionally blocked behind this RX DMA gate.


## Phase 21 exit status — 2026-09-09

Phase 21 — **Full Network Stack** is complete for the host, loopback, QEMU virtual-network and software-integration scope. The same packet, Ethernet, ARP, IPv4, UDP, TCP, device and socket path is exercised by `ping` and `curl`; success requires a validated protocol response. DHCPv4, DNS, external TCP and validated HTTP redirects pass in the QEMU user-net harness. The scheduler CR3-resume correction also restores repeated user-process network commands after the initial Ring 3 transition.

Physical RTL8125 TX/RX, link negotiation, interrupt delivery, reset/recovery and final Ring 3 qualification on the Ryzen/ASUS target remain `NOT TESTED`. These are hardware evidence gates and are not replaced by host or QEMU results. They are tracked in the physical qualification phase.


## IPv6 foundation checkpoint — 2026-09-09

The first IPv6 protocol slice is implemented. Ethernet now accepts IPv6 EtherType `0x86DD`. The wire layer validates IPv6 version, payload length, traffic class and flow label. ICMPv6 Echo Request/Reply uses the RFC pseudo-header checksum, and bounded Neighbor Solicitation/Advertisement bodies are available for later neighbor-cache integration. Host tests cover valid packets and corrupted checksum/type rejection.

The full IPv6 gate remains open until address configuration, Router Advertisement handling, neighbor-cache/device dispatch, routing and IPv6 socket transport are integrated and exercised on QEMU or physical networking.


## IPv6 control-plane checkpoint — 2026-09-09

Router Solicitation and Router Advertisement support now carries a bounded Prefix Information option. The implementation derives a `/64` SLAAC address, generates a modified-EUI-64 link-local address from a MAC address, and maintains an expiring eight-entry Neighbor Cache. These semantics are host-tested. IPv6 transport sockets and device-stack dispatch remain the final integration gate for full Phase 21 IPv6 closure.


## IPv6 stack integration checkpoint — 2026-09-09

The device stack now accepts IPv6 Ethernet frames into a dedicated bounded RX queue and exposes a Neighbor Cache-backed IPv6 transmit path. Unknown neighbors are rejected until a verified cache entry exists. IPv6 transport socket endpoints and independent IPv6 traffic evidence are still required before a full-network IPv6 PASS.
