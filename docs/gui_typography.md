# RIX OS — Tasarım Sistemi Spesifikasyonu (Design System Spec) — C Uygulaması

> **Bu doküman kimin için:** Bu dosya, RIX OS masaüstü arayüzünü **C dilinde** kodlayacak bir geliştirici/AI ajanı için yazılmıştır. Amaç yorum bırakmamaktır: her renk, ölçü, boşluk, gölge ve davranış **kesin değerlerle, C sabitleri/struct'ları olarak** tanımlanmıştır. Uygulayan kişi/model, aşağıdaki token'ları doğrudan bir `design_tokens.h` başlık dosyasına birebir aktarabilmelidir. Web/CSS terminolojisi (backdrop-filter, transition, grid-template vb.) burada **kullanılmaz**; her kavram C tarafında karşılığıyla (struct alanı, çizim fonksiyonu, frame-loop hesaplaması) tarif edilir.

> **Nasıl kullanılmalı:** Her bileşen bölümü şu sırayla okunmalı: (1) Anatomi → (2) Token'lar (struct/#define) → (3) Durumlar (enum + çizim farkı) → (4) Davranış/animasyon (frame başına hesap) → (5) Erişilebilirlik notu. Belirsiz bir durumda **Bölüm 2 (Temel Token'lar)** her zaman kaynak olarak kullanılmalı.

---

## 0. Teknoloji Yığını Önerisi (C tarafı)

C'de bir masaüstü GUI'si yazarken aşağıdaki katmanlar önerilir. AI/geliştirici bu kararı proje başında netleştirmeli, çünkü aşağıdaki tüm çizim talimatları bu katmanlar üzerinden ifade edilir.

| Katman | Önerilen kütüphane | Neden |
|---|---|---|
| Pencere/olay döngüsü + framebuffer erişimi | **SDL2** (`SDL_CreateWindow`, `SDL_CreateRenderer`) | Platformlar arası, C API'si sade, olay kuyruğu (mouse/klavye) hazır |
| 2B vektör çizim (yuvarlak köşe, daire, çizgi, dolgu) | **NanoVG** (C, OpenGL üzerine) *veya* **Cairo** (C, yazı tipi + vektör çizim güçlü) | Piksel piksel dikdörtgen çizmek yerine yüksek seviye `nvgRoundedRect`, `nvgArc` gibi fonksiyonlar sağlar |
| Yazı tipi render | **stb_truetype.h** (tek header, bağımlılıksız) veya Cairo/NanoVG'nin kendi font modülü | Glyph rasterize + metrik hesaplama |
| Bulanıklaştırma (blur) | Yazılımsal **kutu bulanıklığı (box blur)**, aşağı-örnekleme (downsample) + yukarı-örnekleme (upsample) ile (Bölüm 2.6) | GPU shader yoksa CPU'da hızlı yaklaşık gaussian blur |
| Animasyon zamanlayıcı | Sabit adımlı (fixed-timestep) `update(delta_ms)` döngüsü | Bölüm 5'teki `ease()` fonksiyonları için gereklidir |

