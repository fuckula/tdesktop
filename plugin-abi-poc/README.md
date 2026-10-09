# Plugin ABI proof of concept

The contract the fork's plugin system will be built on, verified end to end.
Standalone: no tdesktop build, no Docker, no submodules. Qt6 Core + C++ compiler
+ cargo, a few seconds.

```
./build_all.sh
```

Exit status is the verdict. Both suites assert **exact check counts**, so a test
that silently stops running fails the run instead of shrinking a green number.

## What is here

| Path | Role |
|---|---|
| `tdp_plugin.h` | The C ABI. No Qt types. Both languages implement this. |
| `tdp_plugin_qt.h` | Optional Qt layer, for plugins that want signals. Kept separate so a pure-ABI plugin never sees Qt. |
| `host/` | Stands in for tdesktop: loads plugins, drives both boundaries. |
| `rustplugin/` | Rust plugin — `staticlib` + `cdylib`. |
| `cppplugin/` | C++ plugin, same header, no Qt. |
| `s1_static/` | Static-Qt loader probe (spike S1). |

## Suite 1 — the ABI contract (`run.log`, 34 checks)

Phase A runs the C ABI for **every** plugin regardless of language: version
negotiation, descriptor, handshake, the message pipeline, a context-menu
contribution, persistence, panic containment, teardown order. Phase B runs only
for plugins that are also Qt plugins, proving `QObject` integration and a
signal-driven callback crossing into Rust.

## Suite 2 — static Qt (`s1_static/run.log`, 10 checks)

One plugin source, compiled twice with the sole difference `-DQT_STATICPLUGIN`.
That define is what a static Qt puts in its own `qtcore-config.h`, so a plugin
built against a static Qt gets it whether it wants to or not.

Official tdesktop builds link Qt statically. Under `QT_STATICPLUGIN`,
`qplugin.h` selects `StaticPayload`, which emits no `.note.qt.metadata` ELF note,
so `QPluginLoader` cannot see the plugin. The probe demonstrates this against a
release-like host and shows the loader path that still works.

## Findings that shaped the design

- **`cargo build` alone cannot produce a Qt-loadable `.so`.** rustc's `cdylib`
  link step emits a version script that localizes everything except the crate's
  own `#[no_mangle]` exports, hiding `qt_plugin_instance`. Fix: cargo builds a
  `staticlib`, the C++ driver performs the final link (`rustplugin/CMakeLists.txt`).
- **The loader must be `dlopen`-based, not `QPluginLoader`.** See suite 2.
- **Rust panics must be caught at every entry point** (`catch_unwind`), otherwise
  unwinding out of an `extern "C"` frame aborts the process.
- **`unload()` returns `false` until the last handle releases** — Qt shares the
  refcount by path, so a plugin is not unloaded by dropping one reference.
- **`Qt::UniqueConnection` silently breaks functor-based `connect()`.**

## Status

Proof of concept. Stage M1 promotes this into the client as the real
`plugin_host`; the ABI header is expected to survive that move unchanged.
