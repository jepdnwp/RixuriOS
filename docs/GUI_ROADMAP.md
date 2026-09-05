# RixuriOS GUI Roadmap — Desktop Platform

**Status:** Detailed implementation roadmap
**Target:** RixuriOS Desktop / RIX Shell
**Prerequisite:** `docs/ROADMAP.md` Phase 34 — Pre-GUI Platform Certification
**Primary reference:** `docs/GUI_IMPLEMENTATION.md` and `docs/GUI_DESIGN.md`

> The GUI is not a collection of screens. It is a complete userspace desktop platform built on the already-certified RixuriOS kernel, services, graphics stack, input stack, IPC, filesystem, security model and package system.

---

## 0. GUI entry gate

GUI work may become the primary workstream only when the pre-GUI certification has demonstrated:

- stable userspace process creation and destruction;
- working memory protection and `mmap`/shared-memory primitives;
- working IPC and synchronization;
- working VFS/RixFS and file descriptors;
- working PTY/TTY and real keyboard/mouse input;
- working users/groups/authorization;
- working service manager/PID 1;
- working logging and crash reporting;
- working package/update/recovery system;
- usable framebuffer/GPU abstraction;
- Vulkan capability is advertised only where genuinely implemented;
- terminal recovery remains available if graphical services fail.

GUI must never become a reason to weaken these foundations.

---

# GUI-01 — Architecture and Contracts

### Goal
Define the entire graphical userspace before writing the compositor.

### Build
- process model for GUI services;
- IPC protocol versioning;
- object IDs/handles;
- client/server ownership rules;
- shared-memory buffer contract;
- event/message model;
- synchronization primitives;
- capability/permission model;
- crash/restart boundaries;
- logging and tracing categories.

### Core processes

```text
rix-session
 ├── rix-compositor
 ├── rix-input
 ├── rix-notify
 ├── rix-settings
 ├── rix-launcher
 ├── rix-shell
 └── applications
```

The compositor must not be PID 1 and must not require kernel-internal GUI code.

### Checkpoints
`GUI-01-SPEC`, `GUI-01-IPC`, `GUI-01-SECURITY`, `GUI-01-DOCS`.

---

# GUI-02 — Display and Scanout

### Implement
- enumerate displays;
- identify resolution and refresh modes;
- pixel-format abstraction;
- framebuffer/scanout buffers;
- double/triple buffering;
- vblank/timing model;
- display hotplug events;
- multi-monitor topology;
- DPI/scale information;
- rotation architecture.

### Failure handling
If GPU acceleration is unavailable, the desktop must have a software/fallback renderer sufficient to show recovery UI and terminal.

### Evidence
Real display mode selection, real scanout and real frame presentation. A memory buffer that is never displayed is not display support.

---

# GUI-03 — Rix Graphics API

Build a small RixuriOS userspace graphics abstraction above the kernel GPU/Vulkan interfaces.

### Objects
- device;
- surface;
- image;
- buffer;
- texture;
- sampler;
- command list;
- fence;
- semaphore/event;
- swapchain;
- shader/pipeline.

### Requirements
- explicit ownership;
- reference counting/lifetime;
- synchronization;
- GPU/CPU memory visibility;
- device-loss handling;
- debug labels;
- frame timing.

The desktop toolkit must not directly manipulate hardware registers.

---

# GUI-04 — Compositor Core

### Rendering model
Use a scene graph/composition model:

```text
Desktop Scene
 ├── Wallpaper
 ├── Panels
 ├── Widgets
 ├── Windows
 │    ├── Surface
 │    ├── Decoration
 │    └── Popup/Tooltip
 └── Cursor
```

### Implement
- scene tree;
- damage tracking;
- clipping;
- transforms;
- opacity;
- rounded rectangles;
- shadows;
- blur/backdrop effects;
- texture composition;
- frame scheduling;
- vblank-aware presentation;
- cursor composition;
- screenshot capture.

### Performance
Avoid repainting the entire desktop when only a notification changes.

---

# GUI-05 — Window System

