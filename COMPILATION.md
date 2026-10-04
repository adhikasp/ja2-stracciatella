# Compilation

## Dependencies

- SDL3 >= `3.0.0` (version `3.4.16` is included in this repo for Windows and macOS).
- cmake
- Rust and Cargo
- Your systems compiler

## Optional dependencies

Stracciatella bundles a few other projects for development purposes. If you have them installed already,
the system version will be used. This holds for: gtest and string theory.

## Python tooling

The e2e harness (`tools/ja2ctl.py`, `tests/e2e/`) and the asset tools
(`tools/assets/`) are Python; everything except the golden-image resolution
tests runs on the standard library alone. Those tests need Pillow (PNG decode)
and numpy (per-pixel compare), managed by [uv](https://docs.astral.sh/uv/) and
locked in `uv.lock`:

```sh
uv sync
```

This creates `.venv/` in the repo root; a fresh cmake configure picks that
interpreter up for `ctest`. Run the tools through it with
`uv run python tools/ja2ctl.py ...`, or activate it (`source .venv/bin/activate`;
on Windows `.venv\Scripts\activate`).

## The raw commands behind `tools/dev.py`

[`tools/dev.py`](tools/dev.py) is the one entry point for day-to-day work on
macOS and Windows (see [AGENTS.md](AGENTS.md)); it runs everything below in the
right environment and is safe to run repeatedly. Use the raw commands when you
need something the wrapper does not cover.

### macOS

The build directory is `build`:

```sh
cmake -S . -B build -G Ninja            # once; drop "-G Ninja" if you have no ninja
cmake --build build --parallel $(sysctl -n hw.logicalcpu)
./build/ja2 -unittests
./build/ja2 -res 1280x720
```

Incremental — `cmake --build` only recompiles what changed, and re-runs cmake
itself when `CMakeLists.txt` changed.

### Windows (MSYS2 MinGW64)

The build directory is `_bin`, and every build command needs the MinGW64
environment: run it through a login shell with `MSYSTEM=MINGW64` set, because
plain PowerShell/cmd does not have `gcc`/`cmake`/`cargo` on `PATH`:

```sh
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && cmake --build . --parallel \$(nproc)"
```

Configure a fresh build directory with Ninja (much faster than MSYS Makefiles;
a build directory keeps the generator it was configured with, so switch by
configuring a new one):

```sh
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && cmake .. -G Ninja"
```

For a package build use the old generator: `cmake .. -G 'MSYS Makefiles' -DCPACK_GENERATOR=ZIP && make package`.

Test and run:

```sh
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && ./ja2.exe -unittests"
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && ./ja2.exe -res 1280x720"
```

**Long paths.** Agent worktrees live in deep directories and the build tree goes
deeper still (cargo's `target/` is the worst offender), which is how a build
runs into Windows' MAX_PATH limit. Map the worktree to a drive letter and do
everything through that drive; `tools/dev.py setup` picks the letter and
re-creates the mapping for you (a reboot drops it):

```sh
subst W: C:\Workspace\ja2-stracciatella\.claude\worktrees\agent-123
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd /w && mkdir -p _bin && cd _bin && cmake .. -G Ninja && cmake --build . --parallel \$(nproc)"
```

## General Notes

We use cmake as our build system, which is aimed at an out-of-source build. That means that you should call
cmake from a directory that is different from the source directory. You can create a directory inside the source
directory (`_bin` is ignored by git). Cmake only needs to be executed once unless you want to change options.

```sh
mkdir _bin && cd _bin
```

## Faster builds

Nothing below is required — each one is picked up automatically when the machine offers it.

- **sccache** — with `sccache` on `PATH` when cmake runs, it becomes the compiler launcher and
  Cargo's `RUSTC_WRAPPER` (`-DUSE_SCCACHE=OFF` to opt out). The cache is shared by *every* build
  directory, so a second worktree, a branch switch or a reverted change reuses the objects instead
  of recompiling them: `pacman -S mingw-w64-x86_64-sccache` (MSYS2), `brew install sccache`
  (macOS), `cargo install sccache --locked` (elsewhere).
  sccache hashes absolute paths by default, which makes every worktree its own cache island —
  set `SCCACHE_BASEDIRS` to the directory the build is spelled from (the worktree root, e.g.
  `export SCCACHE_BASEDIRS="$PWD"`) to strip it from the cache key and share objects across
  worktrees and checkouts. `tools/dev.py` sets this for you; builds that bypass it need it too.
- **lld** — `-DUSE_LLD=ON` (the default) links with `ld.lld` when the toolchain provides it, which
  cuts the link of the monolithic `ja2` binary to a fraction of GNU ld's time. MSYS2:
  `pacman -S mingw-w64-x86_64-lld`. Where `ld.lld` is absent the option is skipped silently.
- **Ninja** — configure a *fresh* build directory with `-G Ninja` and build with `cmake --build .`
  (append `--parallel` for a job count). Ninja does the scheduling itself and does not start a
  shell per recipe line, which is what makes the MSYS Makefiles generator the slow choice on
  Windows. MSYS2: `pacman -S mingw-w64-x86_64-ninja`. A build directory keeps the generator it was
  configured with: switch by configuring a new one.
- **Unity builds** — `-DCMAKE_UNITY_BUILD=ON` compiles batches of translation units together,
  following the `UNITY_GROUP`s set in `src/**/CMakeLists.txt` (sources that must stay on their own
  carry `SKIP_UNITY_BUILD_INCLUSION`). Many fewer translation units to build from scratch; the
  price is that editing one file rebuilds its whole group.
- **Dependency bumps** — a changed pin refreshes the downloaded sources for you
  (`cmake/DepRefresh.cmake`), so bumping a dependency in an existing build directory rebuilds what
  depends on it instead of silently linking the old objects against the new headers.

## Rust notes

We suggest to install Rust and Cargo using [rustup](http://rustup.rs/). This way you will get the most recent version
installed in your home directory. As rust is a rapidly developing language the binaries provided by your distribution
might be too old to build ja2-stracciatella and its dependencies. When using rustup the correct version of rust should
be automatically selected.

If you don't want to use rustup, you can always look up the currently required version in the
[min-rust-version file](https://github.com/ja2-stracciatella/ja2-stracciatella/blob/master/min-rust-version)

## Build on Linux or freeBSD

```sh
cmake path/to/source
make
```

If you want to be able to install the resulting binary on your system, please ensure that `CMAKE_INSTALL_PREFIX` matches
with `EXTRA_DATA_DIR`. Example: `cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DEXTRA_DATA_DIR=/usr/local/share/ja2 path/to/source`

## Build on OpenBSD (tested on -current as of mid-November 2021)

```sh
# The bundled/downloaded GTest sources fail to build.
doas pkg_add gtest

cmake path/to/source -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-openbsd.cmake
make
```

## Build on NixOS

Open a nix-shell that downloads all required dependencies, builds, and then automatically exits the nix-shell.

```sh
nix-shell -p rustc cargo sdl3 fltk cmake libGL --run "cmake path/to/source && make"
```

## Build for Windows on Linux using MinGW (cross build)

Additional requirements: MinGW compiler

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-mingw.cmake path/to/source
make
```

If you are using rustup, you might need to add the MinGW target to the rust toolchain before compiling.

When building for 64-bit:

```sh
rustup target add x86_64-pc-windows-gnu
```

When building for 32-bit:

```sh
rustup target add i686-pc-windows-gnu
```

## Build on macOS

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-macos.cmake path/to/source
make
```

## Build on Windows using MSYS2

Install [msys2](https://www.msys2.org/).

Open the msys2 shell.
Use "MSYS MinGW 64-bit" to build 64-bit and "MSYS MinGW 32-bit" to build 32-bit.

Update msys2, you might have to restart the msys2 shell and run the command again:

```sh
pacman -Syu
```

Install the build environment and dependencies:

```sh
pacman -S base-devel
```

to build 64-bit:

```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-rust mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL3 mingw-w64-x86_64-fltk
```

to build 32-bit:

```sh
pacman -S mingw-w64-i686-toolchain mingw-w64-i686-rust mingw-w64-i686-cmake mingw-w64-i686-SDL3 mingw-w64-i686-fltk
```

Get ja2-stracciatella, cd into it, and build the package:

```sh
mkdir _bin && cd _bin
cmake .. "-GMSYS Makefiles" -DCPACK_GENERATOR=ZIP
make package
```

For faster iteration, configure a fresh build directory with `-G Ninja` instead and build with
`cmake --build .` — see [Faster builds](#faster-builds).

You now have a zip file with the game, including the dll dependencies.

## Build for Android

The Android project uses Gradle with CMake for the native code (see `android/app/build.gradle`).
The supported ABIs are `armeabi-v7a`, `arm64-v8a`, `x86`, and `x86_64`.

Install

- Android NDK (see `ndkVersion` in [`android/app/build.gradle`](android/app/build.gradle))
- Rust Android targets: `armv7-linux-androideabi`, `aarch64-linux-android`, `i686-linux-android`, `x86_64-linux-android`
  (install via `rustup target add <target>`)

The build with

```sh
cd android
./gradlew assembleDebug
```

For a release build (requires a signing keystore, see [`ci-setup.sh`](.ci/ci-setup.sh)):

```sh
./gradlew assembleRelease
```

You can also use Android Studio to compile the project. For this, just open the `android` directory in Android Studio with all prerequisites installed.

## Generate Visual Studio Solution

If you are most familiar using Visual Studio for development you can generate a solution from the sources.

Install Visual C++, CMake tools, MSBuild and Windows SDK with Visual Studio Installer.

Then in Visual Studio's Developer Command Prompt, change to the ja2-stracciatella project directory, and generate the solution with CMake:

```sh
mkdir _bin
cd _bin
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-msvc.cmake ..
```

__Note__: If you add, move or delete any files. Please make sure to reflect your changes in the `CMakeLists.txt` files,
rerun cmake and reload your Solution before making any additional changes. Otherwise other build systems might fail
 when trying to build your changes.

## Generate XCode Project

If you are most familiar using XCode for development you can generate a project from the sources.

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-macos.cmake -G "XCode" path/to/source
```

__Note__: If you add, move or delete any files. Please make sure to reflect your changes in the `CMakeLists.txt` files,
rerun cmake and reload your XCode project before making any additional changes. Otherwise other build systems might fail
 when trying to build your changes.

## Additional Options

If you want to configure the build differently, you can pass additional options to
cmake. The supported options are:

| Switch        | Description           | Default  |
| ------------- |-------------| -----|
| `EXTRA_DATA_DIR` | Directory to read externalized data from relative to binary location. Useful for creating installable packages that have a fixed data path. | `` |
| `LOCAL_SDL_LIB` | Use SDL library from this directory. | `` |
| `WITH_UNITTESTS` | Build with unit tests | `ON` |
| `WITH_FIXMES` | Build with fixme messages | `OFF` |
| `WITH_MAEMO` | Build with right click mapped to F4 (menu button) | `OFF` |
| `WITH_EDITOR_SLF` | Download the latest free editor.slf during build | `OFF` |

Example:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-macos.cmake -DWITH_FIXMES=ON path/to/source
make
```
