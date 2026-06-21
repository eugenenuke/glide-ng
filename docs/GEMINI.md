# project-ng: 3dfx Glide Wrapper & Modernization Engine
## Global Context & Authoritative Guidelines for AI Coding Agents (`GEMINI.md`)

Welcome, Agent. This document serves as your **authoritative project context, architectural blueprint, and behavioral manual** for working within the `project-ng` repository. 

Read this document fully before analyzing the codebase, writing code, or proposing modifications. It contains the project's core mission, structural layouts, coding standards, quality assurance pipelines, and strict workflow policies.

---

## 1. Project Mission & Objectives

The goal of `project-ng` is to build a modern, high-performance, and pixel-perfect emulation wrapper for the legacy **3dfx Glide 2.x and 3.x Graphics APIs**. 

Originally designed in the 1990s for Voodoo Graphics accelerators, Glide is a low-overhead, hardware-close 3D rendering API. This project modernizes Glide by intercepting frontend API calls and translating them dynamically into modern rendering pipelines across three distinct backends:
1.  **Vulkan Backend (`vk`)**: The primary, high-performance GPU hardware rendering path.
2.  **OpenGL ES 3.2 Backend (`gles`)**: A secondary hardware path tailored for mobile, embedded, and highly compatible OpenGL environments.
3.  **Software Backend (`sw`)**: A CPU-based reference rasterizer that implements pixel-perfect, mathematically precise rendering. It acts as the ground-truth oracle for all behavioral and rendering state validations.

---

## 2. Directory Structure & Architecture

```
3dfx/
├── docs/                               # Engineering playbooks, plans, and instructions
│   ├── troubleshooting_playbook.md     # Authoritative logging, debugging, and diagnostics
│   └── visual_analysis_agent_instructions.md # Visual regression and screen capturing manual
├── include/                            # Public headers exposed to applications
│   ├── 3dfx/                           # Common 3dfx type and macro definitions
│   ├── glide/                          # Glide 2.x public API headers
│   └── glide3/                         # Glide 3.x public API headers
├── src/                                # Core implementation of the wrapper
│   ├── core/                           # Shared state machines, vertex layout decoders, splash animations
│   ├── glide2/                         # Glide 2.x API frontend entry points
│   ├── glide3/                         # Glide 3.x API frontend entry points
│   └── backends/                       # Emulated rendering backends
│       ├── vulkan/                     # Vulkan GPU translation layer
│       ├── gles/                       # OpenGL ES 3.2 GPU translation layer
│       └── software/                   # CPU reference rasterizer
├── tests/                              # Verification and Quality Assurance
│   ├── test_glide3.cpp                 # Catch2 unit tests for the Glide 3.x frontend
│   ├── test_architecture.cpp           # Catch2 unit tests for core wrapper pipelines
│   └── benchmark.py                    # Throughput and performance profiling script
└── refs/                               # Detached historical reference submodules (PRISTINE)
    ├── glide/                          # Primary reference library and test suites
    └── 3dfx-Glide-API/                 # Secondary historical library versions (CVG/H3 targets)
```

---

## 3. Reference Resources & Submodules

To preserve the absolute historical accuracy of the Glide wrappers, the repository links against two pristine reference submodules under `refs/`:

