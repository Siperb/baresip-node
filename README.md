# baresip-node (Step 1: Project skeleton)

This is the minimal scaffold for a Node-API (`.node`) addon that will embed **baresip** in the next steps.
For Step 1 we intentionally keep the native layer stubbed so you can compile immediately, then wire in deps later.

## Prereqs
- Node 18+ (or 20+/22+)
- CMake 3.20+
- A C/C++ toolchain (Xcode on macOS, MSVC 2022 on Windows, build-essential on Linux)

## Build
```bash
npm ci
npm run build
```

Expected output:
- `build/Release/baresip_node.node` (native addon)
- `dist/index.js` (TypeScript build)

## Next (Step 2)
- Add deps (`re`, `rem`, `baresip`, `opus`, `libsrtp2`) as submodules and wire them into `CMakeLists.txt`.
- Replace stub functions in `src/native/c_api.c` with real calls into libbaresip and friends.
- Expand the JS wrapper in `src/index.ts` to surface registration/call APIs and stats polling.
