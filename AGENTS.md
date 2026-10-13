# Instructions for AI coding agents

This workspace is a multi-project PS5/homebrew environment, not a single app. Treat each top-level directory as a separate project or subsystem unless the task clearly targets the root workspace itself.

## Primary projects in this workspace

- [README.md](README.md) — high-level overview of the HdRezka-for-PS5 project and the overall architecture.
- [hdrezka_vk/](hdrezka_vk/) — Vulkan-based sample/demo code and related assets.
- [native/](native/) — native homebrew title and packaging workflow.
- [www/](www/) — browser UI assets and web client code.
- [PS5_VKHomebrewUI/](PS5_VKHomebrewUI/) — UI-focused homebrew project with its own local instructions.
- [PS5_Vulkan/](PS5_Vulkan/) — Vulkan driver/runtime work with its own project conventions.
- [PS5_Mesa/](PS5_Mesa/) — Mesa-related code and PS5 integration work.
- [PS5_PayloadSDK/](PS5_PayloadSDK/) — payload SDK and support code.

## Before making changes

1. Read the closest project README or AGENTS file for the target directory before patching code.
2. Prefer the smallest relevant scope: if the task is in `native/`, work in `native/`; if it is in `PS5_Vulkan/`, work in `PS5_Vulkan/`.
3. Check the target project's build and test commands, because each project has its own toolchain and build system.
4. If a change touches UI, graphics, or PS5 packaging, validate against the project's local docs before claiming success.

## Working rules

- Do not assume there is one build command or one compiler across the whole workspace.
- Do not mix code patterns from different subprojects without checking the local conventions first.
- Do not add a new rendering path, framework, or cross-project dependency unless the target project explicitly supports it.
- Keep edits minimal and scoped to the affected project.

## Good default workflow

- Identify the correct subsystem.
- Read the relevant README and local AGENTS file.
- Make the smallest possible fix.
- Run the smallest relevant build/test command in that project.
- Report verification with the actual command output and result.

## Useful references

- [README.md](README.md)
- [native/README.md](native/README.md)
- [hdrezka_vk/README.md](hdrezka_vk/README.md)
- [PS5_VKHomebrewUI/AGENTS.md](PS5_VKHomebrewUI/AGENTS.md)
- [PS5_Vulkan/AGENTS.md](PS5_Vulkan/AGENTS.md)

This file is intentionally brief: the detailed project-specific guidance lives in the subproject documentation and local AGENTS files.
