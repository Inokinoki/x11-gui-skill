#!/usr/bin/env bash
#
# xvfb-gui-loop.sh - Claude Code Skill for Xvfb GUI Automation
#
# Usage: xvfb-gui-loop.sh <command> [args...]
#

set -e

# Configuration
XVFB_DISPLAY="${XVFB_DISPLAY:-:99}"
XVFB_RESOLUTION="${XVFB_RESOLUTION:-1920x1080x24}"

log() { echo "[xvfb] $*"; }
error() { echo "[xvfb] ERROR: $*" >&2; }

# Check for required tools
check_xdotool() {
    if ! command -v xdotool &>/dev/null; then
        error "xdotool not found. Install with: sudo apt install xdotool"
        return 1
    fi
}

# Check for screenshot tool (maim or ffmpeg)
get_capture_tool() {
    if command -v maim &>/dev/null; then
        echo "maim"
    elif command -v ffmpeg &>/dev/null; then
        echo "ffmpeg"
    elif command -v import &>/dev/null; then
        echo "import"
    else
        return 1
    fi
}

# Check if display is available
display_available() {
    xdpyinfo -display "$1" >/dev/null 2>&1
}

# Command: start - Start Xvfb and run a command
cmd_start() {
    local resolution="${1:-$XVFB_RESOLUTION}"
    shift || true

    if [ $# -eq 0 ]; then
        error "start requires a command to run"
        echo "Usage: xvfb-gui-loop.sh start [resolution] <command> [args...]"
        return 1
    fi

    log "Starting Xvfb with resolution: $resolution"

    # Run command with xvfb-run
    xvfb-run --auto-servernum --server-args="-screen 0 $resolution" "$@" &
    local pid=$!

    log "Started command with PID: $pid"
    echo "APP_PID=$pid"

    # Wait for X display to be ready
    sleep 1
    echo "DISPLAY=$DISPLAY"
}

# Command: attach - Attach to existing display (including xpra)
cmd_attach() {
    local display="$1"

    if [ -z "$display" ]; then
        error "attach requires display argument"
        return 1
    fi

    if ! display_available "$display"; then
        error "Cannot access display $display"
        return 1
    fi

    export DISPLAY="$display"
    log "Attached to display: $display"
    echo "DISPLAY=$display"

    # Check if xpra (optional)
    if command -v xpra &>/dev/null && xpra list 2>/dev/null | grep -q "$display"; then
        echo "TYPE=xpra"
    else
        echo "TYPE=x11"
    fi
}

# Command: capture - Take screenshot
cmd_capture() {
    local output="${1:-/tmp/xvfb-capture-$(date +%Y%m%d-%H%M%S).png}"

    local tool=$(get_capture_tool)
    if [ -z "$tool" ]; then
        error "No screenshot tool found."
        echo "Install one of: maim, ffmpeg, or import (ImageMagick)"
        echo "  sudo apt install maim     # Recommended"
        echo "  sudo apt install ffmpeg   # Alternative"
        echo "  sudo apt install imagemagick  # Alternative"
        return 1
    fi

    log "Capturing $DISPLAY to $output (using $tool)"

    case "$tool" in
        maim)
            maim -d "$DISPLAY" "$output"
            ;;
        ffmpeg)
            local dims=$(xdpyinfo -display "$DISPLAY" 2>/dev/null | grep "dimensions:" | awk '{print $2}' || echo "1920x1080")
            ffmpeg -f x11grab -video_size "$dims" -i "$DISPLAY" -frames:v 1 -q:v 2 "$output" 2>/dev/null
            ;;
        import)
            import -display "$DISPLAY" -window root "$output"
            ;;
    esac

    if [ -f "$output" ]; then
        log "Captured: $output"
        echo "SCREENSHOT=$output"
    else
        error "Capture failed"
        return 1
    fi
}

# Command: record - Start/stop video recording (requires ffmpeg)
cmd_record() {
    if ! command -v ffmpeg &>/dev/null; then
        error "ffmpeg not found. Video recording requires ffmpeg."
        echo "Install with: sudo apt install ffmpeg"
        return 1
    fi

    local output="${1:-/tmp/xvfb-recording.mp4}"
    local pid_file="/tmp/xvfb-recording.pid"

    # Check if recording
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            log "Stopping recording..."
            kill -INT "$pid"
            sleep 1
            kill "$pid" 2>/dev/null || true
            rm -f "$pid_file"
            log "Recording saved: $output"
            echo "RECORDING_STOPPED=$output"
            return 0
        fi
    fi

    # Start recording
    local dims=$(xdpyinfo -display "$DISPLAY" 2>/dev/null | grep "dimensions:" | awk '{print $2}' || echo "1920x1080")
    log "Starting recording to $output"

    ffmpeg -f x11grab -framerate 30 -video_size "$dims" -i "$DISPLAY" \
        -c:v libx264 -preset ultrafast -crf 23 "$output" &
    echo $! > "$pid_file"

    log "Recording started (PID: $!)"
    echo "RECORDING_PID=$!"
    echo "RECORDING_FILE=$output"
}

# Command: type - Type text
cmd_type() {
    check_xdotool || return 1
    local text="$1"
    [ -z "$text" ] && { error "type requires text"; return 1; }
    log "Typing: $text"
    xdotool type -- "$text"
}

# Command: key - Press key
cmd_key() {
    check_xdotool || return 1
    local keysym="$1"
    [ -z "$keysym" ] && { error "key requires keysym"; return 1; }
    log "Sending key: $keysym"
    xdotool key "$keysym"
}

# Command: click - Click mouse
cmd_click() {
    check_xdotool || return 1
    local button="${1:-1}"
    log "Clicking button $button"
    xdotool click "$button"
}

