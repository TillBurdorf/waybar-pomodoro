#!/usr/bin/env bash
# Live preview runner for Waybar Pomodoro UI
# Opens a single persistent window tiled by Hyprland that never closes or reopens.
# Automatically hot-reloads UI code edits and displays errors in-place.

set -e

mkdir -p tmp
exec go run ./cmd/waybar-pomodoro dev
