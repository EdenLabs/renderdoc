/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "renderdoccmd.h"
#include <dlfcn.h>
#include <iconv.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>

#if defined(RENDERDOC_WINDOWING_XLIB)
#include <X11/Xlib-xcb.h>
#endif

#if defined(RENDERDOC_WINDOWING_WAYLAND_REPLAY)
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#endif

#include <replay/renderdoc_replay.h>

void Daemonise()
{
  // don't change dir, but close stdin/stdou
  daemon(1, 0);
}

static Display *display = NULL;

#if defined(RENDERDOC_WINDOWING_WAYLAND_REPLAY)

// State for a minimal Wayland client window used for replay preview.
struct WaylandWindow
{
  wl_display *display       = NULL;
  wl_registry *registry     = NULL;
  wl_compositor *compositor = NULL;
  wl_surface *surface       = NULL;

  xdg_wm_base *wmBase   = NULL;
  xdg_surface *xdgSurf  = NULL;
  xdg_toplevel *toplevel = NULL;

  uint32_t width  = 0;
  uint32_t height = 0;

  // Set true once the first xdg_surface configure has been acked.
  bool configured = false;
  // Set true when the compositor requests close.
  bool closed = false;
};

// --- xdg_toplevel listener ---------------------------------------------------

static void xdgToplevelConfigure(void *data, xdg_toplevel *, int32_t w,
                                 int32_t h, wl_array *)
{
  WaylandWindow *win = (WaylandWindow *)data;
  if(w > 0 && h > 0)
  {
    win->width = (uint32_t)w;
    win->height = (uint32_t)h;
  }
}

static void xdgToplevelClose(void *data, xdg_toplevel *)
{
  WaylandWindow *win = (WaylandWindow *)data;
  win->closed = true;
}

// configure_bounds was added in xdg-shell version 4, wm_capabilities in version 5.
// Guard the extra callbacks so that this compiles against older protocol headers.
#ifdef XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION
static void xdgToplevelConfigureBounds(void *, xdg_toplevel *,
                                       int32_t, int32_t)
{
}
#endif

#ifdef XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION
static void xdgToplevelWmCapabilities(void *, xdg_toplevel *,
                                      wl_array *)
{
}
#endif

static const xdg_toplevel_listener toplevelListener = {
    xdgToplevelConfigure,
    xdgToplevelClose,
#ifdef XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION
    xdgToplevelConfigureBounds,
#endif
#ifdef XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION
    xdgToplevelWmCapabilities,
#endif
};

// --- xdg_surface listener ----------------------------------------------------

static void xdgSurfaceConfigure(void *data, xdg_surface *surf,
                                uint32_t serial)
{
  WaylandWindow *win = (WaylandWindow *)data;
  xdg_surface_ack_configure(surf, serial);
  win->configured = true;
}

static const xdg_surface_listener surfaceListener = {
    xdgSurfaceConfigure,
};

// --- xdg_wm_base listener ---------------------------------------------------

static void xdgWmBasePing(void *, xdg_wm_base *base, uint32_t serial)
{
  xdg_wm_base_pong(base, serial);
}

static const xdg_wm_base_listener wmBaseListener = {
    xdgWmBasePing,
};

// --- wl_registry listener ----------------------------------------------------

