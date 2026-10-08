# Architecture: threads

`rn-gtk-host` runs React Native's C++ core (ReactCxxPlatform, Fabric,
Hermes) next to GTK4. GTK is single-threaded, and React Native wants its
JS off the UI thread, so work is split across these threads:

| Thread | Runs | Created by |
| --- | --- | --- |
| **GTK main** | Widgets: mounting, drawing, input, scrolling, the frame clock (native Animated steps, scroll-to animations), DevUI | `main()` |
| **JS** (`rngtk-js`) | Hermes, the React reconciler, RuntimeScheduler, TurboModules, Yoga layout and text measurement for commits made from JS | `JsMessageQueueThread`, once per JS instance |
| **rngtk-network** | All libsoup work (fetch, XHR, WebSocket, Metro, image downloads) on its own `GMainContext` | first HTTP or WebSocket use |
| Image workers (2) | Reading image files, decoding `data:` URIs, decoding images into `GdkTexture`s | `GtkImageLoader` |
| ReactHost's reload thread | Tearing down and re-creating the JS instance on reload | ReactHost, per reload |
| Dev loader | The first bundle download from Metro (it blocks on a future) | `RNGtkHost`, dev mode |
| Hermes, timers, inspector | Hermes' GC, ReactCxxPlatform's timer and packager-reconnect threads | ReactCxxPlatform |

## Rules

- **GTK only on the main thread.** Everything that reaches widgets from
  another thread goes through the main loop:
  - Fabric calls `GtkMountingManager::executeMount` on the JS thread (or the
    reload thread). Transactions are queued and applied on the main thread
    in commit order.
  - `dispatchCommand` (`scrollTo`...) and `synchronouslyUpdateViewOnUIThread`
    (direct prop updates) from the JS thread join the same queue, so a
    command never overtakes the mount that created its view.
  - LogBox's surface delegate starts and stops its surface from the JS
    thread (SurfaceManager is thread-safe), and shows or hides its widget
    on the main thread.
  - DevUI (banners, the dev menu), image results and network-backed errors
    are posted to the main loop.
- **JS only on the JS thread.** Input, scroll and image events go through
  Fabric's event emitters and `EventDispatcher`, which queue them. Host
  calls into JS (`reload()`, `emitDeviceEvent`) go through ReactHost's
  RuntimeScheduler.
- **No thread waits on another that may be waiting on it.** The JS thread
  never waits on the main thread: mounts and commands are posted, never
  run synchronously. The main thread waits on the JS thread only in
  `quitSynchronous()` at shutdown, which finishes the running task and
  joins. Reloads tear down from ReactHost's reload thread.

## The event beat

React Native delivers queued events to JS on an *event beat*. On iOS that's
a main run loop observer firing before the run loop sleeps. Here it's a
`GSource` on the GTK main loop whose `prepare()` (just before the main loop
polls) calls `RunLoopObserverManager::onRender()`. Events raised while
handling input or a frame are flushed once the main loop goes idle. The
beat only schedules work on the JS thread (`EventBeat::induce()`); it never
runs JS.

While ReactHost creates a JS instance (start-up or reload) it replaces the
observer the beat reads. The beat is paused from the JS thread factory
call until the new instance hands the mounting manager its UIManager.

## Native Animated

C++ Animated (`useNativeDriver: true`) keeps its node graph in
`NativeAnimatedNodesManager`. JS creates and starts animations on the JS
thread, and the manager asks for per-frame callbacks then. The host keeps
the callback under a mutex and adds a GTK frame-clock tick on the main
thread. Each frame steps the graph on the main thread and applies values
with `synchronouslyUpdateViewOnUIThread`, between commits, like Android
and iOS. Scroll-driven animations (sticky headers) take scroll events
through the UIManager's event listener.

## Text

Yoga measures text with `TextLayoutManager` on the thread that lays out:
the JS thread for commits from JS, the main thread for state updates made
there (ScrollView offsets). `pango_context_for_current_thread()` gives each
thread its own Pango font map and context, since Pango font maps aren't
thread-safe. The other threads' contexts copy the main context's font
options and resolution, so measured and drawn text agree. The gallery
self-test checks that every mounted Text draws at its measured size.

## Shutdown and reload

- **Reload** (Ctrl+R, Metro's `r`, LogBox): ReactHost's reload thread stops
  the surfaces (their last mounts post to the main thread), quits and joins
  the JS thread, and builds a new instance with a new JS thread. The
  dev-loop test reloads four times and checks the thread count stays flat
  (15 threads in the test).
- **Exit:** the host stops the surfaces, then destroys ReactHost, which
  joins the JS thread.

## Measured effect

Scrolling the 10,000-row FlatList (GalleryLists self-test, GNOME Wayland
on a VM): when JS ran on the GTK main thread, the main thread spent about
11 ms of CPU per frame. With the JS thread it spends about 1.1 ms, close to
the 0.9 ms of an idle frame. Frame intervals stay at 16.7 ms p50. The p95
(about 33 ms) is the same before and after: the main thread is idle in
those frames, and this VM's GPU and compositor set the limit.

## Checking with ThreadSanitizer

```sh
cmake -S linux -B build/tsan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build/tsan
TSAN_OPTIONS="halt_on_error=0 report_signal_unsafe=0 log_path=tsan" \
  build/tsan/rn-gtk-host --bundle examples/hello-world/build/index.bundle.js --module GalleryLists --self-test
BUILD_DIR=build/tsan scripts/test-dev-loop.sh   # reloads under TSan
```

This instruments React Native, folly, Yoga and our code. Hermes is a
prebuilt library, and the distribution's GLib, GTK, Pango, cairo and
librsvg aren't instrumented. GLib synchronizes with its own futex-based
locks, so TSan reports data handed between threads through GLib
(`g_idle_add`, `g_main_context_invoke`, `g_thread_pool_push`, async
queues) as races. Those reports, and ones inside those libraries, are
false positives.

The first run found two real races, both fixed:

- **`ReactNativeFeatureFlagsDynamicProvider`** looks flags up with a
  non-const `folly::dynamic::operator[]`, which inserts missing keys. Once
  the JS and main threads both read flags, two threads mutate one map. The
  host now uses a `ReactNativeFeatureFlagsDefaults` subclass.
- **SoupNetworking's thread start-up** notified a condition variable on
  the constructor's stack after releasing its lock, so the constructor
  could return and destroy it during `notify_one()`.

After those fixes, all four gallery self-tests and the dev-loop script
(four reloads) pass under TSan. Every remaining report is a GLib handoff
or inside an uninstrumented library.
