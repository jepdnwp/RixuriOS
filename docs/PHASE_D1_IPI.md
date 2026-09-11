# RixuriOS Değişiklik Belgesi — Ring3'ten SMP Faz D1'e (2026-09-11)

Bu belge, bu çalışma oturumunda ağaca giren tüm değişiklikleri, her birinin
nedenini, kanıt durumunu ve bundan sonraki işleri tek yerde toplar.
İddia yok, sadece doğrulanmış durum: PASS yazan her satırın arkasında
çalıştırılmış bir test/kanıt vardır; eksikler açıkça işaretlidir.

İlgili commit'ler (sırayla): `12340ac`, `3e59e75`, `f80ef9d`, `cc21850`
ve bu belgeyle birlikte gelen D1 commit'i.

---

## 1. Ring3'e ilk giriş (user_entry.S, vmm.c, scheduler)

**Sorun:** Gerçek donanımda (ASUS B650M-R + Ryzen 7 7700) boot,
`BOOT: USER ENTRY B` satırından sonra donuyordu; QEMU'da çalışıyordu.

**Kök nedenler ve düzeltmeler** (`kernel/arch/x86_64/user_entry.S`,
`kernel/mm/vmm.c`, `kernel/sched/scheduler.c` çevresi):

- İlk giriş `lretq` + paylaşılan 32 baytlık `.bss` frame kullanıyordu.
  Yerine: task stack'inde kurulan IRETQ frame'i (NMI/SMI penceresinde
  taşma yok), `RFLAGS=0x002` (IF kapalı giriş — ilk `int 0x80` dönüşünde
  `syscall_dispatch` IF'i açıyor; bekleyen fiziksel IRQ'nun
  TSS.RSP0/IRETQ teslimiyle yarışması engelleniyor), `cld`,
  `current_pml4_phys`'in MOV CR3'ten hemen sonra senkronlanması.
- `CR4.PGE` temizleniyor (`vmm_early_init`): PGE=1 iken MOV CR3 eski
  firmware global TLB girdilerini temizlemez; gerçek AMI BIOS'ta bayat
  global eşleme ilk user CR3'ünde yanlış fetch'e yol açabiliyordu.
- Tanı için `BOOT: USER ENTRY C post-cr3` marker'ı eklendi (B var / C yok
  ise arıza `mov cr3`/store içindedir; C var devamı yoksa `iretq`/ilk
  user talimatındadır).
- `x86_enter_user_return` / `x86_enter_user_context` IF politikası ilk
  tasarıma döndürüldü (girişte IF=0).

**Kanıt:** QEMU ring3 milestone PASS (`USER_ENTER → SYSCALL_OK →
SHELL READY`); fiziksel makinede shell'e ulaşıldı, `echo`/`chehelp`
fork yolu çalıştı.

## 2. MMIO haritalama (`vmm_map_mmio`)

**Sorun:** Sürücüler (`nvme` başta) BAR'ları o anki CR3'e `vmm_map_page`
ile mapliyordu. Syscall bağlamında bu, user PML4'ine supervisor girdiler
düşürüyordu; validator haklı olarak `REFUSED reason=-5` verip shell
görevini öldürüyordu.