### Window lifecycle
`CREATE → MAP → FOCUS → RESIZE/MOVE → MINIMIZE/MAXIMIZE → HIDE → CLOSE → DESTROY`

### Features
- normal windows;
- modal dialogs;
- transient windows;
- popups;
- tooltips;
- fullscreen;
- always-on-top;
- tiling/snap;
- workspaces;
- virtual desktops;
- window rules;
- application identity;
- per-window scale.

### Input routing
- pointer focus;
- keyboard focus;
- capture/grab;
- modal routing;
- shortcut routing;
- accessibility focus.

---

# GUI-06 — Input System

Connect the already-real USB/HID input path to the desktop.

```text
USB/xHCI
   ↓
HID
   ↓
Kernel input
   ↓
Input service
   ↓
Compositor/window server
   ↓
Application
```

### Implement
- keyboard layouts;
- modifiers;
- compose/dead keys architecture;
- repeat rate/delay;
- mouse buttons;
- wheel/scroll;
- pointer acceleration policy;
- touchpad architecture;
- multi-device input;
- hotplug;
- input device permissions.

No synthetic input may be used to claim hardware completion.

---

# GUI-07 — Text, Fonts and Internationalization

### Text engine
- UTF-8 validation;
- Unicode codepoint handling;
- grapheme-aware cursor movement;
- shaping architecture;
- bidirectional text architecture;
- fallback fonts;
- font discovery/cache;
- glyph rasterization;
- subpixel/antialiasing policy;
- emoji support architecture.

### Internationalization
- Turkish and Unicode correctness;
- locale-aware formatting;
- date/time formatting;
- keyboard layouts;
- RTL readiness.

---

# GUI-08 — Rix Design System

Create one component language used by every first-party application.

### Components
- `RixWindow`
- `RixPanel`
- `RixCard`
- `RixButton`
- `RixToggle`
- `RixSlider`
- `RixInput`
- `RixList`
- `RixTree`
- `RixDialog`
- `RixMenu`
- `RixTooltip`
- `RixNotification`
- `RixTab`
- `RixProgress`
- `RixIcon`

### Rules
- common spacing scale;
- typography scale;
- corner-radius scale;
- shadow levels;
- elevation model;
- animation timings;
- focus states;
- disabled/error/success states;
- dark/light/high-contrast themes.

---

# GUI-09 — RIX Desktop Shell

This is the visual system represented by the reference design.

### Top panel
Left:

`RIX` logo and system menu.

Center:

`time + date`.

Right:

`network + audio + notifications + power`.

### Desktop
Keep the center intentionally calm. Widgets live around the edges and appear only when useful.

### Left widgets
- clock;
- calendar;
- system monitor;
- optional weather/media/future widgets through a controlled plugin API.

### Bottom dock
- pinned applications;
- running applications;
- launcher;
- active-window indicators;
- auto-hide behavior;
- keyboard navigation.

### Right control center
One unified panel containing:

1. notifications;
2. quick settings;
3. brightness;
4. volume;
5. power/session actions;
6. settings shortcut.

---

# GUI-10 — Notification Center

### Model
Every notification has:

- application/source;
- title;
- body;
- timestamp;
- priority;
- category;
- optional action buttons;
- optional progress;
- expiration policy;
- privacy visibility policy.

### Categories
- system;
- security;
- network;
- update;
- storage;
- hardware;
- application;
- transfer.

### Features
- grouping;
- collapse/expand;
- mark read;
- clear all;
- notification history;
- Do Not Disturb;
- critical-notification policy.

---

# GUI-11 — Quick Settings / Control Center

### Controls
- Wi-Fi;
- Ethernet;
- Bluetooth architecture;
- audio output/input;
- brightness;
- night mode;
- Do Not Disturb;
- screen lock;
- system monitor;
- power.

### Architecture

```text
GUI control
   ↓
Desktop service
   ↓
OS API
   ↓
service/driver
   ↓
hardware
```

The GUI must never fake a successful state when the underlying service rejected the operation.

---

# GUI-12 — Widget Framework

### Widget lifecycle
`CREATE → LOAD → UPDATE → RENDER → INPUT → HIDE → DESTROY`

