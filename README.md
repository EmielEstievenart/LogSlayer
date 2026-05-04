# LogSlayer

LogSlayer is a terminal log viewer for watching one or more log sources while they change. It can open local files, folders of log files, and SSH log sources, then lets you search, filter, jump between matches, and manage sources from an interactive command palette.

## Usage

Start it with one or more files:

```sh
LogSlayer app.log worker.log
```

You can also pass sources with `--file`, including SSH sources:

```sh
LogSlayer --file app.log --file ssh://user@example.com/var/log/app.log
```

Run the built executable with `--help` or `-h` for the full list of command line options, source syntax, key bindings, config locations, and command palette commands.

## Building

This is a CMake project using presets. You need:

- CMake 3.21 or newer.
- A supported compiler from the presets: GCC on Linux, Visual Studio 2022/MSVC on Windows, or LLVM/Clang on Windows.
- The git submodules checked out with `git submodule update --init --recursive`.
- A Boost source tree, passed through `BOOST_ROOT` or `-DBOOST_ROOT=<path-to-boost>`.

Boost is not fetched as a submodule. CMake calls `add_subdirectory("${BOOST_ROOT}" my_boost)` and builds the needed Boost libraries from that source tree: `asio`, `system`, `program_options`, `log`, `filesystem`, `thread`, `regex`, `date_time`, and `atomic`.

Typical preset build:

```sh
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

or, for Linux:

```sh
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
```

The presets read `BOOST_ROOT` from the environment and build under `out/build/<preset>`. There are debug and release presets for Linux GCC, Windows MSVC, and Windows Clang, plus a Linux ThreadSanitizer preset with CTest disabled.

Tests are included through GoogleTest and can be run with the matching test preset:

```sh
ctest --preset windows-msvc-debug
```

One note: `libs/ftxui_components` is a git submodule using an SSH GitHub URL, so cloning the submodules needs GitHub SSH access unless that URL is changed locally.
