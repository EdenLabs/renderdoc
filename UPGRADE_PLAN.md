# RenderDoc Fork: Qt6 + Wayland Upgrade Plan

Reference document for upgrading a personal RenderDoc fork to Qt6 with full
Wayland support. Cross-platform (Windows, macOS, Linux) must be maintained.

**Baseline**: RenderDoc v1.x branch, Qt 5.6+, Scintilla 3.7.2, PySide2

**Target**: Qt6, Scintilla 5.5.x, PySide6, native Wayland capture + UI

---

## Agent Work Packages

Ordered for execution in WSL (Arch/EndeavourOS). Designed for background
agent execution with minimal human review.

**Design principles**:
- Each task is small enough to fit in 1-2 agent context windows
- Verification is automated where possible (grep, build, launch)
- Non-building intermediate states are OK if grep-verifiable
- Build checkpoints are explicit -- only certain tasks require compilation
- Human review is only needed for new code, not mechanical migrations

### WSL prerequisites (manual, not agent-driven)

```bash
# Qt6 dev, Wayland dev, Python, build tools
sudo pacman -S qt6-base qt6-svg qt6-tools qt6-wayland qt6-5compat \
    cmake ninja python python-pyside6 pyside6 shiboken6 \
    wayland wayland-protocols extra-cmake-modules \
    libxcb xcb-util-keysyms libx11 \
    vulkan-devel vulkan-headers
```

Note: `qt6-5compat` provides `Core5Compat` which Scintilla 5.x needs for
QTextCodec on Qt6. This is a permanent dependency (upstream Scintilla's
design choice, not ours).

---

### Task group A: Build system Qt6 switch

Small, focused. Gets cmake + qmake configuring against Qt6 without attempting
a full build. Foundation for everything else.

**Build architecture note**: On Linux/macOS, qrenderdoc uses a hybrid build:
`qrenderdoc/CMakeLists.txt` is a wrapper that generates a `.pri` config file
and invokes qmake. The actual build is driven by `qrenderdoc/qrenderdoc.pro`.
CMake handles renderdoc core (C++ library), SWIG, PySide detection, and
configuration passthrough. qmake handles compiling qrenderdoc itself.

#### A1: CMake wrapper Qt6 update

