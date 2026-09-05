/* RixuriOS UEFI loader: load a validated ELF64 kernel and transfer a
 * versioned boot handoff after ExitBootServices(). */
#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))
#define SYSVABI __attribute__((sysv_abi))

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef struct { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } EFI_GUID;
typedef struct { uint64_t Signature; uint32_t Revision, HeaderSize, CRC32, Reserved; } EFI_TABLE_HEADER;

#define EFI_SUCCESS 0ULL
#define EFI_LOAD_ERROR 1ULL
#define EFI_INVALID_PARAMETER 2ULL
#define EFI_BUFFER_TOO_SMALL 5ULL
#define EFI_NOT_FOUND 14ULL
#define EFI_ERROR(s) ((s) != EFI_SUCCESS)
#define EFI_PAGE_SIZE 4096ULL
#define EFI_ALLOCATE_ADDRESS 2u
#define EFI_LOADER_DATA 2u
#define EFI_FILE_MODE_READ 1ULL
#define PT_LOAD 1u
#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define EM_X86_64 0x3Eu
#define RIXURI_BOOT_MAGIC 0x52584955ULL
#define RIXURI_BOOT_VERSION 1u

static int add_overflow(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) return 1;
    *out = a + b;
    return 0;
}
static int mul_overflow(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0 && b > UINT64_MAX / a) return 1;
    *out = a * b;
    return 0;
}
static int align_up(uint64_t value, uint64_t *out) {
    if (value > UINT64_MAX - (EFI_PAGE_SIZE - 1ULL)) return 1;
    *out = (value + EFI_PAGE_SIZE - 1ULL) & ~(EFI_PAGE_SIZE - 1ULL);
    return 0;
}
static uint64_t align_down(uint64_t value) { return value & ~(EFI_PAGE_SIZE - 1ULL); }
static void memzero(void *dst, size_t n) { uint8_t *p = dst; while (n--) *p++ = 0; }
static void memcpy8(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src; while (n--) *d++ = *s++;
}
static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) return 0;
    for (unsigned i = 0; i < 8; ++i) if (a->Data4[i] != b->Data4[i]) return 0;
    return 1;
}

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(uint32_t, uint32_t, size_t, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, size_t);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(size_t *, void *, size_t *, size_t *, uint32_t *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(uint32_t, size_t, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, size_t);

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL; void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages; EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap; EFI_ALLOCATE_POOL AllocatePool; EFI_FREE_POOL FreePool;
    void *CreateEvent; void *SetTimer; void *WaitForEvent; void *SignalEvent;
    void *CloseEvent; void *CheckEvent; void *InstallProtocolInterface; void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface; EFI_HANDLE_PROTOCOL HandleProtocol; void *Reserved;
    void *RegisterProtocolNotify; void *LocateHandle; void *LocateDevicePath; void *InstallConfigurationTable;
    void *LoadImage; void *StartImage; void *Exit; void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices; void *GetNextMonotonicCount; void *Stall;
    void *SetWatchdogTimer; void *ConnectController; void *DisconnectController;
    void *OpenProtocol; void *CloseProtocol; void *OpenProtocolInformation;
    void *ProtocolsPerHandle; void *LocateHandleBuffer; EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces; void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32; void *CopyMem; void *SetMem; void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct EFI_CONFIGURATION_TABLE { EFI_GUID VendorGuid; void *VendorTable; } EFI_CONFIGURATION_TABLE;
typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr; CHAR16 *FirmwareVendor; uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle; void *ConIn; EFI_HANDLE ConsoleOutHandle; void *ConOut;
    EFI_HANDLE StandardErrorHandle; void *StdErr; void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices; size_t NumberOfTableEntries; EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t Revision; EFI_HANDLE ParentHandle; EFI_SYSTEM_TABLE *SystemTable; EFI_HANDLE DeviceHandle;
    void *FilePath; void *Reserved; uint32_t LoadOptionsSize; void *LoadOptions;
    void *ImageBase; uint64_t ImageSize; uint32_t ImageCodeType, ImageDataType; void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, uint64_t, uint64_t);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, size_t *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *, EFI_GUID *, size_t *, void *);