static void registryGlobal(void *data, wl_registry *reg,
                           uint32_t name, const char *interface,
                           uint32_t version)
{
  WaylandWindow *win = (WaylandWindow *)data;

  if(strcmp(interface, wl_compositor_interface.name) == 0)
  {
    win->compositor =
        (wl_compositor *)wl_registry_bind(reg, name,
                                          &wl_compositor_interface, 4);
  }
  else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
  {
    win->wmBase =
        (xdg_wm_base *)wl_registry_bind(reg, name,
                                        &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(win->wmBase, &wmBaseListener, win);
  }
}

static void registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

static const wl_registry_listener registryListener = {
    registryGlobal,
    registryGlobalRemove,
};

// Create a Wayland window. Returns true on success, false on failure.
// The caller is responsible for calling WaylandWindowDestroy when done.
static bool WaylandWindowCreate(WaylandWindow &win, wl_display *wlDisplay,
                                uint32_t w, uint32_t h, const char *title)
{
  win.display = wlDisplay;
  win.width   = w;
  win.height  = h;

  win.registry = wl_display_get_registry(win.display);
  wl_registry_add_listener(win.registry, &registryListener, &win);

  // Roundtrip to receive registry globals.
  wl_display_roundtrip(win.display);

  if(!win.compositor || !win.wmBase)
  {
    std::cerr << "Wayland compositor missing wl_compositor or xdg_wm_base"
              << std::endl;
    return false;
  }

  win.surface = wl_compositor_create_surface(win.compositor);
  win.xdgSurf = xdg_wm_base_get_xdg_surface(win.wmBase, win.surface);
  xdg_surface_add_listener(win.xdgSurf, &surfaceListener, &win);

  win.toplevel = xdg_surface_get_toplevel(win.xdgSurf);
  xdg_toplevel_add_listener(win.toplevel, &toplevelListener, &win);
  xdg_toplevel_set_title(win.toplevel, title);
  xdg_toplevel_set_app_id(win.toplevel, "renderdoccmd");

  // Commit the surface to trigger the initial configure sequence.
  wl_surface_commit(win.surface);

  // Block until the compositor sends the initial configure event so that
  // the surface is ready for use before we hand it to the replay driver.
  while(!win.configured && !win.closed)
    wl_display_roundtrip(win.display);

  return true;
}

static void WaylandWindowDestroy(WaylandWindow &win)
{
  if(win.toplevel)
    xdg_toplevel_destroy(win.toplevel);
  if(win.xdgSurf)
    xdg_surface_destroy(win.xdgSurf);
  if(win.surface)
    wl_surface_destroy(win.surface);
  if(win.wmBase)
    xdg_wm_base_destroy(win.wmBase);
  if(win.compositor)
    wl_compositor_destroy(win.compositor);
  if(win.registry)
    wl_registry_destroy(win.registry);

  win = {};
}

// The Wayland display connection used for renderdoccmd windows.
static wl_display *waylandDisplay = NULL;

#endif    // RENDERDOC_WINDOWING_WAYLAND_REPLAY

WindowingData DisplayRemoteServerPreview(bool active, const rdcarray<WindowingSystem> &systems)
{
  static WindowingData remoteServerPreview = {WindowingSystem::Unknown};

#if defined(RENDERDOC_WINDOWING_WAYLAND_REPLAY)
  // Static state for the Wayland remote server preview window. Persists across
  // calls so that the window stays alive while the remote server is active.
  static WaylandWindow waylandPreviewWindow = {};

  if(waylandDisplay)
  {
    bool wantWayland = false;
    for(size_t i = 0; i < systems.size(); i++)
    {
      if(systems[i] == WindowingSystem::Wayland)
        wantWayland = true;
    }

    if(wantWayland)
    {
      if(active)
      {
        if(remoteServerPreview.system == WindowingSystem::Unknown)
        {
          if(!WaylandWindowCreate(waylandPreviewWindow, waylandDisplay,
                                  1280, 720, "Remote Server Preview"))
          {
            std::cerr << "Couldn't create Wayland preview window"
                      << std::endl;
            return remoteServerPreview;
          }

          remoteServerPreview =
              CreateWaylandWindowingData(waylandPreviewWindow.display,
                                        waylandPreviewWindow.surface);
        }
        else
        {
          // Dispatch pending events without blocking.
          wl_display_dispatch_pending(waylandDisplay);
          wl_display_flush(waylandDisplay);
        }
      }
      else
      {
        WaylandWindowDestroy(waylandPreviewWindow);
        remoteServerPreview = {WindowingSystem::Unknown};
      }

      return remoteServerPreview;
    }
  }
#endif

// we only have the preview implemented for platforms that have xlib & xcb. It's unlikely
// a meaningful platform exists with only one, and at the time of writing no other windowing
// systems are supported on linux for the replay
#if defined(RENDERDOC_WINDOWING_XLIB) && defined(RENDERDOC_WINDOWING_XCB)
  if(active)
  {
    if(remoteServerPreview.system == WindowingSystem::Unknown)
    {
      // if we're first initialising, create the window
      if(display == NULL)
        return remoteServerPreview;

      int scr = DefaultScreen(display);

      xcb_connection_t *connection = XGetXCBConnection(display);

      if(connection == NULL)
      {
        std::cerr << "Couldn't get XCB connection from Xlib Display" << std::endl;
        return remoteServerPreview;
      }

      XSetEventQueueOwner(display, XCBOwnsEventQueue);

      const xcb_setup_t *setup = xcb_get_setup(connection);
      xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
      while(scr-- > 0)
        xcb_screen_next(&iter);

      xcb_screen_t *screen = iter.data;

      uint32_t value_mask, value_list[32];

      xcb_window_t window = xcb_generate_id(connection);

      value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
      value_list[0] = screen->black_pixel;
      value_list[1] =
          XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

      xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 1280, 720, 0,
                        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, value_mask, value_list);

      /* Magic code that will send notification when window is destroyed */
      xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
      xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, 0);

      xcb_intern_atom_cookie_t cookie2 = xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
      xcb_intern_atom_reply_t *atom_wm_delete_window = xcb_intern_atom_reply(connection, cookie2, 0);

      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
                          XCB_ATOM_STRING, 8, sizeof("Remote Server Preview") - 1,
                          "Remote Server Preview");

      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, (*reply).atom, 4, 32, 1,
                          &(*atom_wm_delete_window).atom);
      free(reply);

      xcb_map_window(connection, window);

      bool xcb = false, xlib = false;

      for(size_t i = 0; i < systems.size(); i++)
      {
        if(systems[i] == WindowingSystem::Xlib)
          xlib = true;
        if(systems[i] == WindowingSystem::XCB)
          xcb = true;
      }

      // prefer xcb
      if(xcb)
        remoteServerPreview = CreateXCBWindowingData(connection, window);
      else if(xlib)
        remoteServerPreview = CreateXlibWindowingData(display, (Drawable)window);

      xcb_flush(connection);
    }
    else
    {
      // otherwise, we can pump messages here, but we don't actually care to process any. Just clear
      // the queue
      xcb_generic_event_t *event = NULL;

      xcb_connection_t *connection = remoteServerPreview.xcb.connection;

      if(remoteServerPreview.system == WindowingSystem::Xlib)
        connection = XGetXCBConnection(remoteServerPreview.xlib.display);

      if(connection)
      {
        do
        {
          event = xcb_poll_for_event(connection);
          if(event)
            free(event);
        } while(event);
      }
    }
  }
  else
  {
    // reset the windowing data to 'no window'
    remoteServerPreview = {WindowingSystem::Unknown};
  }
