# RixuriOS Gerçek Donanım Hata Denetimi

**Tarih:** 2026-09-10  
**Hedef:** ASUS B650 / AMD Ryzen 7 7700  
**Kapsam:** UEFI, PMM/VMM, CR3, exception dönüşü, scheduler, SMP/APIC, PCI/DMA, NVMe, xHCI, RTL8125, GOP ve test altyapısı.

## Sonuç

RixuriOS’un QEMU’da çalışıp gerçek donanımda CR3 debug çıktısından sonra durmasını açıklayabilecek birden fazla bağımsız risk bulundu. En kritik sonuç, önceki `CR3` bulgusunun tek başına yeterli olmadığıdır. Kaynakta **P0 seviyesinde üç ayrı risk** vardır:

1. **Hata kodlu exception dönüşü bozuk olabilir.** `ISR_ERR` yalnızca sentetik vector’ı atlıyor; CPU’nun stack’e koyduğu error code da atılmadığı için `iretq` yanlış RIP okur. Bu, gerçek makinedeki ilk page fault’u triple fault veya sessiz reset gibi gösterebilir.
2. **4-level paging varsayımı LA57 ile korunmuyor.** `CR4.LA57` temizlenmeden 4-level PML4 kökü kullanılıyor. Firmware bu biti açık bırakırsa ilk gerçek CR3 geçişi bozulabilir.
3. **SMP AP trampoline’ı debug amacıyla sonsuz `hlt` döngüsüne giriyor.** AP, `ap_entry` fonksiyonuna normal yoldan geçmiyor. Bu bulgu kesin kod hatasıdır; ancak normal jump düzeltmesi ilk denemede QEMU SMP akışında yeni bir takılma ürettiği için değişiklik geri alındı ve ayrı bir kontrollü düzeltme gerektiriyor.

Bunlara ek olarak GOP framebuffer’ın karakter başına senkron çizilmesi, page-table doğrulamasının yüzeysel olması, MMIO cache türünün belirtilmemesi, DMA adreslerinin sanal pointer olarak kullanılması, IOMMU desteğinin bulunmaması ve QEMU test kapsamının fiziksel platformu temsil etmemesi önemli P1 riskleridir.

> **QEMU PASS, gerçek donanım PASS değildir.** QEMU testleri bu raporda yalnızca emülasyon kapsamı olarak değerlendirildi.

## Öncelik tablosu

