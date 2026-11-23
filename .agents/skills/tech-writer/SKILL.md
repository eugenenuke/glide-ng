---
name: tech-writer
description: Authoritative technical writing standards, documentation taxonomy, voice rules, and verification checks for glide-ng documentation.
---

# Workspace Skill: Technical Writer & Documentation Standards

Activate this skill whenever creating, refactoring, or reviewing any Markdown documentation under `README.md`, `CHANGELOG.md`, or `docs/` (including `docs/user-guide/`, `docs/developer-guide/`, `docs/catalog/`, `docs/HISTORY.md`, and `docs/ROADMAP.md`).

---

## 1. Documentation Taxonomy (Strict Separation of Concerns)

Every document in the repository has one clear audience and lifecycle. Never blend conversational notes, historical changelogs, future plans, and current-state manuals in the same file.

| Location | Audience | Voice & Scope |
| :--- | :--- | :--- |
| [`README.md`](../../../README.md) | First-time visitors & users | Crisp landing page: what `glide-ng` is, supported platforms/backends, quick-start build & run, links to docs. |
| [`CHANGELOG.md`](../../../CHANGELOG.md) | Users & packagers | Keep-a-Changelog format (`[Unreleased]`, `[X.Y.Z] - YYYY-MM-DD`). Concise release notes from `0.8.0-beta.1` onward. |
| [`docs/README.md`](../../../docs/README.md) | All readers | Top-level documentation index linking User Guide, Developer Guide, Catalog, History, and Roadmap. |
| `docs/user-guide/` | Players, testers & benchmarkers | Present-tense, task-oriented manuals: building, configuring (`glide-ng.ini` / env vars), hardware/version selection, benchmarking, troubleshooting, and bug reporting ([`reporting-bugs.md`](../../../docs/user-guide/reporting-bugs.md)). |
| `docs/developer-guide/` | Contributors & maintainers | Present-tense technical reference: architecture, API coverage, backend internals, testing workflow, and releasing. |
| `docs/catalog/` | Players & testers | Per-title compatibility pages (`games/`, `demos/`, `tools/`) paired with validated drop-in `glide-ng.ini` presets. |
| [`docs/HISTORY.md`](../../../docs/HISTORY.md) | Curious readers & maintainers | Concise retrospective of pre-beta milestones, architectural pivots, and design decisions. Past tense is appropriate **only** here and in `CHANGELOG.md`. |
| [`docs/ROADMAP.md`](../../../docs/ROADMAP.md) | Maintainers & contributors | Forward-looking work only (active + upcoming milestones and backlog). Move completed milestones to `CHANGELOG.md` / `docs/HISTORY.md`. |

---

## 2. Voice & Tone Rules

1. **Present tense, current state**: Describe how `glide-ng` works today (`"The Vulkan backend splits W-buffer shaders..."`), never how it evolved (`"We refactored the Vulkan backend in M11.6b to..."`).
2. **Objective, third-person or imperative**: Never use conversational pronouns (`"I"`, `"we"`, `"let's"`, `"our session"`) in user guides, developer guides, or catalog entries.
   - Prefer: `"Run cmake --build build"` or `"The software backend selects AVX2 by default."`
   - Avoid: `"We added AVX2 by default in Milestone M11, and now I recommend running..."`
3. **Zero internal milestone/task jargon in manuals**: Do not cite internal milestone IDs (`M1`, `M10.5`, `T2.4`, `Phase 3`), branch names (`feat/m9-...`), or raw commit hashes inside `README.md`, `docs/user-guide/`, `docs/developer-guide/`, or `docs/catalog/`. Those belong strictly in `CHANGELOG.md`, `docs/HISTORY.md`, or `docs/ROADMAP.md`.
4. **No speculative future promises in manuals**: Document implemented behavior accurately. If a feature is intentionally unsupported or out of scope (e.g., hardware-register-only `SST_GRXCLK`), state that plainly in one sentence without drafting design proposals inside a user manual.

---

## 3. Ground-Truth Verification Mandate

Never copy numbers, defaults, CLI flags, INI keys, or symbol counts from older documents without checking the live source tree:

- **Environment variables & INI keys**: Verify against [`include/glide_ng/env_vars.h`](../../../include/glide_ng/env_vars.h), [`src/core/IniConfigLoader.cpp`](../../../src/core/IniConfigLoader.cpp), and [`glide-ng.ini.example`](../../../glide-ng.ini.example).
- **Hardware profiles & API versions**: Verify against [`src/core/WrapperConfig.h`](../../../src/core/WrapperConfig.h) and `src/core/WrapperConfig.cpp`.
- **ABI exports**: Verify exact counts via `src/glide211/glide.def`, `src/glide2/glide2x.def`, `src/glide3/glide3x.def`, and `nm -D --defined-only build/lib/libglide*.so` (`libglide.so`, `libglide2x.so`, `libglide3x.so`).
- **Version strings & release flow**: Verify against root [`VERSION`](../../../VERSION), [`tools/gen_version_header.cmake`](../../../tools/gen_version_header.cmake), and [`scripts/bump_version.sh`](../../../scripts/bump_version.sh).
- **Commands**: Only include build, test, Wine, or DOSBox commands that match current CMake targets and binary names.

---

## 4. Formatting, Links & Public-Repo Hygiene

1. **Relative Markdown links only**: Always link repository files with relative paths (`../user-guide/configuration.md`), never `file:///...` URIs or hardcoded absolute filesystem paths.
2. **Zero PII or local machine artifacts**: Before committing any documentation change, scan tracked docs for local home paths (`/home/`, `/Users/`, `C:\Users\`), usernames, corporate hostnames, or internal tracker links.
3. **Zero bundled third-party binaries**: Reference external demo/driver archives via clean links (e.g., `https://3dfx.retropc.se/artwork.html`) rather than committing third-party executables or PDFs.
4. **Scannable structure**:
   - Lead every guide with a 1–2 sentence summary of scope.
   - Use Markdown tables for matrices (flags, INI keys, card profiles, backends, benchmark results).
   - Keep code blocks copy-pasteable with explicit shell language tags (`bash`, `ini`, `cmake`, `text`).
   - Avoid excessive decorative emojis in technical documentation.

---

## 5. Pre-Commit Documentation Audit Checklist

Run these checks before committing any documentation change:

1. **Link & anchor audit**: Verify every relative Markdown link and `#heading-anchor` resolves to an existing file and heading.
2. **Catalog INI validation**: Run `GLIDE_NG_HEADLESS=1 GLIDE_NG_BACKEND=software ./build/bin/Glide3xWrapperTests "[Catalog]"` whenever touching `glide-ng.ini.example` or `docs/catalog/`.
3. **PII / local-path scan**: Confirm zero matches for local user paths or machine identifiers across all tracked Markdown files.
4. **Voice check**: Confirm zero conversational `"we/I"` narration or stale milestone references leaked into `docs/user-guide/` or `docs/developer-guide/`.
