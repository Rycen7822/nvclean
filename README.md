# nvclean

`nvclean` is a compact `nvidia-smi`-style CLI written in C. It queries NVIDIA GPUs through NVML directly and does not spawn or parse `nvidia-smi`.

The output is intentionally short: time, driver/NVML/CUDA driver versions, per-GPU status, and active compute/graphics processes.

## Requirements

- NVIDIA GPU with a working NVIDIA driver
- NVML runtime library
- NVML header file
- C compiler and `make`

On WSL, the common paths are:

```bash
/usr/local/cuda/include/nvml.h
/usr/lib/wsl/lib/libnvidia-ml.so.1
```

If `nvml.h` is missing on WSL, install the CUDA Toolkit package, not the Linux display driver.

## Build

```bash
make
```

For WSL, the default Makefile settings use:

```bash
NVML_INCLUDE=/usr/local/cuda/include
NVML_LIBDIR=/usr/lib/wsl/lib
```

For a regular Linux install where NVML is under `/usr/lib/x86_64-linux-gnu`, build with:

```bash
make NVML_LIBDIR=/usr/lib/x86_64-linux-gnu
```

If your system only has `libnvidia-ml.so.1` and not the linker symlink `libnvidia-ml.so`, build with the library path directly:

```bash
make LDLIBS=/usr/lib/wsl/lib/libnvidia-ml.so.1
```

## Install

```bash
make install
```

This installs to `~/.local/bin/nvclean` by default. Override `PREFIX` if needed:

```bash
make install PREFIX=/usr/local
```

## Example

```text
Time: Thu May  7 00:28:37 2026
NVML: 595.71.01 | Driver: 596.36 | CUDA driver: 13.2

GPU 0: NVIDIA GeForce RTX 5090
  Bus ID: 00000000:02:00.0
  Persistence: On | Display: On | ECC: N/A
  Fan: 37% | Temp: 47C | Perf: P0
  Power: 101W/600W | Memory: 6962MiB/32607MiB | Utilization: 7%
  Compute mode: Default

Processes:
  GPU 0 | PID 28 | Type G | /Xwayland | Memory N/A
  GPU 0 | PID 629519 | Type C | /python3.12 | Memory N/A
```

Process memory may be reported as `N/A` on WSL/Windows WDDM systems because that NVML field is not always available there.

## License

MIT
