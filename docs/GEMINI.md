# project-ng: AI Agent Guidelines (`docs/GEMINI.md`)

> **Moved**: The single authoritative workspace manifest and agent rules now live in [.agents/AGENTS.md](file:///usr/local/google/home/eianiuk/gemini/3dfx/.agents/AGENTS.md) (tracked in git), paired with domain skills under `.agents/skills/`:
>
> - `wsi-window-injection` — Native child surfaces (`NativeSurfaceHost`), X11/Wayland WSI, DOSBox quirks, aspect ratio
> - `build-and-test` — CMake/CTest, `Glide2xWrapperTests` / `Glide3xWrapperTests`, benchmarks, DOSBox launch commands
> - `3dfx-expert` — Voodoo 1/2 specs, Glide 2/3 state serialization, DOSBox `setConfig` ABI
> - `vulkan-expert` — Vulkan WSI, SPIR-V shader pipeline, barriers, swapchain blit
> - `gles-expert` — Direct EGL 1.5 + OpenGL ES 3.2, GLX isolation, Gamma LUT presentation
> - `rasterizer-oracle` — Software reference rasterizer, LFB row flipping, headless mode
> - `visual-validator` — RLE frame dumps, timed stdin piping, XWayland/Wayland capture rules
>
> **Active Roadmap & Tracking Document**: [docs/plans/option_a_and_cleanup_tracker.md](file:///usr/local/google/home/eianiuk/gemini/3dfx/docs/plans/option_a_and_cleanup_tracker.md)
>
> **Troubleshooting Playbook**: [docs/troubleshooting_playbook.md](file:///usr/local/google/home/eianiuk/gemini/3dfx/docs/troubleshooting_playbook.md)
