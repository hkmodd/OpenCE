# OpenXR headers

The OpenXR API's C headers, by the Khronos Group, licensed Apache-2.0 OR
MIT (see `LICENSE-Apache-2.0.txt` and `LICENSE-MIT.txt`).

Upstream: https://github.com/KhronosGroup/OpenXR-SDK, tag `release-1.1.65`,
commit `24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8`.

Only these files of `include/openxr/` are kept, unchanged (SHA-256):

| File | SHA-256 |
| --- | --- |
| `openxr/openxr.h` | `a1882433318c34f7b52995d7f2a1ee5827f4bd56373e0b7fd7c1ef97eec8ad31` |
| `openxr/openxr_platform.h` | `150939f39aaf1159e9f59ab53b5d5863c08d5777efa13820b5cbcd3562eb068d` |
| `openxr/openxr_platform_defines.h` | `7dc30ad27f1082a71f84f5f5adf10762558fc9fe57fdc7628d796d22be3b0dbe` |
| `openxr/openxr_loader_negotiation.h` | `3dca4239643a8505355de010c6d9e18dd05eae91c9b976add2fc878ac416890f` |

The SDK's loader (C++) is not used. The headset (`display.vr`,
`port/linux/src/vr_openxr.c`) finds the active runtime's manifest itself
(`port/windows/src/win32_vr.c`, `port/linux/src/posix_vr.c`), loads its
library and negotiates with it through `openxr_loader_negotiation.h`; API
layers are not loaded. The headers build into the Linux and Windows
platform layers only; the Android and web builds compile none of them.
