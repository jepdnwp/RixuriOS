#pragma once
#include <stdint.h>
#include <stddef.h>
#include "pci.h"

typedef struct rix_pci_driver rix_pci_driver_t;
typedef struct {
    const rix_pci_device_t *pci;
    const rix_pci_driver_t *driver;
    uint8_t claimed;
} rix_device_t;
struct rix_pci_driver {
    const char *name;
    uint16_t vendor_id;
    uint16_t device_id;
    int (*probe)(rix_device_t *device);
    void (*remove)(rix_device_t *device);
};
int device_model_init(void);
int device_model_register_driver(const rix_pci_driver_t *driver);
size_t device_model_count(void);
rix_device_t *device_model_get(size_t index);
int device_model_bind(void);
