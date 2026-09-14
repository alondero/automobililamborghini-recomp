# Glossary

These definitions describe this repository, not every N64 project.

| Term | Plain-English meaning |
| --- | --- |
| ROM | A dump of the original game cartridge. The port needs the supported USA dump, but does not ship one. |
| Static recompilation | Translating the machine code in a ROM into native source before running the game. |
| Decompilation | Reconstructing human-readable source that matches the original program. This project is not fully decompiled. |
| Generated code | Source produced from the ROM and recompiler configuration. It is disposable and must not be edited by hand. |
| Recompiled function | A generated native function corresponding to an original game function. |
| Guest | The emulated N64 program and its address space. |
| Host | The PC or Android program that runs the recompiled game. |
| Guest memory | The emulated RDRAM byte array. A guest address is not a normal host pointer. |
| RDRAM | The N64's main memory. Port code uses this word in developer documentation. |
| Hook | A controlled call from generated game code into hand-written port code. |
| Patch | A small, reviewable change applied to a dependency or source boundary. |
| Symbol | A name and address that tells the recompiler what a function or data object is. |
| Stub | A placeholder implementation used when a generated function is not yet routed or needed. |
| N64Recomp | The tool that generates native code from the ROM and symbol/config data. |
| RSPRecomp | The tool that generates native code for selected N64 signal/audio processor tasks. |
| N64ModernRuntime | Runtime libraries that provide the translated game's platform and scheduling layer. |
| RT64 | The renderer used by this port to turn N64 display-list work into a host image. |
| F3DEX | A family of N64 graphics microcode commands. Most players never need this term. |
| RecompFrontend | Shared settings, input, and profile UI used by several ports. |
| Controller Pak | The removable N64 memory card used by this game for records and progress. |
| CTest | CMake's test runner. |
| ares | An external N64 emulator that can be used as a comparison reference when installed. |
| PVS | Potentially visible set: a list of track pieces that the original game considers visible. |
| Track Lab | This project's experimental tool for inspecting and applying narrow stock-track visibility corrections. |
| Portable mode | A mode that stores settings and saves beside the executable. |
| Backend | The graphics API selected for a window, such as Vulkan or Direct3D 12. |