### Requirements
- sandboxed/plugin-safe API;
- resource limits;
- update frequency limits;
- persistence;
- placement/grid system;
- resize handles;
- accessibility tree;
- theme integration.

### Built-in widgets
- Clock;
- Calendar;
- System;
- Network;
- Storage;
- Audio;
- Battery when available.

All telemetry is real data from system services.

---

# GUI-13 — Launcher and Application Model

### Launcher
- app search;
- categories;
- favorites;
- recent apps;
- keyboard-first navigation;
- application metadata;
- icons;
- desktop actions.

### Application metadata
Define a Rix application manifest containing:

- application ID;
- executable;
- icon;
- name;
- localized names;
- capabilities;
- supported file types;
- URL schemes;
- single/multiple-instance policy.

---

# GUI-14 — Settings Application

Sections:

- System;
- Appearance;
- Desktop;
- Display;
- Audio;
- Network;
- Input;
- Bluetooth;
- Users;
- Security;
- Storage;
- Applications;
- Updates;
- Developer;
- Accessibility;
- About RixuriOS.

The settings application uses public service APIs rather than editing kernel state directly.

---

# GUI-15 — File Manager

### Required
- directory browsing;
- tabs;
- split view;
- thumbnails architecture;
- copy/move/delete;
- rename;
- permissions;
- file properties;
- search;
- mounts;
- removable media;
- progress UI;
- conflict resolution;
- trash/recycle architecture;
- safe unmount/eject.

### Safety
Formatting and destructive disk operations require explicit target identification and confirmation.

---

# GUI-16 — Terminal Application

The GUI terminal is a frontend to the real PTY/TTY system.

### Features
- multiple tabs;
- split panes;
- font/scale settings;
- copy/paste;
- search;
- selection;
- hyperlinks;
- truecolor;
- terminal resize;
- shell integration;
- crash recovery.

It must behave as a real terminal, not as a shell simulator.

---

# GUI-17 — Power, Session and Lock Screen

### Session
- login;
- logout;
- lock;
- switch user;
- session restore policy.

### Power
- sleep;
- hibernate architecture;
- shutdown;
- reboot;
- display power;
- battery state when available.

### Lock screen
- separate security boundary;
- no application access to credentials;
- secure input path;
- failed-login throttling;
- recovery policy.

---

# GUI-18 — Accessibility

### Foundation
- accessibility tree;
- roles/states/properties;
- keyboard navigation;
- focus management;
- screen reader API;
- high contrast;
- text scaling;
- UI scaling;
- reduced motion;
- large cursor;
- color-independent status indicators.

Accessibility is part of the toolkit contract, not a late patch.

---

# GUI-19 — Theme and Material Engine

### Visual modes
- Rix Light;
- Rix Dark;
- Rix OLED;
- Rix High Contrast;
- Custom.

### Material properties
- transparency;
- blur;
- border;
- shadow;
- radius;
- elevation;
- accent color.

### Performance fallback

```text
Full material
   ↓ if unsupported/expensive
Reduced material
   ↓
Opaque panels
   ↓
Software/simple rendering
```

The desktop must remain usable without blur or advanced GPU effects.

---

# GUI-20 — Workspace and Window Management UX

- virtual desktops;
- workspace overview;
- keyboard shortcuts;
- window snapping;
- tiling zones;
- drag-to-workspace;
- multi-monitor workspace policy;
- per-application window rules;
- restore previous geometry.

---

# GUI-21 — System Monitor and Telemetry UI

Display real:

- per-core CPU;
- RAM;
- swap when implemented;
- disk usage;
- NVMe health/temperature when available;
- network RX/TX;
- process list;
- services;
- GPU usage/memory when supported;
- audio devices;
- USB devices.

Telemetry must come from `/proc`, `/sys`, device/service APIs or equivalent RixuriOS interfaces.

---

# GUI-22 — Update Center

- available updates;
- changelog;
- package dependencies;
- download progress;
- verification;
- installation transaction;
- restart requirement;
- rollback status;
- previous-version recovery.

No UI may claim “updated successfully” before the update transaction has actually committed and verified.

---