| Öncelik | Bulgu | Kanıt | Etki |
|---|---|---|---|
| P0 | Error-code ISR cleanup hatası | `kernel/arch/x86_64/interrupts.S:27-37` | #PF/#GP/#DF sonrası yanlış `iretq`, triple fault veya reset |
| P0 | LA57 normalize edilmiyor | `kernel/mm/vmm.c:17-23,35` | 5-level firmware state üzerinde 4-level CR3 kullanımı |
| P0 | AP trampoline debug parkı | `kernel/arch/x86_64/smp_trampoline.S:132-152` | AP hiçbir zaman `ONLINE` olmaz; SMP boot yanlış raporlanır |
| P1 | CR3 doğrulaması recursive değil | `kernel/mm/vmm.c:49-110`, `kernel/process/process.c:322-371` | Bozuk alt tablo veya reserved bit gerçek CPU’da fault üretir |
| P1 | CR3 sonrası güvenli failure path yok | `kernel/process/process.c:498-546` | Fault handler’a ulaşmadan görünür donma |
| P1 | GOP format/stride/bounds doğrulaması eksik | `boot/efi_main.c:55-57`, `kernel/tty/tty.c:385-398` | Yanlış VRAM yazımı, yavaşlık veya framebuffer dışına taşma |
| P1 | Framebuffer mirror her karakterde çalışıyor | `kernel/serial.c:27-33`, `kernel/tty/tty.c:414-426` | Gerçek GOP’ta debug çıktısı donma gibi görünebilir |
| P1 | MMIO cache attribute yok | `kernel/mm/vmm.c:22,41`, xHCI/NVMe mappingleri | LAPIC/IOAPIC/NVMe/xHCI register erişimi fizikselde güvenilmez olabilir |
| P1 | DMA sanal adresi fiziksel adres sanılıyor | `kernel/usb/xhci.c:864-900,1063-1100` | Gerçek xHCI yanlış buffer’a erişir veya durur |
| P1 | IOMMU/AMD-Vi uygulanmamış | `kernel/pci/iommu.c:3-18` | BIOS’ta IOMMU açıksa tüm DMA aygıtları translation fault alabilir |
| P1 | NVMe PRP yalnızca iki sayfayı destekliyor | `kernel/storage/nvme.c:50-65` | Büyük filesystem işlemleri gerçek SSD’de başarısız olur |
| P1 | FPU/XMM task state sözleşmesi yok | `Makefile`, scheduler/context switch | SSE kullanan user/kernel kodu context switch sonrası bozulabilir |
| P1 | Test harness pipefail hatası | `scripts/run-all-tests.sh:2,21` | Başarısız test yanlışlıkla PASS raporlanabilir |
| P2 | PIT scheduler tick iki kez çağrılıyor | `kernel/arch/x86_64/pit.c:8-9`, `irq.c:20-30` | Timer ölçeği iki katına çıkar |
| P2 | PCI enumeration MCFG/ECAM kullanmıyor | `kernel/arch/x86_64/acpi.c`, `kernel/pci/pci.c` | Fiziksel PCI cihazları eksik veya yanlış keşfedilebilir |
| P2 | x2APIC yolu yok | `kernel/arch/x86_64/apic.c`, `smp.c` | BIOS x2APIC açıkken AP başlatma başarısız olabilir |
| P2 | INIT/SIPI gecikmeleri sabit pause sayacı | `kernel/arch/x86_64/smp.c:11-19,309-333` | QEMU ile Ryzen timing davranışı ayrışır |
| P2 | MSI-X hedefi sabit ve table mapping dar | `kernel/pci/msix.c:24-46` | Çok çekirdekli gerçek cihazlarda interrupt kaybı |
| P2 | Linker executable-stack warning | `kernel/tty/font_psf.o` | Build hijyeni ve gelecekteki toolchain uyumluluğu zayıf |

## Uygulanan düzeltmeler

Bu audit sırasında yüksek güvenli ve lokal değişiklikler uygulandı:

- `ISR_ERR` dönüşü `addq $8` yerine `addq $16` yapıldı. Sentetik vector ve CPU error code birlikte temizleniyor.
- 4-level paging kullanıldığı için `CR4.LA57` açıkça temizleniyor.
- SMP trampoline fallback mapping’inde executable trampoline sayfasına `NX` verilmesi kaldırıldı.
- PIT’in `scheduler_tick()` fonksiyonu iki kez çağırması düzeltildi; tick sahipliği `pit_irq()` içinde bırakıldı.
- `scripts/run-all-tests.sh` artık `set -euo pipefail` kullanıyor.
- SMP testinin trampoline sayfasının executable olması gerektiğini doğrulayan assertion güncellendi.

SMP trampoline’ını `ap_entry` fonksiyonuna geçirmek için yapılan ilk değişiklik QEMU 2-vCPU testinde beklenmeyen bir takılma üretti. Bu değişiklik geri alındı. Kaynak hâlâ AP’nin debug `hlt` döngüsünü içeriyor; bu nedenle SMP için ayrı bir düzeltme ve daha küçük marker’larla yeni test gereklidir.

## Doğrulama sonucu

Aşağıdaki doğrulamalar başarılıdır:

```text
make clean CROSS=
make all CROSS=
make test CROSS=
```

Host test sonucu `rc=0` oldu. QEMU 1-vCPU user boot daha önce başarılıydı. QEMU 2-vCPU koşusu AP trampoline debug parkı nedeniyle `SMP: AP start timeout` davranışını gösteriyordu; normal AP jump denemesi geri alındı.

Build hâlâ şu uyarıyı veriyor:

```text
kernel/tty/font_psf.o: missing .note.GNU-stack section implies executable stack
```

Bu uyarı mevcut CR3 donmasının doğrudan nedeni değildir, ancak düzeltilmelidir.

## Donanımda sonraki test sırası

İlk fiziksel testte ayrıntılı framebuffer logu kapatılmalı ve yalnızca COM1/POST marker kullanılmalıdır. Her marker tek byte veya kısa sabit dizi olmalıdır:

```text
A = CR3 öncesi
B = CR3 yazısı tamamlandı
C = yeni CR3 altında ilk instruction
D = yeni CR3 altında ilk stack yazımı
E = user iretq öncesi
F = exception handler girişi
```

Aynı image ile aşağıdaki üç koşu ayrı ayrı alınmalıdır:

1. Framebuffer mirror açık, ayrıntılı debug açık.
2. Framebuffer mirror kapalı, yalnızca seri marker açık.
3. User CR3 geçişi kapalı, yalnızca kernel boot ve SMP açık.

Her koşuda şu değerler kaydedilmelidir: `CR0`, `CR3`, `CR4`, `EFER`, `CR2`, `RSP`, `RSP0`, `IDTR`, `GDTR`, `TR`, CPUID `MAXPHYADDR`, `LA57`, UEFI memory-map özeti, GOP base/size/pitch/format ve firmware IOMMU durumu.

## Düzeltilmesi gereken sonraki işler

İlk sırada tam recursive page-table validator yazılmalıdır. Validator tüm reachable PML4/PDPT/PD/PT girdilerini cycle-safe biçimde dolaşmalı; `MAXPHYADDR`, reserved bitler, `PS` hizası, NX/NXE uyumu, canonical adresler ve kernel-critical VA’ları kontrol etmelidir.

İkinci sırada CR3 geçişi minimal assembly trampoline’a taşınmalıdır. Trampoline yalnızca önceden doğrulanmış code, stack, IDT ve fault-record alanlarını kullanmalı; CR3 sonrası ilk marker C kodu çağırmadan yazılmalıdır.

Üçüncü sırada SMP AP trampoline normal akıştan ayrılmalıdır. Fetch probe tamamlandıktan sonra `ap_entry` çağrılmalı; hata durumunda park döngüsü yalnızca açık diagnostic mode’da kullanılmalıdır. AP başlatma tamamlanmadan scheduler çok çekirdekli moda geçmemelidir.

Dördüncü sırada DMA için merkezi bir API uygulanmalıdır. xHCI, NVMe ve RTL8125 yalnızca fiziksel adresi doğrulanmış, page-pinned ve cache/barrier sözleşmesi tanımlı buffer’ları kullanmalıdır. IOMMU açık ve kapalı iki BIOS koşusu ayrı test edilmelidir.

## Sınırlar

Bu rapor kaynak kodu, host testleri ve QEMU çıktıları üzerinden hazırlanmıştır. ASUS B650/Ryzen 7 7700 üzerinde doğrudan çalıştırma yapılmadı. Bu nedenle P0/P1 bulgularının çoğu güçlü kod kanıtına dayanır, fakat kullanıcının özel donanımındaki tek kök neden olarak yalnızca seri/POST marker’lı fiziksel tekrar sonrasında ilan edilmelidir.

## References

[1]: https://github.com/jepdnwp/RixuriOS "RixuriOS source repository"
[2]: https://wiki.osdev.org/Interrupt_Service_Routines "OSDev Interrupt Service Routines"
[3]: https://wiki.osdev.org/Paging "OSDev x86 Paging"
[4]: https://uefi.org/specs/UEFI/2.10/ "UEFI Specification 2.10"
[5]: https://www.amd.com/system/files/TechDocs/24593.pdf "AMD64 Architecture Programmer’s Manual Volume 2"
[6]: https://www.intel.com/content/www/us/en/docs/developer-manuals/64-ia-32-architectures-software-developer-vol-3-part-1-manual.html "Intel 64 and IA-32 Architectures Software Developer’s Manual Volume 3"


