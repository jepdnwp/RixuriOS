#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_PCI_MAX_DEVICES 256

typedef struct { uint8_t bus,device,function; uint16_t vendor_id,device_id; uint8_t class_code,subclass,prog_if,revision; uint8_t header_type; uint32_t bars[6]; } rix_pci_device_t;

int pci_init(void);
uint32_t pci_config_read32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset);
int pci_config_write32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset,uint32_t value);
uint32_t pci_config_read32_ecam(uint16_t segment,uint8_t bus,uint8_t device,uint8_t function,uint8_t offset);
int pci_config_write32_ecam(uint16_t segment,uint8_t bus,uint8_t device,uint8_t function,uint8_t offset,uint32_t value);
int pci_find_capability(const rix_pci_device_t *device,uint8_t capability,uint8_t *offset);
size_t pci_device_count(void);
const rix_pci_device_t *pci_device(size_t index);
