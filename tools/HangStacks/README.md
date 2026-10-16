# Native hang stacks (developer only)

Build `main.cpp` with Windows x64 MSVC and `dbghelp.lib`. Run
`HangStacks.exe <pid> [thread-id ...]` to emit module-relative native stacks.
No debugger attachment, hooks, memory patches, raw dumps or network symbols.
Each sampled thread is briefly suspended; RAII restores only the added suspend
count. Preexisting suspension is reported and preserved. Do not terminate the
reader during a sample. Symbol names without private PDBs are approximate;
module RVAs can be resolved with a matching linker map and verified code bytes.
The tool is never included in a mod package.