## İkinci aşama uygulanan düzeltmeler

Devam denetiminde aşağıdaki gerçek kod hataları da düzeltildi:

- xHCI Setup Stage TRB artık `bmRequestType`, `bRequest`, `wValue`, `wIndex` ve `wLength` alanlarının tamamını 64-bit parameter alanına paketliyor. Önceki kod `wIndex` ve `wLength` alanlarını TRB status alanına koyuyordu.
- xHCI control ve endpoint transferlerinde çağıranın sanal buffer adresi doğrudan DMA adresi olarak kullanılmıyor; mevcut address-space üzerinden `vmm_translate()` ile fiziksel adres elde ediliyor.
- LAPIC, IOAPIC, xHCI ve NVMe MMIO mappinglerinde PWT/PCD flagleri eklenerek uncached erişim politikası başlatıldı.
- IOAPIC register selector tipi `uint8_t` yerine `uint32_t` yapıldı. Çok sayıda redirection entry olduğunda selector wrap-around riski kaldırıldı.

Bu değişiklikler için tekrar:

```text
make clean CROSS=
make all CROSS=
make test CROSS=
```

çalıştırıldı ve sonuç `rc=0` oldu.

### Kalan sınırlama

`vmm_translate()` ile tek fiziksel başlangıç adresi elde etmek, çok sayfalı ve fiziksel olarak parçalı DMA buffer’ları hâlâ tam olarak çözmez. xHCI ve NVMe için sonraki adım gerçek scatter/gather veya IOMMU DMA API’sidir. Şu anki değişiklik yanlış sanal adres kullanımını azaltır, fakat buffer’ın tüm sayfalarının fiziksel olarak ardışık olduğunu kanıtlamaz.


## SMP devam sonucu

SMP tasarım belgesinde AP’lerin Phase B’de `ap_entry()` sonrasında park etmesi bekleniyor. Bu nedenle `hlt` döngüsünün kendisi tek başına hata değil; asıl eksik, trampoline’ın fetch probe sonrasında `ap_entry` adresine normal geçiş yapmaması ve AP’nin `ONLINE` işaretlenememesidir.

Ayrıca fetch probe içinde ayrı bir teşhis hatası bulundu. Entry pointer `%rax` içinde tutulurken UART marker için `%ah` değiştiriliyor, sonra `(%rax)` okunuyordu. Bu nedenle Y probe gerçek entry adresinin ilk 8 byte’ını değil, bozulmuş bir adresi okuyabiliyordu. Pointer `%r13` içinde korunarak probe doğru adresten okuyacak şekilde düzeltildi. AP’nin normal C entry’ye jump etmesi ise QEMU 2-vCPU testinde beklenmeyen bir takılma ürettiği için bu aşamada uygulanmadı.

Bu aşamanın sonucu şudur: QEMU’da AP’nin `R/P/L/G/T/Y` marker’ları görülüyor, fakat `online=2` kabul kapısı geçilmiyor. Normal jump için bir sonraki kontrollü çözüm, AP C entry’sini değil önce trampoline içinde ayrı bir raw marker ve minimal park stub’ını test etmek; ardından AP’nin kendi IDT/GDT/TSS ve per-CPU stack sözleşmesini kurmaktır.


## AP direct-entry deneyi

Fetch probe ve `hex64` çağrısı bypass edilerek T marker’ından sonra doğrudan `jmp *%r13` denendi. QEMU 2-vCPU logu `...T...J` sonrasında ilerlemedi; BSP ikinci SIPI/teslimat beklemesinde kaldı ve `SMP: AP poll` görülmedi. Bu sonuç, AP’nin doğrudan C entry’ye geçişinde henüz güvenli bir C/exception ortamı bulunmadığını gösterir; fakat tek başına fault adresini kanıtlamaz.

Deney geri alındı. Stabil debug parkı korundu. Entry pointer’ın `%ah` UART marker tarafından bozulmasını düzelten teşhis değişikliği korunuyor. Bir sonraki doğru çözüm, C `ap_entry()` öncesinde AP’ye IDTR yükleyen, minimal fault marker yazan ve yalnızca `ONLINE` state’i yayınlayan assembly wrapper’dır. Bundan sonra C park kodu çağrılmalıdır.