#endif

  return remoteServerPreview;
}

void DisplayRendererPreview(IReplayController *renderer, TextureDisplay &displayCfg, uint32_t width,
                            uint32_t height, uint32_t numLoops)
{
#if defined(RENDERDOC_WINDOWING_WAYLAND_REPLAY)
  if(waylandDisplay)
  {
    rdcarray<WindowingSystem> systems = renderer->GetSupportedWindowSystems();

    bool wantWayland = false;
    for(size_t i = 0; i < systems.size(); i++)
    {
      if(systems[i] == WindowingSystem::Wayland)
        wantWayland = true;
    }

    if(wantWayland)
    {
      WaylandWindow win = {};

      if(!WaylandWindowCreate(win, waylandDisplay, width, height,
                              "renderdoccmd"))
      {
        std::cerr << "Couldn't create Wayland preview window"
                  << std::endl;
        return;
      }

      IReplayOutput *out =
          renderer->CreateOutput(
              CreateWaylandWindowingData(win.display, win.surface),
              ReplayOutputType::Texture);

      if(!out)
      {
        std::cerr << "Couldn't create replay output on Wayland surface"
                  << std::endl;
        WaylandWindowDestroy(win);
        return;
      }

      out->SetTextureDisplay(displayCfg);

      uint32_t loopCount = 0;

      while(!win.closed)
      {
        // Non-blocking dispatch of Wayland events (configure, ping, close).
        while(wl_display_prepare_read(waylandDisplay) != 0)
          wl_display_dispatch_pending(waylandDisplay);
        wl_display_flush(waylandDisplay);
        wl_display_read_events(waylandDisplay);
        wl_display_dispatch_pending(waylandDisplay);

        renderer->SetFrameEvent(10000000, true);
        out->Display();

        usleep(100000);

        loopCount++;

        if(numLoops > 0 && loopCount == numLoops)
          break;
      }

      WaylandWindowDestroy(win);
      return;
    }
  }
#endif

// we only have the preview implemented for platforms that have xlib & xcb. It's unlikely
// a meaningful platform exists with only one, and at the time of writing no other windowing
// systems are supported on linux for the replay
#if defined(RENDERDOC_WINDOWING_XLIB) && defined(RENDERDOC_WINDOWING_XCB)
  // need to create a hybrid setup xlib and xcb in case only one or the other is supported.
  // We'll prefer xcb

  if(display == NULL)
  {
    std::cerr << "Couldn't open X Display" << std::endl;
    return;
  }

  int scr = DefaultScreen(display);

  xcb_connection_t *connection = XGetXCBConnection(display);

  if(connection == NULL)
  {
    std::cerr << "Couldn't get XCB connection from Xlib Display" << std::endl;
    return;
  }

  XSetEventQueueOwner(display, XCBOwnsEventQueue);

  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  while(scr-- > 0)
    xcb_screen_next(&iter);

  xcb_screen_t *screen = iter.data;

  uint32_t value_mask, value_list[32];

  xcb_window_t window = xcb_generate_id(connection);

  value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  value_list[0] = screen->black_pixel;
  value_list[1] =
      XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

  xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, width, height, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, value_mask, value_list);

  /* Magic code that will send notification when window is destroyed */
  xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
  xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, 0);

  xcb_intern_atom_cookie_t cookie2 = xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
  xcb_intern_atom_reply_t *atom_wm_delete_window = xcb_intern_atom_reply(connection, cookie2, 0);

  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
                      8, sizeof("renderdoccmd") - 1, "renderdoccmd");

  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, (*reply).atom, 4, 32, 1,
                      &(*atom_wm_delete_window).atom);
  free(reply);

  xcb_map_window(connection, window);

  rdcarray<WindowingSystem> systems = renderer->GetSupportedWindowSystems();

  bool xcb = false, xlib = false;

  for(size_t i = 0; i < systems.size(); i++)
  {
    if(systems[i] == WindowingSystem::Xlib)
      xlib = true;
    if(systems[i] == WindowingSystem::XCB)
      xcb = true;
  }

  IReplayOutput *out = NULL;

  // prefer xcb
  if(xcb)
  {
    out = renderer->CreateOutput(CreateXCBWindowingData(connection, window),
                                 ReplayOutputType::Texture);
  }
  else if(xlib)
  {
    out = renderer->CreateOutput(CreateXlibWindowingData(display, (Drawable)window),
                                 ReplayOutputType::Texture);
  }
  else
  {
    std::cerr << "Neither XCB nor XLib are supported, can't create window." << std::endl;
    std::cerr << "Supported systems: ";
    for(size_t i = 0; i < systems.size(); i++)
      std::cerr << (uint32_t)systems[i] << std::endl;
    std::cerr << std::endl;
    return;
  }

  out->SetTextureDisplay(displayCfg);

  xcb_flush(connection);

  uint32_t loopCount = 0;

  bool done = false;
  while(!done)
  {
    xcb_generic_event_t *event;

    event = xcb_poll_for_event(connection);
    if(event)
    {
      switch(event->response_type & 0x7f)
      {
        case XCB_EXPOSE: break;
        case XCB_CLIENT_MESSAGE:
          if((*(xcb_client_message_event_t *)event).data.data32[0] == (*atom_wm_delete_window).atom)
          {
            done = true;
          }
          break;
        case XCB_KEY_RELEASE:
        {
          const xcb_key_release_event_t *key = (const xcb_key_release_event_t *)event;

          if(key->detail == 0x9)
            done = true;
        }
        break;
        case XCB_DESTROY_NOTIFY: done = true; break;
        default: break;
      }
      free(event);
    }

    renderer->SetFrameEvent(10000000, true);
    out->Display();

    usleep(100000);

    loopCount++;

    if(numLoops > 0 && loopCount == numLoops)
      break;
  }
