---
name: visual-validator
description: Headless QA capture, RLE frame dumps, timed stdin-piping, and cross-backend visual regression validator.
---

# Workspace Skill: Visual Validator & QA Specialist

Activate this skill when running visual regression suites, capturing cross-backend parity screenshots, or testing interactive 3dfx SDK tests inside [tests/](../../../tests/) or [demos/](../../../demos/).

## 1. Targeted Verification Map

| Modified Component | Target Verification Tests / Demos |
| :--- | :--- |
| **State Reset & Combiners** (`glide2_entry.cpp`, `glide3_entry.cpp`) | `Glide2xWrapperTests`, `Glide3xWrapperTests`, `test10_replica_demo`, `fog_showcase_demo` |
| **Vertex Decoding & Layouts** (`src/core/VertexLayout.cpp`) | `Glide3xWrapperTests`, `glide3_cube`, `glide3_gears` |
| **LFB & Texture Parsing** (`LfbManager.cpp`, `3dfParser.cpp`) | `test12_replica_demo`, `large_texture_showcase`, `perspective_mipmapping_showcase` |
| **Splash & Banner Animation** (`src/core/GlideSplashAnimator.cpp`) | `splash_showcase_demo` |
| **Gamma LUT & Blending** (`VulkanBackend.cpp`, `OpenGLESBackend.cpp`) | `gamma_showcase_demo`, `chromakey_range_demo`, `fog_showcase_demo` |
| **Windowing / Child Surface Injection** (`NativeSurfaceHost.cpp`) | `child_surface_embed_test` (X11 + Wayland), DOSBox-X (windowed + fullscreen), SVN DOSBox (`dosbox-0`) |

## 2. Timed Stdin Piping for Interactive Legacy Tests

Many legacy 3dfx SDK tests cycle modes via keyboard (`F`, `SPACE`, etc.). Automate them headlessly using a subshell pipe:

```bash
(sleep 7.0; echo -n "f"; sleep 10.0) | ./test34.exe
```

> [!WARNING]
> Always keep a trailing `sleep` (`>= 10.0`s) in the input subshell! Closing the pipe write-end puts `tlKbHit()` into a permanent EOF-readable state, causing the test loop to spin at 100% CPU.

## 3. Capture Methods & Wayland vs. XWayland Caveats

1. **Preferred Headless / Wayland-Safe Method (RLE Frame Dumps)**:
   - Official 3dfx tests support `-d <file>` to dump `IMAGE_SRLE` buffers directly from the wrapper, independent of X11/Wayland compositors. Convert using `./build/bin/rle_to_ppm` (built with `-DBUILD_VISUAL_TESTS=ON`).
2. **X11 / XWayland Window Capture (`import`)**:
   - Dynamically resolve runtime directory and Mutter XWayland auth (**never** hardcode a numeric UID):
     ```bash
     export DISPLAY="${DISPLAY:-:0}"
     export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
     export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
     MUTTER_AUTH=$(ps aux | grep -o '/run/user/[0-9]*/\.mutter-Xwaylandauth\.[a-zA-Z0-9]*' | head -n 1)
     if [ -n "$MUTTER_AUTH" ]; then export XAUTHORITY="$MUTTER_AUTH"; fi
     ```
   - Always guard `import` with `timeout 5s`:
     ```bash
     timeout 5s import -window "$WIN_TITLE" "$OUTPUT_PNG"
     ```
   - **Important Caveat**: ImageMagick `import -window` only sees X11/XWayland top-level windows. On native Wayland, or when embedded as a native child surface inside DOSBox, the top-level window title belongs to DOSBox (not the wrapper) and Wayland subsurfaces cannot be scraped via X11 `import`. Use `-d` dumps, `sw_validate`, or ask the user for live visual confirmation on native Wayland / fullscreen DOSBox runs.
