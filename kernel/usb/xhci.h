#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {uint8_t bus,device,function;uint64_t bar0;uint8_t cap_length;uint8_t max_slots;uint8_t max_intrs;uint8_t max_ports;uint32_t hci_version;uint32_t hcc_params1;uint32_t usbcmd;uint32_t usbsts;} rix_xhci_controller_t;
int xhci_init(void);
size_t xhci_controller_count(void);
const rix_xhci_controller_t *xhci_controller(size_t index);
