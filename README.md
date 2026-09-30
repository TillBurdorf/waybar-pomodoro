# Waybar Pomodoro 🍅

A lightweight, daemon-driven Pomodoro timer built in Go specifically for **Waybar** on Linux (Wayland). It features real-time Unix socket IPC, desktop notifications, session history logging, and a dedicated GTK4 control popup styled with Catppuccin Mocha.

---

## Features

- ⚡ **Zero-Polling Daemon**: Runs a background daemon that broadcasts timer updates over a local Unix domain socket directly to Waybar.
- 🍅 **25 / 5 Cycle**: 25-minute focus sessions followed by 5-minute short breaks.
- 🔔 **Desktop Notifications**: Automatic alerts via `notify-send` when sessions or breaks conclude.
- 🖥️ **Native GTK4 GUI Control Panel**: Sleek floating window built with GTK4 featuring a live countdown, progress bar, today's focus stats, and media controls.
- 📊 **Session Logging & History**: Automatically tracks completed sessions in `~/.local/share/waybar-pomodoro/stats.jsonl`.
- ⌨️ **Full CLI Control**: Simple CLI commands to toggle, skip, reset, or query stats from keybindings or scripts.

---

## Prerequisites

- **Go**: 1.21 or newer
- **GTK 4 development files**:
  - **Arch Linux**: `gtk4`
  - **Fedora**: `gtk4-devel`
  - **Debian / Ubuntu**: `libgtk-4-dev`
- **Notification Daemon**: `libnotify` (`notify-send`)

---

## Installation

Clone the repository and run `make install`:

```bash
git clone https://github.com/your-username/waybar-pomodoro.git
cd waybar-pomodoro
make install
```

This compiles the binary and places it into `~/.local/bin/waybar-pomodoro`. Ensure `~/.local/bin` is in your `$PATH`.

---

## Window Manager / Compositor Setup (Important)

Because tiling compositors automatically tile new windows to fill screen space, you **must** configure a window rule to float the control window (`waybar-pomodoro ui`).

### Hyprland

#### Standard Configuration (`hyprland.conf`)
Add the following rule to `~/.config/hypr/hyprland.conf`:

```ini
windowrulev2 = float, class:^(io.github.waybarpomodoro.gtk)$
windowrulev2 = center, class:^(io.github.waybarpomodoro.gtk)$
windowrulev2 = size 320 360, class:^(io.github.waybarpomodoro.gtk)$
```

#### Lua Configuration (`looknfeel.lua` / `hyprland.lua`)
If using Hyprland with Lua configuration:

```lua
hl.window_rule({
    match = { class = "^io.github.waybarpomodoro.gtk$" },
    float = true,
    center = true,
    size = { 320, 360 },
})
```

---

### Sway / i3

Add the following to your Sway/i3 configuration (`~/.config/sway/config`):

```
for_window [app_id="io.github.waybarpomodoro.gtk"] floating enable, move position center
```

---

## Waybar Configuration

Add the custom module to your Waybar configuration (usually `~/.config/waybar/config.jsonc`):

```jsonc
"custom/pomodoro": {
    "format": "{}",
    "return-type": "json",
    "exec": "waybar-pomodoro waybar",
    "on-click": "waybar-pomodoro ui",
    "on-click-right": "waybar-pomodoro toggle",
    "on-click-middle": "waybar-pomodoro stop"
}
```

Add `"custom/pomodoro"` to your `modules-left`, `modules-center`, or `modules-right`.

### Waybar Styling (`style.css`)

You can style the module based on its status classes:

```css
#custom-pomodoro {
    padding: 0 10px;
    color: #cdd6f4; /* Default Catppuccin Mocha Text when paused */
}

/* Green when timer is running */
#custom-pomodoro.running {
    color: #a6e3a1; /* Catppuccin Mocha Green */
}
```

---

## CLI Usage

| Command | Description |
| :--- | :--- |
| `waybar-pomodoro ui` | Opens or toggles the GTK4 control dashboard |
| `waybar-pomodoro dev` | Launches standalone mock UI with test hotkeys |
| `waybar-pomodoro waybar` | Streams JSON status updates directly for Waybar |
| `waybar-pomodoro toggle` | Starts or pauses the active countdown |
| `waybar-pomodoro skip` | Skips to the next phase (Work ⇄ Break) |
| `waybar-pomodoro reset` | Resets the current phase countdown to its start |
| `waybar-pomodoro stop` | Stops the timer and resets back to Work phase (25:00) |
| `waybar-pomodoro stats` | Displays today's completed session count and total focus time |
| `waybar-pomodoro daemon` | Runs the timer server in the foreground |

---

## UI Development & Live Preview

To test and develop the GTK4 UI with instant live-reloading:

```bash
make dev
```

This starts [air](https://github.com/air-verse/air) to watch for file changes in `internal/ui/` and `cmd/`. Every time you save changes to the Go or GTK code:
- The preview window is immediately rebuilt and refreshed (<0.5s).
- It runs with mock data, so you don't need the background daemon running.
- **Interactive Preview Keys**:
  - `M`: Toggle between Focus 🍅 and Break ☕ themes
  - `P`: Cycle progress bar (100% → 50% → 25% → 5%)
  - `C`: Cycle completed session dot counts
  - `+` / `-`: Add or subtract 1 minute
  - `Space`: Run / pause local simulated countdown
  - `S` / `R` / `X`: Test Skip, Reset, and Stop actions
  - `Q` / `Esc`: Exit preview

---

## Session History

Each completed 25-minute focus session is automatically appended to:
```
~/.local/share/waybar-pomodoro/stats.jsonl
```

Format:
```json
{"timestamp":"2026-09-29T15:30:00Z","mode":"work","duration_seconds":1500}
```

You can inspect your history anytime using the CLI or tools like `jq`:
```bash
waybar-pomodoro stats
# or
cat ~/.local/share/waybar-pomodoro/stats.jsonl | jq .
```

---

## License

MIT