#else
  std::cerr << "No supporting windowing systems defined at build time (xlib and xcb)" << std::endl;
#endif
}

void sig_handler(int signo)
{
  if(usingKillSignal)
    killSignal = true;
  else
    exit(1);
}

int main(int argc, char *argv[])
{
  setlocale(LC_CTYPE, "");

  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  GlobalEnvironment env;

#if defined(RENDERDOC_WINDOWING_WAYLAND_REPLAY)
  // Try to connect to a Wayland compositor. If WAYLAND_DISPLAY is set the
  // session is likely Wayland-native; otherwise we skip and fall back to X.
  if(getenv("WAYLAND_DISPLAY"))
  {
    waylandDisplay = wl_display_connect(NULL);
    if(waylandDisplay)
      env.waylandDisplay = waylandDisplay;
  }
#endif

#if defined(RENDERDOC_WINDOWING_XLIB) || defined(RENDERDOC_WINDOWING_XCB)
  // call XInitThreads - although we don't use xlib concurrently the driver might need to.
  XInitThreads();

  // we don't check if display successfully opened, it's only a problem if it's needed later.
  display = env.xlibDisplay = XOpenDisplay(NULL);
#endif

  // add compiled-in support to version line
  {
    std::string support = "APIs supported at compile-time: ";
    int count = 0;

#if defined(RENDERDOC_SUPPORT_VULKAN)
    support += "Vulkan, ";
    count++;
#endif

#if defined(RENDERDOC_SUPPORT_GL)
    support += "GL, ";
    count++;
#endif

#if defined(RENDERDOC_SUPPORT_GLES)
    support += "GLES, ";
    count++;
#endif

    if(count == 0)
    {
      support += "None.";
    }
    else
    {
      // remove trailing ', '
      support.pop_back();
      support.pop_back();
      support += ".";
    }

    add_version_line(support);

    support = "Windowing systems supported at compile-time: ";
    count = 0;

#if defined(RENDERDOC_WINDOWING_XLIB)
    support += "xlib, ";
    count++;
#endif

#if defined(RENDERDOC_WINDOWING_XCB)
    support += "XCB, ";
    count++;
#endif

#if defined(RENDERDOC_WINDOWING_WAYLAND)
    support += "Wayland, ";
    count++;
#endif

#if defined(RENDERDOC_SUPPORT_VULKAN)
    support += "Vulkan KHR_display, ";
    count++;
#endif

    if(count == 0)
    {
      support += "None.";
    }
    else
    {
      // remove trailing ', '
      support.pop_back();
      support.pop_back();
      support += ".";
    }

    add_version_line(support);
  }

  int ret = renderdoccmd(env, argc, argv);

#if defined(RENDERDOC_WINDOWING_XLIB) || defined(RENDERDOC_WINDOWING_XCB)
  if(display)
    XCloseDisplay(display);
#endif

#if defined(RENDERDOC_WINDOWING_WAYLAND_REPLAY)
  if(waylandDisplay)
    wl_display_disconnect(waylandDisplay);
#endif

  return ret;
}
