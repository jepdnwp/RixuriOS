# RixuriOS GUI Implementation Manual

Bu belge **GUI'nin nasıl yapılacağını** anlatır. `GUI_ROADMAP.md` neyin hangi sırayla yapılacağını; `GUI_DESIGN.md` ise son ürünün nasıl görünmesi ve davranması gerektiğini tanımlar.

## 1. Temel mimari

GUI kernel'e gömülmeyecek. Ana sınır:

```text
UEFI / Kernel
      ↓
GPU + Display + Input drivers
      ↓
RixuriOS system APIs
      ↓
IPC / shared memory / synchronization
      ↓
rix-session
      ├── rix-compositor
      ├── rix-input
      ├── rix-notify
      ├── rix-settings
      ├── rix-launcher
      └── applications
```

Her servis ayrı process olabilir. Bir servis çöktüğünde kernel ve diğer servisler çalışmaya devam etmelidir.

## 2. Kod organizasyonu

Önerilen userspace düzeni:

```text
user/
  gui/
    core/          # GUI object model, event loop, IPC
    compositor/    # scene graph and frame composition
    window/        # window protocol and management
    input/         # keyboard/pointer event translation
    text/          # UTF-8, shaping, font handling
    render/        # software renderer + GPU/Vulkan backend
    toolkit/       # widgets and Rix Design System
    shell/         # panel, dock, launcher, widgets
    services/      # notifications, settings, power, network UI
    apps/          # first-party applications
    theme/         # themes/material system
    accessibility/# accessibility tree and adapters
    tests/         # GUI protocol/render/input tests
```

Kernel tarafında yalnızca gerekli device/display/input/IPC primitives bulunur. GUI policy userspace'te kalır.

## 3. Önce contract, sonra renderer

İlk kod renderer olmamalı. Önce aşağıdaki contract'ları yaz:

1. display API;
2. surface/buffer API;
3. input event ABI;
4. window protocol;
5. compositor protocol;
6. shared-memory buffer protocol;
7. synchronization/fence protocol;
8. application manifest;
9. notification protocol;
10. settings/service protocol;
11. accessibility tree protocol.

Her contract için:

- version;
- message layout;
- ownership;
- lifetime;
- permissions;
- error codes;
- cancellation;
- timeout;
- backwards compatibility

tanımlanır.

## 4. Display backend'i yap

İlk gerçek grafik yolu:

```text
physical/QEMU display
        ↓
display discovery
        ↓
scanout buffer
        ↓
renderer
        ↓
present
```

İlk milestone yalnızca tek renk/dikdörtgen/diagnostic frame göstermek olabilir. Ancak bu frame gerçekten display'e sunulmalıdır.

Sonra:

- double buffering;
- damage rectangles;
- vsync/vblank timing;
- multiple outputs;
- scale/DPI;
- hotplug

eklenir.

## 5. Surface modelini oluştur

Her uygulama doğrudan ekrana çizmez. Uygulama bir surface üretir:

```text
Application
   ↓
Surface
   ↓
Shared buffer
   ↓
Compositor
   ↓
Scene
   ↓
GPU/software renderer
   ↓
Scanout
```

Buffer yaşam döngüsü açık olmalı:

`ALLOCATED → ATTACHED → DRAWING → READY → COMPOSING → PRESENTED → RELEASED`.

CPU ve GPU aynı buffer'a aynı anda kontrolsüz yazmamalıdır.

## 6. Event loop

GUI'nin merkezinde deterministic event loop bulunmalı:

```text
input events
system events
IPC messages
timers
frame callbacks
        ↓
   event queue
        ↓
   dispatch
        ↓
application/compositor state
        ↓
   invalidation
        ↓
   render
```

Event queue taşarsa sınırsız bellek tüketmek yerine backpressure veya kontrollü coalescing uygulanmalıdır. Pointer hareketleri gibi birleştirilebilir event'ler birleştirilebilir; click/key events kaybedilmemelidir.

## 7. Window server

Window object için minimum state:

```text
id
owner process
surface
position
size
scale
state
visibility
focus
z-order
workspace
parent/transient owner
permissions
```

Window operations:

`create → configure → map → focus → resize/move → minimize/maximize → unmap → destroy`.

İstemciye verilen window ID tahmin edilemez veya cross-process erişimi yetkisiz olmamalıdır.

## 8. Compositor

Compositor bir scene graph tutar:

```text
Root
├── Background
├── TopPanel
├── DesktopWidgets
├── Workspace
│   ├── Window A
│   ├── Window B
│   └── Popup
├── Dock
└── Cursor
```

Her node için:

- transform;
- clip;
- opacity;
- visible;
- damage;
- input region

tutulur.

### Rendering sırası

1. changed regions hesapla;
2. scene traversal;
3. clipping;
4. opaque regions ile gereksiz çizimi ele;
5. textures/buffers bind et;
6. effects uygula;
7. cursor'u composit et;
8. frame fence bekle;
9. present et.

Tüm ekranı her event'te yeniden çizmekten kaçın.

## 9. Software renderer

GPU backend hazır değilken GUI'nin tamamen kullanılamaz kalmaması için küçük bir software renderer gerekir.

İlk primitive'ler:

- clear;
- rectangle;
- rounded rectangle;
- image blit;
- alpha blend;
- clipping;
- glyph bitmap.

Blur ve gelişmiş efektler daha sonra gelir.

## 10. GPU/Vulkan backend

GPU backend doğrudan widget toolkit'e bağlanmaz.

```text
Toolkit
  ↓
Rix Render API
  ↓
Vulkan backend
  ↓
Vulkan loader/ICD
  ↓
GPU driver
```

Renderer resource cache kullanabilir; ancak resource lifetime açık olmalıdır.

GPU device loss olduğunda:

```text
GPU loss
 ↓
stop submissions
 ↓
collect diagnostics
 ↓
reset/reinitialize if possible
 ↓
recreate resources
 ↓
software fallback if possible
```

## 11. Input

Gerçek input yolu korunur:

```text
USB
 ↓
xHCI
 ↓
HID
 ↓
Kernel input
 ↓
rix-input
 ↓
compositor/window
 ↓
application
```

`rix-input`:

- keycode → logical key;
- keyboard layout;
- modifiers;
- repeat;
- pointer coordinates;
- button state;
- scroll;
- device identity

dönüşümlerini yapar.

## 12. Text engine

GUI'nin okunabilirliği için text sistemi toolkit'ten ayrılır:

```text
UTF-8
 ↓
Unicode codepoints
 ↓
text segmentation
 ↓
shaping
 ↓
glyph selection
 ↓
glyph rasterization
 ↓
GPU/software atlas
```

Font dosyaları untrusted input kabul edilir ve parser fuzzing'e tabi tutulur.

## 13. Toolkit

Toolkit retained-mode veya kontrollü scene/widget model kullanabilir.

Örnek:

```text
Window
└── Column
    ├── Header
    ├── Card
    │   ├── Label
    │   └── Button
    └── List
```

Her widget:

- measure;
- layout;
- paint;
- event;
- accessibility

safhalarına sahip olur.

## 14. Rix Design System

Önce token sistemi oluştur:

```text
spacing
radius
typography
font sizes
line heights
shadow/elevation
opacity
animation duration
accent
surface/background
```

Sonra widget'ları bu token'lardan üret.

Böylece panel, Settings ve Mayo birbirinden kopuk görünmez.

## 15. Referans masaüstünü üretme sırası

Görseldeki tasarım için doğrudan bütün ekranı kodlama. Şu sırayla oluştur:

### A — Base desktop

- wallpaper;
- top panel;
- bottom dock.

### B — Left widgets

- clock;
- calendar;
- system card.

### C — Right control center

- notification panel;
- quick settings;
- sliders;
- settings shortcut.

### D — Window system

- application window;
- focus;
- move/resize;
- minimize/maximize.

### E — launcher

- app grid;
- search;
- favorites.

### F — first-party applications

- Terminal;
- Files;
- Settings;
- System Monitor;
- Mayo.

### G — polish

- blur;
- shadows;
- animation;
- hover states;
- transitions.

## 16. Top panel implementation

Panel three regions:

```text
LEFT                  CENTER                 RIGHT
RIX                   clock                  status icons
```

Panel state should be supplied by services:

- clock service;
- network service;
- audio service;
- notification service;
- power service.

The panel does not query hardware registers.

## 17. Notification service

Applications send a notification IPC message. The service validates it, applies policy and sends the desktop representation.

State:

`NEW → QUEUED → VISIBLE → READ/DISMISSED → ARCHIVED/EXPIRED`.

Critical notifications require separate policy and cannot be silently discarded by a normal “clear all”.

## 18. Quick settings

Each toggle has:

```text
UI state
requested state
service state
hardware state
error state
```

Never optimistically leave the UI in “enabled” forever when the service reports failure.

