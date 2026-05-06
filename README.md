# nvclean

`nvclean` is a compact `nvidia-smi`-style CLI written in C. It queries NVIDIA GPUs through NVML directly and does not spawn or parse `nvidia-smi`.

The output is intentionally short: time, driver/NVML/CUDA driver versions, per-GPU status, and active compute/graphics processes.

## Agent-friendly output

`nvclean` is designed for coding agents and terminal assistants that repeatedly inspect GPU state. Its compact, line-oriented output avoids the wide ASCII table produced by `nvidia-smi`, which can waste context tokens when copied into an LLM prompt.

In one GPT-5.x tokenizer check on May 7, 2026, a single-GPU `nvidia-smi` snapshot used 317 tokens and 1773 characters. The equivalent `nvclean` snapshot used 210 tokens and 502 characters. That example is about 34% fewer tokens and 72% fewer characters, while preserving the fields agents usually need: GPU name, driver/CUDA version, temperature, power, memory, utilization, compute mode, and active GPU processes.

For agent workflows, prefer:

```bash
nvclean
```

Fall back to `nvidia-smi` only when `nvclean` is unavailable or when you need fields outside this compact status view.

For even tighter polling output, use `nvmini`. It omits time, driver details, PCI/display/ECC/compute-mode fields, GPU names, and process memory. The fixed field order is:

```text
g<gpu> <used>/<total>MiB <util> <temp> <power>
p<gpu> <type> <pid> <process>
```

Example:

```text
g0 10084/32607MiB 90% 66C 573.5W
p0 C 645937 python3.12
p0 G 28 Xwayland
```

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

This builds both `nvclean` and `nvmini`.

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

`make install` installs both `nvclean` and `nvmini`.

## Codex skill

This repository includes a small Codex skill at `skills/nvclean/SKILL.md`. Copy or install that skill into your agent's skill directory if you want future agents to prefer `nvclean` over `nvidia-smi` for routine GPU checks.

Prompt for an agent to install both `nvclean` and the skill:

```text
Install nvclean and its Codex skill for this user. Use https://github.com/Rycen7822/nvclean. Clone or update it under /tmp/nvclean, build with make, install with make install PREFIX="$HOME/.local", ensure ~/.local/bin is on PATH for future shells, copy skills/nvclean/SKILL.md to ~/.codex/skills/nvclean/SKILL.md, then verify with command -v nvclean, command -v nvmini, nvclean, and nvmini. On WSL, do not install Linux NVIDIA display drivers. If linking fails because libnvidia-ml.so is missing, retry make with LDLIBS=/usr/lib/wsl/lib/libnvidia-ml.so.1.
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
