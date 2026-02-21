# Post-Upgrade Review and Test Plan

Manual work remaining after the automated Qt6 + Wayland upgrade.
Everything below requires a human at the keyboard.

---

## Code Review

These files contain non-mechanical new code written by agents.
Review for correctness, edge cases, and style.

### F1: EGL display detection hooks (priority: high)

**Files**:
- `renderdoc/driver/gl/egl_hooks.cpp` — new `eglGetPlatformDisplayEXT` hook,
  updated `eglGetPlatformDisplay` and `eglGetDisplay` hooks to populate
  `eglhook.displays` map
- `renderdoc/driver/gl/egl_dispatch_table.h` — added `PFN_eglGetPlatformDisplayEXT`
  typedef and `EGL_HOOKED_SYMBOLS` entry

**Review focus**:
- [ ] Verify the `eglhook.displays[ret]` population is correct for all three
  hook functions (GetDisplay, GetPlatformDisplay, GetPlatformDisplayEXT)
- [ ] Confirm `RENDERDOC_WINDOWING_WAYLAND` ifdef placement doesn't break
  the X11-only build path
- [ ] Check that the EXT function pointer typedef signature matches the
  EGL spec (`const EGLint *` vs `const EGLAttrib *`)

### F2: Wayland keyboard dispatch (priority: high)

**File**: `renderdoc/os/posix/linux/linux_stringio.cpp`

**Review focus**:
- [ ] Lock ordering in `GetWaylandKeyState()` — lock is dropped before
  `wl_display_dispatch_queue_pending` to avoid deadlock with callbacks.
  Verify the `displays` map stability assumption (append-only) holds
- [ ] `wl_proxy_set_queue` calls on registry, seat, and keyboard proxies —
  verify all objects land on the dedicated queue
- [ ] Two-roundtrip pattern in `UseWaylandDisplay()` — confirm both are on
  the dedicated queue, not the default queue
- [ ] No cleanup path — matches existing code pattern but worth noting

### G2: Wayland surface embedding (priority: medium)

**File**: `qrenderdoc/Code/CaptureContext.cpp`

**Review focus**:
- [ ] `QNativeInterface::Private::QWaylandWindow` usage — private Qt API,
  may break across Qt6 minor versions. Confirm it works on the target
  Qt6 version
- [ ] `window->winId()` force-creation before accessing native interface —
  verify this is necessary and sufficient
- [ ] Fallback to `CreateHeadlessWindowingData(1, 1)` on null native
  interface — acceptable behavior?

### G3: Output window dimension tracking (priority: medium)

**Files**:
- `renderdoc/api/replay/renderdoc_replay.h` — new `SetDimensions` on `IReplayOutput`
- `renderdoc/replay/replay_driver.h` — new `SetOutputWindowDimensions` on `IReplayDriver`
- `renderdoc/driver/vulkan/vk_replay.h` — `pendingWidth`/`pendingHeight` fields
- `renderdoc/driver/vulkan/vk_outputwindow.cpp` — `SetOutputWindowDimensions` impl
- `renderdoc/driver/vulkan/vk_linux.cpp` — reads pending dimensions
- `qrenderdoc/Widgets/CustomPaintWidget.cpp` — resize event propagation

**Review focus**:
- [ ] Thread safety — `pendingWidth`/`pendingHeight` are `int32_t` written
  by UI thread, read by replay thread. Relies on hardware atomicity of
  aligned 32-bit writes. Acceptable?
- [ ] No `CHECK_REPLAY_THREAD()` on `ReplayOutput::SetDimensions` — intentional
  for cross-thread use. Verify this is safe with the replay proxy
- [ ] `CustomPaintWidget::resizeInternal` null-checks `m_Output` — verify
  this covers the initialization window before output is assigned

### H1: renderdoccmd Wayland window (priority: low)

**Files**:
- `renderdoccmd/renderdoccmd_linux.cpp` — ~330 lines of new Wayland client code
- `renderdoccmd/CMakeLists.txt` — wayland-scanner protocol generation