1.  **Primary Reference (`refs/glide/`)**:
    *   Contains the original Glide source code tree and official C test suites.
    *   Glide 2.x Tests: [refs/glide/glide2x/sst1/glide/tests/](file:///usr/local/google/home/eianiuk/gemini/3dfx/refs/glide/glide2x/sst1/glide/tests/)
    *   Glide 3.x Tests: [refs/glide/glide3x/h3/glide3/tests/](file:///usr/local/google/home/eianiuk/gemini/3dfx/refs/glide/glide3x/h3/glide3/tests/)
2.  **Alternative Reference (`refs/3dfx-Glide-API/`)**:
    *   Contains alternative target configurations (such as CVG and H3 hardware trees).
    *   Highly valuable for side-by-side comparative reviews of `.c` and `.h` files to check for differences in test configurations and headers.

> [!IMPORTANT]
> **The Golden Rule of Reference Submodules**:
> The `refs/` directory contains **pristine historical artifacts**. You must **NEVER** edit, commit, or stage any changes to files inside `refs/` under any circumstances. 
> To run experiments, you must strictly adhere to the **Experimentation Scratch Protocol** (see Section 6).

---

## 4. Development & Testing Practices

### A. Test-Driven Development (TDD) with Catch2
We use a strict TDD loop to prevent regressions. All core mechanics (vertex layout decoding, state serialization, LFB locking/unlocking, row-flipping, backface culling, and chromakeying) are guarded by Catch2 unit tests.

*   Unit test files: [tests/test_glide3.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/test_glide3.cpp) and [tests/test_architecture.cpp](file:///usr/local/google/home/eianiuk/gemini/3dfx/tests/test_architecture.cpp).
*   **Compilation & Execution**:
    ```bash
    # Rebuild the test suite
    cmake --build build -j$(nproc)
    
    # Run the test runner
    ./build/bin/GlideWrapperTests
    ```
*   **The Mandate**: Every bug fix, state addition, or frontend alignment **must** be accompanied by a corresponding Catch2 unit test written under the software reference backend.

### B. Performance Benchmarking
To ensure that vertex decoding, coordinate unsnapping, or state serialization changes do not introduce performance overhead, run the automated headless benchmark:
```bash
python3 tests/benchmark.py
```
*   **Headless execution**: Runs with `GLIDE_HEADLESS=1` and `GLIDE_WRAPPER_MAX_FPS=0` to profile maximum pipeline throughput.
*   **Acceptance Criteria**: Any performance delta within $\pm 3\%$ is considered standard LLVMpipe/system noise, confirming zero performance regressions.

---

## 5. Diagnostic & Debugging Workflows

When debugging application-level failures, incorrect rendering, or crashes, apply the authoritative procedures outlined in the **[troubleshooting_playbook.md](file:///usr/local/google/home/eianiuk/gemini/3dfx/docs/troubleshooting_playbook.md)**:

### A. Configuring Verbose Logs
1. Create or edit `glide_config.json` in the application's working directory:
   ```json
   { "logLevel": 4 }
   ```
2. Alternatively, override via environment variables:
   ```bash
   export GLIDE_WRAPPER_LOG_LEVEL=4
   export GLIDE_WRAPPER_LOG_TO_CONSOLE=1
   ```
3. Inspect `glide-ng.log` for `[WARN]` or `[DEBUG]` traces to locate state initialization gaps or missing API mappings.

### B. Multi-Backend Behavioral Bisection
Run the application across all three backends (`sw`, `vk`, `gles`) using the `GLIDE_BACKEND` environment variable to isolate the failure layer:
*   **All Backends Fail**: Points to a **frontend logic bug** (e.g., a state restoration leak in `glide3_entry.cpp` or coordinate transformation error in `VertexLayout`).
*   **Only GPU Backends Fail (Software Succeeds)**: Points to a **vertex parameter/pipeline corruption** (e.g., stack garbage/uninitialized coordinates being parsed by modern GPU shaders) or shader compilation issue.
*   **Only Vulkan or Only GLES Fails**: Points to a **backend-specific driver/API configuration mismatch** (e.g., incorrect blending setup, wrong cull-face orientation, or scissor clip rectangle boundaries).

### C. Advanced Stdin Piping for Interactive Tests
To automate capturing or cycling through multiple modes of interactive tests headlessly (without GUI automation utilities):
*   Pipe keystrokes directly using a subshell:
    ```bash
    (sleep 7.0; echo -n "f"; sleep 10.0) | ./test34.exe
    ```
*   **CRITICAL WARNING**: Always include a long trailing `sleep` in the input subshell. If the pipe closes, the test's keyboard checking loop (`while (tlKbHit())`) will see a permanent EOF readable state and spin in an infinite loop, freezing the application.

---

## 6. The Experimentation Scratch Protocol

If you must temporarily modify a reference test file (e.g. `test34.c`) to inject printf statements, alter vertex coordinates, or change state parameters for debugging, you must **strictly isolate the changes**:

1.  **Copy to Scratch**: Copy the target reference `.c` or `.h` file to the parent project's `./scratch/` directory (or your conversation's scratch directory).
2.  **Modify Locally**: Perform all edits, debugging, and experimentation exclusively on the copy inside `./scratch/`.
3.  **Compile and Execute**: Compile the scratch binary pointing to the wrapper's headers and library, keeping all changes localized.
4.  **Enforce Cleanliness**: Never commit, stage, or leave any dirty state in the submodules. Ensure the original files in the `refs/` submodules remain 100% untouched.

---

## 7. How to "Activate" This Customization Document

This document is written as a markdown file for your review under `docs/GEMINI.md`. 

To make this document **actively and strictly loaded** as a system-level workspace rule by the AI agent platform (e.g., Gemini, Jetski, or Antigravity) at the start of every session:

1.  Create a directory named **`.agents`** at the root of the workspace.
2.  Move or copy this file into the `.agents/` directory and rename it to **`AGENTS.md`**:
    ```bash
    mkdir -p .agents
    cp docs/GEMINI.md .agents/AGENTS.md
    ```
3.  The agent platform automatically discovers `.agents/AGENTS.md` and loads all its instructions directly into the system prompt, enforcing perfect alignment with your project guidelines!
