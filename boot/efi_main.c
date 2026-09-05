/* RixuriOS UEFI loader: self-contained subset of the UEFI interfaces needed to
 * load an ELF64 kernel, capture the final memory map and exit boot services. */
#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))
#define SYSVABI __attribute__((sysv_abi))
#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL 5
#define EFI_LOAD_ERROR 1
#define EFI_INVALID_PARAMETER 2
#define EFI_NOT_FOUND 14
#define EFI_ERROR(x) ((x) != EFI_SUCCESS)
#define EFI_PAGE_SIZE 4096ULL
#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_LOADER_DATA 4
#define EFI_FILE_MODE_READ 1ULL
#define EFI_FILE_DIRECTORY 0x10ULL

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    {0x0964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef struct { uint32_t Data1; uint16_t Data2,Data3; uint8_t Data4[8]; } EFI_GUID;
typedef struct { uint64_t lo, hi; } EFI_LBA;

typedef struct EFI_TABLE_HEADER { uint64_t Signature; uint32_t Revision, HeaderSize, CRC32, Reserved; } EFI_TABLE_HEADER;

typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(uint32_t, uint32_t, size_t, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, size_t);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(size_t *, void *, size_t *, size_t *, uint32_t *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(uint32_t, size_t, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, uint64_t);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(uint32_t, EFI_GUID *, void *, size_t *, EFI_HANDLE **);

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL; void *RestoreTPL;
    void *AllocatePages_unused;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent; void *SetTimer; void *WaitForEvent; void *SignalEvent;
    void *CloseEvent; void *CheckEvent; void *InstallProtocolInterface; void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface; EFI_HANDLE_PROTOCOL HandleProtocol; void *Reserved;
    void *RegisterProtocolNotify; void *LocateHandle; EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
    void *LocateDevicePath; void *InstallConfigurationTable;
    EFI_ALLOCATE_PAGES AllocatePages;
    void *OpenProtocol; void *CloseProtocol; void *OpenProtocolInformation;
    void *ProtocolsPerHandle; void *LocateHandleBuffer2; EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces; void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32; void *CopyMem; void *SetMem; void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct EFI_CONFIGURATION_TABLE { EFI_GUID VendorGuid; void *VendorTable; } EFI_CONFIGURATION_TABLE;

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor; uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle; void *ConIn;
    EFI_HANDLE ConsoleOutHandle; void *ConOut;
    EFI_HANDLE StandardErrorHandle; void *StdErr;
    void *RuntimeServices; EFI_BOOT_SERVICES *BootServices;
    size_t NumberOfTableEntries; EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t Revision; EFI_HANDLE ParentHandle; EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle; void *FilePath; void *Reserved;
    uint32_t LoadOptionsSize; void *LoadOptions;
    void *ImageBase; uint64_t ImageSize; uint32_t ImageCodeType, ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, uint64_t, uint64_t);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, size_t *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *, EFI_GUID *, size_t *, void *);
struct EFI_FILE_PROTOCOL {
    uint64_t Revision; EFI_FILE_OPEN Open; EFI_FILE_CLOSE Close; void *Delete;
    EFI_FILE_READ Read; void *Write; void *GetPosition; void *SetPosition;
    EFI_FILE_GET_INFO GetInfo; void *SetInfo; void *Flush;
};
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL { uint64_t Revision; EFI_STATUS (EFIAPI *OpenVolume)(void *, EFI_FILE_PROTOCOL **); } EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t phys;
    uint64_t virt;
    uint64_t pages;
    uint64_t attr;
} rixuri_efi_memory_desc_t;

typedef struct {
    uint64_t magic;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint64_t rsdp;
    uint64_t kernel_phys_base;
    uint64_t kernel_phys_end;
} rixuri_boot_info_t;

#define RIXURI_BOOT_MAGIC 0x52584955ULL
#define ELF_MAGIC 0x464c457fU
#define PT_LOAD 1
#define EI_NIDENT 16

