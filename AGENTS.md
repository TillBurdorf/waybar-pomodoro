# Agent Guidelines for `waybar-pomodoro`

## ⚠️ DO NOT Run `make dev` or `dev.sh`

**NEVER run `make dev`, `./dev.sh`, or `waybar-pomodoro dev`.**

- The user already runs the live preview in their own terminal and keeps the tiled UI preview window open permanently on their screen.
- Launching `make dev` or `dev.sh` in background or subagent processes creates duplicate windows and disrupts the user's desktop layout.
- The user's open dev preview window automatically watches `internal/ui/` and hot-reloads code edits (and error messages) in place as soon as you save files.

---

## Allowed & Recommended Commands

When verifying code changes, use standard build and test commands:

- **Check compilation**: `go build ./cmd/waybar-pomodoro`
- **Run tests**: `go test ./...`
- **Build / install binary** (when requested or testing production build): `make build` or `make install`

---

## Architecture Overview

- [`cmd/waybar-pomodoro/main.go`](file:///home/till/Dev/waybar-pomodoro/cmd/waybar-pomodoro/main.go): CLI entry point (`daemon`, `waybar`, `ui`, `dev`, `stats`, `toggle`, etc.).
- [`internal/ui/view.go`](file:///home/till/Dev/waybar-pomodoro/internal/ui/view.go): Shared Fyne UI layout and component logic (`BuildView`).
- [`internal/ui/theme.go`](file:///home/till/Dev/waybar-pomodoro/internal/ui/theme.go): Catppuccin Mocha palette and custom Fyne theme (`NewMochaTheme`).
- [`internal/ui/ui.go`](file:///home/till/Dev/waybar-pomodoro/internal/ui/ui.go): Production floating Waybar popup (`RunUI`).
- [`internal/ui/dev.go`](file:///home/till/Dev/waybar-pomodoro/internal/ui/dev.go): Live-reloading dev preview host (`RunDevUI`).
- [`internal/timer/`](file:///home/till/Dev/waybar-pomodoro/internal/timer): Background daemon and Waybar client logic.
- [`internal/stats/`](file:///home/till/Dev/waybar-pomodoro/internal/stats): Session tracking and history storage.
- [`internal/ipc/`](file:///home/till/Dev/waybar-pomodoro/internal/ipc): Unix domain socket IPC between Waybar, UI, and daemon.
