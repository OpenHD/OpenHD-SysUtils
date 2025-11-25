This repository started as a collection of shell scripts for odds and ends that
were "not a good fit to be done in c++ / openhd core." It has now been
consolidated into a single C++ utility that provides the same boot-time and
maintenance behaviors without relying on complex bash pipelines.

## Building the utility

1. Configure CMake: `cmake -S . -B build`
2. Build the binary: `cmake --build build`

The resulting `openhd_sys_utils` executable lives in `build/` and can be
installed into `/usr/local/bin` (or your preferred prefix) with
`cmake --install build`.

Existing shell entrypoints remain as thin wrappers that forward to the binary
for compatibility with existing service files and packaging.