**Do** (in `qrenderdoc/CMakeLists.txt`):
1. Rename `QMAKE_QT5_COMMAND` variable to `QMAKE_QT6_COMMAND` (line 12).
   Default to `qmake6` (Arch's Qt6 qmake binary name)
2. Update version check (line 29): `5.5.999` -> `6.0` (require Qt >= 6.0)
3. Update error message (line 32) to reference Qt6

**Verify**: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_QRENDERDOC=ON ..`
configures without errors, prints "Building using Qt 6.x.y".

#### A2: qrenderdoc.pro Qt6 update

**Do** (in `qrenderdoc/qrenderdoc.pro`):
1. Update version check (lines 11-13): require Qt >= 6.0 instead of 5.6
2. Add `core5compat` to module list (line 7): `QT += core gui widgets svg network core5compat`
   (Scintilla 5.x needs QTextCodec via core5compat on Qt6)
3. Remove `x11extras` from the Linux module list (line 161): Qt6 removed
   this module. The C++ migration to `QNativeInterface` happens in task C7.
4. Update C++ standard (line 137): `CONFIG += c++14` -> `CONFIG += c++17`
   (required for Scintilla 5.x)
5. Update `SCI_LEXER=1` define (line 423): remove it. Scintilla 5.x moved
   lexers to Lexilla; this define no longer exists.
6. Update Scintilla/Lexilla source paths (lines 424-436): add Lexilla
   sources, update Scintilla include paths for 5.x layout

**Verify**: qmake processes the .pro file without errors (may not fully build
yet due to deprecated API usage in source code).

---

### Task group B: Scintilla 5.x upgrade

Can run after A1. Replaces vendored Scintilla and adapts the integration.
Build not expected until after group C.

#### B1: Replace vendored Scintilla sources

**Do**:
1. Download Scintilla 5.5.x source from scintilla.org
2. Delete contents of `qrenderdoc/3rdparty/scintilla/`
3. Copy upstream 5.5.x sources into `qrenderdoc/3rdparty/scintilla/`

**Verify**: `ls qrenderdoc/3rdparty/scintilla/qt/ScintillaEditBase/` contains
`ScintillaEditBase.h`, `ScintillaEditBase.cpp`, `ScintillaQt.h`, `ScintillaQt.cpp`,
`PlatQt.h`, `PlatQt.cpp`.

#### B2: Add Lexilla sources

**Do**:
1. Download matching Lexilla source from scintilla.org
2. Create `qrenderdoc/3rdparty/lexilla/`
3. Copy Lexilla sources into it

**Verify**: `ls qrenderdoc/3rdparty/lexilla/lexlib/` and
`ls qrenderdoc/3rdparty/lexilla/lexers/` both contain `.cxx` files.

#### B3: Update build globs for Scintilla + Lexilla

**Do**:
1. In `qrenderdoc/qrenderdoc.pro` (lines 424-436): update Scintilla source
   paths for 5.x layout, add Lexilla source/include paths. Use `3rdparty/`
   (lowercase) consistently -- existing camelCase `3rdParty/` in the Xcode
   CMake glob (line 400) works on case-insensitive macOS but will fail on
   Linux for new Lexilla paths.
2. In `qrenderdoc/CMakeLists.txt` Xcode glob section (lines 395-404): add
   Lexilla files, update Scintilla paths. Fix casing to `3rdparty/`.
3. Update include paths for new Scintilla 5.x and Lexilla header layout in
   both .pro and CMakeLists.txt

**Verify**: `cmake ..` configures without errors. qmake processes .pro without
errors about missing source files.

#### B4: Audit local Scintilla patches

**Do**:
1. For each of the 6 local patches (listed in Phase 1.2), check if the
   issue they fixed exists in Scintilla 5.5.x
2. Reapply any patches that are still relevant (likely only the
   `WA_StaticContents` removal from commit `497575b68`)
3. Document findings: which patches were already fixed upstream, which
   were reapplied

**Verify**: Agent produces a summary of patch audit results.

#### B5: Migrate lexer loading API

**Do**:
1. In `ScintillaSyntax.cpp`: replace `setLexer(SCLEX_*)` with
   `CreateLexer()` + `SCI_SETILEXER` per Phase 1.4 code example
2. Grep all qrenderdoc `.cpp` files for `setLexer`, `SCLEX_`,
   `SCI_SETLEXER`, `SCI_SETLEXERLANGUAGE` -- fix any remaining instances
3. Verify `ScintillaSyntax.h` custom IDs (`SCLEX_GLSL` etc.) are only used
   for branching, not passed to Scintilla

**Verify**:
- `grep -rn "setLexer\|SCI_SETLEXER\b" qrenderdoc/Code/ qrenderdoc/Windows/`
  returns zero hits (excluding comments)
- `grep -rn "CreateLexer\|SCI_SETILEXER" qrenderdoc/Code/` returns hits in
  `ScintillaSyntax.cpp`

#### B6: getText return value audit

**Do**:
1. Grep for `getText`, `getSelText`, `getCurLine` in qrenderdoc consumer code
   (excluding `3rdparty/`)
2. Read upstream 5.5.x `ScintillaEdit.cpp` `TextReturner` implementation
3. Determine if the `+1` pattern in consumer code needs adjustment
4. Fix if needed, document findings if not

**Verify**: Agent produces an audit report with specific file:line references
and whether changes were needed.

#### B7: Fix Scintilla notification signals

**Do**:
1. `grep -rn "updateUi" qrenderdoc/Code/ qrenderdoc/Windows/` (excluding 3rdparty)
2. If any slots connect to `updateUi()`, update signature to accept the new
   parameter from Scintilla 5.x

**Verify**: grep confirms all `updateUi` connections match the 5.x signature.

---

### Task group C: Qt6 API migrations

Each task is a single deprecated API pattern. Can run in parallel after A1.
Each is independently grep-verifiable. Build not expected until all are done.

#### C1: QDesktopWidget -> QScreen

**Do**: Replace per Phase 2.1 pattern. Grep for `QDesktopWidget` in all
qrenderdoc code (excluding `3rdparty/scintilla` and `3rdparty/lexilla`).

**Verify**: `grep -rn "QDesktopWidget" qrenderdoc/ --include="*.cpp" --include="*.h"` returns zero hits outside vendored Qt headers.

#### C2: QRegExp -> QRegularExpression

**Do**: Replace per Phase 2.2 pattern. Only in qrenderdoc code, not 3rdparty.

**Verify**: `grep -rn "QRegExp" qrenderdoc/Code/ qrenderdoc/Windows/ qrenderdoc/Widgets/` returns zero hits.

#### C3: QString::SplitBehavior

**Do**: Replace `QString::SkipEmptyParts` -> `Qt::SkipEmptyParts` (and
`KeepEmptyParts`). Per Phase 2.3.

**Verify**: `grep -rn "QString::SkipEmptyParts\|QString::KeepEmptyParts" qrenderdoc/` returns zero hits.

#### C4: QAction include paths

**Do**: `grep -rn "QtWidgets/QAction" qrenderdoc/` and replace with
`<QAction>` or `<QtGui/QAction>`.

**Verify**: `grep -rn "QtWidgets/QAction" qrenderdoc/Code/ qrenderdoc/Windows/` returns zero hits.

#### C5: Qt version guards

**Do**: Remove `#if QT_VERSION >= QT_VERSION_CHECK(5, ...)` guards per
Phase 2.6. Keep the Qt6 codepath, delete the else branch and the guard.

**Verify**: `grep -rn "QT_VERSION_CHECK.5," qrenderdoc/Code/ qrenderdoc/Windows/` returns zero hits.

#### C6: High-DPI attribute removal

**Do**: Remove `Qt::AA_EnableHighDpiScaling` AND `Qt::AA_UseHighDpiPixmaps`
plus surrounding version checks in `qrenderdoc.cpp`. Both attributes are
removed in Qt6 (always-on behavior). Per Phase 2.7.

**Verify**: `grep -rn "AA_EnableHighDpiScaling\|AA_UseHighDpiPixmaps" qrenderdoc/` returns zero hits.

#### C7: X11Extras -> QNativeInterface

**Do**: Replace all `QX11Info` usage with Qt6 `QNativeInterface` API per
Phase 2.8 code example. Linux-only code behind `#ifdef`. Known files:
- `qrenderdoc/Code/qrenderdoc.cpp` (`QX11Info::display()`)
- `qrenderdoc/Code/CaptureContext.h` (include)
- `qrenderdoc/Code/CaptureContext.cpp` (`QX11Info::connection()`, `display()`)
- `qrenderdoc/Windows/Dialogs/CameraControlsDialog.cpp` (`QX11Info::connection()`)

Note: `QX11Info::connection()` returns an `xcb_connection_t*`. The Qt6
equivalent is `qApp->nativeInterface<QNativeInterface::QX11Application>()->connection()`.

**Verify**: `grep -rn "QX11Info" qrenderdoc/Code/ qrenderdoc/Windows/` returns zero hits.

#### C8: QTextCodec audit (non-Scintilla)

**Do**: `grep -rn "QTextCodec" qrenderdoc/ --include="*.cpp" --include="*.h"`
excluding `3rdparty/`. If any hits remain in qrenderdoc proper, migrate to
`QStringConverter` or remove if the codec is UTF-8.

**Verify**: grep returns zero hits outside `3rdparty/`.

---

### BUILD CHECKPOINT 1

**After**: All of A, B, and C are complete.

**Do**:
```bash
cd build && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_QRENDERDOC=ON ..
ninja 2>&1 | head -100
```

If build fails, iterate on compile errors. These should be minor fixups --
the systematic migrations above cover the known breaking changes. Any
remaining errors are edge cases the grepping missed.

**Verify**:
- `ninja` succeeds with no errors
- `./bin/qrenderdoc` launches in WSLg
- Shader viewer shows syntax-highlighted text (Scintilla works)
- Python shell renders and accepts input
- Commit the working state

**Human review**: Quick sanity check. If it builds and launches, the
mechanical migration succeeded. No need to review individual file changes.

---

### Task group D: Wayland cmake flag split

Small, independent. Can run anytime after BUILD CHECKPOINT 1.

#### D1: Split wayland cmake flag

**Do**: Per Phase 3.4. Replace `ENABLE_UNSUPPORTED_EXPERIMENTAL_POSSIBLY_BROKEN_WAYLAND`
with `ENABLE_WAYLAND_CAPTURE` (default ON) and `ENABLE_WAYLAND_UI` (default OFF).

**Verify**:
- `grep -rn "UNSUPPORTED_EXPERIMENTAL" CMakeLists.txt qrenderdoc/CMakeLists.txt qrenderdoc/qrenderdoc.pro` returns zero hits
- `cmake .. -DENABLE_WAYLAND_CAPTURE=ON -DENABLE_WAYLAND_UI=OFF && ninja` succeeds
- `cmake .. -DENABLE_WAYLAND_CAPTURE=ON -DENABLE_WAYLAND_UI=ON && ninja` succeeds

---

### Task group E: PySide6

Can run anytime after BUILD CHECKPOINT 1.

#### E1: PySide6 CMake + code migration

**Do**: All of Phase 4. Update CMake, rename PySide2/Shiboken2 references,
update PythonContext.cpp, verify Shiboken6 API against installed headers.

**Verify**:
- `ninja` succeeds with `-DENABLE_PYRENDERDOC=ON`
- `grep -ri "PySide2\|Shiboken2" qrenderdoc/Code/ qrenderdoc/CMakeLists.txt`
  returns zero hits
- Launch qrenderdoc, open Python shell, type `import renderdoc` -- no error
- Type `import qrenderdoc` -- no error

---

### Task group F: Wayland capture

Can run anytime after D1. Contains new code -- agent should flag for review.

#### F1: EGL display detection hook

**Do**: Per Phase 5.2. Hook `eglGetPlatformDisplay`/`eglGetPlatformDisplayEXT`
in `egl_hooks.cpp`. Use `EGL_PLATFORM_WAYLAND_KHR` to identify Wayland.

**Verify**: Builds with `-DENABLE_WAYLAND_CAPTURE=ON`. Agent flags new code
for human review.

#### F2: Wayland keyboard dispatch fix

**Do**: Per Phase 5.3. Add `wl_event_queue` in `linux_stringio.cpp`,
non-blocking dispatch in the Tick path.

**Verify**: Builds clean. Agent flags new code for human review.

**Human review**: F1 and F2 are the only tasks in the entire plan that write
non-trivial new code against external APIs (EGL, libwayland). Review these.
Everything else is mechanical migration.

---

### Task group G: Wayland UI

Can run anytime after D1. Mix of migration and new plumbing.

#### G1: Qt6 Wayland platform detection

**Do**: Per Phase 6.1. Replace detection in `qrenderdoc.cpp` with Qt6
`QNativeInterface::QWaylandApplication`. Gate `QT_QPA_PLATFORM=xcb` override
on `RENDERDOC_WAYLAND_UI`.

**Verify**: Builds with `-DENABLE_WAYLAND_UI=ON`.

#### G2: Rendering surface embedding

**Do**: Per Phase 6.2. Update `CaptureContext.cpp` surface retrieval to use
`QNativeInterface::Private::QWaylandWindow`.

**Verify**: Builds clean. `grep -rn "AccessWaylandPlatformInterface" qrenderdoc/`
shows updated callers.

#### G3: Output window dimension tracking

**Do**: Per Phase 6.3. Wire `CustomPaintWidget` resize events to update
`OutputWindow` width/height cache.

**Verify**: Builds clean.

#### G4: AccessWaylandPlatformInterface migration

**Do**: Per Phase 6.5. Update or remove the helper in `QRDUtils.cpp` based
on whether `QPlatformNativeInterface` still works in the target Qt6 version.

**Verify**: Builds clean.

### BUILD CHECKPOINT 2

**After**: All of D, E, F, G are complete.

**Do**:
```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_QRENDERDOC=ON \
    -DENABLE_WAYLAND_CAPTURE=ON \
    -DENABLE_WAYLAND_UI=ON \
    -DENABLE_PYRENDERDOC=ON ..
ninja
QT_QPA_PLATFORM=wayland ./bin/qrenderdoc
```

**Verify**:
- Builds with all flags enabled
- Launches on WSLg's Weston compositor
- Also launches with `QT_QPA_PLATFORM=xcb` fallback
- Python shell works
- Commit the working state

**Human review**: Review F1, F2 (new Wayland code). Everything else is
mechanical and build-verified.

---

### Task group H: renderdoccmd Wayland

Can run anytime after D1. New code, flag for review.

#### H1: renderdoccmd Wayland window creation

**Do**: Per Phase 6.6. Add Wayland client code to `renderdoccmd_linux.cpp`,
mirror existing XCB flow. Gate behind `RENDERDOC_WINDOWING_WAYLAND`.

**Verify**: Builds clean with wayland capture flag. Agent flags for review.

---

### Task group W: Windows build system (NOT in WSL)

Do on Windows, anytime after BUILD CHECKPOINT 1.

#### W1: qrenderdoc.pro Qt6 update

**Do**: Per Phases 3.2, 3.3, 4.5. Update .pro version checks, module list,
PySide detection. Replace bundled Qt5 in `3rdparty/qt/` with Qt6.

**Verify**: qmake + MSVC build succeeds, qrenderdoc launches on Windows.

---

### Execution order

```
A1 ─> B1,B2 (parallel) ─> B3 ─> B4,B5,B6,B7 (parallel)
 │                                      │
 └─> C1..C8 (all parallel) ────────────>│
                                         v
                                 BUILD CHECKPOINT 1  ── commit
                                    │    │    │
                                    v    v    v
                                   D1   E1   W1
                                    │         (Windows)
                            ┌───────┼───────┐
                            v       v       v
                         F1,F2   G1..G4    H1
                            │       │       │
                            v       v       v
                                 BUILD CHECKPOINT 2  ── commit
```

Groups B, C, and their sub-tasks can run in parallel. F and G can run in
parallel. Human review needed only at checkpoints and for F1, F2, H1
(new Wayland code).

Total human review points: 2 build checkpoints + 3 new-code reviews.

---

## Detailed Phase Reference

Everything below is the detailed technical reference that the agent tasks
above draw from.

---

## Dependency Graph

```
Phase 1: Scintilla 5.x ──┐
                          ├──> Phase 2: Qt6 Core Port ──> Phase 3: Build System
                          │           │
Phase 4: PySide6 ─────────┘           │
       (after Qt6)                     │
                                       ├──> Phase 5: Wayland Capture
                                       │       (linux-only, independent)
                                       │
                                       └──> Phase 6: Wayland UI
                                               (depends on Qt6 wayland backend)
```

Phase 1 and Phase 2 are sequential. Phase 4 depends on Phase 2. Phases 5 and 6
depend on Phase 3. Phase 5 and 6 can be done in either order, but 5 is simpler
and provides earlier value.

---

## Phase 1: Scintilla 3.7.2 to 5.5.x

**Estimated effort**: 3-5 days
**Risk**: Medium (narrow integration surface, but getText off-by-one is subtle)

### Why before Qt6

Scintilla 5.x ships with a Qt6-compatible platform layer. Upgrading first means
we get Qt6 Scintilla support for free and avoid patching 3.7.2 for Qt6
compatibility. Note: Scintilla 5.x still uses QTextCodec on Qt6 via the
`core5compat` module (upstream's design choice). This is a permanent small
dependency.

### 1.1 Replace vendored sources

Replace the entire `qrenderdoc/3rdparty/scintilla/` directory with upstream
Scintilla 5.5.x sources. Additionally, add Lexilla sources (the lexer library
split out in Scintilla 5.0).

**Source layout after replacement**:
```
qrenderdoc/3rdparty/scintilla/     (upstream Scintilla 5.5.x)
qrenderdoc/3rdparty/lexilla/       (upstream Lexilla, matching version)
```

Both are available from https://www.scintilla.org/

### 1.2 Audit local patches

RenderDoc has 6 local patches on top of Scintilla 3.7.2. Audit each against
upstream 5.x to determine which still need reapplying:

| Commit | Description | Likely status in 5.x |
|--------|-------------|---------------------|
| `0b410e987` | QString conversion fixes for no-const-char mode | Likely fixed upstream |
| `c5adc7a4e` | Relative paths for 3rdparty headers | Build-system only, reapply if needed |
| `2400f6e11` | Mouse tracking restore on show (upstream bug 1948) | Likely fixed upstream |
| `3b239dcfa` | Non-ASCII characters in CaseConvert.h comment | Likely fixed upstream |
| `497575b68` | Remove Qt::WA_StaticContents (crash when floating) | May still need reapplying -- check upstream ScintillaEditBase |
| `10aa4662e` | Fall-through comment for compiler warning | Likely fixed upstream (C++17 [[fallthrough]]) |

### 1.3 Build system: add Lexilla

Lexilla sources need to be compiled alongside Scintilla. Since RenderDoc
vendors everything, static compilation is the right approach.

**CMakeLists.txt** (`qrenderdoc/CMakeLists.txt`):
- Add Lexilla source files to the glob:
  ```cmake
  file(GLOB_RECURSE LEXILLA_FILES
      3rdParty/lexilla/lexlib/*.cxx
      3rdParty/lexilla/lexers/*.cxx
      3rdParty/lexilla/src/*.cxx)
  ```
- Add include paths for Lexilla headers
- Remove old `3rdParty/scintilla/lexers/` and `3rdParty/scintilla/lexlib/` from glob

**qrenderdoc.pro**:
- Mirror the same source/header additions
- Add Lexilla include paths

### 1.4 Lexer loading migration

The `SCI_SETLEXER` message (set by integer ID) is removed. Replace with
`SCI_SETILEXER` which takes an `ILexer5*` from Lexilla's `CreateLexer()`.

**Primary file**: `qrenderdoc/Code/ScintillaSyntax.cpp`

Current code (line ~347):
```cpp
if(lexLang == SCLEX_HLSL || lexLang == SCLEX_GLSL || lexLang == SCLEX_BUFFER)
    lexLang = SCLEX_CPP;
scintilla->setLexer(lexLang);
```

New code:
```cpp
#include "Lexilla.h"

const char *lexName = "null";
if(lexLang == SCLEX_HLSL || lexLang == SCLEX_GLSL ||
   lexLang == SCLEX_BUFFER || lexLang == SCLEX_CPP)
    lexName = "cpp";
else if(lexLang == SCLEX_PYTHON)
    lexName = "python";

Scintilla::ILexer5 *pLexer = Lexilla::CreateLexer(lexName);
scintilla->send(SCI_SETILEXER, 0, (sptr_t)pLexer);
```

The custom lexer IDs in `ScintillaSyntax.h` (`SCLEX_GLSL = 1000`,
`SCLEX_HLSL = 1001`, `SCLEX_BUFFER = 1002`) can remain as application-level
constants for branching keyword lists. They just no longer get passed to
Scintilla directly.

**Other files using SCLEX_\* constants** (5 total, verify each):
- `ShaderViewer.cpp`
- `PythonShell.cpp`
- `CommentView.cpp`
- `BufferFormatSpecifier.cpp`
- `ScintillaSyntax.cpp`

Most of these pass the lexer ID to `ConfigureSyntax()` which centralizes the
`setLexer` call. If that's the only callsite, the migration is confined to
`ScintillaSyntax.cpp`.

### 1.5 getText return value audit

**Critical**: Scintilla 5.1.5 changed `SCI_GETTEXT` to return length
*excluding* the NUL terminator (was including). This caused crashes in Geany.

Search for all `getText`, `getSelText`, `getCurLine` calls in qrenderdoc
consumer code. The pattern `edit->getText(edit->textLength() + 1)` appears in
`ShaderViewer.cpp`. Verify that the upstream 5.x `ScintillaEdit::TextReturner`
helper handles the new semantics correctly. If it does, consumer code is fine
unchanged. If not, the `+ 1` needs adjustment.

### 1.6 Notification signal update

`updateUi()` signal gains a parameter in 5.x. Grep for connections to this
signal across qrenderdoc and update slot signatures if any exist.

### 1.7 C++ standard

Scintilla 5.x requires C++17. Verify RenderDoc's build compiles the Scintilla
sources with `-std=c++17` or higher. The Scintilla/Lexilla CMake targets may
need an explicit `target_compile_features(... cxx_std_17)`.

### 1.8 Verification

- All three platforms build clean (no new warnings from Scintilla sources)
- Shader viewer displays HLSL/GLSL with correct syntax highlighting
- Python shell has correct Python highlighting
- Keyword completion still works
- No crashes on text selection, copy, search operations

---

## Phase 2: Qt5 to Qt6 Core Port

**Estimated effort**: 1-2 weeks
**Risk**: Low-medium (mostly mechanical, well-understood migration)

### 2.1 QDesktopWidget removal (8 files)

`QDesktopWidget` is removed in Qt6. Replace with `QScreen` API.

**Files**:
- `qrenderdoc/Windows/Dialogs/CrashDialog.cpp`
- `qrenderdoc/Widgets/Extended/RDTreeView.cpp`
- `qrenderdoc/3rdparty/toolwindowmanager/ToolWindowManager.cpp`
- ~5 additional (grep for `QDesktopWidget`)

**Pattern**:
```cpp
// Old
#include <QDesktopWidget>
QRect screenGeom = QApplication::desktop()->screenGeometry(widget);

// New
#include <QScreen>
QScreen *screen = widget->screen();  // or QGuiApplication::screenAt(pos)
QRect screenGeom = screen->geometry();
```

### 2.2 QRegExp to QRegularExpression (19 files)

Most qrenderdoc code already uses `QRegularExpression`. Remaining `QRegExp`
usage is in ~19 files, heavily in bundled 3rdparty headers. If Scintilla is
already upgraded (Phase 1), most of these are gone.

Core qrenderdoc files to check:
- `qrenderdoc/Windows/Dialogs/VirtualFileDialog.cpp`
- `qrenderdoc/Code/qprocessinfo.cpp`

**Pattern**:
```cpp
// Old
QRegExp re("pattern");
if(re.exactMatch(str)) { ... }

// New
QRegularExpression re("^pattern$");  // exactMatch needs anchoring
if(re.match(str).hasMatch()) { ... }
```

### 2.3 QString::SplitBehavior (3 instances)

```cpp
// Old
str.split(delim, QString::SkipEmptyParts)

// New
str.split(delim, Qt::SkipEmptyParts)
```

Files:
- `Windows/Dialogs/UpdateDialog.cpp`
- `Windows/EventBrowser.cpp`
- (grep for `QString::SkipEmptyParts` and `QString::KeepEmptyParts`)

### 2.4 QAction include path

`QAction` moved from `<QtWidgets/QAction>` to `<QtGui/QAction>` in Qt6. There
are ~515 QAction references. Most should resolve automatically if using
`<QAction>` without module prefix. Grep for explicit `QtWidgets/QAction`
includes.

### 2.5 QVector to QList (209 files)

In Qt6, `QVector` is a typedef for `QList`. Code compiles as-is. No changes
strictly required, but consider:
- Replacing `#include <QVector>` with `#include <QList>` (the old include
  still works via compat header)
- Optional: bulk rename `QVector<T>` to `QList<T>` for clarity

Low priority -- do this only if you want a clean diff.

### 2.6 Qt version guards (9 files)

Remove or simplify all `QT_VERSION_CHECK` guards. Current guards check for Qt
5.8, 5.9, 5.13, 5.14 features that are unconditionally available in Qt6.

Files:
- `Code/qrenderdoc.cpp` (3 guards)
- `Windows/Dialogs/CrashDialog.cpp` (1 guard)
- `Styles/RDTweakedNativeStyle.cpp` (1 guard)
- Scintilla 3rdparty (if still present after Phase 1)

### 2.7 High-DPI attribute changes

`qrenderdoc.cpp` sets `Qt::AA_EnableHighDpiScaling` and
`Qt::AA_UseHighDpiPixmaps`. Both attributes are removed in Qt6 (high-DPI
scaling and pixmap scaling are always enabled). Delete both calls and their
version guards.

### 2.8 X11Extras module removal

Qt6 removed the `QtX11Extras` module. If qrenderdoc uses `QX11Info::display()`
(it does, in `qrenderdoc.cpp` line ~580), replace with:
```cpp
// Old (Qt5)
#include <QX11Info>
Display *dpy = QX11Info::display();

// New (Qt6)
auto *x11App = qApp->nativeInterface<QNativeInterface::QX11Application>();
Display *dpy = x11App ? x11App->display() : nullptr;
```

This is linux-only code behind `#ifdef` guards.

### 2.9 QTextCodec in non-Scintilla code

If Scintilla is upgraded (Phase 1), check for remaining QTextCodec usage
outside Scintilla. Any remaining usage in qrenderdoc proper needs migration to
`QStringConverter` or can potentially just be removed (Qt6 assumes UTF-8
everywhere).

### 2.10 Verification

- Build on all three platforms with Qt6
- All UI panels render correctly
- Dialogs position correctly on multi-monitor setups (QDesktopWidget migration)
- Text search/replace works (QRegularExpression migration)
- High-DPI scaling works on Windows and macOS
- No runtime warnings about deprecated Qt APIs

---

## Phase 3: Build System Updates

**Estimated effort**: 3-5 days
**Risk**: Medium (dual build system, bundled Qt on Windows)

### 3.1 CMakeLists.txt (Unix/macOS) — qmake wrapper

**Architecture reminder**: `qrenderdoc/CMakeLists.txt` is NOT a standard CMake
Qt build. It generates a `.pri` config file and invokes qmake. There are no
`find_package(Qt5)`, `qt5_wrap_ui()`, or `qt5_add_resources()` calls — qmake
handles all of that internally via the `.pro` file.

**What to update in the CMake wrapper**:
- Rename `QMAKE_QT5_COMMAND` variable to `QMAKE_QT6_COMMAND` (line 12).
  Default to `qmake6` (Arch's Qt6 qmake binary name)
- Update version check (line 29): `5.5.999` -> `6.0`
- Update error message (line 32) to reference Qt6
- Update PySide/Shiboken `find_package` calls from `2` to `6` (lines 196-197)
- Update PySide/Shiboken target names in the `.pri` generation section
  (lines 305-344)
- Update the `ENABLE_UNSUPPORTED_EXPERIMENTAL_POSSIBLY_BROKEN_WAYLAND`
  flag name and `.pri` output (lines 273-278, see Phase 3.4)

**What to update in qrenderdoc.pro** (the actual build file):
- Version check: require Qt >= 6.0 (line 11)
- Module list: add `core5compat`, remove `x11extras` on Linux (lines 7, 161)
- C++ standard: `c++14` -> `c++17` (line 137)
- Remove `SCI_LEXER=1` define (line 423) — Scintilla 5.x uses Lexilla
- Update Scintilla/Lexilla source paths and includes (lines 424-436)

Note: Qt6 split `QtSvg` into `QtSvg` (rendering) and `QtSvgWidgets` (widget
classes). qrenderdoc only uses `QSvgRenderer` (in `PipelineStateViewer.cpp`),
which stays in `QtSvg`. No `svgwidgets` module needed in the .pro file.

### 3.2 qrenderdoc.pro (Windows)

Update version check:
```pro
# Old
lessThan(QT_MAJOR_VERSION, 5): error(...)

# New
lessThan(QT_MAJOR_VERSION, 6): error("requires Qt 6; found $$[QT_VERSION]")
```

Update module list:
```pro
# Old
QT += core gui widgets svg network

# New
QT += core gui widgets svg network
```

(Module name stays `svg` -- qrenderdoc only uses `QSvgRenderer`, which
remains in the base SVG module. `svgwidgets` is not needed.)

### 3.3 Bundled Qt on Windows

The `qrenderdoc/3rdparty/qt/` directory contains bundled Qt 5.9.4 for Windows
builds. This entire directory needs replacement with Qt6 equivalents:
- Headers in `include/`
- Libraries in `Win32/lib/` and `x64/lib/`
- qmake and tools in `Win32/bin/` and `x64/bin/`
- Platform plugins in `Win32/plugins/` and `x64/plugins/`
- mkspecs in `Win32/mkspecs/` and `x64/mkspecs/`

Download from Qt installer or build from source. Use the same Qt6 version
across all platforms for consistency.

### 3.4 Wayland cmake flag split

Separate the current monolithic flag into two:

```cmake
# Capture-side: hook Wayland WSI calls in target applications
option(ENABLE_WAYLAND_CAPTURE
    "Enable Wayland capture support (hooking Wayland applications)" OFF)

# UI-side: run qrenderdoc natively on Wayland
option(ENABLE_WAYLAND_UI
    "Enable native Wayland support for qrenderdoc UI (experimental)" OFF)
```

Both replace `ENABLE_UNSUPPORTED_EXPERIMENTAL_POSSIBLY_BROKEN_WAYLAND`. The
capture flag is safe to default ON once tested. The UI flag stays OFF until
Phase 6 is complete.

Map to compile definitions:
- `ENABLE_WAYLAND_CAPTURE` -> `-DRENDERDOC_WINDOWING_WAYLAND` (existing define)
- `ENABLE_WAYLAND_UI` -> `-DRENDERDOC_WAYLAND_UI` (new define, used in
  qrenderdoc only)

### 3.5 Verification

- Windows: builds with qmake + bundled Qt6
- macOS: builds with CMake + system/brew Qt6
- Linux: builds with CMake + system Qt6
- `renderdoccmd` builds on all platforms (no Qt dependency)
- PySide is disabled for this phase (enabled in Phase 4)

---

## Phase 4: PySide2 to PySide6

**Estimated effort**: 3-5 days
**Risk**: Medium (Shiboken API changes, but integration surface is small)

### 4.1 Overview

RenderDoc uses SWIG (a custom fork) for Python binding generation, NOT
Shiboken codegen. Shiboken is used only at **runtime** for QWidget type
conversion between Python and C++. The integration is ~25 API call sites in
a single file (`PythonContext.cpp`), plus CMake/pro build configuration.

### 4.2 CMake changes

```cmake
# Old
find_package(Shiboken2 QUIET)
find_package(PySide2 QUIET)
if(PySide2_FOUND AND Shiboken2_FOUND
   AND TARGET Shiboken2::libshiboken
   AND TARGET PySide2::pyside2)

# New
find_package(Shiboken6 QUIET)
find_package(PySide6 QUIET)
if(PySide6_FOUND AND Shiboken6_FOUND
   AND TARGET Shiboken6::libshiboken
   AND TARGET PySide6::pyside6)
```

Update defines:
- `PYSIDE2_ENABLED` -> `PYSIDE6_ENABLED`
- `PYSIDE2_SYS_PATH` -> `PYSIDE6_SYS_PATH`
- Library link: `-lshiboken2` -> `-lshiboken6` (verify exact naming)

### 4.3 PythonContext.cpp Shiboken API migration

**Header includes**:
```cpp
// Old
#include <pyside.h>
#include <shiboken.h>

// New -- verify exact headers for Shiboken6
#include <pyside.h>      // may be unchanged
#include <shiboken.h>    // may be unchanged
```

**Module imports** (lines ~409-426):
```cpp
// Old
Shiboken::AutoDecRef core(Shiboken::Module::import("PySide2.QtCore"));
SbkPySide2_QtCoreTypes = Shiboken::Module::getTypes(core);

Shiboken::AutoDecRef gui(Shiboken::Module::import("PySide2.QtGui"));
SbkPySide2_QtGuiTypes = Shiboken::Module::getTypes(gui);

Shiboken::AutoDecRef widgets(Shiboken::Module::import("PySide2.QtWidgets"));
SbkPySide2_QtWidgetsTypes = Shiboken::Module::getTypes(widgets);

// New
Shiboken::AutoDecRef core(Shiboken::Module::import("PySide6.QtCore"));
SbkPySide6_QtCoreTypes = Shiboken::Module::getTypes(core);

Shiboken::AutoDecRef gui(Shiboken::Module::import("PySide6.QtGui"));
SbkPySide6_QtGuiTypes = Shiboken::Module::getTypes(gui);

Shiboken::AutoDecRef widgets(Shiboken::Module::import("PySide6.QtWidgets"));
SbkPySide6_QtWidgetsTypes = Shiboken::Module::getTypes(widgets);
```

**Global type arrays** -- rename:
```cpp
// Old
PyTypeObject **SbkPySide2_QtCoreTypes = NULL;
PyTypeObject **SbkPySide2_QtGuiTypes = NULL;
PyTypeObject **SbkPySide2_QtWidgetsTypes = NULL;

// New
PyTypeObject **SbkPySide6_QtCoreTypes = NULL;
PyTypeObject **SbkPySide6_QtGuiTypes = NULL;
PyTypeObject **SbkPySide6_QtWidgetsTypes = NULL;
```

**QWidget conversion functions** (~25 call sites total):
```cpp
Shiboken::Object::checkType(widget)     // verify unchanged in Shiboken6
Shiboken::Object::cppPointer(...)       // verify signature
Shiboken::SbkType<QWidget>()            // verify unchanged
Shiboken::Object::newObject(...)        // verify signature
```

These Shiboken functions are the main risk area. Check the Shiboken6 API
headers to verify signatures. The functions likely exist with the same names
but may have different parameter types or return types.

### 4.4 SWIG interface files

The SWIG `.i` files (`renderdoc.i`, `qrenderdoc.i`) should not need changes
for PySide6 since SWIG generates the bindings independently. However, verify:
- Custom typemaps for Qt types still work
- The `container_handling.i` type conversion infrastructure is unaffected

### 4.5 qrenderdoc.pro (Windows)

Update the PySide2 detection and linking:
```pro
# Old
exists( $$_PRO_FILE_PWD_/3rdparty/pyside/include/PySide2/pyside.h ) { ... }

# New
exists( $$_PRO_FILE_PWD_/3rdparty/pyside/include/PySide6/pyside.h ) { ... }
```

Update bundled PySide in `3rdparty/pyside/` with PySide6 equivalents.

### 4.6 Documentation updates

Grep for `PySide2` in all `.rst`, `.md`, and comment blocks. Update references.
The wiki page at `https://github.com/baldurk/renderdoc/wiki/PySide2` should
be noted as outdated in the fork.

### 4.7 Verification

- Python shell opens and accepts input
- `import renderdoc` works in the Python console
- `import qrenderdoc` works and can access widget getters
- PySide6 QWidget objects are properly converted when passed between Python
  and C++
- Python extension loading works (Extensions.h callbacks)
- All platforms: disable PySide6, verify graceful degradation (opaque widget
  handles still work)

---

## Phase 5: Wayland Capture Stabilization

**Estimated effort**: 2-3 days
**Risk**: Low-medium (code mostly exists, needs testing and small fixes)
**Platform**: Linux only. Windows and macOS codepaths untouched.

### 5.1 Enable the capture flag by default (in your fork)

After splitting the cmake flag (Phase 3.4), enable `ENABLE_WAYLAND_CAPTURE`
by default in your fork's CMake. This compiles in the Wayland WSI hooking
and keyboard input code that already exists.

### 5.2 EGL display detection improvement

The current `UseUnknownDisplay()` in `linux_stringio.cpp:532-556` uses a
`dladdr` hack to distinguish `wl_display*` from X11 `Display*` by checking
if the first pointer-sized bytes point to `wl_display_interface`.

This works but is fragile. A more robust approach: hook
`eglGetPlatformDisplay` / `eglGetPlatformDisplayEXT` in the EGL hooking
layer. These functions take an explicit `EGLenum platform` parameter
(`EGL_PLATFORM_WAYLAND_KHR`, `EGL_PLATFORM_X11_KHR`, etc.) which
unambiguously identifies the display type.

**Files**:
- `renderdoc/driver/gl/egl_hooks.cpp` (hook implementation)
- `renderdoc/os/posix/linux/linux_stringio.cpp` (display registration)

### 5.3 Keyboard dispatch for pure Vulkan apps

The Wayland keyboard input in `linux_stringio.cpp` uses `wl_keyboard`
callbacks which only fire when the application dispatches Wayland events.
Pure Vulkan apps that don't use libwayland for their event loop won't pump
these callbacks.

Fix: create a dedicated `wl_event_queue` for RenderDoc's keyboard listener
and do a non-blocking `wl_display_dispatch_queue_pending()` during
`RenderDoc::Tick()` (which runs inside the present call).

```cpp
// In UseWaylandDisplay():
wl_event_queue *rdocQueue = wl_display_create_queue(disp);
// Assign registry and seat objects to this queue

// In GetKeyState() or a Tick() callsite:
wl_display_dispatch_queue_pending(disp, rdocQueue);
```

This ensures keyboard state updates even if the app doesn't dispatch the
default queue frequently. The `wl_event_queue` mechanism is explicitly
designed for this multi-component-on-shared-connection use case (per
ppaalanen's comment on the issue).

**Files**:
- `renderdoc/os/posix/linux/linux_stringio.cpp`

### 5.4 Vulkan capture path verification

The Vulkan Wayland capture path in `vk_linux.cpp:86-110` looks correct.
`vkCreateWaylandSurfaceKHR` is hooked, registers the surface, and stores
the display. Verify by:

1. Building with `ENABLE_WAYLAND_CAPTURE=ON`
2. Launching a Wayland Vulkan app via `renderdoccmd capture`
3. Triggering a capture via keyboard shortcut
4. Verifying the capture file is valid and can be replayed

### 5.5 EGL/GL capture path verification

The EGL hooking path should work for Wayland GL apps since EGL is the only
GL binding API on Wayland. Verify:

1. Launch a Wayland EGL/GL app via renderdoc
2. Confirm API detection shows "OpenGL" or "OpenGL ES"
3. Trigger capture
4. Verify replay works

### 5.6 Verification

- Vulkan Wayland app: capture and replay works
- EGL/GL Wayland app: capture and replay works
- Keyboard shortcuts (F12/PrintScreen for capture) work while app has focus
- In-application API (`StartFrameCapture`/`EndFrameCapture`) works
- Overlay text renders (this is API-level, not platform-specific)
- qrenderdoc running under XWayland can connect to and control Wayland apps

---

## Phase 6: Native Wayland UI

**Estimated effort**: 1-2 weeks (high variance due to compositor-specific issues)
**Risk**: High (subsurface embedding, compositor quirks)
**Platform**: Linux only. Windows and macOS codepaths untouched.

### 6.1 Qt6 Wayland platform detection

Replace the current Qt5 Wayland detection in `qrenderdoc.cpp`:

```cpp
// Old (Qt5, lines ~579-594)
if(QGuiApplication::platformName() == "wayland")
{
    // Shows scary warning dialog
    env.waylandDisplay =
        (wl_display *)AccessWaylandPlatformInterface("display", NULL);
}

// New (Qt6)
if(QGuiApplication::platformName() == "wayland")
{
    // No warning dialog -- this is a supported path now
    auto *waylandApp =
        qApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if(waylandApp)
        env.waylandDisplay = waylandApp->display();
}
```

Remove the `QT_QPA_PLATFORM=xcb` force-override in `qrenderdoc.cpp` (line
~208) when Wayland UI is enabled. Keep it as the default when
`RENDERDOC_WAYLAND_UI` is not defined.

### 6.2 Rendering surface embedding

This is the hardest part. The texture viewer, mesh viewer, and other panels
embed Vulkan/GL replay surfaces inside Qt widgets via `CustomPaintWidget`.

**Current flow** (`CaptureContext.cpp:2192-2222`):
```cpp
if(m_CurWinSystem == WindowingSystem::Wayland)
{
    wl_surface *surface =
        (wl_surface *)AccessWaylandPlatformInterface("surface",
                                                     window->windowHandle());
    return CreateWaylandWindowingData(m_WaylandDisplay, surface);
}
```

**Qt6 equivalent**:
```cpp
if(m_CurWinSystem == WindowingSystem::Wayland)
{
    auto *waylandWindow =
        window->windowHandle()->nativeInterface<
            QNativeInterface::Private::QWaylandWindow>();
    if(waylandWindow)
    {
        wl_surface *surface = waylandWindow->surface();
        return CreateWaylandWindowingData(m_WaylandDisplay, surface);
    }
}
```

**Key concern**: `QNativeInterface::Private::QWaylandWindow` is a private Qt
API. It may change between Qt6 minor versions. Check availability and
stability. Alternative: continue using `QPlatformNativeInterface` if it still
works in Qt6's Wayland backend.

### 6.3 Output window dimension tracking

Currently a TODO in `vk_linux.cpp:304-312`:
```cpp
if(outw.m_WindowSystem == WindowingSystem::Wayland)
{
    RDCWARN("Need Wayland query for current surface dimensions");
    w = RDCMAX(1U, outw.width);
    h = RDCMAX(1U, outw.height);
    return;
}
```

Wayland doesn't support querying surface dimensions on demand. The compositor
tells you your size via `xdg_toplevel::configure` events. For embedded
surfaces (subsurfaces), the parent determines the size.

**Solution**: When the Qt widget hosting the replay surface resizes, propagate
the new dimensions to the `OutputWindow` struct. Connect to the widget's
`resizeEvent` and update `outw.width` / `outw.height`. The cached-value
approach already in place becomes correct once the cache is actually updated.

**Files**:
- `qrenderdoc/Widgets/CustomPaintWidget.cpp` (emit resize)
- `renderdoc/driver/vulkan/vk_linux.cpp` (consume cached dimensions)
- Possibly `renderdoc/driver/vulkan/vk_replay.h` (OutputWindow struct)

### 6.4 Subsurface synchronization

When Qt creates a `wl_surface` for an embedded widget and RenderDoc renders
to it via Vulkan, both Qt's compositor and Vulkan are submitting buffer
updates to the same surface hierarchy. This can cause:
- Flicker (unsynchronized buffer swaps)
- Black rectangles (surface not yet committed)
- Resize glitches (old buffer size displayed during resize)

**Mitigation strategies** (try in order):
1. Use `wl_subsurface_set_desync()` for the replay surface so it updates
   independently of Qt's frame
2. Ensure `vkQueuePresentKHR` and Qt's frame callbacks don't race
3. If subsurface issues persist, consider rendering to an offscreen buffer
   and blitting to a `QImage` displayed in a standard `QWidget` (fallback,
   higher latency)

This is the most experimental part of the entire plan. Expect compositor-
specific behavior. Test on your target compositor (likely Sway, Hyprland, or
KDE Plasma) and fix issues as they appear.

### 6.5 AccessWaylandPlatformInterface migration

The helper in `QRDUtils.cpp:3519-3524` uses `QPlatformNativeInterface`:
```cpp
void *AccessWaylandPlatformInterface(const QByteArray &resource, QWindow *window)
{
    QPlatformNativeInterface *native =
        QGuiApplication::platformNativeInterface();
    return native->nativeResourceForWindow(resource, window);
}
```

Qt6 deprecated `QPlatformNativeInterface` in favor of `QNativeInterface`. If
the old API still works in Qt6 (it does in early Qt6 versions), you can defer
this migration. If it's removed in your target Qt6 version, replace all
callers with the typed `QNativeInterface` equivalents from 6.2 above.

### 6.6 renderdoccmd Wayland support (optional)

`renderdoccmd_linux.cpp` creates raw XCB windows for replay preview. Adding
Wayland support means writing a minimal Wayland client:
- Connect to `wl_display`
- Get `wl_compositor` and `xdg_wm_base` from registry
- Create `wl_surface` + `xdg_surface` + `xdg_toplevel`
- Handle configure events for dimensions
- Pass `wl_surface` to the replay driver

This is ~200 lines of standard Wayland client code. Lower priority since
`renderdoccmd` is primarily used headless. Can fall back to XCB via XWayland
for the preview window.

### 6.7 Verification

- qrenderdoc launches natively on Wayland (no `QT_QPA_PLATFORM=xcb`)
- All UI panels render correctly
- Texture viewer displays captured textures
- Mesh viewer renders geometry
- Pipeline state viewer works
- Resize and window management work without flicker
- Multi-monitor works
- Drag and drop works (file drops)
- Keyboard shortcuts work (no XWayland required)
- All of the above also still work on Windows and macOS

---

## Cross-Platform Validation Checklist

After all phases, verify the complete matrix:

| Feature | Windows | macOS | Linux (X11) | Linux (Wayland) |
|---------|---------|-------|-------------|-----------------|
| qrenderdoc launches | | | | |
| Vulkan capture | | | | |
| OpenGL capture | | | | |
| Capture via keyboard | | | | |
| Capture via API | | | | |
| Replay/inspection | | | | |
| Texture viewer | | | | |
| Shader viewer (Scintilla) | | | | |
| Python shell | | | | |
| PySide6 widget access | | | | |
| Remote capture | | | | |
| renderdoccmd replay | | | | |

---

## Fork Maintenance Strategy

### Upstream sync
- Keep a `upstream/v1.x` branch tracking baldurk's `v1.x`
- Periodically merge upstream into your working branch
- Capture-side code (`renderdoc/` directory) changes most frequently upstream;
  keep your diff there minimal
- `qrenderdoc/` changes less frequently upstream; the Qt6 port creates a large
  but stable diff that rarely conflicts

### Qt6-only, no dual support guards
Go Qt6-only. Do not use `#if QT_VERSION` guards to keep Qt5 codepaths alive.

Rationale: dual guards add noise to every changed file, create a second
untested codepath with zero users, and don't actually help with upstream
merges -- upstream Qt5 code that merges cleanly through a Qt5 guard still
needs testing and verification on Qt6, so you're doing the same porting work
either way, just deferred and less visible. The qrenderdoc/ directory changes
infrequently upstream, so the incremental cost of porting new upstream Qt5
additions to Qt6 during merge is small.

### Merge strategy
- `qrenderdoc/` merges will require manual resolution when upstream touches
  Qt5-specific code. This is infrequent and each instance is small.
- The Scintilla upgrade is a clean directory replacement -- no merge conflicts
  with upstream's vendored 3.7.2 (just re-replace on merge)

### What to never touch
- `renderdoc/api/` -- the public API headers. Upstream changes here affect
  every consumer.
- `renderdoc/driver/` -- core capture/replay logic. Upstream improvements
  (new extensions, driver workarounds) flow through here constantly.
- Keep these directories as close to upstream as possible.