**Kural:** Bu doküman "immediate-mode" (her frame'de yeniden çizilen) bir GUI mimarisi varsayar — yani her widget, her frame'de kendi `draw(ctx, state)` fonksiyonunu çağırır. Retained-mode (DOM benzeri ağaç + diffing) istenirse, aynı token'lar geçerli kalır ama widget struct'larına bir `bool dirty` alanı eklenmelidir.

---

## 1. Genel Tasarım Dili

| Özellik | Değer |
|---|---|
| Tema modu | Açık (Light) — birincil; Koyu (Dark) — Bölüm 12'de karşılıklarıyla tanımlı |
| Görsel stil | Soft glassmorphism (buzlu cam), düşük kontrastlı gölgeler, yüksek negatif alan |
| Köşe felsefesi | Büyük corner-radius (sert köşe yok; `radius < 8` piksel kullanılmaz) |
| Derinlik modeli | 3 katman: (Z0) Duvar kağıdı → (Z1) Widget'lar/dock → (Z2) Geçici katmanlar (bildirim çekmecesi, context menü) — bkz. Bölüm 2.5 |
| Hareket felsefesi | "Sakin" easing; hiçbir animasyon 300ms'yi geçmez; bounce/elastic yok |
| Yoğunluk (density) | Orta-gevşek — sıkışık değil, geniş padding'li |

---

## 2. Temel Token'lar (`design_tokens.h`)

### 2.1 Renk Token'ları — Açık Tema

Renkler `RGBA8` struct'ı olarak tutulur (her kanal 0–255). Saydamlık için `a` alanı kullanılır; render katmanı (NanoVG/Cairo) bunu 0.0–1.0 aralığına kendi çevirir.

```c
// design_tokens.h

typedef struct { uint8_t r, g, b, a; } RGBA8;

/* ---- Zemin / Arkaplan (wallpaper gradient) ---- */
#define COLOR_WALLPAPER_START  (RGBA8){0xEC, 0xED, 0xEF, 255}
#define COLOR_WALLPAPER_END    (RGBA8){0xD7, 0xDA, 0xDD, 255}

/* ---- Yüzey (glass) renkleri ---- */
#define COLOR_SURFACE_GLASS         (RGBA8){255, 255, 255, 184} /* ~%72 opak */
#define COLOR_SURFACE_GLASS_STRONG  (RGBA8){255, 255, 255, 219} /* ~%86 opak, çekmece */
#define COLOR_SURFACE_GLASS_WEAK    (RGBA8){255, 255, 255, 140} /* ~%55 opak, dock */
#define BLUR_RADIUS_GLASS_PX        28

/* ---- Metin ---- */
#define COLOR_TEXT_PRIMARY     (RGBA8){0x1C, 0x1D, 0x1F, 255}
#define COLOR_TEXT_SECONDARY   (RGBA8){0x8A, 0x8D, 0x93, 255}
#define COLOR_TEXT_TERTIARY    (RGBA8){0xB4, 0xB7, 0xBC, 255}
#define COLOR_TEXT_ON_ACCENT   (RGBA8){0xFF, 0xFF, 0xFF, 255}

/* ---- Accent (vurgu mavi) ---- */
#define COLOR_ACCENT           (RGBA8){0x2F, 0x6F, 0xED, 255}
#define COLOR_ACCENT_HOVER     (RGBA8){0x2A, 0x63, 0xD6, 255}
#define COLOR_ACCENT_ACTIVE    (RGBA8){0x25, 0x57, 0xBE, 255}
#define COLOR_ACCENT_SUBTLE    (RGBA8){0xEA, 0xF1, 0xFE, 255}

/* ---- Durum renkleri ---- */
#define COLOR_SUCCESS          (RGBA8){0x3D, 0xBE, 0x6C, 255}
#define COLOR_SUCCESS_SUBTLE   (RGBA8){0xE8, 0xF9, 0xEE, 255}
#define COLOR_WARNING          (RGBA8){0xE8, 0xA6, 0x3D, 255}
#define COLOR_WARNING_SUBTLE   (RGBA8){0xFD, 0xF3, 0xE3, 255}
#define COLOR_DANGER           (RGBA8){0xE5, 0x48, 0x4D, 255}
#define COLOR_DANGER_SUBTLE    (RGBA8){0xFD, 0xEC, 0xEC, 255}
#define COLOR_INFO             COLOR_ACCENT
#define COLOR_INFO_SUBTLE      COLOR_ACCENT_SUBTLE

/* ---- Kenarlık / ayraç ---- */
#define COLOR_BORDER_HAIRLINE  (RGBA8){0, 0, 0, 15}   /* ~%6 opak siyah */
#define COLOR_BORDER_STRONG    (RGBA8){0, 0, 0, 26}   /* ~%10 opak siyah */

/* ---- Gölge tanımı (blur_radius, offset_y, RGBA8 renk) ---- */
typedef struct { int blur_px; int offset_y_px; RGBA8 color; } ShadowSpec;
#define SHADOW_SM (ShadowSpec){  6, 2,  (RGBA8){0,0,0,10} }
#define SHADOW_MD (ShadowSpec){ 24, 8,  (RGBA8){0,0,0,15} }
#define SHADOW_LG (ShadowSpec){ 40, 16, (RGBA8){0,0,0,26} }
#define SHADOW_FOCUS_RING (ShadowSpec){ 0, 0, (RGBA8){0x2F,0x6F,0xED,90} } /* 3px halka, blur=0 */
```

### 2.2 Boşluk (Spacing) Ölçeği — 4px taban birim

```c
#define SPACE_1   4
#define SPACE_2   8
#define SPACE_3  12
#define SPACE_4  16   /* standart kart iç boşluğu, widget'lar arası boşluk */
#define SPACE_5  20   /* çekmece iç boşluğu, ekran kenar boşluğu */
#define SPACE_6  24   /* bölüm arası boşluk */
#define SPACE_8  32   /* büyük ekran kenar boşluğu */
#define SPACE_10 40   /* büyük bölüm ayrımı */
```

**Kural:** Kodda `13`, `18` gibi bu listede olmayan piksel değeri **asla doğrudan yazılmaz**; her yerleşim hesaplaması bu `#define`'lardan biriyle veya bunların toplamıyla yapılır.

### 2.3 Köşe Yarıçapı (Radius) Ölçeği

```c
#define RADIUS_SM    10   /* küçük butonlar, rozetler */
#define RADIUS_MD    14   /* toggle kutuları, ikon butonlar */
#define RADIUS_LG    20   /* standart widget/kart */
#define RADIUS_XL    28   /* büyük yüzeyler: bildirim çekmecesi, dock kapsülü */
#define RADIUS_FULL  -1   /* özel değer: "tam daire" anlamına gelir, çizim fonksiyonu
                              genişlik/2'yi radius olarak kullanır */
```

Yuvarlak dikdörtgen çizmek için önerilen imza (NanoVG kullanıldığını varsayarak):

```c
void draw_rounded_rect(NVGcontext *vg, float x, float y, float w, float h,
                        float radius, RGBA8 fill_color) {
    if (radius < 0) radius = (w < h ? w : h) / 2.0f; /* RADIUS_FULL durumu */
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, nvgRGBA(fill_color.r, fill_color.g, fill_color.b, fill_color.a));
    nvgFill(vg);
}
```

### 2.4 Tipografi Ölçeği

**Font dosyası:** Sistemle birlikte gömülü bir **Inter** varyantı (`Inter-Regular.ttf`, `Inter-Medium.ttf`, `Inter-SemiBold.ttf`) `stb_truetype` veya Cairo font-face olarak yüklenir; sisteme bağımlı font aramasına güvenilmez (platformlar arası tutarlılık için font binary'si uygulamayla birlikte dağıtılır).

```c
typedef struct {
    const char *font_file;   /* örn. "Inter-Medium.ttf" */
    float       size_px;
    float       line_height_mult;
} TextStyle;

#define FONT_DISPLAY      (TextStyle){"Inter-Medium.ttf",   42.0f, 1.1f}  /* büyük saat */
#define FONT_H1           (TextStyle){"Inter-SemiBold.ttf", 20.0f, 1.3f}  /* "Bildirimler" */
#define FONT_H2           (TextStyle){"Inter-SemiBold.ttf", 16.0f, 1.35f} /* kart başlıkları */
#define FONT_BODY         (TextStyle){"Inter-Regular.ttf",  14.0f, 1.5f}
#define FONT_BODY_MEDIUM  (TextStyle){"Inter-Medium.ttf",   14.0f, 1.5f}
#define FONT_CAPTION      (TextStyle){"Inter-Regular.ttf",  12.0f, 1.4f}
#define FONT_MICRO        (TextStyle){"Inter-Medium.ttf",   11.0f, 1.3f}
```

**Kural — sabit genişlikli rakam (tabular numerals):** Saat, yüzde ve KB/s gibi değerler her 1 saniyede bir güncellenirken metin genişliği oynamamalı. Inter fontunun `tnum` OpenType özelliği varsa (Cairo/HarfBuzz ile) etkinleştirilir; yoksa basit çözüm: rakamları **monospace bir alt-küme** ile çiz (her rakam glyph'i için sabit ilerleme genişliği kullan, `stbtt_GetCodepointHMetrics` yerine sabit `digit_advance_px` uygula).

### 2.5 Katman (Z-order) Sırası — Çizim Sırası Kuralı

Immediate-mode bir render döngüsünde "z-index" diye bir şey yoktur; **çizim sırası** z-order'ı belirler. Her frame'de şu sırayla çizilmelidir:

```c
typedef enum {
    LAYER_WALLPAPER = 0,   /* en altta çizilir */
    LAYER_WIDGETS,         /* saat, takvim, sistem izleme widget'ları */
    LAYER_DOCK,
    LAYER_TOPBAR,
    LAYER_OVERLAY,         /* bildirim & hızlı ayarlar çekmecesi */
    LAYER_MODAL,           /* sistem uyarıları, onay kutuları */
    LAYER_CONTEXT_MENU,    /* sağ tık menüsü, tooltip — en üstte */
    LAYER_COUNT
} RenderLayer;

void render_frame(AppState *app, NVGcontext *vg) {
    draw_wallpaper(vg, &app->wallpaper);
    for (int i = 0; i < app->widget_count; i++) draw_widget(vg, &app->widgets[i]);
    draw_dock(vg, &app->dock);
    draw_topbar(vg, &app->topbar);
    if (app->overlay.is_open)      draw_overlay(vg, &app->overlay);
    if (app->modal.is_open)        draw_modal(vg, &app->modal);
    if (app->context_menu.is_open) draw_context_menu(vg, &app->context_menu);
}
```

### 2.6 Blur Uygulaması (Yazılımsal Yaklaşık Gaussian Blur)

Cam-panel efekti için, panelin **arkasındaki** ekran içeriğini önce küçük bir dokuya (texture) render edip kutu bulanıklığı (box blur, 2-3 geçiş) uygulanması önerilir — gerçek zamanlı tam çözünürlük Gaussian blur CPU'da pahalıdır.

```c
/* Basitleştirilmiş algoritma:
   1. Panelin arkasındaki bölgeyi 1/4 çözünürlükte bir offscreen texture'a çiz.
   2. O texture üzerinde yatay + dikey box blur uygula (3 geçiş ~ gaussian yaklaşıklığı verir).
   3. Bulanıklaştırılmış texture'ı panel boyutuna geri ölçekleyip (upsample) panel
      zemini olarak kullan, üzerine COLOR_SURFACE_GLASS rengini alpha-blend ile bindir. */
void apply_box_blur_pass(uint8_t *pixels, int w, int h, int radius);
```

**Performans notu:** Blur her frame yeniden hesaplanmak zorunda değildir; panelin arkasındaki içerik değişmediyse (statik wallpaper + hareketsiz widget'lar) blur texture'ı cache'lenip yalnızca panel açılışında bir kez üretilebilir.

---

## 3. Izgara ve Yerleşim Sistemi

### 3.1 Masaüstü Yerleşimi (≥1280px pencere genişliği)

Ekranda yapısal/sabit "sol panel" veya "sağ panel" **yoktur**. Masaüstü tamamen serbest bir tuval; üzerinde iki bağımsız öğe grubu konumlanır:

1. **Widget'lar** — masaüstüne sabitlenmiş veya sürüklenebilir bağımsız kartlar (Saat, Takvim, Sistem İzleme). Her widget kendi `(x, y, w, h)` konumunu bir `WidgetInstance` struct'ında tutar; varsayılan olarak sol-üstte dikey dizilirler ama kod tarafında herhangi bir noktaya taşınabilir.
2. **Bildirim & Hızlı Ayarlar Çekmecesi** — top bar'daki zil ikonuna tıklanınca sağ üstten açılan **tek bir geçici katman** (`LAYER_OVERLAY`). İçinde üst üste iki bölüm barındırır: Bildirimler (4.6) ve Hızlı Ayarlar (4.7). Kalıcı değildir; `overlay.is_open = false` olduğunda hiç çizilmez.

```c
typedef struct {
    float x, y, w, h;      /* piksel cinsinden konum/boyut */
    WidgetType type;       /* WIDGET_CLOCK, WIDGET_CALENDAR, WIDGET_SYSMONITOR, ... */
    void *state;           /* widget'a özel veri (örn. ClockState*) */
} WidgetInstance;

typedef struct {
    bool  is_open;
    float anim_progress;   /* 0.0 = kapalı, 1.0 = tam açık; Bölüm 5'teki ease ile ilerler */
    NotificationList notifications;
    QuickSettingsState quick_settings;
} OverlayState;
```

- Widget yığını ve dock **her zaman çizilir** (koşulsuz); çekmece ise `is_open` bayrağına bağlıdır.
- Çekmece açıldığında diğer widget'ların konumunu **değiştirmez**, sadece üzerine çizilir (Bölüm 2.5 sırası).
- Ekran kenarlarından widget/çekmece gruplarına min. `SPACE_5` (20px) boşluk bırakılır.
- Varsayılan widget yığını genişliği: `#define WIDGET_COLUMN_WIDTH_PX 312`, kartlar arası dikey boşluk `SPACE_4` (16px).

### 3.2 Pencere Boyutuna Göre Davranış

| Pencere genişliği | Davranış |
|---|---|
| ≥1280px | Tam düzen (yukarıdaki gibi) |
| 900–1279px | `WIDGET_COLUMN_WIDTH_PX` 280'e düşer, font boyutları `* 0.95f` ölçeklenir |
| <900px | Widget yığını gizlenir (`widget.visible = false`), tam ekran "Bugün" görünümü açılabilir; dock genişler |

---

## 4. Bileşen Kütüphanesi

### 4.1 Kart (Card) — Temel Çizim Fonksiyonu

**Anatomi:** `[İkon/Başlık satırı] → [Ayraç (opsiyonel)] → [İçerik gövdesi] → [Alt bilgi (opsiyonel)]`

```c
typedef struct {
    float x, y, w, h;
    bool  is_hovered;
    bool  is_focused;
} CardParams;

void draw_card(NVGcontext *vg, CardParams p) {
    ShadowSpec shadow = p.is_hovered ? SHADOW_LG : SHADOW_MD;
    draw_drop_shadow(vg, p.x, p.y, p.w, p.h, RADIUS_LG, shadow);

    float draw_y = p.is_hovered ? p.y - 1.0f : p.y; /* hover'da 1px yukarı kalkış */
    draw_glass_surface(vg, p.x, draw_y, p.w, p.h, RADIUS_LG,
                        COLOR_SURFACE_GLASS, BLUR_RADIUS_GLASS_PX);

    if (p.is_focused) {
        draw_ring(vg, p.x, draw_y, p.w, p.h, RADIUS_LG, SHADOW_FOCUS_RING, 3);
    }
}
```

**Durumlar:** `is_hovered` (mouse konumu kart sınırları içinde mi — her frame `point_in_rect()` ile kontrol edilir), `is_focused` (klavye ile `Tab` ile seçili mi — `AppState.focused_widget_id` alanıyla takip edilir).

### 4.2 Saat Widget'ı

- Analog saat çapı 90px, `nvgArc` ile çizilen kadran çizgisi yok; sadece 12/3/6/9 pozisyonlarında 4 kısa çizgi (tik).
  - Saat akrebi: `COLOR_TEXT_PRIMARY`, kalınlık 3px, uzunluk = yarıçap × 0.50.
  - Dakika yelkovanı: `COLOR_TEXT_PRIMARY`, kalınlık 2px, uzunluk = yarıçap × 0.75.
  - Saniye kolu (opsiyonel): `COLOR_ACCENT`, kalınlık 1px, uzunluk = yarıçap × 0.85; açısı `(seconds + ms/1000.0f) / 60.0f * 360.0f` ile **sürekli** hesaplanır (adım adım sıçramaz).
  - Merkez nokta: 6px çap dolu daire, `COLOR_TEXT_PRIMARY`.
- Açı hesaplama:
  ```c
  float hour_angle_deg   = ((hour % 12) + minute / 60.0f) / 12.0f * 360.0f;
  float minute_angle_deg = (minute + second / 60.0f) / 60.0f * 360.0f;
  float second_angle_deg = (second + ms / 1000.0f) / 60.0f * 360.0f;
  ```
- Sağ tarafta dijital gösterim: `FONT_DISPLAY` ile `"HH:MM"`, altında `FONT_CAPTION` + `COLOR_TEXT_SECONDARY` ile tam tarih (`"2 Eylül 2025 Salı"` — `strftime` ile üretilir).
- Yerleşim: yatay iki blok, aralarında `SPACE_4`, dikey ortalanmış (`analog_y + analog_h/2 == digital_block_center_y`).

### 4.3 Takvim Widget'ı

- Başlık satırı: Ay + Yıl (`FONT_H2`) solda; sağda 24×24px tıklama alanlı ay-değiştir ok ikonu (`COLOR_TEXT_SECONDARY`, hover'da `COLOR_TEXT_PRIMARY`).
- Hafta başlığı satırı: `FONT_MICRO`, `COLOR_TEXT_TERTIARY`; dizi **Pazartesi'den başlar**: `{"Pzt","Sal","Çar","Per","Cum","Cmt","Paz"}`.
- Gün hücreleri: 36×36px dokunma alanı, 7 sütunlu grid (`cell_x = grid_origin_x + col * cell_size`).
  - Bugünkü aya ait gün: `COLOR_TEXT_PRIMARY`.
  - Önceki/sonraki ay günleri: `COLOR_TEXT_TERTIARY`.
  - **Seçili gün:** arka planda `RADIUS_FULL` dolu daire `COLOR_ACCENT`, metin `COLOR_TEXT_ON_ACCENT`, `FONT_BODY` ağırlığı `SemiBold`'a çıkar.
  - Hover (seçili olmayan gün): arka planda `RADIUS_FULL` dolu daire `COLOR_ACCENT_SUBTLE`.
- Grid tarih hesaplama: ayın 1. gününün haftanın hangi gününe denk geldiğini bul (Pazartesi=0 kabul ederek), önceki ayın son günlerinden geriye doğru doldur, standart takvim matrisi algoritması uygulanır (Zeller's congruence veya basit `struct tm` tabanlı hesap yeterlidir).

### 4.4 Sistem İzleme Widget'ı (CPU/RAM/Disk)

```c
typedef enum { RING_OK, RING_WARNING, RING_DANGER } RingLevel;

RingLevel ring_level_for_percent(float pct) {
    if (pct >= 90.0f) return RING_DANGER;
    if (pct >= 70.0f) return RING_WARNING;
    return RING_OK;
}

void draw_ring_gauge(NVGcontext *vg, float cx, float cy, float radius,
                      float percent, RingLevel level) {
    RGBA8 progress_color =
        level == RING_DANGER  ? COLOR_DANGER  :
        level == RING_WARNING ? COLOR_WARNING :
                                 COLOR_ACCENT;

    /* Arka iz (track) — tam daire */
    draw_arc(vg, cx, cy, radius, 0.0f, 360.0f, 5.0f, COLOR_BORDER_STRONG);
    /* İlerleme yayı — 12 yönünden (üstten) başlar, saat yönünde ilerler */
    float sweep_deg = (percent / 100.0f) * 360.0f;
    draw_arc(vg, cx, cy, radius, -90.0f, -90.0f + sweep_deg, 5.0f, progress_color);
}
```

- Ring boyutu: 64×64px, `stroke_width = 5px`; 3 eşit sütunda CPU / RAM / Disk.
- Ring içi ortalanmış metin: yüzde değeri `FONT_BODY_MEDIUM`, altında etiket `FONT_MICRO` + `COLOR_TEXT_SECONDARY`.
- Ağ bölümü (alt kısım): `1px` ayraç çizgisi `COLOR_BORDER_HAIRLINE`, üstte `SPACE_4` boşluk; satırda ikon + bağlantı adı + durum solda, ↓/↑ hız değerleri sağda (`FONT_CAPTION`, sabit-genişlik rakam kuralı).
- **Sparkline grafik:** son 60 saniyelik veriyi tutan dairesel bir buffer (`float samples[60]`) üzerinden `nvgLineTo` ile çizilen kırık çizgi; yükseklik 32px, `stroke_width = 1.5px`, `COLOR_ACCENT`. Her 1 saniyede bir yeni örnek eklenir, buffer sola kayar (`memmove`).

### 4.5 Üst Çubuk (Top Bar)

- Yükseklik: 52px, zemin `COLOR_SURFACE_GLASS_WEAK`, alt kenarda `1px` çizgi `COLOR_BORDER_HAIRLINE`.
- Sol: logo (24×24 raster/SVG) + ürün adı `FONT_H2`.
- Orta: tarih+saat metni `FONT_BODY_MEDIUM`; tıklanınca (opsiyonel) takvim widget'ına odak/highlight animasyonu tetiklenir.
- Sağ: sistem ikonları soldan sağa → ağ, ses, bildirim (zil), güç. Her biri 36×36px tıklama alanı, aralarında `SPACE_2`.
  - Bildirim ikonunda okunmamış işareti: ikonun sağ-üst köşesinde 6px çap dolu daire, `COLOR_ACCENT` (normal) veya `COLOR_DANGER` (kritik bildirim varsa) — bayrak: `app->has_unread_notifications`, `app->has_critical_notification`.

### 4.6 Bildirim Çekmecesi Bölümü (Notification Section)

- Konum: top bar'ın hemen altında, sağa yaslı; `anim_progress` 0→1 ilerlerken hem `opacity` hem `y_offset` değişir (Bölüm 5).
- Genişlik: 400px sabit; yükseklik içerik kadar, `max_height = pencere_yüksekliği - 80px`, taşarsa `nvgScissor` ile kırpılıp dikey scroll uygulanır.
- Zemin: `COLOR_SURFACE_GLASS_STRONG`, `RADIUS_XL`, `SHADOW_LG`.
- Başlık satırı: `"Bildirimler"` (`FONT_H1`) solda; `"Tümünü temizle"` metin-buton sağda (`COLOR_TEXT_SECONDARY`, hover `COLOR_ACCENT`).
- Her bildirim satırı struct'ı:
  ```c
  typedef struct {
      NotificationCategory category; /* CAT_SYSTEM, CAT_FILE, CAT_NETWORK, CAT_MEDIA, CAT_WARNING, CAT_ERROR */
      char title[64];
      char description[160];
      time_t timestamp;
  } NotificationItem;
  ```
  Satır düzeni: sol 36×36px rozet ikonu (kategori-rengi-subtle arka plan üzerinde kategori rengi ikon) → sağda üstte başlık (`FONT_BODY_MEDIUM`) + zaman damgası (`FONT_CAPTION`, `COLOR_TEXT_TERTIARY`, `format_relative_time()` ile `"25 dk önce"` gibi üretilir) → altta açıklama (`FONT_CAPTION`, `COLOR_TEXT_SECONDARY`).
  Satırlar arası `1px` ayraç `COLOR_BORDER_HAIRLINE`, dikey padding `SPACE_4`. Hover: zemin `RGBA8{0,0,0,5}` ile hafif koyulaşır.
- Boş durum (0 bildirim): ortalanmış ikon + `"Yeni bildirim yok"` metni `COLOR_TEXT_TERTIARY`.

### 4.7 Hızlı Ayarlar Bölümü (Quick Settings Section)

Bildirim bölümünün hemen altında, aynı çekmece içinde ikinci bir kart olarak yer alır (genişlik aynı: 400px).

- **Toggle kartları grid'i:** 3 eşit sütun, aralarında `SPACE_3`.
  ```c
  typedef struct {
      const char *label;      /* "Wi-Fi", "Ethernet", "Bluetooth" */
      const char *status_text;/* "Bağlı", "Kapalı" */
      bool  is_active;
      IconId icon;
  } QuickToggle;

  void draw_quick_toggle(NVGcontext *vg, float x, float y, float w, float h, QuickToggle t) {
      RGBA8 bg   = t.is_active ? COLOR_ACCENT_SUBTLE      : (RGBA8){0,0,0,8};
      RGBA8 icon = t.is_active ? COLOR_ACCENT              : COLOR_TEXT_TERTIARY;
      RGBA8 lbl  = t.is_active ? COLOR_TEXT_PRIMARY        : COLOR_TEXT_TERTIARY;
      draw_rounded_rect(vg, x, y, w, h, RADIUS_MD, bg);
      draw_icon(vg, x + SPACE_3, y + SPACE_3, 20, 20, t.icon, icon);
      draw_text(vg, x + SPACE_3, y + SPACE_3 + 28, t.label, FONT_BODY_MEDIUM, lbl);
      draw_text(vg, x + SPACE_3, y + SPACE_3 + 46, t.status_text, FONT_CAPTION, lbl);
  }
  ```
- **Slider'lar (parlaklık, ses):**
  - Track: yükseklik 4px, `RADIUS_FULL`, zemin `RGBA8{0,0,0,20}`; dolgu kısmı `COLOR_ACCENT`, `fill_width = track_width * (value / max_value)`.
  - Topuz: 16px çap dolu daire, beyaz zemin + `SHADOW_SM`; sürükleme sırasında (mouse-down + drag) 20px'e büyür. Fare konumu `mouse_x` track'e projekte edilerek `value` hesaplanır: `value = clamp((mouse_x - track_x) / track_w, 0, 1) * max_value`.
- **2×2 kısayol butonları** (Gece Modu, Rahatsız Etme, Ekran Kilidi, Sistem İzleme): kare kart `RADIUS_LG`, dikey düzen (ikon üstte 24px → etiket `FONT_BODY_MEDIUM` → durum `FONT_CAPTION` `COLOR_TEXT_TERTIARY`), aktifken toggle kartlarıyla aynı vurgu kuralı uygulanır.
- **Alt satır — "Ayarlar":** tam genişlik tıklanabilir satır; sol dişli ikonu + `"Ayarlar"` metni + sağda `>` ok ikonu; tıklanınca `app->open_screen(SCREEN_SETTINGS)` çağrılır.

### 4.8 Dock (Alt Görev Çubuğu)

- Konum: ekranın alt-orta, `dock_y = window_h - 64 - 20` (20px alt boşluk).
- Kapsül: `RADIUS_XL`, zemin `COLOR_SURFACE_GLASS_WEAK`, `SHADOW_LG`, iç padding `SPACE_2` (dikey) `SPACE_3` (yatay).
- İkon tıklama alanı 44×44px, görsel ikon 22px, `RADIUS_MD`.
- İkonlar arası boşluk: `SPACE_2`.
- Aktif/açık uygulama göstergesi: ikonun altında 4px çap dolu daire `COLOR_ACCENT`; birden fazla pencere açıksa daire yerine 10px genişliğinde bir "pill" çizilir.
- Hover davranışı: `target_scale = is_hovered ? 1.12f : 1.0f`; her frame `current_scale = lerp(current_scale, target_scale, 0.25f)` ile yumuşatılır (basit exponential smoothing, ayrı bir easing eğrisi gerekmez).
- İlk sabit ikon (terminal): "aktif/seçili" olduğu için zemin `COLOR_TEXT_PRIMARY` (koyu dolgu), ikon rengi beyaz — diğer dock ikonlarından ayrışır.

---

## 5. Hareket ve Geçiş (Animasyon) Sistemi — Frame Bazlı Hesaplama

CSS `transition` yerine, her animasyonlu değer bir `AnimatedFloat` struct'ında tutulur ve her frame `update()` fonksiyonuyla ilerletilir:

```c
typedef struct {
    float current;
    float target;
    float duration_ms;
    float elapsed_ms;
    float (*ease_fn)(float t); /* 0..1 girdi, 0..1 çıktı */
} AnimatedFloat;

void animated_float_update(AnimatedFloat *a, float delta_ms) {
    if (a->current == a->target) return;
    a->elapsed_ms += delta_ms;
    float t = a->elapsed_ms / a->duration_ms;
    if (t >= 1.0f) { a->current = a->target; return; }
    a->current = lerp(a->current /*start değeri saklanmalı*/, a->target, a->ease_fn(t));
}

/* Easing fonksiyonları — bounce/elastic YOK, hepsi "sakin" */
float ease_out_cubic(float t) { return 1.0f - powf(1.0f - t, 3.0f); }
```

| Sabit | Süre | Ease fonksiyonu | Kullanım |
|---|---|---|---|
| `MOTION_INSTANT_MS` | 80 | `ease_linear` | Hover renk/opaklık mikro değişimleri |
| `MOTION_FAST_MS` | 150 | `ease_out_cubic` | Buton/toggle durum değişimi |
| `MOTION_BASE_MS` | 220 | `ease_out_cubic` | Çekmece açılış/kapanış, kart hover kalkışı |
| `MOTION_SLOW_MS` | 300 | `ease_out_quint` (overshoot'suz) | Modal açılış |

**Kurallar:**
- Hiçbir `duration_ms` 300'ü geçmez.
- Bounce/elastic/spring-overshoot **kullanılmaz** — `ease_fn` seçiminde her zaman monotonik (geri sıçramayan) eğriler tercih edilir.
- Çekmece açılışında `overlay.anim_progress` 0→1 ilerlerken **hem** `alpha = anim_progress` **hem** `y_offset = lerp(-8, 0, anim_progress)` uygulanır (fade + hafif kayma).
- Sayısal değer güncellemeleri (CPU %, ağ hızı) anlık sıçramaz; `AnimatedFloat` ile 400ms'de eskiden yeniye interpole edilir (sayaç hissi).
- Ana döngü iskeleti:
  ```c
  Uint32 last_ticks = SDL_GetTicks();
  while (running) {
      Uint32 now = SDL_GetTicks();
      float delta_ms = (float)(now - last_ticks);
      last_ticks = now;

      poll_events(&app);           /* mouse/klavye */
      update_animations(&app, delta_ms);
      update_widget_data(&app, delta_ms); /* CPU/RAM örnekleme, saat vb. */
      render_frame(&app, vg);
      SDL_GL_SwapWindow(window);
  }
  ```

---

## 6. İkonografi Kuralları

- Stil: **outline/stroke**, dolgu yok (filled ikon yalnız "aktif" durumda opsiyonel).
- Stroke kalınlığı: sabit **1.75px**.
- Uç/köşe stili: `line_cap = ROUND`, `line_join = ROUND` (NanoVG: `nvgLineCap(vg, NVG_ROUND)`).
- İkon kaynağı: SVG dosyaları derleme zamanında **statik path verisine** (nokta dizileri) dönüştürülüp C dizileri olarak gömülür (örn. bir Python/Node script ile `.svg` → `icon_data.c` üretimi), çalışma zamanında SVG parse edilmez (performans).
- Boyut ölçeği: 16px (rozet içi) / 20px (toggle/menü) / 24px (top bar) / 28px+ (widget içi büyük ikon).
- Renk: ikon her zaman çağrıldığı bağlamın metin rengini parametre olarak alır (`draw_icon(vg, x, y, size, size, icon_id, color)`) — sabit renk gömülmez.

---

## 7. Bildirim Kategori → Renk Eşleme Tablosu

```c
RGBA8 category_bg_color(NotificationCategory c) {
    switch (c) {
        case CAT_SYSTEM:  case CAT_NETWORK: return COLOR_INFO_SUBTLE;
        case CAT_FILE:                      return COLOR_SUCCESS_SUBTLE;
        case CAT_WARNING:                   return COLOR_WARNING_SUBTLE;
        case CAT_ERROR:                     return COLOR_DANGER_SUBTLE;
        case CAT_MEDIA:                      return (RGBA8){139, 92, 246, 31}; /* ~%12 opak mor */
    }
}
RGBA8 category_icon_color(NotificationCategory c) {
    switch (c) {
        case CAT_SYSTEM:  case CAT_NETWORK: return COLOR_INFO;
        case CAT_FILE:                      return COLOR_SUCCESS;
        case CAT_WARNING:                   return COLOR_WARNING;
        case CAT_ERROR:                     return COLOR_DANGER;
        case CAT_MEDIA:                      return (RGBA8){139, 92, 246, 255};
    }
}
```

---

## 8. Erişilebilirlik Gereksinimleri

1. **Kontrast:** `COLOR_TEXT_PRIMARY`, açık zemin üzerinde min. **4.5:1** kontrast oranı sağlamalı. Blur nedeniyle arkadaki içerik zemin rengini kaydırabileceğinden, kontrast hesaplaması `COLOR_SURFACE_GLASS` düz rengi üzerinden (blur öncesi, worst-case) yapılmalıdır.
2. **Tıklama hedefleri:** Minimum **40×40px** — görsel ikon küçük olsa bile `hit_test()` alanı bu ölçüde tutulur.
3. **Klavye navigasyonu:** `AppState.focused_widget_id` ile `Tab`/`Shift+Tab` arasında dolaşılabilmeli; odak sırası görsel sıraya (yukarıdan aşağı, soldan sağa) uymalı; odaklı öğe her zaman `SHADOW_FOCUS_RING` ile çizilmeli.
4. **Renk bağımsızlığı:** Durum bilgisi (Bağlı/Kapalı, aktif/pasif) her zaman **metin veya ikon farkıyla** desteklenir; asla yalnızca renkle iletilmez.
5. **Hareket azaltma:** İşletim sistemi/kullanıcı ayarında "azaltılmış hareket" açıksa, `MOTION_*_MS` sabitleri koddan `0`'a set edilebilecek bir `g_reduced_motion` global bayrağıyla çarpılır: `effective_duration = base_duration * (g_reduced_motion ? 0.0f : 1.0f)`.
6. **Ekran okuyucu / erişilebilirlik ağacı:** C tarafında native bir "screen reader" entegrasyonu yoksa, en azından platformun erişilebilirlik API'sine (Linux: AT-SPI, Windows: UIA, macOS: NSAccessibility) her widget için `role`, `label`, `value` bilgisini expose eden ayrı bir `a11y_tree.c` katmanı planlanmalı; CPU/RAM ring'leri için `value = "CPU kullanımı yüzde 23"` gibi bir metin karşılığı üretilmeli.

---

## 9. Bileşen Durum Matrisi (Hızlı Referans)

| Bileşen | Default | Hover | Active/Selected | Disabled |
|---|---|---|---|---|
| Kart | `SHADOW_MD` | `SHADOW_LG` + `y - 1px` | — | `alpha *= 0.6` |
| Toggle (Quick Settings) | gri zemin, gri ikon | zemin alpha +5 | `COLOR_ACCENT_SUBTLE` zemin, accent ikon | `alpha *= 0.4`, `hit_test` devre dışı |
| Takvim günü | şeffaf | `COLOR_ACCENT_SUBTLE` dolu daire | `COLOR_ACCENT` dolu daire, beyaz metin | — |
| Slider topuzu | 16px | 18px + `SHADOW_SM` | sürüklenirken 20px + `SHADOW_MD` | gri, `is_draggable=false` |
| Dock ikonu | `scale=1.0` | `scale→1.06–1.12` (smoothing ile) | altında nokta göstergesi | — |
| Metin-buton ("Tümünü temizle") | `COLOR_TEXT_SECONDARY` | `COLOR_ACCENT` | — | `COLOR_TEXT_TERTIARY` |

---

## 10. Proje Dosya Organizasyonu (C)

```
/rix-os-gui
  ├── include/
  │   ├── design_tokens.h      (Bölüm 2 — renk/spacing/radius/font #define ve struct'lar)
  │   ├── motion.h             (Bölüm 5 — AnimatedFloat, ease fonksiyonları, süre sabitleri)
  │   └── icons.h              (Bölüm 6 — IconId enum'u, gömülü path verisi bildirimleri)
  ├── src/
  │   ├── render/
  │   │   ├── draw_primitives.c   (draw_rounded_rect, draw_arc, draw_glass_surface, draw_drop_shadow)
  │   │   ├── blur.c               (Bölüm 2.6 — box blur geçişleri)
  │   │   └── text.c               (stb_truetype/Cairo font render sarmalayıcıları)
  │   ├── widgets/
  │   │   ├── clock_widget.c
  │   │   ├── calendar_widget.c
  │   │   ├── sysmonitor_widget.c
  │   │   ├── topbar.c
  │   │   ├── notification_drawer.c
  │   │   ├── quick_settings.c
  │   │   └── dock.c
  │   ├── app_state.c              (AppState struct, widget listesi, overlay durumu)
  │   ├── input.c                  (SDL olay kuyruğu → hit-test → widget event dağıtımı)
  │   └── main.c                   (pencere oluşturma, ana döngü — Bölüm 5 sonundaki iskelet)
  └── assets/
      ├── fonts/  (Inter-*.ttf)
      └── icons/  (kaynak .svg dosyaları — derleme öncesi, runtime'da kullanılmaz)
```

**Kural:** Hiçbir `.c` dosyasında ham hex renk veya sihirli piksel sayısı yazılmaz — her değer `design_tokens.h`'daki `#define`/struct üzerinden çağrılır. Bu, tema değişimini (light/dark) ve gelecekteki marka güncellemelerini tek noktadan (Bölüm 12'deki gibi ikinci bir token seti derleyerek) yönetilebilir kılar.

---

## 11. Uçtan Uca Örnek — CPU Widget'ının Tam C Karşılığı

```c
typedef struct {
    float cpu_percent, ram_percent, disk_percent;
    AnimatedFloat cpu_display; /* sayaç hissi için yumuşatılmış görüntü değeri */
    float samples[60];         /* ağ sparkline ring buffer */
    int   sample_head;
} SysMonitorState;

void sysmonitor_update(SysMonitorState *s, float real_cpu_pct, float delta_ms) {
    s->cpu_percent = real_cpu_pct;
    if (s->cpu_display.target != real_cpu_pct) {
        s->cpu_display.target = real_cpu_pct;
        s->cpu_display.elapsed_ms = 0;
        s->cpu_display.duration_ms = 400.0f;
    }
    animated_float_update(&s->cpu_display, delta_ms);
}

void sysmonitor_draw(NVGcontext *vg, float x, float y, SysMonitorState *s) {
    float cx = x + 32, cy = y + 32; /* 64px ring'in merkezi */
    RingLevel level = ring_level_for_percent(s->cpu_display.current);
    draw_ring_gauge(vg, cx, cy, 28.0f, s->cpu_display.current, level);

    char pct_text[8];
    snprintf(pct_text, sizeof(pct_text), "%d%%", (int)roundf(s->cpu_display.current));
    draw_text_centered(vg, cx, cy, pct_text, FONT_BODY_MEDIUM, COLOR_TEXT_PRIMARY);
    draw_text_centered(vg, cx, cy + 18, "CPU", FONT_MICRO, COLOR_TEXT_SECONDARY);
}
```

Diğer tüm bileşenler bu titizlikte (state struct + `update()` + `draw()` ayrımı) türetilmelidir.

---

## 12. Koyu Tema (Dark Mode) Token Karşılıkları

```c
#ifdef THEME_DARK
  #undef COLOR_WALLPAPER_START
  #undef COLOR_WALLPAPER_END
  #define COLOR_WALLPAPER_START (RGBA8){0x16, 0x17, 0x1A, 255}
  #define COLOR_WALLPAPER_END   (RGBA8){0x0D, 0x0E, 0x10, 255}

  #undef COLOR_SURFACE_GLASS
  #undef COLOR_SURFACE_GLASS_STRONG
  #undef COLOR_SURFACE_GLASS_WEAK
  #define COLOR_SURFACE_GLASS        (RGBA8){30, 31, 34, 184}
  #define COLOR_SURFACE_GLASS_STRONG (RGBA8){30, 31, 34, 224}
  #define COLOR_SURFACE_GLASS_WEAK   (RGBA8){30, 31, 34, 140}

  #undef COLOR_TEXT_PRIMARY
  #undef COLOR_TEXT_SECONDARY
  #undef COLOR_TEXT_TERTIARY
  #define COLOR_TEXT_PRIMARY   (RGBA8){0xF2, 0xF3, 0xF5, 255}
  #define COLOR_TEXT_SECONDARY (RGBA8){0x9A, 0x9D, 0xA3, 255}
  #define COLOR_TEXT_TERTIARY  (RGBA8){0x6B, 0x6E, 0x74, 255}

  #undef COLOR_ACCENT
  #undef COLOR_ACCENT_SUBTLE
  #define COLOR_ACCENT        (RGBA8){0x5B, 0x8D, 0xEF, 255}
  #define COLOR_ACCENT_SUBTLE (RGBA8){0x5B, 0x8D, 0xEF, 41} /* ~%16 opak */

  #undef COLOR_BORDER_HAIRLINE
  #undef COLOR_BORDER_STRONG
  #define COLOR_BORDER_HAIRLINE (RGBA8){255, 255, 255, 20}
  #define COLOR_BORDER_STRONG   (RGBA8){255, 255, 255, 36}
#endif
```

**Not:** Durum renkleri (success/warning/danger) koyu temada aynı hue'yu korur; yalnızca `*_SUBTLE` arka planların alpha değeri düşürülür (`~%14–18`) ki koyu glass zemin üzerinde patlamasın. Derleme zamanında `THEME_DARK` tanımlanarak veya çalışma zamanında iki token tablosu arasında `active_theme` işaretçisi değiştirilerek uygulanabilir.

---

## 13. Yapılmaması Gerekenler (Anti-Patterns)

- ❌ Sert köşe (`radius = 0`) kullanma.
- ❌ Düz/opak beyaz panel çizme — her yüzey mutlaka blur + saydamlık taşımalı (Bölüm 2.6).
- ❌ `COLOR_ACCENT` dışında ikinci bir "marka rengi" ekleme.
- ❌ Gölgeyi yüksek alpha ile koyulaştırıp sert kenar yaratma — gölgeler her zaman düşük opaklık, geniş `blur_px`.
- ❌ Serif font veya kalın/dekoratif font kullanma.
- ❌ `ease_fn` olarak bounce/elastic/overshoot eğrisi seçme.
- ❌ Bir `.c` dosyasında hardcoded hex renk veya piksel sayısı yazma — her zaman `design_tokens.h` referansı.
- ❌ Runtime'da SVG parse etme — ikonlar derleme öncesi statik veriye dönüştürülmeli (performans).
- ❌ Her frame'de blur'u sıfırdan hesaplama — statik arka plan için cache kullan (Bölüm 2.6).
- ❌ Durum bilgisini yalnızca renkle iletme (renk körlüğü ihlali).

---

## 14. Gelişmiş Token Mimarisi — Çalışma Zamanında Tema Değiştirme

Bölüm 2 ve 12'deki `#define`/`#undef` yaklaşımı **derleme zamanlı** bir tema seçimidir (build'i `THEME_DARK` ile yeniden derlemek gerekir). Gerçek bir işletim sisteminde kullanıcı ayarlar menüsünden anlık olarak açık/koyu temayı değiştirebilmeli. Bunun için token'lar **çalışma zamanı struct'ına** taşınmalı:

```c
typedef struct {
    RGBA8 wallpaper_start, wallpaper_end;
    RGBA8 surface_glass, surface_glass_strong, surface_glass_weak;
    RGBA8 text_primary, text_secondary, text_tertiary, text_on_accent;
    RGBA8 accent, accent_hover, accent_active, accent_subtle;
    RGBA8 success, success_subtle, warning, warning_subtle, danger, danger_subtle;
    RGBA8 border_hairline, border_strong;
    ShadowSpec shadow_sm, shadow_md, shadow_lg, shadow_focus_ring;
} ThemeTokens;

extern const ThemeTokens THEME_LIGHT;  /* Bölüm 2.1 değerleriyle sabit initialize edilir */
extern const ThemeTokens THEME_DARK;   /* Bölüm 12 değerleriyle sabit initialize edilir */

typedef struct {
    const ThemeTokens *active;   /* &THEME_LIGHT veya &THEME_DARK */
    AnimatedFloat      theme_blend; /* 0=light, 1=dark; geçiş sırasında ara renk için */
} ThemeManager;

/* Tüm çizim kodu artık COLOR_ACCENT yerine theme->active->accent okur. */
void theme_manager_set(ThemeManager *tm, const ThemeTokens *target) {
    tm->theme_blend.target = (target == &THEME_DARK) ? 1.0f : 0.0f;
    tm->theme_blend.duration_ms = MOTION_BASE_MS;
    tm->theme_blend.elapsed_ms  = 0.0f;
}

/* Geçiş anında iki temayı kanal-kanal blend eder (renk çatlaması olmadan yumuşak geçiş). */
RGBA8 rgba_lerp(RGBA8 a, RGBA8 b, float t) {
    return (RGBA8){
        (uint8_t)lerp(a.r, b.r, t), (uint8_t)lerp(a.g, b.g, t),
        (uint8_t)lerp(a.b, b.b, t), (uint8_t)lerp(a.a, b.a, t)
    };
}
```

**Kural:** Tüm widget `draw()` fonksiyonları renk sabitlerini doğrudan `COLOR_ACCENT` makrosundan değil, parametre olarak geçirilen `const ThemeTokens *theme` işaretçisinden okumalıdır. `#define`'lar yalnızca `THEME_LIGHT`/`THEME_DARK` sabit struct'larını **initialize etmek için** bir kerelik kullanılır; render kodunun geri kalanı makrolara değil struct alanlarına bağımlı olur. Bu, hem runtime tema değişimini hem de gelecekte kullanıcı-tanımlı üçüncü bir temayı (örn. "Yüksek Kontrast") tek bir yeni `ThemeTokens` sabiti eklemekle mümkün kılar.

**Sistem teması takibi:** İşletim sistemi seviyesinde "gündüz/gece otomatik geçiş" özelliği isteniyorsa, `ThemeManager` içine `bool auto_mode` ve `time_t sunrise, sunset` alanları eklenip ana döngüde saat kontrolü ile `theme_manager_set()` tetiklenebilir.

---

## 15. Girdi (Input) Sistemi — Olay Kuyruğu ve Hit-Testing

```c
typedef enum { EVT_MOUSE_MOVE, EVT_MOUSE_DOWN, EVT_MOUSE_UP, EVT_MOUSE_WHEEL,
               EVT_KEY_DOWN, EVT_KEY_UP, EVT_TEXT_INPUT, EVT_WINDOW_RESIZE } EventType;

typedef struct {
    EventType type;
    float x, y;            /* mouse olayları için */
    float wheel_delta;
    SDL_Keycode key;
    char text[32];          /* EVT_TEXT_INPUT için UTF-8 */
    uint32_t modifiers;      /* KMOD_SHIFT, KMOD_CTRL vb. bit-mask */
} UiEvent;
```

**Hit-testing sırası — çizim sırasının TERSİ:** Bir mouse tıklaması geldiğinde, en üstteki katmandan (Bölüm 2.5'teki `LAYER_CONTEXT_MENU`) başlayıp aşağı doğru (`LAYER_WALLPAPER`'a kadar) taranır; ilk eşleşen widget olayı "yakalar" (`event.consumed = true`) ve alt katmanlara iletilmez. Bu, çekmece açıkken arkasındaki widget'lara tıklanamamasını garanti eder.

```c
bool dispatch_event(AppState *app, UiEvent *evt) {
    if (app->context_menu.is_open && hit_test_and_handle(&app->context_menu, evt)) return true;
    if (app->modal.is_open        && hit_test_and_handle(&app->modal, evt))        return true;
    if (app->overlay.is_open      && hit_test_and_handle(&app->overlay, evt))      return true;
    if (hit_test_and_handle(&app->topbar, evt)) return true;
    if (hit_test_and_handle(&app->dock, evt))   return true;
    for (int i = app->widget_count - 1; i >= 0; i--) /* en son eklenen widget en üstte kabul edilir */
        if (hit_test_and_handle(&app->widgets[i], evt)) return true;
    /* Hiçbiri yakalamadıysa: masaüstü boş alana tıklandı → çekmeceyi kapat, seçimi temizle */
    if (evt->type == EVT_MOUSE_DOWN && app->overlay.is_open) overlay_close(&app->overlay);
    return false;
}
```

**Widget sürükleme (drag) durumu:** Her widget `WidgetInstance` struct'ına `bool is_dragging` ve `float drag_offset_x/y` eklenir. `EVT_MOUSE_DOWN` widget başlık alanına denk gelirse `is_dragging = true`; `EVT_MOUSE_MOVE` sırasında `is_dragging` ise widget konumu güncellenir; `EVT_MOUSE_UP` ile sürükleme biter ve yeni konum `settings.json`'a kaydedilir (Bölüm 17).

**Çift tıklama / uzun basma:** `last_click_time` ve `last_click_pos` global olarak tutulur; iki tıklama arasında `<300ms` ve pozisyon farkı `<5px` ise `EVT_DOUBLE_CLICK` sentetik olayı üretilir. Uzun basma (touch/trackpad için) `>500ms` basılı tutma sonrası context menu açar.

---

## 16. Pencere Yönetimi ve Çoklu Monitör Desteği

- `SDL_GetNumVideoDisplays()` ile bağlı monitör sayısı alınır; her monitör için ayrı bir `SDL_Window` + kendi `NVGcontext`'i oluşturulur (her ekranın kendi dock'u ve top bar'ı olabilir — macOS modeli) **veya** tek bir "birincil" ekranda dock/top bar gösterilip diğerleri yalnız masaüstü uzantısı olabilir (Windows modeli). Bu karar proje başında netleştirilmeli; öneri: **birincil ekran modeli**, çünkü tutarlılığı daha kolay korur.
- Widget konumları mutlak piksel yerine **monitör-göreli normalize koordinat** (`0.0–1.0`) olarak saklanmalı ki farklı çözünürlükte monitöre taşındığında konum mantıklı kalsın: `abs_x = monitor_x + norm_x * monitor_w`.
- Monitör bağlantısı kesilirse (`SDL_DISPLAYEVENT_DISCONNECTED`), o ekrandaki widget'lar birincil ekrana otomatik taşınır (kaybolmamalı).
- DPI ölçekleme: `SDL_GetDisplayDPI()` okunup `scale_factor = dpi / 96.0f` hesaplanır; tüm `SPACE_*`, `RADIUS_*`, font `size_px` değerleri çizim anında bu faktörle çarpılır (`effective_px = base_px * scale_factor`) — Retina/4K ekranlarda arayüzün minicik görünmesini engeller.

---

## 17. Ayarlar Kalıcılığı (Persistence)

Kullanıcı tercihleri (tema, widget konumları, dock'a sabitlenen uygulamalar, ses/parlaklık) uygulama kapanınca kaybolmamalı.

- **Format:** İnsan-okunabilir, bağımlılıksız bir seri hale getirme için basit bir **INI benzeri key-value** format veya tek-header bir JSON kütüphanesi (`cJSON`) önerilir.
- **Konum:** `~/.config/rix-os/settings.json` (XDG Base Directory uyumlu).
- **Şema örneği:**
  ```json
  {
    "theme": "dark",
    "widgets": [
      { "type": "clock",     "x": 0.02, "y": 0.06, "monitor": 0 },
      { "type": "calendar",  "x": 0.02, "y": 0.26, "monitor": 0 },
      { "type": "sysmonitor","x": 0.02, "y": 0.55, "monitor": 0 }
    ],
    "dock_pinned_apps": ["terminal", "files", "browser", "code", "music", "settings"],
    "brightness": 0.82,
    "volume": 0.45,
    "reduced_motion": false
  }
  ```
- **Yazma stratejisi:** Her ayar değişikliğinde diske hemen yazmak yerine (I/O maliyeti), değişiklik bir `settings_dirty = true` bayrağı tetikler; ana döngüde en fazla 1 saniyede bir (`debounce`) diske flush edilir.
- **Bozuk dosya toleransı:** `settings.json` parse edilemezse (bozuk/eksik), sessizce göz ardı edilip derleme-zamanı varsayılanlarla (Bölüm 2.1) başlanır — asla crash edilmez.

---

## 18. Ek Widget'lar

Referans görüntüde olmayan ama bir OS'te beklenen widget'lar için aynı `state + update() + draw()` şablonu uygulanır:

### 18.1 Hava Durumu Widget'ı
- Anatomi: sol büyük sıcaklık (`FONT_DISPLAY` boyutunda ama 32px'e küçültülmüş varyant) + hava durumu ikonu; altta şehir adı (`FONT_BODY_MEDIUM`) + düşük/yüksek sıcaklık (`FONT_CAPTION`).
- Veri kaynağı ağ üzerinden geldiği için widget bir `WEATHER_STATE_LOADING / LOADED / ERROR` enum'u taşır; `LOADING` durumunda ring-spinner (`draw_arc` ile dönen kısmi yay, `COLOR_ACCENT`) gösterilir.

### 18.2 Medya Oynatıcı Widget'ı
- Albüm kapağı (kare, `RADIUS_MD`) solda, sağda şarkı adı (`FONT_BODY_MEDIUM`, taşarsa `marquee` kayan yazı efekti) + sanatçı (`FONT_CAPTION`, `COLOR_TEXT_SECONDARY`).
- Alt kısımda ilerleme çubuğu (Bölüm 4.7'deki slider ile aynı token'lar) + oynat/duraklat/ileri/geri ikon butonları (36×36px tıklama alanı).

### 18.3 Pil (Battery) Göstergesi — Top Bar İkonu
- Dolu oranına göre ikon gövdesi doldurulur (`draw_rounded_rect` iç dolgu, dış çerçeve sabit); `<%20` ise dolgu `COLOR_DANGER`, `<%50` ise `COLOR_WARNING`, aksi halde `COLOR_TEXT_PRIMARY` (nötr, alarm durumu yok).
- Şarj oluyorsa üzerine küçük bir yıldırım ikonu bindirilir (`COLOR_ACCENT`).

### 18.4 Hızlı Arama (Spotlight benzeri)
- Genel kısayol (`Cmd/Super + Space`) ile açılan, ekran ortasında `LAYER_MODAL` katmanında beliren tek satırlık arama kutusu; `RADIUS_XL`, `SHADOW_LG`, genişlik 560px.
- Yazı yazıldıkça (`EVT_TEXT_INPUT`) altında canlı sonuç listesi açılır (uygulamalar, dosyalar, ayarlar); her sonuç satırı bildirim satırıyla aynı anatomiyi paylaşır (ikon + başlık + alt açıklama).
- `Esc` tuşu her zaman kapatır; ok tuşları sonuçlar arasında gezinir, `Enter` seçileni açar.

---

## 19. Sağ Tık Menüsü (Context Menu)

```c
typedef struct {
    const char *label;
    IconId icon;          /* opsiyonel, ICON_NONE olabilir */
    bool   is_separator;  /* true ise sadece ince bir ayraç çizilir, label yok sayılır */
    bool   is_disabled;
    void (*on_select)(void *ctx);
} ContextMenuItem;
```

- Zemin `COLOR_SURFACE_GLASS_STRONG`, `RADIUS_MD`, `SHADOW_LG`; her satır 36px yükseklik, sol `SPACE_3` padding'de ikon (varsa) + `FONT_BODY`.
- Açılış konumu: tıklanan `(x, y)` noktası, ama ekran kenarına taşarsa otomatik içe katlanır (`clamp(x, 0, screen_w - menu_w)`).
- Hover'da satır zemin `RGBA8{0,0,0,6}`; `is_disabled` ise metin `COLOR_TEXT_TERTIARY` ve `hit_test` pas geçilir.
- Açılış animasyonu `MOTION_FAST_MS` (150ms) ile `scale 0.96→1.0` + `opacity 0→1`.
- Herhangi bir yere (menü dışına) tıklanması veya `Esc` menüyü kapatır.

---

## 20. Klavye Kısayolları Tablosu

| Kısayol | Eylem |
|---|---|
| `Super/Cmd + Space` | Hızlı arama aç (Bölüm 18.4) |
| `Super/Cmd + ,` | Ayarlar ekranını aç |
| `Super/Cmd + L` | Ekranı kilitle |
| `Super/Cmd + Shift + S` | Ekran görüntüsü al |
| `Alt/Cmd + Tab` | Açık uygulamalar arası geçiş (kendi overlay katmanı, `LAYER_MODAL`) |
| `Esc` | En üstteki geçici katmanı kapat (context menu → modal → overlay sırasıyla) |
| `Tab` / `Shift+Tab` | Odaklı widget'ı ileri/geri değiştir |
| `Enter` / `Space` | Odaklı öğeyi etkinleştir |

**Kural:** Kısayollar merkezi bir `keybinding_table.c` içinde tanımlanır, widget kodlarına gömülmez — böylece kullanıcı ileride kısayolları özelleştirebilir (Bölüm 17'deki settings şemasına `"keybindings": {...}` eklenerek).

---

## 21. Performans Bütçesi

| Metrik | Hedef |
|---|---|
| Frame süresi | ≤ 16.6ms (60 FPS) boşta; etkileşim sırasında ≤ 8.3ms (120Hz ekran desteği) |
| Boşta CPU kullanımı (GUI süreci) | < %1 (widget'lar 1 saniyede bir güncellenir, her frame değil — bkz. aşağı) |
| Bellek (RSS) | < 80MB tipik masaüstü senaryosunda (birkaç widget + dock + boş çekmece) |
| Blur hesaplama maliyeti | Yalnız panel açılış/kapanışında, statik içerik için cache'lenmiş (Bölüm 2.6) |
| Soğuk başlatma (cold start) | < 300ms pencere görünür oluncaya kadar |

**Boşta-iken-çizme stratejisi (idle rendering):** Hiçbir animasyon aktif değilken (`no AnimatedFloat is mid-transition`) ve mouse hareket etmiyorsa, ana döngü her frame yeniden çizmek yerine `SDL_WaitEventTimeout()` ile bir sonraki olayı (veya saniyelik saat tick'ini) bekler — sürekli 60 FPS "boş" render CPU'yu gereksiz yakar, dizüstü pil ömrünü kısaltır. Yalnızca saat saniye kolu hareket ederken (widget'ta saniye kolu görünürse) sürekli render moduna geçilir.

---

## 22. Derleme Sistemi (CMake İskeleti)

```cmake
cmake_minimum_required(VERSION 3.16)
project(rix_os_gui C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
# NanoVG, stb_truetype, cJSON: alt-modül (git submodule) veya vendored /third_party altında

add_executable(rix_os_gui
    src/main.c
    src/app_state.c
    src/input.c
    src/render/draw_primitives.c
    src/render/blur.c
    src/render/text.c
    src/widgets/clock_widget.c
    src/widgets/calendar_widget.c
    src/widgets/sysmonitor_widget.c
    src/widgets/topbar.c
    src/widgets/notification_drawer.c
    src/widgets/quick_settings.c
    src/widgets/dock.c
)

target_include_directories(rix_os_gui PRIVATE include third_party/nanovg/src third_party/stb)
target_link_libraries(rix_os_gui PRIVATE SDL2::SDL2 m)

# Uyarıları hataya çevir — token/spacing kurallarının ihlalini (örn. sihirli sayı) erken yakalamak için
target_compile_options(rix_os_gui PRIVATE -Wall -Wextra -Wpedantic)
```

**Statik analiz kuralı:** CI pipeline'ına bir `grep`/`clang-tidy` kontrolü eklenmesi önerilir: kaynak dosyalarda `#define SPACE_`/`RADIUS_`/`COLOR_` dışında ham `0x` hex renk veya `[0-9]+\.?[0-9]*f?px` benzeri sihirli sayı deseni aranıp build'i kırması (Bölüm 13'teki "hardcoded değer yazma" kuralının otomatik denetimi).

---

## 23. Hata Yönetimi ve Loglama

- GUI süreci **asla** kullanıcıya çökmüş bir pencereyle karşılık vermemeli. Kritik olmayan hatalar (örn. hava durumu API'sinden yanıt gelmemesi, bir ikon dosyasının eksik olması) widget'ı `ERROR` durumuna düşürüp yerine küçük bir uyarı ikonu + "Yüklenemedi" metni çizer, tüm uygulamayı düşürmez.
- Loglama seviyeleri: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`; varsayılan build'de yalnız `WARN`+ konsola/dosyaya (`~/.local/state/rix-os/gui.log`) yazılır.
- `assert()` yalnızca geliştirme build'inde (`NDEBUG` tanımlı değilken) aktif; production build'de mantık hatası bir log satırı + güvenli varsayılana geri dönüş (`fallback`) ile ele alınır (örn. bilinmeyen `NotificationCategory` gelirse `CAT_SYSTEM` varsayılır).
- Bellek: her widget kendi `state`'ini `malloc` eder, `widget_destroy()` ile `free` edilir; büyük/uzun ömürlü uygulamalarda bir **arena allocator** (tek seferde büyük blok ayırıp widget'lar arasında paylaştırma) önerilir, sık `malloc`/`free` çağrısı frame-time dalgalanmasına (jitter) yol açabilir.

---

## 24. Test / QA Kontrol Listesi

- [ ] Her widget, 0 veri / normal veri / uç değer (örn. %100 CPU, 999+ bildirim) senaryolarında taşmadan çizilebiliyor mu?
- [ ] `reduced_motion` açıkken tüm `AnimatedFloat` geçişleri anında (0ms) tamamlanıyor mu?
- [ ] Tema `light ↔ dark` geçişi sırasında hiçbir renk "patlamıyor" (ör. subtle arka plan glass üzerinde opak görünmüyor) mu?
- [ ] Pencere yeniden boyutlandırıldığında (Bölüm 3.2 kırılım noktaları) widget'lar taşmıyor, dock ortalı kalıyor mu?
- [ ] Klavye ile (mouse hiç kullanmadan) tüm interaktif öğelere ulaşılabiliyor mu (Bölüm 8.3)?
- [ ] Çoklu monitör bağlantısı kesilince widget kaybı yaşanmıyor mu (Bölüm 16)?
- [ ] `settings.json` elle bozulduğunda uygulama crash etmeden varsayılanlarla açılıyor mu (Bölüm 17)?
- [ ] Boşta CPU kullanımı hedef değerin (Bölüm 21) altında mı (profiler ile ölçülmeli)?
- [ ] Sağ tık menüsü ekran kenarına yakın açıldığında taşmadan içe katlanıyor mu (Bölüm 19)?

---

## 25. Lokalizasyon (i18n) Notu

- Tüm sabit metinler (`"Bildirimler"`, `"Tümünü temizle"`, gün/ay isimleri) kaynak koda gömülmez; bir `strings_tr.json` / `strings_en.json` çift dilli tablo üzerinden `i18n_get("notifications.title")` gibi bir fonksiyonla çekilir.
- Tarih/saat formatlama `strftime` yerine locale-aware bir katman (`setlocale(LC_TIME, ...)`) ile yapılmalı; 12/24 saat formatı kullanıcı ayarına bağlı olmalı.
- Sağdan-sola (RTL) dil desteği ileride gerekirse, layout hesaplamalarında `x` koordinatı yerine mantıksal `leading/trailing` kavramı kullanılması (şimdiden) önerilir — bu, Bölüm 3–4'teki tüm sabit "sol/sağ" ifadelerinin ileride bir `bool is_rtl` bayrağıyla ayna simetriğe çevrilebilmesini kolaylaştırır.

---

*Bu doküman, RIX OS'in görsel kimliğini **C dilinde, immediate-mode bir render mimarisi** varsayarak tanımlar. SDL2+NanoVG yerine başka bir C grafik yığını (örn. raylib, doğrudan framebuffer + software rasterizer) kullanılırsa, fonksiyon imzaları değişir ama token değerleri (Bölüm 2), spacing/radius skalası ve durum kuralları (Bölüm 9) birebir aynı kalmalıdır. Yeni bir bileşen eklenmesi gerektiğinde önce Bölüm 0'daki teknoloji kararına sadık kalınmalı, ardından en yakın mevcut bileşen (Bölüm 4) `state struct + update() + draw()` şablonu örnek alınarak türetilmelidir.*