## SMP Phase B tamamlandı

AP’nin doğrudan C `ap_entry()` fonksiyonuna geçişi, AP’nin kendi exception/TSS ortamı kurulmadan fault üretme riski taşıdığı için kullanılmadı. Bunun yerine güvenli minimal assembly wrapper uygulandı:

1. AP long mode’a geçer.
2. BSP GDTR ve IDTR snapshot’ı yüklenir.
3. Trampoline’a yazılan ilgili `smp_cpu_t.state` adresi okunur.
4. Assembly seviyesinde `SMP_CPU_ONLINE` değeri yazılır.
5. `mfence` ile BSP poll döngüsüne görünürlük sağlanır.
6. AP debug parkına geçer.

QEMU kabul sonuçları:

```text
-smp 2: SMP: online=2
-smp 4: SMP: online=4
RIXURI:KERNEL_READY
```

Single-CPU user boot ve host testleri de korunmuştur. Bu çözüm Phase B AP discovery/start kabulünü tamamlar; Phase C per-CPU scheduler, TSS/RSP0 ve IPI/TLB shootdown hâlâ ayrı işlerdir. AP’ler henüz scheduler tarafından kullanılmıyor.


## Recursive CR3 validator tamamlandı

`vmm_validate_pml4()` artık yalnızca PML4 kökünü kontrol etmiyor. Tüm reachable page-table ağaçlarını cycle-safe biçimde tarıyor. Kontroller şunları kapsıyor:

- 4 KiB hizası ve fiziksel adres sınırı.
- Present girdilerde izin verilen bitler.
- Alt tablo pointer’larının geçerli fiziksel aralıkta olması.
- 1 GiB PDPTE huge-page hizası.
- 2 MiB PDE huge-page hizası.
- Paylaşılan kernel tabloları için visited set.
- Bozuk veya döngüsel tablo ağacında fail-closed dönüş.

Freestanding kernel buildinde kullanılan implicit `memset` bağımlılığı da kaldırıldı.

Son doğrulama:

```text
make clean CROSS=
make all CROSS=
make test CROSS=
make image CROSS=
```

Host testleri `rc=0` verdi. QEMU 4-vCPU sonucu:

```text
SMP: online=4
RIXURI:KERNEL_READY
```


## DMA devam düzeltmesi

xHCI transferleri için `xhci_dma_linear_pa()` eklendi. Control ve endpoint transferlerinde artık tüm buffer sayfaları yürünüyor. Her sanal sayfanın fiziksel adresi, ilk sayfaya göre beklenen ardışık fiziksel adrese eşleşmiyorsa DMA isteği reddediliyor. Böylece parçalı sanal buffer sessizce tek fiziksel aralık gibi xHCI’ye verilmiyor.

Bu çözüm geçici olarak güvenli fail-closed davranıştır. Gerçek çözüm, scatter/gather veya IOMMU tabanlı DMA API’sidir.

Son doğrulama:

```text
make clean CROSS=
make all CROSS=
make test CROSS=
make image CROSS=
```

Host testleri başarılıdır. Tek CPU QEMU bootunda `RIXURI:KERNEL_READY` görüldü.


## FPU/SSE inceleme sonucu

Kernel disassemblyinde `smp_build_map()` içinde `pxor` üretildiği görüldü. Mevcut scheduler/context-switch FPU state saklamadığı için bu gerçek bir ABI riskidir: kernel veya interrupt yolu XMM state’i kullanıcı sürecine dönerken bozabilir.

Tüm hedefe `-mno-sse` ekleme denemesi user libc içindeki SysV ABI gerektiren `double difftime()` fonksiyonu nedeniyle derlenmedi (`SSE register return with SSE disabled`) ve geri alındı. Bu nedenle FPU konusu kapatılmış sayılmıyor. Doğru sonraki çözüm kernel ve user CFLAGS’lerini ayırmak, kernel’i `-mno-sse -mno-sse2 -mno-mmx` ile derlemek veya tam XSAVE/FXSAVE per-task state desteği eklemektir.

