# OBS GNOME Capture

Successor to [obs-gnome-screencast](https://github.com/fzwoch/obs-gnome-screencast). Makes it a plugin that behaves similarly to `linux-capture` with less requires approvals.

## Features

- Capture GNOME/Mutter desktop or individual windows in OBS Studio
- No xdg-desktop-portal approval popups - uses Mutter's internal API directly
- Supports monitor and per-window capture
- Configurable cursor visibility
- PipeWire-based streaming with low latency

## How It Works

This plugin uses a two-part architecture to capture screen content without user interaction:

### 1. Mutter API Capture

Instead of going through `xdg-desktop-portal` (which requires user approval), the plugin communicates directly with Mutter's D-Bus API:

- **`org.gnome.Mutter.ScreenCast`** - Creates sessions and records monitors/windows directly
- **`org.gnome.Mutter.DisplayConfig`** - Enumerates available monitors/connectors

This is the same idea as original obs-gnome-screencast used but it is actually impossible to extract window-id use JS API.

### 2. Window Discovery via Ptrace Injection

To enumerate available windows for capture, the plugin uses a helper binary (`mutter_inject`) that:

1. Finds the running Mutter process by scanning `/proc` for `libmutter`
2. Uses `ptrace` to attach to Mutter
3. Locates the `meta_display_list_windows` function in `libmutter`
4. Injects a detour that loads `libmutter_list_windows.so` and calls its `meta_display_list_window_detour`
5. The detour writes window info (id, title, WM class) to `/run/user/<uid>/obs_windows.json`
6. OBS reads this JSON file to populate the window selection dropdown

### 3. PipeWire Video Capture

The actual video stream is handled by PipeWire, adapted from the screencast-portal implementation:

- Connects to the PipeWire daemon to negotiate video formats
- Supports modifiers, cursor overlay, and various video formats
- Renders frames directly into OBS via `gs_texture_t`
- Handles stream lifecycle (creation, teardown, renegotiation)
