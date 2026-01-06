This repository holds scripts for things best described as
'Not a good fit to be done in c++ / openhd core'
An example is configuring the OS for a specific camera on RPI
(This requires modifying OS-related things like the /boot/config.txt file and a reboot)

This repo is packaged into openhd-sys-utils and can be installed from our repository

## Documentation

See [SCRIPTS.md](SCRIPTS.md) for a brief description of each utility script, packaging helper, and supporting asset included in this repository.

## Building `openhd_sys_utils`

The `openhd_sys_utils` orchestrator is built with CMake and depends only on the standard C++17 library. To compile the binary:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

The resulting executable is placed at `build/openhd_sys_utils`. Install it to `/usr/local/bin/openhd_sys_utils` (or another location in `PATH`) so `openhd_sys_utils.service` can launch it.

### Runtime usage

Run as root:

```bash
sudo /usr/local/bin/openhd_sys_utils
```

Pass `-hidden` to suppress status output and keep OpenHD running quietly in the background:

```bash
sudo /usr/local/bin/openhd_sys_utils -hidden
```
