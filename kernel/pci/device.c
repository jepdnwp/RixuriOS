#include "device.h"
#include <stddef.h>

#define RIX_DEVICE_MAX 256
#define RIX_DRIVER_MAX 64
static rix_device_t devices[RIX_DEVICE_MAX];
static const rix_pci_driver_t *drivers[RIX_DRIVER_MAX];
static size_t device_count,driver_count;

int device_model_init(void){device_count=0;driver_count=0;return 0;}
int device_model_register_driver(const rix_pci_driver_t *driver){
 if(!driver||!driver->name||!driver->probe||driver_count>=RIX_DRIVER_MAX)return -1;
 for(size_t i=0;i<driver_count;i++)if(drivers[i]==driver)return -1;
 drivers[driver_count++]=driver;return 0;
}
size_t device_model_count(void){return device_count;}
rix_device_t *device_model_get(size_t index){return index<device_count?&devices[index]:NULL;}
int device_model_bind(void){
 device_count=0;
 for(size_t i=0;i<pci_device_count()&&device_count<RIX_DEVICE_MAX;i++){
  const rix_pci_device_t *p=pci_device(i);if(!p)continue;
  rix_device_t *d=&devices[device_count++];d->pci=p;d->driver=NULL;d->claimed=0;
  for(size_t j=0;j<driver_count;j++){
   const rix_pci_driver_t *drv=drivers[j];
   if((drv->vendor_id!=0xffffu&&drv->vendor_id!=p->vendor_id)||(drv->device_id!=0xffffu&&drv->device_id!=p->device_id))continue;
   if(drv->probe(d)==0){d->driver=drv;d->claimed=1;break;}
  }
 }
 return 0;
}