# GUI-23 — Developer Experience

First-party GUI support for:

- terminal;
- Mayo;
- package manager;
- compiler toolchain;
- debugger;
- logs;
- system monitor;
- crash reports;
- service control;
- device information.

Provide a Developer Mode with extra diagnostics without weakening normal security.

---

# GUI-24 — Security UX

Every privileged action uses a centralized authorization service.

Examples:

```text
Format disk
Install system package
Change user permissions
Modify network system-wide
Mount protected filesystem
Change security policy
```

GUI shows the exact target and requested operation before confirmation.

---

# GUI-25 — Crash Recovery

### Failure scenarios
- compositor crash;
- application crash;
- GPU device loss;
- display disconnect;
- input service restart;
- notification service failure;
- filesystem service error;
- out-of-memory.

### Required behavior

```text
Application crash
   ↓
window removed
   ↓
notification/report

Compositor crash
   ↓
supervisor restart
   ↓
recreate session surfaces
   ↓
restore desktop

GPU failure
   ↓
GPU recovery/fallback renderer
   ↓
terminal/recovery UI
```

Kernel panic remains a kernel event and is never hidden by GUI code.

---

# GUI-26 — Performance and Power

Measure:

- input latency;
- frame time;
- frame drops;
- compositor CPU usage;
- GPU usage;
- memory footprint;
- startup time;
- app launch time;
- notification latency;
- workspace-switch latency;
- power consumption where measurable.

Targets must be hardware-dependent and recorded rather than inventing universal numbers.

---

# GUI-27 — Security, Fuzzing and Soak Tests

Fuzz:

- IPC messages;
- application manifests;
- font files;
- image metadata;
- theme files;
- widget/plugin data;
- drag/drop payloads;
- clipboard content;
- notification payloads;
- window protocol messages.

Run long-duration sessions with:

- repeated window creation/destruction;
- workspace switching;
- monitor hotplug;
- USB hotplug;
- network changes;
- app crashes;
- compositor restarts.

---

# GUI-28 — First-Party Applications

Build the initial Rix desktop around:

1. Rix Terminal
2. Rix Files
3. Rix Settings
4. Rix System Monitor
5. Mayo
6. Rix Update Center
7. Rix Software/Package Center
8. Rix Media/Audio UI

Applications must use the public GUI toolkit and service APIs.

---

# GUI-29 — Visual Polish

Only after functionality is stable:

- animation refinement;
- blur tuning;
- shadow tuning;
- typography tuning;
- icon consistency;
- spacing refinement;
- transitions;
- hover/focus states;
- micro-interactions;
- reduced-motion alternatives.

Do not spend engineering time polishing an unverified backend.

---

# GUI-30 — Release Candidate

### RC requirements
- clean graphical boot;
- login/session;
- top panel;
- widgets;
- dock;
- launcher;
- windows;
- notifications;
- quick settings;
- settings;
- file manager;
- terminal;
- Mayo;
- network/audio/power controls;
- multi-monitor baseline;
- accessibility baseline;
- update/recovery path;
- compositor restart;
- terminal fallback.

### Final acceptance
The reference design should be recognizable as RixuriOS, but visual similarity is not enough. Every visible state must correspond to real system state.

---

# GUI release ladder

- **G0 — Graphics:** real display + renderer.
- **G1 — Compositor:** surfaces + composition + synchronization.
- **G2 — Window System:** windows + focus + input.
- **G3 — Toolkit:** text + widgets + design system.
- **G4 — Desktop Shell:** panel + dock + widgets.
- **G5 — System UI:** notifications + quick settings + settings.
- **G6 — Applications:** terminal + files + Mayo + monitor.
- **G7 — Resilience:** crash recovery + fallback.
- **G8 — Accessibility:** usable keyboard/screen-reader/high-contrast paths.
- **G9 — Performance:** measured frame/input/resource behavior.
- **G10 — Graphical Release:** all acceptance evidence complete.

## GUI definition of done

`architecture + IPC + real rendering + real input + real system state + security + recovery + accessibility + performance + regression + documentation`.

A screenshot alone can never close a GUI checkpoint.
