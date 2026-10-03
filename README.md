<p align="center">
  <img src="docs/slippy.svg" width="160" alt="Slippy the frog">
</p>

<h1 align="center">Slippy</h1>

<p align="center">Native plug-ins that let AI agents work in Adobe Illustrator and Photoshop,<br>
with a frog that shows you what they're doing.</p>

Slippy puts an MCP server inside the Adobe app itself. An agent (Claude Code,
Codex, Cursor, Gemini...) connects to it over loopback HTTP, and every call
runs on the app's main thread through Adobe's C++ SDK: no ExtendScript, no
bridge process, no Python. Each edit is one undo step named after what it did,
nothing is saved by surprise, no dialog ever blocks, and Slippy's panel shows
each call as it happens in plain English.

| | macOS | Windows (x64, ARM64) | Port | Docs |
|---|---|---|---|---|
| **Slippy for Illustrator** | `Slippy.aip` | `Slippy.aip` | 7331 | [illustrator/](illustrator/README.md) |
| **Slippy for Photoshop** | `Slippy.plugin` | `Slippy.8li` | 7332 | [photoshop/](photoshop/README.md) |

They're separate installs, each with its own token, and they run side by side.
The Windows builds compile in CI but haven't been run in the apps yet.

## Build

**macOS** (Xcode Command Line Tools only), with the SDKs at
`~/Developer/AdobeIllustratorSDK` and `~/Developer/AdobePhotoshopSDK`:

```sh
make                       # Illustrator: build/Slippy.aip
make install               # into Illustrator's Plug-ins (the first time: sudo make install)
make photoshop             # Photoshop: build/ps/Slippy.plugin
make install-photoshop     # into Photoshop's Plug-ins/Slippy (make that folder yours once - see photoshop/)
make preview               # the panel and Slippy in a plain window, no Adobe app needed
```

**Windows** (Visual Studio 2022, CMake, Python 3): one CMake project builds
both; leave out an SDK to build just the other plug-in.

```sh
cmake -S . -B build-win -A x64 -DAI_SDK="C:/sdk/AdobeIllustratorSDK" -DPS_SDK="C:/sdk/AdobePhotoshopSDK"   # or -A ARM64
cmake --build build-win --config Release
# -> build-win/illustrator/Release/Slippy.aip, build-win/photoshop/Release/Slippy.8li
```

**CI** (`.github/workflows/build.yml`) builds all four on every push to `main`
and on pull requests: macOS (signed with Developer ID and notarized once the
secrets are set, ad hoc until then) and Windows x64 / ARM64. Download them from
the run's artifacts; a `v*` tag publishes them as a release. The SDKs come from
this private repo's `sdk-illustrator-2026` and `sdk-photoshop-2026` releases.

| Setting | What |
|---|---|
| secret `MACOS_CERT_P12` | base64 of the Developer ID Application certificate, exported as a .p12 |
| secret `MACOS_CERT_PASSWORD` | that .p12's password |
| secret `APPLE_ID` | the Apple ID email |
| secret `APPLE_APP_PASSWORD` | an app-specific password for that Apple ID |
| variable `APPLE_TEAM_ID` | the developer team (GM98GV6S97) |

## Connecting an agent

Click **Copy connection** in Slippy's panel and give the agent what it copies,
`http://127.0.0.1:<port>/mcp/<token>`. Any MCP client takes it as a streamable
HTTP server. Agents get the everyday commands as tools, plus `slippy_find` /
`slippy_call` for the rest and `slippy_batch` to make several calls one undo
step. Scripts can use plain JSON-RPC at `/rpc/<token>` (see `client/`).

## The repo

```
shared/         what both plug-ins use, free of Adobe's SDKs: the HTTP server, MCP, JSON,
                platform glue, crash log, and the frog panel (Cocoa on macOS, GDI+ on Windows)
  resources/    the panel's terminal (xterm.js) and the agents' logos
illustrator/    Slippy for Illustrator: its commands, SDK glue, docked panel, resources
photoshop/      Slippy for Photoshop: its commands, action descriptors, floating panel
  uxp/          the earlier UXP + Node version, kept for reference
client/         slippy CLI and Python client (plain JSON-RPC)
tools/          build-time generators (agent logos)
docs/           this page's logo
Makefile        macOS builds        CMakeLists.txt   Windows builds
```
