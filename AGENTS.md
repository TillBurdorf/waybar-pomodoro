# Agent Guidelines for `waybar-pomodoro`

## Architecture Overview

- [`cmd/waybar-pomodoro/main.go`](file:///home/till/Dev/waybar-pomodoro/cmd/waybar-pomodoro/main.go): CLI entry point (`daemon`, `waybar`, `ui`, `dev`, `stats`, `toggle`, `skip`, `reset`, `stop`).
- [`internal/ui/ui.go`](file:///home/till/Dev/waybar-pomodoro/internal/ui/ui.go): Production floating Waybar popup (`RunUI`), single-instance toggle, and daemon subscription.
- [`internal/ui/dev.go`](file:///home/till/Dev/waybar-pomodoro/internal/ui/dev.go): Standalone dev preview host with mock state and interactive hotkeys (`RunDevUI`).
- [`internal/ui/gtk_ui.c`](file:///home/till/Dev/waybar-pomodoro/internal/ui/gtk_ui.c): Native GTK4 UI implementation with Catppuccin Mocha styling, keyboard shortcuts, and control buttons.
- [`internal/ui/gtk_ui.h`](file:///home/till/Dev/waybar-pomodoro/internal/ui/gtk_ui.h): C declarations for the GTK4 UI and Go CGO callbacks.
- [`internal/timer/`](file:///home/till/Dev/waybar-pomodoro/internal/timer): Background daemon and Waybar client logic.
- [`internal/stats/`](file:///home/till/Dev/waybar-pomodoro/internal/stats): Session tracking and history storage.
- [`internal/ipc/`](file:///home/till/Dev/waybar-pomodoro/internal/ipc): Unix domain socket IPC between Waybar, UI, and daemon.

---

## Allowed & Recommended Commands

When verifying code changes, use standard build and test commands:

- **Check compilation**: `go build ./cmd/waybar-pomodoro`
- **Run tests**: `go test ./...`
- **Build / install binary**: `make build` or `make install`
