# Phase 0 results: GTK4 Hello World and widget benchmark

Two environments, both Ubuntu 24.04 with GTK 4.14:

- **Real desktop:** a Parallels VM (arm64) on an Apple M5 Max, GNOME on
  Wayland, GPU through virgl (OpenGL 4.0, Mesa 25.2.8). These are the
  numbers to trust.
- **Cloud container:** 4 vCPU and **no GPU** (Mesa llvmpipe software
  rendering), running headless on X11 (Xvfb) and Wayland (mutter, Weston).
  This is what CI uses, so treat its frame times as relative only.

## Real desktop results (Ubuntu 24.04 VM, Wayland, virgl GPU)

`hello-world --self-test` passes every check here too (GskNglRenderer,
title measured 228px by both paths). GTK picks the `ngl` renderer by
default on this GPU, so the default and forced-`ngl` runs match; the
forced-`ngl` run is shown.

| mode | views | moved/frame | mount ms | mount→paint ms | frame p50 ms | frame p95 ms | layout ms | paint ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| rnview | 1000 | 100% | 4 | 10 | 1.4 | 2.6 | 0.2 | 1.2 |
| rnview | 1000 | 1% | 3 | 9 | 1.0 | 1.4 | 0.2 | 0.7 |
| rnview | 10000 | 100% | 23 | 45 | 5.6 | 6.3 | 1.2 | 3.2 |
| rnview | 10000 | 1% | 24 | 45 | 5.1 | 5.6 | 1.9 | 3.2 |
| fixed | 1000 | 100% | 4 | 8 | 1.8 | 2.5 | 0.4 | 1.2 |
| fixed | 1000 | 1% | 3 | 7 | 1.2 | 1.6 | 0.4 | 0.8 |
| fixed | 10000 | 100% | 28 | 51 | 8.4 | 10.6 | 2.7 | 4.5 |
| fixed | 10000 | 1% | 30 | 55 | 6.7 | 8.5 | 3.1 | 3.8 |

## Cloud results (software rendering)

### Hello World

`hello-world --self-test` passes all 9 checks on X11 (Xvfb), Wayland under
GNOME's compositor (mutter 46 headless) and Wayland under Weston 13:
background, rounded corner and border pixels, text drawn inside its frame,
the text node allocated exactly at its (Yoga-style) frame, and the
off-main-thread Pango measurement matching the widget's own layout to the
pixel (228px for the title on every backend).

![Hello World on X11](images/hello-world-x11.png)

### Benchmark

`widget-benchmark` mounts N views in rows (every 10th view is a text node),
animates "moved/frame" of them every frame for 120 frames, then unmounts.
`rnview` is our widget; `fixed` builds the same tree from `GtkFixed` as a
baseline. `GL` is the renderer GTK picked by default here; `Ngl` is the newer
renderer forced with `GSK_RENDERER=ngl`.

![10,000 views mounted](images/benchmark-10k.png)

