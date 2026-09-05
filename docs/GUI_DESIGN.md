# RixuriOS GUI Design — Visual and Interaction Specification

Bu belge RixuriOS GUI'nin **neye benzeyeceğini** tanımlar. Referans alınan tasarım: açık, ferah, yumuşak köşeli, yarı saydam panelleri olan; merkezde sakin kalan ve bilgiyi kenarlardan sunan masaüstü.

## 1. Tasarım cümlesi

> **RixuriOS masaüstü sakin kalır; sistem gerektiğinde bilgi ve kontrolü yüzeye çıkarır.**

Hedef görünüm:

- modern;
- minimal;
- premium ama gösterişsiz;
- çok boşluklu;
- hafif glass/material yüzeyler;
- güçlü tipografi hiyerarşisi;
- mavi/tek vurgu rengi ağırlıklı başlangıç teması;
- gereksiz ikon ve metin kalabalığı olmayan arayüz.

Bu belge herhangi bir üçüncü taraf masaüstünü kopyalama talimatı değildir; RixuriOS'a özgü bir görsel dil tanımlar.

---

# 2. Ekran kompozisyonu

```text
┌─────────────────────────────────────────────────────────────┐
│ RIX                         DATE/TIME       STATUS   POWER   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│ ┌──────────────┐                         ┌────────────────┐ │
│ │ CLOCK        │                         │ NOTIFICATIONS  │ │
│ │              │                         │                │ │
│ └──────────────┘                         │                │ │
│                                                             │
│ ┌──────────────┐                         ├────────────────┤ │
│ │ CALENDAR     │                         │ QUICK SETTINGS │ │
│ │              │                         │                │ │
│ └──────────────┘                         │                │ │
│                                                             │
│ ┌──────────────┐                                           │
│ │ SYSTEM       │                                           │
│ │ CPU RAM DISK │              CALM DESKTOP                 │
│ └──────────────┘                                           │
│                                                             │
│                         ┌───────────────────────┐           │
│                         │         DOCK          │           │
│                         └───────────────────────┘           │
└─────────────────────────────────────────────────────────────┘
```

Merkez alan bilinçli olarak boş bırakılır. Kullanıcının uygulama pencereleri dışında masaüstü üzerinde sürekli görsel gürültü olmamalıdır.

---

# 3. Üst panel

### Sol

**RIX** wordmark/logo.

Yanında küçük sistem menüsü erişimi.

### Orta

Saat ve tarih.

Örnek:

`22:41`  
`2 Eylül`

### Sağ

Durum ikonları:

`Network · Audio · Notifications · Power`

İkonlar monochrome/simple çizgi dili kullanır; aktif durumlarda vurgu rengi veya küçük indicator kullanılır.

---

# 4. Sol widget kolonları

Sol tarafta sabit ama taşınabilir widget alanı bulunur.

## Clock Card

Büyük dijital saat ve isteğe bağlı analog saat.

```text
22:41
2 Eylül 2025
Salı
```

## Calendar Card

Ay görünümü.

Seçili gün yuvarlak vurgu ile gösterilir.

## System Card

Üç temel ring/gösterge:

```text
CPU    23%
RAM    38%
DISK   41%
```

Alt bölümde network durumu:

```text
Ethernet   Bağlandı
↓ 42.6 KB/s
↑ 18.3 KB/s
```

Bu değerler yalnızca gerçek OS telemetry'si varsa gösterilir.

---

# 5. Sağ panel / Control Center

Sağdan açılan tek bir yüzey iki bölüme ayrılır.

## Notifications

Başlık:

`Bildirimler                 Tümünü temizle`

Kartlar birbirinden ince divider ile ayrılır.

Her bildirim:

```text
[icon]  Title
        Description
                           time
```

Kartlar fazla renkli olmamalıdır. Renk yalnızca anlam için kullanılmalıdır.

## Quick Settings

Bildirimlerin altında:

```text
┌────────┬────────┬────────┐
│ Wi-Fi  │Ethernet│Bluetooth│
└────────┴────────┴────────┘

Brightness   ━━━━━━━━━ ●
Volume       ━━━━━━━ ●

┌────────┬────────┬────────┬────────┐
│ Night  │ DND    │ Lock   │ Monitor│
└────────┴────────┴────────┴────────┘

⚙ Settings                         >
```

Bu panel bir ayarlar uygulamasının yerine geçmez; sık kullanılan sistem kontrollerinin hızlı yüzüdür.

