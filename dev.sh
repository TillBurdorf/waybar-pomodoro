#!/usr/bin/env bash
# Live preview runner for Waybar Pomodoro GTK UI
# Opens a persistent dev preview window tiled by Hyprland.
# Automatically hot-reloads UI code edits when air is available.

set -e

mkdir -p tmp

if command -v air >/dev/null 2>&1; then
    exec air
elif [ -x "$HOME/go/bin/air" ]; then
    exec "$HOME/go/bin/air"
else
    exec go run ./cmd/waybar-pomodoro dev
fi
