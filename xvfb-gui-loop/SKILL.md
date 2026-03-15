---
name: xvfb-gui-loop
description: Xvfb GUI automation - capture screenshots and simulate input for Linux GUI testing
argument-hint: <command> [args...]
disable-model-invocation: false
allowed-tools: Bash(xvfb-run), Bash(xdotool), Bash(maim), Bash(ffmpeg)
---

# Xvfb GUI Loop Skill

Automate Linux GUI applications using Xvfb. Capture screenshots and simulate keyboard/mouse input for AI-assisted GUI design and debugging.

## Quick Start

```bash
# Start Xvfb session with an application
xvfb-gui-loop.sh start firefox

# Capture screenshot
xvfb-gui-loop.sh capture /tmp/screen.png

# Interact
xvfb-gui-loop.sh type "Hello World"
xvfb-gui-loop.sh key Return
xvfb-gui-loop.sh clickat 500 300 1
```

## Commands

| Command | Description | Example |
|---------|-------------|---------|
| `start [resolution] <cmd>` | Start Xvfb and run command | `start firefox` |
| `attach <display>` | Attach to existing display | `attach :100` |
| `capture [file.png]` | Take screenshot | `capture /tmp/s.png` |
| `record [file.mp4]` | Start/stop video (needs ffmpeg) | `record /tmp/v.mp4` |
| `type <text>` | Type text | `type "Hello"` |
| `key <keysym>` | Press key | `key ctrl+s` |
| `click [button]` | Click mouse | `click 1` |
| `clickat <x> <y> [btn]` | Click at position | `clickat 100 200 1` |
| `move <x> <y>` | Move mouse | `move 500 300` |
| `search <pattern>` | Find window | `search "Terminal"` |
| `activate <id>` | Focus window | `activate 0x1a00003` |
| `list` | List windows | `list` |
| `info` | Show session info | `info` |

## Dependencies

**Required:**
- `xvfb` - Virtual X server
- `xdotool` - Input simulation

**Optional (need one for screenshots):**
- `maim` - Recommended for screenshots
- `ffmpeg` - Alternative for screenshots, required for video recording

**Optional:**
- `xpra` - For shared displays (multi-agent sessions)

Install:
```bash
# Minimal (screenshots only)
sudo apt install xvfb xdotool maim

# With video recording
sudo apt install xvfb xdotool maim ffmpeg

# With xpra support
sudo apt install xvfb xdotool maim ffmpeg xpra
```

## Xpra Support

For shared sessions where multiple users/agents can interact:

```bash
# Start xpra session
xpra start :100

# Attach to it
xvfb-gui-loop.sh attach :100

# Commands work on shared display
xvfb-gui-loop.sh capture /tmp/shared.png
```

## Common Keysyms

```
Return  Tab  Escape  space  ctrl+c  ctrl+s  Alt_L  F1-F12
```