struct EFI_FILE_PROTOCOL {
    uint64_t Revision; EFI_FILE_OPEN Open; EFI_FILE_CLOSE Close; void *Delete; EFI_FILE_READ Read;
    void *Write; void *GetPosition; void *SetPosition; EFI_FILE_GET_INFO GetInfo; void *SetInfo; void *Flush;
};
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision; EFI_STATUS (EFIAPI *OpenVolume)(void *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint32_t type, pad; uint64_t phys, virt, pages, attr;
} rixuri_efi_memory_desc_t;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint32_t reserved0;
    uint64_t rsdp;
    uint64_t kernel_phys_base;
    uint64_t kernel_phys_end;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_format;
} rixuri_boot_info_t;

typedef struct { uint32_t Version, Red, Blue, Green, BltOnly; } EFI_GRAPHICS_PIXEL_FORMAT;
typedef struct { uint32_t RedMask, GreenMask, BlueMask, ReservedMask; } EFI_PIXEL_BITMASK;
typedef struct {
    uint32_t Version, HorizontalResolution, VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat; EFI_PIXEL_BITMASK PixelInformation;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct {
    uint32_t MaxMode, Mode; EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info; size_t SizeOfInfo;
    uint64_t FrameBufferBase, FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct { void *QueryMode; void *SetMode; void *Blt; EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode; } EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff, flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf64_hdr_t;
typedef struct { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; } elf64_phdr_t;

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;
static EFI_GUID g_fs_guid = {0x0964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID g_image_guid = {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID g_acpi_guid = {0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
static EFI_GUID g_gop_guid = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static EFI_GUID g_file_info_guid = {0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

static EFI_STATUS load_kernel_file(EFI_HANDLE image, void **out, size_t *out_size) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_STATUS s = g_bs->HandleProtocol(image, &g_image_guid, (void **)&loaded);
    if (EFI_ERROR(s) || !loaded) return s ? s : EFI_LOAD_ERROR;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    s = g_bs->HandleProtocol(loaded->DeviceHandle, &g_fs_guid, (void **)&fs);
    if (EFI_ERROR(s) || !fs) return s ? s : EFI_LOAD_ERROR;
    EFI_FILE_PROTOCOL *root = NULL, *file = NULL;
    s = fs->OpenVolume(fs, &root); if (EFI_ERROR(s)) return s;
    CHAR16 name[] = {'k','e','r','n','e','l','.','e','l','f',0};
    s = root->Open(root, &file, name, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s)) { root->Close(root); return s; }
    size_t info_size = 0;
    s = file->GetInfo(file, &g_file_info_guid, &info_size, NULL);
    if (s != EFI_BUFFER_TOO_SMALL || info_size < 24) { file->Close(file); root->Close(root); return s; }
    void *info = NULL;
    s = g_bs->AllocatePool(EFI_LOADER_DATA, info_size, &info);
    if (EFI_ERROR(s)) { file->Close(file); root->Close(root); return s; }
    s = file->GetInfo(file, &g_file_info_guid, &info_size, info);
    if (EFI_ERROR(s)) { g_bs->FreePool(info); file->Close(file); root->Close(root); return s; }
    uint64_t file_size = *(uint64_t *)((uint8_t *)info + 8);
    g_bs->FreePool(info);
    if (file_size == 0 || file_size > (uint64_t)(size_t)-1) { file->Close(file); root->Close(root); return EFI_LOAD_ERROR; }
    void *data = NULL;
    s = g_bs->AllocatePool(EFI_LOADER_DATA, (size_t)file_size, &data);
    if (EFI_ERROR(s)) { file->Close(file); root->Close(root); return s; }
    size_t read = (size_t)file_size;
    s = file->Read(file, &read, data); file->Close(file); root->Close(root);
    if (EFI_ERROR(s) || read != (size_t)file_size) { g_bs->FreePool(data); return EFI_LOAD_ERROR; }
    *out = data; *out_size = read; return EFI_SUCCESS;
}

static EFI_STATUS load_elf(const void *data, size_t size, uint64_t *entry, uint64_t *base, uint64_t *end) {
    if (size < sizeof(elf64_hdr_t)) return EFI_LOAD_ERROR;
    const elf64_hdr_t *h = data;
    if (h->ident[0] != 0x7f || h->ident[1] != 'E' || h->ident[2] != 'L' || h->ident[3] != 'F' ||
        h->ident[4] != ELFCLASS64 || h->ident[5] != ELFDATA2LSB || h->ident[6] != 1 ||
        h->machine != EM_X86_64 || h->version != 1 || h->ehsize != sizeof(elf64_hdr_t) ||
        h->phentsize != sizeof(elf64_phdr_t) || h->phnum == 0) return EFI_LOAD_ERROR;
    uint64_t ph_bytes;
    if (mul_overflow(h->phnum, sizeof(elf64_phdr_t), &ph_bytes) || h->phoff > size || ph_bytes > size - h->phoff) return EFI_LOAD_ERROR;
    uint64_t lo = UINT64_MAX, hi = 0;
    for (uint16_t i = 0; i < h->phnum; ++i) {
        const elf64_phdr_t *p = (const elf64_phdr_t *)((const uint8_t *)data + h->phoff + (uint64_t)i * sizeof(*p));
        if (p->type != PT_LOAD) continue;
        if (p->filesz > p->memsz || p->offset > size || p->filesz > size - p->offset) return EFI_LOAD_ERROR;
        uint64_t mem_end, seg_end;
        if (add_overflow(p->p_paddr, p->memsz, &mem_end) || align_up(mem_end, &seg_end)) return EFI_LOAD_ERROR;
        if (p->p_paddr < lo) lo = align_down(p->p_paddr);
        if (seg_end > hi) hi = seg_end;
        if (p->align > 1 && (p->align & (p->align - 1ULL)) != 0) return EFI_LOAD_ERROR;
        if (p->align > 1 && ((p->p_vaddr - p->p_offset) & (p->align - 1ULL)) != 0) return EFI_LOAD_ERROR;
    }
    if (lo == UINT64_MAX || hi <= lo || h->entry < lo || h->entry >= hi) return EFI_LOAD_ERROR;
    uint64_t span = hi - lo;
    size_t pages = (size_t)(span / EFI_PAGE_SIZE);
    if (pages == 0 || (uint64_t)pages != span / EFI_PAGE_SIZE) return EFI_LOAD_ERROR;
    EFI_PHYSICAL_ADDRESS addr = lo;
    EFI_STATUS s = g_bs->AllocatePages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, pages, &addr);
    if (EFI_ERROR(s) || addr != lo) return EFI_ERROR(s) ? s : EFI_LOAD_ERROR;
    memzero((void *)(uintptr_t)lo, (size_t)span);
    for (uint16_t i = 0; i < h->phnum; ++i) {
        const elf64_phdr_t *p = (const elf64_phdr_t *)((const uint8_t *)data + h->phoff + (uint64_t)i * sizeof(*p));
        if (p->type == PT_LOAD && p->filesz) memcpy8((void *)(uintptr_t)p->p_paddr, (const uint8_t *)data + p->offset, (size_t)p->filesz);
    }
    *entry = h->entry; *base = lo; *end = hi; return EFI_SUCCESS;
}

static uint64_t find_rsdp(void) {
    for (size_t i = 0; i < g_st->NumberOfTableEntries; ++i)
        if (guid_equal(&g_st->ConfigurationTable[i].VendorGuid, &g_acpi_guid)) return (uint64_t)(uintptr_t)g_st->ConfigurationTable[i].VendorTable;
    return 0;
}

static void capture_gop(rixuri_boot_info_t *boot) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    if (EFI_ERROR(g_bs->LocateProtocol(&g_gop_guid, NULL, (void **)&gop)) || !gop || !gop->Mode || !gop->Mode->Info) return;
    boot->framebuffer_base = gop->Mode->FrameBufferBase;
    boot->framebuffer_size = gop->Mode->FrameBufferSize;
    boot->framebuffer_width = gop->Mode->Info->HorizontalResolution;
    boot->framebuffer_height = gop->Mode->Info->VerticalResolution;
    boot->framebuffer_pitch = gop->Mode->Info->HorizontalResolution * 4u;
    boot->framebuffer_format = gop->Mode->Info->PixelFormat.Version;
}

static EFI_STATUS capture_memory_map(void **map, size_t *capacity, size_t *map_size, size_t *descriptor_size, uint32_t *version, size_t *map_key) {
    size_t need = 0, ds = 0, key = 0; uint32_t ver = 0;
    EFI_STATUS s = g_bs->GetMemoryMap(&need, NULL, &key, &ds, &ver);
    if (s != EFI_BUFFER_TOO_SMALL || ds == 0) return s ? s : EFI_LOAD_ERROR;
    if (need > (size_t)-1 - ds * 16u) return EFI_LOAD_ERROR;
    size_t cap = need + ds * 16u;
    void *buf = NULL;
    s = g_bs->AllocatePool(EFI_LOADER_DATA, cap, &buf);
    if (EFI_ERROR(s)) return s;
    size_t actual = cap;
    s = g_bs->GetMemoryMap(&actual, buf, &key, &ds, &ver);
    if (EFI_ERROR(s)) { g_bs->FreePool(buf); return s; }
    *map = buf; *capacity = cap; *map_size = actual; *descriptor_size = ds; *version = ver; *map_key = key;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    g_st = st; g_bs = st->BootServices;
    if (!g_bs || !g_bs->AllocatePages || !g_bs->AllocatePool || !g_bs->GetMemoryMap || !g_bs->ExitBootServices) return EFI_INVALID_PARAMETER;
    void *file = NULL; size_t file_size = 0;
    EFI_STATUS s = load_kernel_file(image, &file, &file_size); if (EFI_ERROR(s)) return s;
    uint64_t entry = 0, base = 0, end = 0;
    s = load_elf(file, file_size, &entry, &base, &end);
    g_bs->FreePool(file); if (EFI_ERROR(s)) return s;

    rixuri_boot_info_t *boot = NULL;
    s = g_bs->AllocatePool(EFI_LOADER_DATA, sizeof(*boot), (void **)&boot); if (EFI_ERROR(s)) return s;
    memzero(boot, sizeof(*boot));
    boot->magic = RIXURI_BOOT_MAGIC; boot->version = RIXURI_BOOT_VERSION; boot->size = sizeof(*boot);
    boot->rsdp = find_rsdp(); boot->kernel_phys_base = base; boot->kernel_phys_end = end;
    capture_gop(boot);

    void *map = NULL; size_t map_capacity = 0, map_size = 0, desc_size = 0, map_key = 0; uint32_t desc_version = 0;
    s = capture_memory_map(&map, &map_capacity, &map_size, &desc_size, &desc_version, &map_key); if (EFI_ERROR(s)) return s;
    boot->memory_map = (uint64_t)(uintptr_t)map; boot->memory_map_size = map_size;
    boot->memory_descriptor_size = desc_size; boot->memory_descriptor_version = desc_version;

    s = g_bs->ExitBootServices(image, map_key);
    if (EFI_ERROR(s)) {
        /* ExitBootServices may reject a stale map key. Refresh the map in the
         * already allocated buffer: no allocation is permitted on this path. */
        size_t retry_size = map_capacity; size_t retry_key = 0;
        s = g_bs->GetMemoryMap(&retry_size, map, &retry_key, &desc_size, &desc_version);
        if (EFI_ERROR(s)) return s;
        if (retry_size > map_capacity) return EFI_LOAD_ERROR;
        boot->memory_map_size = retry_size; boot->memory_descriptor_size = desc_size; boot->memory_descriptor_version = desc_version;
        s = g_bs->ExitBootServices(image, retry_key);
        if (EFI_ERROR(s)) return s;
    }

    ((SYSVABI void (*)(const rixuri_boot_info_t *))(uintptr_t)entry)(boot);
    return EFI_LOAD_ERROR;
}