Deneme geri alındı; final build ve QEMU 4-vCPU regresyonu tekrar başarılıdır:

```text
SMP: online=4
RIXURI:KERNEL_READY
```


## FPU/SSE riski kapatıldı

Kernel ve user derleme bayrakları ayrıldı. Kernel kaynakları artık şu ek bayraklarla derleniyor:

```text
-mno-mmx -mno-sse -mno-sse2 -msoft-float
```

User programları SysV ABI uyumluluğu için önceki CFLAGS ile derlenmeye devam ediyor; böylece `double difftime()` gibi SSE register return kullanan user libc kodu bozulmuyor.

Kernel disassembly taramasında `xmm`, `ymm` veya `mm0..mm7` talimatı kalmadı. Host testleri başarılı ve QEMU 4-vCPU boot sonucu:

```text
SMP: online=4
RIXURI:KERNEL_READY
```

Bu çözüm kernel’in kullanıcı FPU/XMM state’ini değiştirmesini engeller. User süreçleri arasında gerçek FPU paylaşımı gerektiğinde ayrıca XSAVE/FXSAVE context yönetimi eklenmelidir.


## PCI/MMIO devam düzeltmeleri

Gerçek PCI cihazlarının QEMU’dan farklı cache davranışları için ek kontroller yapıldı:

- RTL8125 MMIO alanları artık PWT/PCD ile uncached eşleniyor.
- E1000 MMIO alanları uncached eşleniyor.
- MSI-X table page uncached eşleniyor.
- RTL8125 ve E1000 BAR mappinglerinde BAR’ın sayfa içi offseti hesaba katılıyor; önceki `size` tabanlı sayfa hesabı unaligned BAR’da son sayfayı eksik eşleyebilirdi.
- E1000 BAR boyut ve adres taşması kontrolleri eklendi.

IOMMU implementasyonu hâlâ bilerek unavailable dönüyor; gerçek DMAR/IVRS parsing ve translation domain olmadan IOMMU aktifmiş gibi davranılmıyor. Bu fail-closed davranış korunuyor.

Son doğrulama:

```text
make clean CROSS=
make all CROSS=
make test CROSS=
make image CROSS=
```

Host testleri ve QEMU 4-vCPU boot başarılı:

```text
SMP: online=4
RIXURI:KERNEL_READY
```


## Console ve GOP framebuffer düzeltmeleri

Kernel diagnostic logları artık framebuffer’a karakter karakter yazılmıyor; `kernel_log()` ve `kernel_log_n()` yalnızca COM1’e yönlendiriliyor. Normal user terminal çıktısı `serial_write()` üzerinden ekranda kalıyor. Böylece gerçek donanımda yavaş/uncached GOP framebuffer’ın debug logları nedeniyle sistemi donmuş gibi göstermesi engelleniyor.

`tty_set_framebuffer()` için fail-closed kontroller eklendi:

- `base`, width ve height sıfır kontrolü.
- `pitch >= width * 4` kontrolü.
- `pitch * (height - 1) + width * 4 <= framebuffer_size` kontrolü.
- uint64 overflow-safe hesap.
- yalnızca desteklenen pixel formatlarının kabulü.

Son doğrulama:

```text
make clean CROSS=
make all CROSS=
make test CROSS=
make image CROSS=
```

QEMU 4-vCPU sonucu:

```text
SMP: online=4
RIXURI:KERNEL_READY
```


## x2APIC desteği

Gerçek Ryzen sistemlerde firmware x2APIC modunu açık bırakabildiği için LAPIC erişimi genişletildi:

- x2APIC modu IA32_APIC_BASE üzerinden algılanıyor.
- LAPIC ID, EOI, SVR ve LINT0 MSR üzerinden erişiliyor.
- INIT/SIPI IPI’ları x2APIC ICR MSR üzerinden gönderiliyor.
- SMP başlangıcı artık x2APIC CPU kayıtlarını otomatik olarak atlamıyor.
- xAPIC MMIO yolu geriye dönük korunuyor.