## 19. Widget system

Widget instances have:

```text
widget id
version
position
size
state
permissions
update interval
```

Widgets cannot run arbitrary privileged code. Plugin APIs should be sandboxable and resource-limited.

## 20. Application launcher

Application manifests are indexed by `rix-launcher`.

Search pipeline:

```text
keyboard input
 ↓
normalization
 ↓
application index
 ↓
ranking
 ↓
results
 ↓
launch request
```

Launch requests go through the session/application manager, not directly through raw process creation from GUI code.

## 21. Settings

Settings UI reads/writes service configuration through stable APIs.

Example:

```text
Brightness slider
 ↓
Display service
 ↓
GPU/display API
 ↓
hardware
```

A failed hardware operation returns an error to the service and UI.

## 22. File manager

File operations should be asynchronous and cancellable:

```text
copy request
 ↓
file service
 ↓
VFS
 ↓
RixFS/block layer
```

The UI displays real progress from the operation rather than an animation with fabricated percentage.

## 23. Terminal

Terminal application allocates a PTY and launches the user's shell through the normal process/session APIs.

```text
Terminal window
 ↓
PTY master
 ↓
user shell
 ↓
TTY/PTY
 ↓
kernel
```

Terminal emulation parses ANSI/VT output and renders it through the same text/rendering stack.

## 24. System Monitor

System Monitor consumes real telemetry:

```text
/proc / /sys / service APIs
        ↓
telemetry service
        ↓
System Monitor
```

Use sampling intervals appropriate to each metric. Do not poll high-cost kernel interfaces every frame.

## 25. Session and login

Boot flow:

```text
PID 1
 ↓
service manager
 ↓
login/auth
 ↓
rix-session
 ↓
compositor + desktop services
 ↓
launcher/desktop
```

Authentication secrets never pass through ordinary application IPC in plaintext when the architecture can avoid it.

## 26. Crash supervision

Use the service manager to supervise GUI components.

```text
rix-compositor crash
 ↓
supervisor
 ↓
collect crash report
 ↓
restart
 ↓
recreate surfaces
 ↓
restore session
```

Repeated crashes trigger a safe mode rather than an infinite restart loop.

## 27. Testing strategy

### Unit
- layout calculations;
- UTF-8;
- color/material calculations;
- widget state machines;
- window geometry;
- protocol encoding/decoding.

### Integration
- input → window;
- application → surface;
- surface → compositor;
- notification → panel;
- settings → service;
- file manager → VFS;
- terminal → PTY.

### Fault tests
- malformed IPC;
- invalid window ID;
- destroyed surface;
- disconnected display;
- GPU loss;
- compositor restart;
- input device removal;
- service timeout;
- out-of-memory;
- corrupt font/theme/manifest.

### Soak
Run long sessions with continuous:

- window creation/destruction;
- workspace switching;
- notifications;
- terminal sessions;
- file operations;
- display changes;
- USB hotplug.

## 28. Performance methodology

Record:

- frame time distribution;
- p95/p99 input latency;
- CPU/GPU usage;
- memory allocations;
- texture/resource count;
- compositor wakeups;
- startup time.

Do not select arbitrary performance claims before measuring on actual hardware.

## 29. Security methodology

Treat as hostile:

- applications;
- manifests;
- IPC clients;
- font files;
- images;
- clipboard data;
- drag/drop data;
- theme files;
- widgets/plugins.

Never trust client-provided sizes, object IDs, pointers, buffer offsets or counts.

## 30. Final implementation order

```text
1. Contracts
2. Display backend
3. Surface/buffer system
4. Event loop
5. Window protocol
6. Compositor
7. Software renderer
8. Input service
9. Text/font engine
10. Rix toolkit
11. Design system
12. Desktop shell
13. Notifications
14. Quick settings
15. Launcher
16. Settings
17. Terminal
18. Files
19. System Monitor
20. Mayo integration
21. Accessibility
22. Themes/materials
23. GPU/Vulkan optimization
24. Crash recovery
25. Soak/fuzz/security
26. Release candidate
```

## 31. GUI implementation rule

Do not jump directly to the final screenshot.

Build a real operating-system path underneath every visible feature. If the UI displays `Ethernet: Connected`, there must be a real network service state behind it. If it displays `42% RAM`, that number must come from real telemetry. If the terminal accepts a key, that input must ultimately come from the real input subsystem.

**The screenshot is the visual target; the OS state is the source of truth.**
