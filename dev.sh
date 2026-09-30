#!/usr/bin/env bash
# Live preview runner for Waybar Pomodoro GTK UI
# Opens a persistent dev preview window tiled by Hyprland.
# Automatically hot-reloads UI code edits when air is available.

set -e

mkdir -p tmp

# Stop any background air processes that would kill the dev preview window on edit
pkill -f "air" 2>/dev/null || true

exec go run ./cmd/waybar-pomodoro dev