---

# 6. Dock

Dock ekranın alt-orta bölümünde yüzer.

Özellikler:

- rounded container;
- hafif translucency;
- ince border;
- soft shadow;
- pinned apps;
- running-app indicator;
- launcher button;
- optional auto-hide.

İkonlar arasında eşit spacing bulunur.

Aktif uygulama küçük bir çizgi/dot ile belirtilir.

---

# 7. Glass/material dili

Panel yüzeyleri tamamen şeffaf değildir.

Önerilen katman:

```text
background tint
+ controlled transparency
+ optional backdrop blur
+ 1px subtle border
+ soft shadow
+ rounded corners
```

Blur performans veya donanım nedeniyle kullanılamıyorsa panel opak/reduced-material moda geçer.

## Material levels

`M0` — opaque  
`M1` — translucent  
`M2` — translucent + blur  
`M3` — enhanced effects

Varsayılan masaüstü M1/M2 arasında olabilir.

---

# 8. Renk sistemi

Başlangıç teması:

- açık arka plan;
- beyaz/çok açık surface;
- koyu metin;
- mavi primary accent;
- yeşil success;
- sarı warning;
- kırmızı error.

Renkler dekoratif olarak aşırı kullanılmaz.

Aynı tasarım token'ları Dark/OLED/High Contrast temalarında farklı değerlere bağlanır.

---

# 9. Tipografi

Öncelik:

1. okunabilirlik;
2. hiyerarşi;
3. dengeli spacing;
4. Unicode/Türkçe desteği.

Başlıklar orta ağırlıkta, body text normal ağırlıkta tutulur.

Sistem genelinde aynı font metrics kullanılmalıdır.

---

# 10. Köşe ve spacing dili

Tüm UI rastgele radius kullanmamalı.

Örnek token ailesi:

```text
radius-xs
radius-sm
radius-md
radius-lg
radius-xl
```

Aynı şekilde spacing:

```text
space-1
space-2
space-3
space-4
space-6
space-8
space-12
```

Widget'lar ve pencereler bu sistemden değer alır.

---

# 11. Animasyon dili

Animasyonlar:

- kısa;
- yumuşak;
- tahmin edilebilir;
- interruptible

olmalı.

Örnekler:

- notification slide/fade;
- panel open/close;
- dock reveal;
- workspace transition;
- window minimize/maximize;
- hover/focus transition.

`prefers-reduced-motion` benzeri sistem tercihi etkin olduğunda efektler azaltılır veya kapatılır.

---

# 12. Pencere görünümü

Pencereler:

- temiz titlebar;
- minimum gereksiz chrome;
- tutarlı radius;
- soft shadow;
- açık focus indicator

kullanır.

Modal pencere arka planı hafif karartabilir ancak kullanıcıyı kilitleyen gereksiz animasyonlar olmamalıdır.

---

# 13. Workspace görünümü

Workspace overview açıldığında:

```text
┌──────────────────────────────────────┐
│ Workspace 1                          │
│ [Terminal] [Mayo] [Files]            │
│                                      │
│ Workspace 2                          │
│ [Browser] [Docs]                     │
└──────────────────────────────────────┘
```

Kullanıcı pencereyi sürükleyerek başka workspace'e taşıyabilir.

---

# 14. Launcher

Launcher ekranı:

- arama alanı;
- uygulama grid'i;
- favorites;
- recent apps;
- kategoriler

içerir.

Arama klavye-first tasarlanır.

Örnek:

```text
┌────────────────────────────────────┐
│ Search applications...             │
├────────────────────────────────────┤
│ Mayo                               │
│ Terminal                           │
│ Settings                           │
│ Files                              │
└────────────────────────────────────┘
```

---

# 15. Ayarlar görünümü

Ayarlar uygulaması sol navigation + sağ content düzenini kullanabilir:

```text
┌───────────────┬────────────────────────────┐
│ Sistem        │                            │
│ Görünüm       │       Display              │
│ Ekran         │                            │
│ Ses           │       settings             │
│ Ağ            │                            │
│ Kullanıcılar  │                            │
│ Güvenlik      │                            │
└───────────────┴────────────────────────────┘
```

---

# 16. Dosya yöneticisi görünümü

Rix Files:

- sidebar;
- breadcrumbs;
- list/grid view;
- tabs;
- split view;
- file properties;
- transfer progress

