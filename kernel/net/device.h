#pragma once
#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_NET_DEVICE_IP 0x0a00020fu
#define RIX_NET_DEVICE_NETMASK 0xffffff00u
#define RIX_NET_DEVICE_GATEWAY 0x0a000202u
#define RIX_NET_DEVICE_DNS 0x0a000203u

typedef struct {
    uint8_t present;
    uint8_t link_up;
    uint8_t mac[6];
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
} rix_net_device_info_t;

int rix_net_device_init(void);
const rix_net_device_info_t *rix_net_device_info(void);
/* Backend that won probe order ("e1000", "rtl8125", or "none"). The E1000
 * path is byte-identical to before; RTL8125 is a fallback only. */
const char *rix_net_device_backend(void);
/* Apply DHCP-learned configuration. Nonzero fields replace the static
   defaults; zero netmask/gateway/dns keep the current values. */
int rix_net_device_configure(uint32_t address, uint32_t netmask,
                             uint32_t gateway, uint32_t dns);
int rix_net_device_transmit(const rix_net_packet_t *packet);
int rix_net_device_receive(rix_net_packet_t *packet);