| backend | renderer | mode | views | moved/frame | mount ms | mount→paint ms | frame p50 ms | frame p95 ms | layout ms | paint ms | RSS MB |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| x11 | GL | rnview | 1000 | 100% | 4 | 52 | 17.3 | 22.8 | 1.2 | 13.8 | 39 |
| x11 | GL | rnview | 1000 | 1% | 5 | 57 | 12.2 | 23.5 | 1.1 | 9.3 | 39 |
| x11 | GL | rnview | 10000 | 100% | 47 | 388 | 110.7 | 134.6 | 4.4 | 105.9 | 56 |
| x11 | GL | rnview | 10000 | 1% | 45 | 371 | 108.2 | 121.5 | 4.0 | 102.9 | 57 |
| x11 | GL | fixed | 1000 | 100% | 7 | 61 | 19.7 | 31.2 | 1.6 | 15.1 | 39 |
| x11 | GL | fixed | 1000 | 1% | 6 | 56 | 11.2 | 19.6 | 1.2 | 7.9 | 39 |
| x11 | GL | fixed | 10000 | 100% | 60 | 350 | 98.9 | 128.9 | 10.0 | 83.0 | 85 |
| x11 | GL | fixed | 10000 | 1% | 56 | 313 | 77.5 | 93.2 | 9.0 | 68.9 | 85 |
| x11 | Ngl | rnview | 1000 | 100% | 5 | 64 | 23.7 | 29.2 | 1.2 | 19.1 | 39 |
| x11 | Ngl | rnview | 1000 | 1% | 5 | 65 | 8.6 | 16.8 | 1.3 | 4.6 | 39 |
| x11 | Ngl | rnview | 10000 | 100% | 46 | 187 | 47.8 | 59.2 | 3.7 | 42.7 | 79 |
| x11 | Ngl | rnview | 10000 | 1% | 46 | 179 | 47.0 | 57.0 | 4.0 | 43.0 | 79 |
| x11 | Ngl | fixed | 1000 | 100% | 6 | 62 | 23.4 | 29.3 | 1.5 | 17.9 | 39 |
| x11 | Ngl | fixed | 1000 | 1% | 6 | 67 | 7.2 | 9.9 | 1.2 | 3.2 | 39 |
| x11 | Ngl | fixed | 10000 | 100% | 58 | 216 | 78.2 | 97.2 | 9.5 | 58.8 | 79 |
| x11 | Ngl | fixed | 10000 | 1% | 62 | 227 | 56.3 | 69.3 | 8.8 | 47.8 | 79 |
| wayland | GL | rnview | 1000 | 100% | 5 | 39 | 14.6 | 21.3 | 0.3 | 14.8 | 35 |
| wayland | GL | rnview | 1000 | 1% | 4 | 36 | 10.1 | 17.1 | 0.2 | 10.5 | 35 |
| wayland | GL | rnview | 10000 | 100% | 45 | 365 | 106.0 | 129.6 | 3.3 | 102.1 | 53 |
| wayland | GL | rnview | 10000 | 1% | 47 | 311 | 79.0 | 91.8 | 3.2 | 76.6 | 116 |
| wayland | GL | fixed | 1000 | 100% | 9 | 53 | 17.6 | 21.5 | 0.8 | 16.2 | 35 |
| wayland | GL | fixed | 1000 | 1% | 9 | 55 | 10.3 | 17.0 | 0.3 | 10.5 | 35 |
| wayland | GL | fixed | 10000 | 100% | 58 | 255 | 92.2 | 111.7 | 8.2 | 80.9 | 83 |
| wayland | GL | fixed | 10000 | 1% | 57 | 256 | 75.5 | 90.8 | 7.9 | 69.2 | 83 |
| wayland | Ngl | rnview | 1000 | 100% | 5 | 24 | 24.9 | 30.6 | 0.6 | 24.3 | 4 |
| wayland | Ngl | rnview | 1000 | 1% | 5 | 29 | 5.7 | 9.1 | 0.2 | 5.8 | 9 |
| wayland | Ngl | rnview | 10000 | 100% | 49 | 150 | 53.1 | 72.2 | 3.1 | 50.0 | 48 |
| wayland | Ngl | rnview | 10000 | 1% | 50 | 153 | 52.2 | 66.2 | 3.1 | 50.1 | 48 |
| wayland | Ngl | fixed | 1000 | 100% | 6 | 27 | 24.3 | 31.5 | 0.7 | 22.8 | 9 |
| wayland | Ngl | fixed | 1000 | 1% | 7 | 30 | 5.5 | 8.5 | 0.3 | 5.5 | 9 |
| wayland | Ngl | fixed | 10000 | 100% | 59 | 182 | 81.2 | 103.5 | 8.9 | 65.9 | 49 |
| wayland | Ngl | fixed | 10000 | 1% | 62 | 181 | 56.5 | 72.8 | 8.5 | 49.4 | 49 |

## What this tells us

1. **10,000 views fit easily in a 60 fps frame on a real GPU.** On the VM,
   moving every one of 10,000 views each frame costs about 5.6 ms (p95
   6.3 ms) against a 16.7 ms budget, and 1,000 views cost about 1.4 ms. This
   is a virtualized GPU, so bare-metal hardware should do at least as well.
2. **Our widget beats `GtkFixed`.** At 10k views it lays out about 2x faster
   (1.2 vs 2.7 ms), paints faster (3.2 vs 4.5 ms), mounts faster (23 vs 28 ms)
   and has a lower frame p95 (6.3 vs 10.6 ms). That confirms the research
   decision not to build on `GtkFixed`.
3. **Mounting is cheap.** 10,000 native views are created and inserted in
   about 23 ms and first painted 45 ms after the mount starts.
4. **Paint scales with total view count, not with how many views changed.**
   Moving 1% of 10k views costs the same paint time as moving all of them
   (3.2 ms), on the GPU and in software. It is cheap enough not to matter at
   these sizes. If huge lists ever need it, the fix is to cache unchanged
   subtrees (for example, render nodes per row), and virtualized lists will
   keep real view counts well below 10k anyway.
5. **Renderer:** GTK 4.14 picks `ngl` on a real GPU. The older `gl`
   renderer only showed up on software rendering in the cloud, where it was
   about 2x slower on our per-view rounded clips. It is worth avoiding the
   clip for plain rounded backgrounds later, but it is not a blocker.
6. **X11 and Wayland behave the same** for layout and paint. One real
   difference found: on Wayland a client-side title bar is part of the
   window's default size, so the RN root view must size the window (no
   `gtk_window_set_default_size`), which is what the spike now does.
7. **Text measurement off the main thread matches rendering to the pixel**
   on X11, Wayland and the real desktop, so the planned threaded
   `TextLayoutManager` design holds for this case.

## Not covered yet

x86_64 hardware, Mint Cinnamon on X11 with a real GPU, fractional scaling,
and the actual React Native runtime (Fantom/ReactCxxPlatform build) mounting
into these widgets, which is the next step of Phase 0.