**Düzeltme** (`kernel/mm/vmm.h`, `kernel/mm/vmm.c`): yeni `vmm_map_mmio()`
API'si — eşleme her zaman kernel PML4'üne yapılır, ilgili slot o anki
ağaca ödünç verilir (anında kullanılabilir), paylaşılan slottaki
fiziksel adreslerde identity VA korunur, diğerleri paylaşılan MMIO
penceresinden (PML4 slot 300) window VA alır (phys aralığına göre
dedup'lı). Validator, kayıtlı supervisor-UC yapraklar için VA takibiyle
muafiyet uygular; diğer her şey fail-closed kalır. Dönüştürülen
sürücüler: `nvme`, `xhci`, `e1000`, `rtl8125`, `msix`, `ioapic`
(`e1000`/`xhci` artık VA'yı struct'ta taşıyor, phys'ten türetmiyor).

**Kanıt:** `echo hi` fork+exec+exit döngüsü temiz; `REFUSED` kayboldu;
`make test` yeşil.

## 3. TTY echo + framebuffer (`kernel/tty/tty.c`)

**Sorun:** Canonical echo her tuşta tüm satırı silip yeniden yazıyordu
(`\r ESC[2K`): shell prompt'u ilk harfte uçuyordu; ayrıca host
`tty_test`'teki `echo output queue` beklentisini bozuyordu.

**Düzeltme:** Artımlı echo (eklenen bayt + kayan kuyruk, silmede
`\b`+kuyruk+boşluk, ok tuşlarında tekli ESC hareketleri, history'de
imleç-sol + `ESC[K`). `\n` → `\n`, PS/2 Enter (`\r`) → `\r\n` echo edilir.

**Ek:** GOP framebuffer 512 GiB üstündeyse (dGPU VRAM BAR) user
CR3'lerinde eşli değildi; ilk FB dokunuşu iç içe fault → sessiz donma
üretirdi. `tty_set_framebuffer` artık paylaşılmayan aralığı paylaşılan
MMIO penceresinden mapliyor; konsol her CR3 altında yazılabilir.

**Kanıt:** `tty_test` PASS (önce FAIL'di); prompt yazarken korunuyor.

## 4. Fault forensics (`kernel/arch/x86_64/idt.c`)

- Tüm CPU istisnaları IST#1 üzerinden (bozuk stack'te bile teşhis).
- Döküm: RSP0, task tablosu, fault-RSP stack'i (user fault'ta RSP0 tuzağı),
  RIP baytları, `current`/`hw` CR3 karşılaştırması, cr3trace.
- `process_activate` reddinde sebep kodu (`pid/tgt/reason`).
- Dispatch girişinde port `0x80` POST kodu.

## 5. SMP Faz B — AP triple-fault döngüsü (`smp_trampoline.S`)

**Sorun:** QEMU `-smp 4`'te AP1 SIPI sonrası reset döngüsü (29 reset;
`RPL` sonra sessizlik). `qemu -d int` kanıtı: AP `EFER=0x500` (NXE
kapalı) iken NX bayraklı LAPIC sayfasına dokununca `#PF(RSVD, e=0008)`;
AP hâlâ BIOS IDT'sinde olduğu için `#PF → #GP → #DF → triple fault`.

**Düzeltme:** Trampoline, LME yanına `EFER.NXE` bitini de açıyor
(BSP'deki `vmm_early_init` aynası). Template 318 bayt (< 0x200 limiti).

**Kanıt:** `-smp 4` → `SMP: online=4`, `AP 1/2/3 online`,
`KERNEL_READY`, `USER_ENTER`, `SHELL READY`, 0 exception.

## 6. SMP Faz B1 — AP CPU normalizasyonu + kernel IDT

- Trampoline, paging kapalıyken BSP politikasını aynalıyor:
  CR4=MCE|PAE|OSFXSR|OSXMMEXCPT (LA57/PCIDE/SMEP/SMAP/PKE/PGE kapalı),
  CR0=PE|WP (EM/TS kapalı).
- Long mode, snapshot kernel IDTR'yi `lidt` ile yüklüyor: AP fault'ları
  artık IST#1 handler'larına düşüyor (sessiz triple fault yerine
  teşhisli duruş). GDT bilerek trampoline'de kalmıyor — **aşağıdaki B2
  maddesine bakın** (ilk sürümde GDT trampoline'de bırakılmıştı, 8.
  madde bu kararı düzeltti).
- Paylaşılan IST#1, park halindeki Faz-B AP'leri için kabul edildi
  (per-CPU TSS, Faz C'nin işi).

**Kanıt:** QEMU-monitor kaydında park halindeki AP'de
`IDT=<kernel idt[] VMA>`, `EFER=0xD00`, IF=0 hlt parkı.

## 7. SMP Faz C1 — per-CPU stack + `smp_cpu_id` (`smp.c`, `smp.h`)

- `smp_start_aps`, AP başına sıfırlanmış 16 KiB PMM stack ayırıp
  `stack_phys`'e kaydediyor (alan zaten rezerve edilmişti, hiç
  doldurulmuyordu) ve trampoline'e bunun tepesini veriyor; AP artık C
  kodunu 4 KiB trampoline sayfasında çalıştırmıyor. Tahsis hatası o AP'yi
  atlar (DEGRADED, panic yok). Guard page yok (Faz C/E takibi).
- `smp_setup_trampoline` sözleşmesi: nonzero + 8-aligned (sayfa-içi
  zorunluluğu kalktı).
- Yeni `smp_cpu_id()` (LAPIC-ID taraması, bilinmeyende -1). Scheduler'a
  dokunulmadı.

**Kanıt:** host testi (arena stub'ı, farklılık ve cpu_id pozitif/negatif
case'leri); QEMU `-smp 4` GDB kaydında her park AP'nin RSP'si kendi
kayıtlı aralığında (`cpu1 0x107ff0 ∈ [0x104000,0x108000)` vb.), üçü de
ONLINE.

## 8. SMP Faz D1 — IPI altyapısı + ping/pong + shootdown ilkeli

**Tasarım** (`kernel/arch/x86_64/{apic.h,apic.c,interrupts.S,idt.c,smp.h,smp.c}`):

- Yeni `lapic_send_ipi()` (fixed delivery, INIT/SIPI ile aynı sınırlı
  delivery-status disiplini) ve `lapic_ap_enable()` (AP'de SVR + EOI).
- `interrupts.S`'te `IPI` makrosu → `isr224`/`isr225`,
  `x86_ipi_dispatch` çağırır; IDT'de DPL0 gate'ler
  (`SMP_IPI_PING=0xE0`, `SMP_IPI_SHOOTDOWN=0xE1`; 0xFF bilerek boş).
- `smp_ping(i)`: hedef doğrulama (-2), gönderim, sınırlı ack anketi
  (0/-1). `smp_shootdown(va)`: adres doğrulama (-2), yerelde invlpg,
  tek-uçuş seq protokolüyle ONLINE AP'lere broadcast + ack anketi (0/-1).
  VMM kancası (unmap tetiklemesi) Faz D2'ye bırakıldı.
- `ap_entry`: SVR enable → ONLINE yayını → `sti; hlt` parkı (PIT
  BSP-routed olduğu için AP'ye yalnızca yönlendirilmiş IPI'ler gelir).
- Boot self-test: her ONLINE AP'ye bir ping + scratch VA shootdown;
  hepsi sınırlı, loglar, asla panic yok.
- **Kritik düzeltme (bu turda teşhis edildi):** AP, trampoline GDT'sinde
  kalırsa (`0x08` = 32-bit segment) kernel IDT kapılarının istediği
  kernel CS yüklenemez → ilk AP kesmesinde CS-load `#GP(0x8)` →
  triple fault. Kanıt: `-d int` kaydında `v=0d e=0008`,
  `IP=ap_entry+106` (park `sti;hlt` döngüsü), gate/CS hepsi doğruydu.
  Çözüm: AP, snapshot kernel GDT'ye geçiyor (far-return 0x08, data
  0x10, FS null) ve kernel TSS'yi `ltr` ile yüklüyor (IST kapıları
  için; RSP0 CPL değişimi olmadan kullanılmaz, Phase C'ye kadar
  paylaşımlı). IF=0 boyunca sıralama güvenli.

**Kanıt durumu (dürüst):** `make test` RC=0 (ping/timeout/seq/dispatch
pozitif+negatif case'leri, synth-AP sürücülü uçtan uca yol dahil);
`make image` RC=0. QEMU `-smp 4` hüküm koşusu sürüyor: verbal ping
satırları (`SMP: ping N ok`) ve `SHELL READY` bekleniyor. UP regresyonu
(`-smp 1` SHELL READY) her turda korunuyor.
QEMU `-smp 4`'te AP park + BSP anket deseni GDB ile doğrulandı.

## 9. Test altyapısı onarımları (`tests/`)

- `e1000_test.c` / `rtl8125_test.c`: sürücüler `vmm_map_page`'den
  `vmm_map_mmio`'ya geçince link kırılmıştı; mevcut stub desenine
  `vmm_map_mmio` stub'ı eklendi + `driver.mmio` init edildi.
- `smp_test.c`: NX assertion, üretimin bilinçli executable-trampoline
  kararına çekildi (önceki FAIL'in sebebi); template sembol-tabanlı
  olduğu için asm büyümelerine oto-uyumlu.

## 10. Ağaçta hazır bulunan harici iş (bu oturuma ait değil)

- `kernel/usb/xhci.c` (+515): xHCI register offset düzeltmeleri
  (USBCMD/USBSTS 0x80/0x84 → 0x00/0x04, spec'e uygunluk; not: eski
  değerler QEMU'da sıfır okunup gerçek silikonda takılıyordu),
  port-change/W1C bitleri, reset zamanlamaları.
- `kernel/main.c`: xHCI hotplug hata logunda throttle (değişiklikte veya
  ~2sn'de bir).
- Derleniyor (`-Werror` temiz); QEMU'da xHCI controller yok (0),
  dolayısıyla davranışsal olarak nötr. Ayrı commit olarak alındı,
  yazarı bu belgeyi hazırlayan değildir.

## 11. Bilinen açıklar ve sınırlamalar (kabul edilmiş, gizlenmemiş)

- `-smp 4` ping hükümleri bu belge yazılırken koşuyordu (madde 8);
  `SHELL READY` + `ping ok` görülmeden Faz D1 COMPLETE yazılmaz.
- Fiziksel donanım PASS yok (Ryzen 7700 üzerinde doğrulama açık iş).
- CI yok. `symlink_test.c` yok (SKIP). `sysroot` bootstrap-only
  (musl portu Faz 23/P12).
- `kfree` no-op; mmap yok (ENOSYS); thread/futex/TLS/signal-delivery/
  TCP-retransmit/DNS yok; per-CPU TSS yok (paylaşılan IST#1, park AP'ler
  için kabul); AP parkı dışında scheduler hâlâ kooperatif.
- Yuvalanmış sanallaştırmada (WSL2→TCG, WHPX) `pause`-kalibreli
  beklemeler duvar-saatinde çok yavaş: response anketleri 10 tur×10K
  pause'a indirildi (ack gecikmesine göre hâlâ ~1000× marj);
  protokol anketleri (ONLINE 60 tur) silikona dokunulmadı.
- `nul` (58KB, 9 Eylül'den kalma build-log artığı) ve `build/` çıktıları
  commitlenmedi.

## 12. Bundan sonra (sırayla)

1. **D1 kapatma:** `-smp 4` hüküm koşusunu sonuca erdir (ping ok/timeout
   satırları + shell). `timeout` çıkarsa teslimat yoluna dönülür;
   `ok` çıkarsa Faz D1 yeşil.
2. **Faz D2:** VMM unmap kancası (shootdown tetiklemesi) + invlpg
   broadcast tamamlama + host/QEMU kanıtı.
3. **Faz C (kalanı):** per-CPU TSS/IST + per-CPU scheduler slotları;
   AP park yerini per-CPU idle'a hazırlama.
4. **P1:** gerçek preemptive scheduler (timer kesmesinden scheduling,
   `preemption_test`; fork/exec/wait davranışını koruyarak).
5. **P2–P4:** mmap → dynamic ELF loader (`ld-rix`, auxv) → TLS.
6. **P5–P6:** kernel thread + futex + pthread; gerçek signal delivery.
7. **P7–P8:** VFS eksikleri (symlink/readlink/fstat); TCP retransmission/
   window + DNS (loopback-dışı kanıtla).
8. **P9:** donanım qualification altyapısı (RTL8125/NVMe/xHCI/USB-HID/
   RX 6800 XT için ayrı kayıtl
...[truncated 615 chars]