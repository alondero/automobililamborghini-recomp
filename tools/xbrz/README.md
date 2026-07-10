# xBRZ pixel-art scaler (vendored)

xBRZ v1.8 by Zenju (GPLv3, same licence as this repo — see `License.txt`), as
redistributed in [ioistired/xbrz.py](https://github.com/ioistired/xbrz.py) with that
project's small C API additions (`xbrz_scale` extern-C wrapper). Local modifications:

- `xbrz.cpp`: dropped the `#include "dummy_module.cpp"` Python-module stub.
- `xbrz.h`: `xbrz_scale_defaults` made `inline` (it is defined in the header, which
  otherwise double-defines when two translation units include it).
- `xbrz_cli.cpp` (ours): stdin/stdout CLI used by `tools/upscale_texture.py`.

Build (any C++17 compiler; MinGW example):

```
g++ -O3 -std=gnu++17 -static xbrz.cpp xbrz_cli.cpp -o xbrz_cli.exe
```

`upscale_texture.py` looks for the binary next to these sources (`xbrz_cli.exe` /
`xbrz_cli`) or via the `XBRZ_CLI` env var, and builds it automatically if `g++` is on
PATH (g++ only — this box has a broken `cc` shim; other compilers work fine built by
hand and pointed at via `XBRZ_CLI`).

The CLI protocol: `xbrz_cli <factor 2-6> <width> <height>`, raw R,G,B,A bytes for
`width*height` pixels on stdin, the `factor`-scaled image in the same layout on stdout.
Pixels are converted internally to xBRZ's A-top-byte layout (its `getAlpha`/`getRed`
accessors read bytes 3/2/1/0 — the "ABGR on little-endian" comment in `xbrz.h`
describes a different layout than the code actually uses; trust `xbrz_tools.h`).