kullanır.

Destructive operations visually distinguish edilir ve açık hedef gösterilir.

---

# 17. Terminal görünümü

Rix Terminal sistemin gerçek PTY'sini kullanır.

Görsel olarak:

- sade;
- monospace;
- truecolor;
- tabs;
- split panes;
- search;
- copy/paste;
- adjustable font

sunabilir.

Terminal uygulaması shell'i simüle etmez.

---

# 18. System Monitor görünümü

Grafik ve tablo kombinasyonu:

```text
CPU
Core 0  ███████░░
Core 1  ████░░░░░
Core 2  ██░░░░░░░

Memory
8.2 / 32 GB

Processes
PID   Name       CPU   RAM
1     init       0.2%  2M
42    shell      0.1%  3M
```

Grafikler gerçek telemetry ile güncellenir.

---

# 19. Sistem durumlarının görsel dili

Her kontrolün state'i açıkça görünür:

```text
Normal
Active
Disabled
Loading
Success
Warning
Error
Unavailable
Permission denied
```

`Unavailable` ile `Off` aynı şey değildir.

Örneğin Bluetooth hardware yoksa:

`Bluetooth — Kullanılamıyor`

gösterilir; `Kapalı` denmez.

---

# 20. Empty states

Boş ekranlar “Nothing here” ile bırakılmaz.

Örnek:

```text
No notifications

Bildirim geldiğinde burada görünecek.
```

Aynı yaklaşım Files, Bluetooth, Network ve System Monitor'da uygulanır.

---

# 21. Error states

Hata mesajı:

1. ne oldu;
2. neden;
3. ne yapılabilir

söylemeli.

Örneğin:

```text
Disk bağlanamadı

RixFS metadata doğrulanamadı.

[ Ayrıntıları göster ]
[ Recovery Shell ]
```

Sadece `Error 5` gösterilmez.

---

# 22. Responsive davranış

GUI farklı ekranlarda bozulmamalı.

### Large desktop
Widget kolonları + dock + geniş workspace.

### Medium
Widget yoğunluğu azaltılabilir.

### Small
Control Center tam/yarım ekran olabilir; dock daha kompakt olur.

### HiDPI
UI scale sistemsel olarak uygulanır.

---

# 23. Multi-monitor

Her monitor:

- kendi scale;
- resolution;
- refresh;
- orientation

bilgisine sahip olabilir.

Panel/dock politikası:

- primary only;
- every monitor;
- focused monitor

seçeneklerine sahip olabilir.

---

# 24. Privacy

Kilit ekranında hassas bildirim içeriği gizlenebilir.

Örneğin:

`3 yeni bildirim`

gösterilirken özel mesajın içeriği kullanıcı doğrulanana kadar saklanabilir.

Screenshot/screen-recording durumları kullanıcıya görünür şekilde işaretlenebilir.

---

# 25. Erişilebilir tasarım

Görsel tasarım hiçbir zaman erişilebilirliği bozmamalı.

- keyboard focus;
- visible focus ring;
- sufficient contrast;
- text scaling;
- reduced motion;
- screen-reader labels;
- non-color status indicators;
- large touch targets

desteklenir.

---

# 26. RixuriOS kimliği

RixuriOS GUI'nin ayırt edici özellikleri:

1. **Sakin merkez alan**
2. **Kenar tabanlı bilgi panelleri**
3. **Tek birleşik Control Center**
4. **RIX üst paneli**
5. **Yüzen dock**
6. **Minimal glass/material**
7. **Gerçek zamanlı sistem bilgisi**
8. **Terminal-first yaklaşımın GUI'de de korunması**
9. **Recovery-first davranış**
10. **Tutarlı Rix Design System**

---

# 27. Final görsel hedef

Kullanıcı masaüstüne baktığında ilk izlenim:

> “Temiz, sakin ve modern.”

İkinci izlenim:

> “Sistem hakkında ihtiyacım olan bilgi burada.”

Üçüncü izlenim:

> “Kontroller tek yerde ve ne yaptıkları açık.”

Ve en önemlisi:

> **Gördüğüm her sistem bilgisi gerçekten işletim sisteminden geliyor.**

Bu nedenle görsel hedef ile teknik hedef ayrılmaz:

`RIX Desktop görünümü ← Rix GUI Toolkit ← Desktop Services ← OS APIs ← RixuriOS kernel/drivers`