# Command: clickat - Click at position
cmd_clickat() {
    check_xdotool || return 1
    local x="$1" y="$2" button="${3:-1}"
    [ -z "$x" ] || [ -z "$y" ] && { error "clickat requires x y"; return 1; }
    log "Clicking at ($x,$y) button $button"
    xdotool mousemove "$x" "$y" click "$button"
}

# Command: move - Move mouse
cmd_move() {
    check_xdotool || return 1
    local x="$1" y="$2"
    [ -z "$x" ] || [ -z "$y" ] && { error "move requires x y"; return 1; }
    log "Moving to ($x,$y)"
    xdotool mousemove "$x" "$y"
}

# Command: drag - Drag from one position to another
cmd_drag() {
    check_xdotool || return 1
    local x1="$1" y1="$2" x2="$3" y2="$4" button="${5:-1}"
    [ -z "$x1" ] || [ -z "$y1" ] || [ -z "$x2" ] || [ -z "$y2" ] && { error "drag requires x1 y1 x2 y2"; return 1; }
    log "Dragging ($x1,$y1) -> ($x2,$y2)"
    xdotool mousemove "$x1" "$y1"
    xdotool mousedown "$button"
    usleep 100000
    xdotool mousemove "$x2" "$y2"
    xdotool mouseup "$button"
}

# Command: search - Find window by name
cmd_search() {
    check_xdotool || return 1
    local pattern="$1"
    [ -z "$pattern" ] && { error "search requires pattern"; return 1; }

    log "Searching for: $pattern"
    local results=$(xdotool search --name "$pattern" 2>/dev/null)

    if [ -z "$results" ]; then
        log "No windows found matching '$pattern'"
        return 0
    fi

    while IFS= read -r winid; do
        local name=$(xdotool getwindowname "$winid" 2>/dev/null || echo "Untitled")
        echo "WINDOW=$winid NAME=\"$name\""
    done <<< "$results"
}

# Command: activate - Focus window
cmd_activate() {
    check_xdotool || return 1
    local window_id="$1"
    [ -z "$window_id" ] && { error "activate requires window_id"; return 1; }
    log "Activating window $window_id"
    xdotool windowactivate --sync "$window_id"
}

# Command: list - List windows
cmd_list() {
    check_xdotool || return 1
    log "Windows on $DISPLAY:"

    local windows=$(xdotool search --all "" 2>/dev/null)
    if [ -n "$windows" ]; then
        while IFS= read -r winid; do
            local name=$(xdotool getwindowname "$winid" 2>/dev/null || echo "Untitled")
            echo "WINDOW=$winid NAME=\"$name\""
        done <<< "$windows"
    else
        log "No windows found"
    fi
}

# Command: info - Show session info
cmd_info() {
    echo "=== Xvfb Session Info ==="
    echo "DISPLAY=$DISPLAY"

    local dims=$(xdpyinfo -display "$DISPLAY" 2>/dev/null | grep "dimensions:" | head -1)
    echo "SCREEN: $dims"

    local ptr=$(xdotool getmouselocation 2>/dev/null)
    echo "POINTER: $ptr"

    local active=$(xdotool getactivewindow 2>/dev/null)
    if [ -n "$active" ]; then
        local name=$(xdotool getwindowname "$active" 2>/dev/null)
        echo "ACTIVE_WINDOW=$active NAME=\"$name\""
    fi

    local count=$(xdotool search --all "" 2>/dev/null | wc -l)
    echo "WINDOW_COUNT=$count"
    echo "========================="
}

# Show help
show_help() {
    cat <<'EOF'
Xvfb GUI Loop - Claude Code Skill

Usage: xvfb-gui-loop.sh <command> [args...]

Commands:
  start [resolution] <cmd>   Start Xvfb and run command
  attach <display>           Attach to existing display (including xpra)
  capture [file.png]         Take screenshot
  record [file.mp4]          Start/stop video recording
  type <text>                Type text
  key <keysym>               Press key (e.g., Return, ctrl+s)
  click [button]             Click mouse (1=left, 2=mid, 3=right)
  clickat <x> <y> [button]   Click at position
  move <x> <y>               Move mouse
  drag <x1> <y1> <x2> <y2>   Drag operation
  search <pattern>           Find window by name
  activate <window_id>       Focus window
  list                       List all windows
  info                       Show session info

Examples:
  xvfb-gui-loop.sh start firefox
  xvfb-gui-loop.sh start 1280x720x24 gedit
  xvfb-gui-loop.sh attach :100
  xvfb-gui-loop.sh capture screen.png
  xvfb-gui-loop.sh type "Hello"
  xvfb-gui-loop.sh key ctrl+s
  xvfb-gui-loop.sh clickat 100 200 1

Common keysyms: Return, Tab, Escape, space, ctrl+c, ctrl+s, Alt_L, F1-F12
EOF
}

# Main
if [ $# -eq 0 ]; then
    show_help
    exit 0
fi

cmd="$1"
shift || true

case "$cmd" in
    start)     cmd_start "$@" ;;
    attach)    cmd_attach "$@" ;;
    capture)   cmd_capture "$@" ;;
    record)    cmd_record "$@" ;;
    type)      cmd_type "$@" ;;
    key)       cmd_key "$@" ;;
    click)     cmd_click "$@" ;;
    clickat)   cmd_clickat "$@" ;;
    move)      cmd_move "$@" ;;
    drag)      cmd_drag "$@" ;;
    search)    cmd_search "$@" ;;
    activate)  cmd_activate "$@" ;;
    list)      cmd_list "$@" ;;
    info)      cmd_info "$@" ;;
    help|--help|-h) show_help ;;
    *)         error "Unknown command: $cmd"; show_help; exit 1 ;;
esac