**Review focus**:
- [ ] xdg-shell protocol usage — configure/ack cycle, ping/pong
- [ ] Event dispatch in render loop — non-blocking `prepare_read` /
  `read_events` / `dispatch_pending` pattern
- [ ] Cleanup order in `WaylandWindowDestroy()`
- [ ] Selection logic — Wayland preferred when `WAYLAND_DISPLAY` is set,
  falls back to XCB. Correct for XWayland environments?

---

## Runtime Testing

### Linux (X11/XWayland) — basic regression

```bash
QT_QPA_PLATFORM=xcb ./build/bin/qrenderdoc
```

- [ ] UI launches, all panels render
- [ ] Shader viewer shows syntax highlighting (Scintilla 5.x works)
- [ ] Python shell accepts input
- [ ] Open a capture file — texture viewer, mesh viewer, pipeline state work
- [ ] Keyboard shortcuts function

### Linux (native Wayland) — new functionality

```bash
QT_QPA_PLATFORM=wayland ./build/bin/qrenderdoc
```

- [ ] UI launches natively on Wayland compositor (no XWayland)
- [ ] Window management works (resize, minimize, maximize, close)
- [ ] All panels render correctly
- [ ] No flicker or black rectangles in texture/mesh viewers
- [ ] Drag and drop works (file drops)
- [ ] Multi-monitor works

### Wayland capture — Vulkan

```bash
# From qrenderdoc running on Wayland, launch a Wayland Vulkan app
# Or use renderdoccmd:
./build/bin/renderdoccmd capture -w <vulkan_wayland_app>
```

- [ ] App launches under RenderDoc
- [ ] F12 / PrintScreen capture hotkey works (F2 keyboard dispatch fix)
- [ ] Capture file is produced and valid
- [ ] Replay works — texture viewer shows captured textures
- [ ] renderdoccmd replay preview window appears on Wayland (H1)

### Wayland capture — EGL/OpenGL

```bash
./build/bin/renderdoccmd capture -w <egl_wayland_app>
```

- [ ] EGL display correctly detected as Wayland (F1 hook)
- [ ] API detection shows "OpenGL" or "OpenGL ES"
- [ ] Capture and replay work

### renderdoccmd standalone

```bash
# Wayland replay window
WAYLAND_DISPLAY=$WAYLAND_DISPLAY ./build/bin/renderdoccmd replay <capture.rdc>

# XCB fallback
unset WAYLAND_DISPLAY
./build/bin/renderdoccmd replay <capture.rdc>
```

- [ ] Wayland replay window opens and renders
- [ ] XCB fallback still works when WAYLAND_DISPLAY unset

### PySide6 (requires pyside6 package installed)

```bash
# In qrenderdoc Python shell:
import renderdoc
import qrenderdoc
```

- [ ] Both imports succeed
- [ ] Widget access works (e.g., `qrenderdoc.Extensions()`)
- [ ] Python extension loading works

---

## Windows Build (W1)

Requires a Windows machine with Qt6 installed.

### Setup

1. Download Qt6 (matching version, ideally 6.8.x+) for MSVC
2. Replace `qrenderdoc/3rdparty/qt/` with Qt6 headers/libs/tools
3. If using PySide6 on Windows, replace `qrenderdoc/3rdparty/pyside/`

### Build

```cmd
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_QRENDERDOC=ON ..
cmake --build . --config Debug
```

Or use the `.vcxproj` directly (already updated for PySide6).

### Test

- [ ] qrenderdoc launches
- [ ] All panels render
- [ ] Vulkan capture works
- [ ] D3D11/D3D12 capture works
- [ ] Python shell works
- [ ] PySide6 widget access works (if PySide6 bundled)

---

## Test Apps

Good candidates for Wayland capture testing:

- **vkcube** (`vulkan-tools` package) — minimal Vulkan, good smoke test
- **weston-simple-egl** (`weston` package) — minimal EGL/Wayland
- **glmark2-wayland** — OpenGL ES benchmark under Wayland
- Any game running native Wayland (e.g., via SDL3 with Wayland backend)

For X11 regression, the same apps with `GDK_BACKEND=x11` or
`SDL_VIDEODRIVER=x11` work.