typedef struct {
    unsigned char ident[EI_NIDENT]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff, flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf64_hdr_t;
typedef struct {
    uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
} elf64_phdr_t;

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;

static int memeq(const void *a, const void *b, size_t n) {
    const unsigned char *x=a,*y=b; for(size_t i=0;i<n;i++) if(x[i]!=y[i]) return 0; return 1;
}
static void *memcpy8(void *d,const void*s,size_t n){ unsigned char*D=d;const unsigned char*S=s;for(size_t i=0;i<n;i++)D[i]=S[i];return d; }
static void memset8(void*d,unsigned char v,size_t n){unsigned char*D=d;for(size_t i=0;i<n;i++)D[i]=v;}

static EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID acpi_guid = {0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};

static EFI_STATUS open_kernel(EFI_HANDLE image, void **data, size_t *size) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_STATUS s = BS->HandleProtocol(image, &image_guid, (void**)&loaded);
    if (EFI_ERROR(s)) return s;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    s = BS->HandleProtocol(loaded->DeviceHandle, &fs_guid, (void**)&fs);
    if (EFI_ERROR(s)) return s;
    EFI_FILE_PROTOCOL *root = 0, *file = 0;
    s = fs->OpenVolume(fs, &root); if(EFI_ERROR(s)) return s;
    CHAR16 name[] = {'\\','k','e','r','n','e','l','.','e','l','f',0};
    s = root->Open(root, &file, name, EFI_FILE_MODE_READ, 0); if(EFI_ERROR(s)){root->Close(root);return s;}
    size_t info_size = 0;
    EFI_GUID file_info_guid = {0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
    s = file->GetInfo(file,&file_info_guid,&info_size,0);
    if(s != EFI_BUFFER_TOO_SMALL){file->Close(file);root->Close(root);return s;}
    void *info=0; s=BS->AllocatePool(EFI_LOADER_DATA,info_size,&info); if(EFI_ERROR(s)){file->Close(file);root->Close(root);return s;}
    s=file->GetInfo(file,&file_info_guid,&info_size,info); if(EFI_ERROR(s)){BS->FreePool(info);file->Close(file);root->Close(root);return s;}
    uint64_t file_size=*(uint64_t*)((unsigned char*)info+8);
    BS->FreePool(info);
    void *buf=0; s=BS->AllocatePool(EFI_LOADER_DATA,(size_t)file_size,&buf); if(EFI_ERROR(s)){file->Close(file);root->Close(root);return s;}
    size_t read=(size_t)file_size; s=file->Read(file,&read,buf); file->Close(file); root->Close(root);
    if(EFI_ERROR(s)||read!=(size_t)file_size){BS->FreePool(buf);return EFI_LOAD_ERROR;}
    *data=buf; *size=read; return EFI_SUCCESS;
}

static EFI_STATUS load_elf(const void *data, size_t size, uint64_t *entry, uint64_t *base, uint64_t *end) {
    if(size<sizeof(elf64_hdr_t)) return EFI_LOAD_ERROR;
    const elf64_hdr_t*h=data;
    if(*(const uint32_t*)h->ident!=ELF_MAGIC || h->ident[4]!=2 || h->ident[5]!=1 || h->machine!=0x3e || h->phentsize!=sizeof(elf64_phdr_t)) return EFI_LOAD_ERROR;
    if(h->phoff + (uint64_t)h->phnum*sizeof(elf64_phdr_t) > size) return EFI_LOAD_ERROR;
    uint64_t lo=~0ULL, hi=0;
    for(uint16_t i=0;i<h->phnum;i++){
        const elf64_phdr_t*p=(const elf64_phdr_t*)((const unsigned char*)data+h->phoff+i*sizeof(*p));
        if(p->type!=PT_LOAD) continue;
        if(p->filesz>p->memsz || p->offset+p->filesz>size) return EFI_LOAD_ERROR;
        uint64_t seg_lo=p->p_paddr & ~(EFI_PAGE_SIZE-1), seg_hi=(p->p_paddr+p->memsz+EFI_PAGE_SIZE-1)&~(EFI_PAGE_SIZE-1);
        if(seg_lo<lo)lo=seg_lo; if(seg_hi>hi)hi=seg_hi;
    }
    if(lo==~0ULL || hi<=lo) return EFI_LOAD_ERROR;
    size_t pages=(size_t)((hi-lo)/EFI_PAGE_SIZE); EFI_PHYSICAL_ADDRESS addr=lo;
    EFI_STATUS s=BS->AllocatePages(EFI_ALLOCATE_ADDRESS,EFI_LOADER_DATA,pages,&addr); if(EFI_ERROR(s)) return s;
    memset8((void*)(uintptr_t)lo,0,(size_t)(hi-lo));
    for(uint16_t i=0;i<h->phnum;i++){
        const elf64_phdr_t*p=(const elf64_phdr_t*)((const unsigned char*)data+h->phoff+i*sizeof(*p));
        if(p->type!=PT_LOAD) continue;
        memcpy8((void*)(uintptr_t)p->p_paddr,(const unsigned char*)data+p->offset,(size_t)p->filesz);
    }
    *entry=h->entry; *base=lo; *end=hi; return EFI_SUCCESS;
}

static uint64_t find_rsdp(void) {
    for(size_t i=0;i<ST->NumberOfTableEntries;i++){
        EFI_CONFIGURATION_TABLE*t=&ST->ConfigurationTable[i];
        if(memeq(&t->VendorGuid,&acpi_guid,sizeof(EFI_GUID))) return (uint64_t)(uintptr_t)t->VendorTable;
    }
    return 0;
}

static EFI_STATUS final_memory_map(void **map,size_t *map_size,size_t *desc_size,uint32_t *version,uint64_t *key){
    size_t n=0, ds=0; uint32_t v=0; EFI_STATUS s=BS->GetMemoryMap(&n,0,&ds,&ds,&v);
    (void)s; n += ds*8;
    void *buf=0; s=BS->AllocatePool(EFI_LOADER_DATA,n,&buf); if(EFI_ERROR(s)) return s;
    size_t actual=n; s=BS->GetMemoryMap(&actual,buf,&ds,&ds,&v); if(EFI_ERROR(s)){BS->FreePool(buf);return s;}
    /* The UEFI key is the map's map-key field; GetMemoryMap returns it through
       the fourth parameter. This wrapper deliberately keeps the exact ABI below. */
    *map=buf; *map_size=actual; *desc_size=ds; *version=v; *key=(uint64_t)(uintptr_t)ds;
    return EFI_SUCCESS;
}

/* Correct GetMemoryMap declaration with the map key output parameter. */
typedef EFI_STATUS (EFIAPI *GET_MEMORY_MAP_REAL)(size_t*,void*,uint64_t*,size_t*,uint32_t*);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    ST=st; BS=st->BootServices;
    void *file=0; size_t file_size=0;
    EFI_STATUS s=open_kernel(image,&file,&file_size); if(EFI_ERROR(s)) return s;
    uint64_t entry=0,base=0,end=0;
    s=load_elf(file,file_size,&entry,&base,&end); BS->FreePool(file); if(EFI_ERROR(s)) return s;

    /* Allocate the handoff after kernel loading, then take the final map. */
    rixuri_boot_info_t *boot=0; s=BS->AllocatePool(EFI_LOADER_DATA,sizeof(*boot),(void**)&boot); if(EFI_ERROR(s)) return s;
    memset8(boot,0,sizeof(*boot)); boot->magic=RIXURI_BOOT_MAGIC; boot->rsdp=find_rsdp(); boot->kernel_phys_base=base; boot->kernel_phys_end=end;

    GET_MEMORY_MAP_REAL gm=(GET_MEMORY_MAP_REAL)(uintptr_t)BS->GetMemoryMap;
    size_t map_size=0,desc_size=0; uint64_t map_key=0; uint32_t version=0;
    s=gm(&map_size,0,&map_key,&desc_size,&version); if(s!=EFI_BUFFER_TOO_SMALL) return s;
    map_size += desc_size*8;
    void *map=0; s=BS->AllocatePool(EFI_LOADER_DATA,map_size,&map); if(EFI_ERROR(s)) return s;
    s=gm(&map_size,map,&map_key,&desc_size,&version); if(EFI_ERROR(s)) return s;
    boot->memory_map=(uint64_t)(uintptr_t)map; boot->memory_map_size=map_size; boot->memory_descriptor_size=desc_size; boot->memory_descriptor_version=version;

    s=BS->ExitBootServices(image,map_key); if(EFI_ERROR(s)) {
        /* One retry is required when an allocation/event changes the map. */
        map_size=0; s=gm(&map_size,0,&map_key,&desc_size,&version); if(s!=EFI_BUFFER_TOO_SMALL)return s;
        map_size += desc_size*8; s=gm(&map_size,map,&map_key,&desc_size,&version); if(EFI_ERROR(s))return s;
        boot->memory_map_size=map_size; s=BS->ExitBootServices(image,map_key); if(EFI_ERROR(s))return s;
    }

    void (*kernel_entry)(rixuri_boot_info_t *) SYSVABI = (void (*)(rixuri_boot_info_t *)) (uintptr_t)entry;
    kernel_entry(boot);
    for(;;) __asm__ volatile("cli; hlt");
}