QEMU mevcut xAPIC yolunda regresyonsuz çalıştı:

```text
SMP: online=4
RIXURI:KERNEL_READY
```

x2APIC gerçek donanımda ayrıca cold-boot doğrulaması gerektiriyor; özellikle firmware’in APIC modunu AP startup öncesi değiştirmediği doğrulanmalı.


## Çoklu IOAPIC ve GSI düzeltmesi

IOAPIC sürücüsü artık yalnızca `acpi_ioapic(0)` kaydına bağlı değil. MADT içindeki tüm IOAPIC kayıtları eşleniyor ve her biri kendi `gsi_base`/redirection count bilgisiyle tutuluyor. IRQ yönlendirme, ilgili GSI aralığını kapsayan IOAPIC’i seçiyor.

Ayrıca IOAPIC destination APIC ID API’si 8-bitten 32-bit’e çıkarıldı. x2APIC sistemlerde yüksek APIC ID’lerinin truncation ile yanlış CPU’ya yönlendirilmesi engellendi.

QEMU doğrulaması:

```text
ACPI CPUs: 4 IOAPICs: 1
SMP: online=4
IRQ: PIT routed through IOAPIC; interrupts enabled
RIXURI:KERNEL_READY
```


## x2APIC IPI teslimat kontrolü

x2APIC ICR yazımından sonra artık delivery-status bitinin temizlenmesi bounded polling ile bekleniyor. Önceki uygulama MSR yazısından hemen sonra başarılı dönüyordu; fiziksel APIC’in IPI kuyruğu hâlâ meşgulse sonraki INIT/SIPI gönderimi yarışabilirdi.

Timeout davranışı xAPIC yolu ile aynıdır: APIC teslimatı sınırlı sürede tamamlanmazsa hata döner ve AP startup güvenli biçimde başarısız sayılır.

QEMU regresyonu başarılıdır:

```text
SMP: online=4
IRQ: PIT routed through IOAPIC; interrupts enabled
RIXURI:KERNEL_READY
```


## ACPI CPU duplicate düzeltmesi

SMP map oluşturma artık APIC ID bazında deduplication yapıyor. Bazı firmware’lerin aynı CPU’yu legacy Local APIC ve x2APIC MADT entry’si olarak iki kez bildirmesi durumunda:

- tek bir CPU slotu tutuluyor,
- enabled bilgisi birleştiriliyor,
- x2APIC bilgisi korunuyor,
- aynı AP’ye iki kez INIT/SIPI gönderilmiyor.

QEMU regresyonu:

```text
ACPI CPUs: 4 IOAPICs: 1
SMP: online=4
RIXURI:KERNEL_READY
```


## Ring3 validator regresyonu düzeltildi

Tam regresyon sırasında gerçek bir validator hatası bulundu. Validator, kernel’in 128 GiB üst sınırının dışına erişilmeyen early identity huge-page leaflerini geçersiz sayıyordu. Ayrıca fiziksel adres 0’a başlayan legal 2 MiB identity mappingini table-pointer gibi değerlendiriyordu.

Düzeltme:

- Page-table pointer’ları hâlâ gerçek fiziksel adres sınırıyla doğrulanıyor.
- PDE/PDPTE huge-page leafleri yalnızca hizalama ve reserved-bit kurallarıyla doğrulanıyor.
- Erişilmeyen identity-map leaflerinin fiziksel RAM sınırını aşabilmesi reddedilmiyor.
- Physical address maskesi NX bitini adresin parçası yapmayacak şekilde ayrıştırıldı.

Önceki ring3 başarısızlığı bu nedenleydi. Güncel ring3 milestone testi başarılı:

```text
RING3: trampoline after CR3
RIXURI:USER_ENTER
RIXURI:SYSCALL_OK
RIXURI: SHELL READY
qemu ring3 milestone test: PASS
```
