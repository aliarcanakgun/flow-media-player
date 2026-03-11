# flow-media-player

A minimal, fullscreen video player built with **SDL2** and **libmpv**.  
It reads a JSON playlist, plays each video back-to-back, and adds smooth fade transitions between them. No UI chrome, no distractions — just the video.

## Features

- **JSON playlist** — point it at a `.json` file and it plays through your videos in order. paths can be relative to the playlist file.
- **per-video volume** — set a different volume level for each entry in the playlist.
- **skip points** — define timestamp markers per video; hit a number key (1-9) to jump there with a quick crossfade.
- **fade transitions** — 1 s fade-in when a video starts, 600 ms fade-out when it ends or when switching tracks.
- **crossfade seeks** — jumping to a skip point or restarting does a fast 150 ms crossfade instead of a hard cut.
- **pause / play overlay** — a small icon appears in the corner and fades out after a second.
- **auto-hide cursor** — the mouse cursor is hidden so it doesn't sit on top of your video.
- **hardware decoding** — uses `hwdec=auto` so playback is easy on the CPU when your GPU supports it.

## Playlist format

> **Note:** Currently, the JSON playlist must be created manually. A simple UI tool is planned for the future to easily generate and manage these playlists.

Create a JSON file like this:

```json
[
    {
        "path": "videos/intro.mp4",
        "volume": 80,
        "skip_points": [30, 95, 210]
    },
    {
        "path": "videos/main.mp4",
        "volume": 50,
        "skip_points": []
    }
]
```

- **path** — absolute or relative to the playlist file.
- **volume** — 0–100, defaults to 100 if omitted.
- **skip_points** — list of timestamps in seconds. keys 1–9 jump to the corresponding entry.

## Controls

| key | action |
|---|---|
| **Space** | Pause / Resume |
| **→** | Next video |
| **←** | Previous video |
| **↑ / ↓** | Volume up / down (±10) |
| **1 – 9** | Jump to skip point |
| **0** | Restart current video (paused) |
| **Esc** | Quit |
| **Win** | Pause and minimize |

## Building

### Prerequisites

- **CMake** 3.10+
- a C++17 compiler (MSVC, MinGW, etc.)
- **libmpv** dev files — run the included `download_mpv.ps1` to grab them, or drop `libmpv.dll.a` and the headers into the `libmpv/` folder yourself.

### Steps

```bash
# configure
cmake -B build -S .

# build
cmake --build build --config Release
```

The executable ends up in `build/Release/`. Make sure `libmpv-2.dll` is next to it (the build copies it automatically, or you can copy it from `libmpv/`).

## How it works

The whole player lives in a single `main.cpp`. SDL2 creates a borderless fullscreen window with an OpenGL context, and libmpv renders each frame into it. Transitions are drawn as a fullscreen black quad with varying alpha — no shaders, just fixed-function GL.  
Playlist navigation, volume, and skip points are all handled through mpv's client API.

## License

[GNU General Public License v3.0](LICENSE)
