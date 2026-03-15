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
SESSIONS_DIR="/tmp/xvfb-gui-loop"
mkdir -p "$SESSIONS_DIR"

log() { echo "[xvfb] $*"; }
error() { echo "[xvfb] ERROR: $*" >&2; }

# Get paths
get_display_file() { echo "$SESSIONS_DIR/display"; }
get_pid_file() { echo "$SESSIONS_DIR/xvfb.pid"; }
get_auth_file() { echo "$SESSIONS_DIR/xauth"; }
get_auth_path_file() { echo "$SESSIONS_DIR/xauth.path"; }

# Save/load display and auth
save_session() {
    echo "$DISPLAY" > "$(get_display_file)"
    [ -n "$XAUTHORITY" ] && echo "$XAUTHORITY" > "$(get_auth_path_file)"
}
load_session() {
    if [ -f "$(get_display_file)" ]; then
        export DISPLAY=$(cat "$(get_display_file)")
    fi
    if [ -f "$(get_auth_path_file)" ]; then
        export XAUTHORITY=$(cat "$(get_auth_path_file)")
    fi
    [ -n "$DISPLAY" ] && return 0 || return 1
}

# Check tools
check_xdotool() {
    if ! command -v xdotool &>/dev/null; then
        error "xdotool not found. Install with: sudo apt install xdotool"
        return 1
    fi
}

get_capture_tool() {
    if command -v maim &>/dev/null; then echo "maim"
    elif command -v ffmpeg &>/dev/null; then echo "ffmpeg"
    elif command -v import &>/dev/null; then echo "import"
    else return 1; fi
}

display_available() { xdpyinfo -display "$1" >/dev/null 2>&1; }

# Command: start
cmd_start() {
    local resolution="${1:-$XVFB_RESOLUTION}"
    shift || true

    [ $# -eq 0 ] && { error "start requires a command to run"; return 1; }

    # Find free display
    local display_num=99
    while xdpyinfo -display ":$display_num" >/dev/null 2>&1; do
        display_num=$((display_num + 1))
    done
    local display=":$display_num"

    # Create auth file with proper cookie
    local auth_file=$(get_auth_file)
    local cookie=$(xxd -l 16 -p /dev/urandom)
    xauth -f "$auth_file" add "$display" . "$cookie" 2>/dev/null || true

    log "Starting Xvfb with resolution: $resolution on $display"

    # Start Xvfb with auth (use -ac to disable access control for local connections)
    Xvfb "$display" -screen 0 "$resolution" -ac -auth "$auth_file" -nolisten tcp &
    local xvfb_pid=$!
    echo "$xvfb_pid" > "$(get_pid_file)"

    # Wait for X server
    for i in $(seq 1 30); do
        if xdpyinfo -display "$display" >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done

    export DISPLAY="$display"
    export XAUTHORITY="$auth_file"
    save_session

    log "Xvfb ready on $DISPLAY"

    # Run application with proper environment
    log "Starting: $*"
    env DISPLAY="$DISPLAY" XAUTHORITY="$auth_file" "$@" &
    local app_pid=$!

    log "Started command with PID: $app_pid"
    echo "APP_PID=$app_pid"
    echo "DISPLAY=$DISPLAY"
}

# Command: attach
cmd_attach() {
    local display="$1"
    [ -z "$display" ] && { error "attach requires display argument"; return 1; }
    display_available "$display" || { error "Cannot access display $display"; return 1; }

    export DISPLAY="$display"
    save_session
    log "Attached to display: $display"
    echo "DISPLAY=$display"

    if command -v xpra &>/dev/null && xpra list 2>/dev/null | grep -q "$display"; then
        echo "TYPE=xpra"
    else
        echo "TYPE=x11"
    fi
}

# Command: use
cmd_use() {
    local display="$1"
    if [ -z "$display" ]; then
        load_session || { error "No display set. Use 'start' or 'attach' first."; return 1; }
        log "Using saved display: $DISPLAY"
    else
        export DISPLAY="$display"
        save_session
        log "Using display: $DISPLAY"
    fi
    echo "DISPLAY=$DISPLAY"
}

# Command: capture
cmd_capture() {
    load_session || true
    local output="${1:-/tmp/xvfb-capture-$(date +%Y%m%d-%H%M%S).png}"

    [ -z "$DISPLAY" ] && { error "No display set. Run 'start' or 'attach' first."; return 1; }

    local tool=$(get_capture_tool)
    [ -z "$tool" ] && {
        error "No screenshot tool found."
        echo "Install one of: maim, ffmpeg, or import"
        return 1
    }

    log "Capturing $DISPLAY to $output (using $tool)"

    case "$tool" in
        maim) DISPLAY="$DISPLAY" maim "$output" ;;
        ffmpeg)
            local dims=$(xdpyinfo -display "$DISPLAY" 2>/dev/null | grep "dimensions:" | awk '{print $2}' || echo "1920x1080")
            DISPLAY="$DISPLAY" ffmpeg -f x11grab -video_size "$dims" -i "$DISPLAY" -frames:v 1 -q:v 2 "$output" 2>/dev/null
            ;;
        import) DISPLAY="$DISPLAY" import -window root "$output" ;;
    esac

    [ -f "$output" ] && { log "Captured: $output"; echo "SCREENSHOT=$output"; } || { error "Capture failed"; return 1; }
}

# Command: record
cmd_record() {
    load_session || true
    [ -z "$DISPLAY" ] && { error "No display set"; return 1; }
    command -v ffmpeg &>/dev/null || { error "ffmpeg required for recording"; return 1; }

    local output="${1:-/tmp/xvfb-recording.mp4}"
    local pid_file="$SESSIONS_DIR/recording.pid"

    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            log "Stopping recording..."
            kill -INT "$pid"; sleep 1; kill "$pid" 2>/dev/null || true
            rm -f "$pid_file"
            log "Recording saved: $output"
            echo "RECORDING_STOPPED=$output"
            return 0
        fi
    fi

    local dims=$(xdpyinfo -display "$DISPLAY" 2>/dev/null | grep "dimensions:" | awk '{print $2}' || echo "1920x1080")
    log "Starting recording to $output"
    DISPLAY="$DISPLAY" ffmpeg -f x11grab -framerate 30 -video_size "$dims" -i "$DISPLAY" \
        -c:v libx264 -preset ultrafast -crf 23 "$output" &
    echo $! > "$pid_file"
    log "Recording started (PID: $!)"
    echo "RECORDING_PID=$!"
    echo "RECORDING_FILE=$output"
}

# Input commands (all need xdotool)
cmd_type() { load_session || true; check_xdotool || return 1; [ -z "$1" ] && { error "type requires text"; return 1; }; log "Typing: $1"; DISPLAY="$DISPLAY" xdotool type -- "$1"; }
cmd_key() { load_session || true; check_xdotool || return 1; [ -z "$1" ] && { error "key requires keysym"; return 1; }; log "Sending key: $1"; DISPLAY="$DISPLAY" xdotool key "$1"; }
cmd_click() { load_session || true; check_xdotool || return 1; log "Clicking button ${1:-1}"; DISPLAY="$DISPLAY" xdotool click "${1:-1}"; }
cmd_clickat() {
    load_session || true; check_xdotool || return 1
    [ -z "$1" ] || [ -z "$2" ] && { error "clickat requires x y"; return 1; }
    log "Clicking at ($1,$2)"; DISPLAY="$DISPLAY" xdotool mousemove "$1" "$2" click "${3:-1}"
}
cmd_move() {
    load_session || true; check_xdotool || return 1
    [ -z "$1" ] || [ -z "$2" ] && { error "move requires x y"; return 1; }
    log "Moving to ($1,$2)"; DISPLAY="$DISPLAY" xdotool mousemove "$1" "$2"
}
cmd_drag() {
    load_session || true; check_xdotool || return 1
    [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ] || [ -z "$4" ] && { error "drag requires x1 y1 x2 y2"; return 1; }
    log "Dragging ($1,$2) -> ($3,$4)"
    DISPLAY="$DISPLAY" xdotool mousemove "$1" "$2"
    DISPLAY="$DISPLAY" xdotool mousedown "${5:-1}"
    usleep 100000
    DISPLAY="$DISPLAY" xdotool mousemove "$3" "$4"
    DISPLAY="$DISPLAY" xdotool mouseup "${5:-1}"
}

# Window commands
cmd_search() {
    load_session || true; check_xdotool || return 1
    [ -z "$1" ] && { error "search requires pattern"; return 1; }
    log "Searching for: $1"
    local results=$(DISPLAY="$DISPLAY" xdotool search --name "$1" 2>/dev/null)
    [ -z "$results" ] && { log "No windows found"; return 0; }
    while IFS= read -r winid; do
        local name=$(DISPLAY="$DISPLAY" xdotool getwindowname "$winid" 2>/dev/null || echo "Untitled")
        echo "WINDOW=$winid NAME=\"$name\""
    done <<< "$results"
}
cmd_activate() {
    load_session || true; check_xdotool || return 1
    [ -z "$1" ] && { error "activate requires window_id"; return 1; }
    log "Activating window $1"; DISPLAY="$DISPLAY" xdotool windowactivate --sync "$1"
}
cmd_list() {
    load_session || true; check_xdotool || return 1
    log "Windows on $DISPLAY:"
    local windows=$(DISPLAY="$DISPLAY" xdotool search --all "" 2>/dev/null)
    [ -n "$windows" ] && while IFS= read -r winid; do
        local name=$(DISPLAY="$DISPLAY" xdotool getwindowname "$winid" 2>/dev/null || echo "Untitled")
        echo "WINDOW=$winid NAME=\"$name\""
    done <<< "$windows" || log "No windows found"
}

# Command: info
cmd_info() {
    load_session || true
    echo "=== Xvfb Session Info ==="
    echo "DISPLAY=$DISPLAY"
    xdpyinfo -display "$DISPLAY" 2>/dev/null | grep "dimensions:" | head -1 | sed 's/^/SCREEN: /'
    echo "POINTER: $(xdotool getmouselocation 2>/dev/null)"
    local active=$(xdotool getactivewindow 2>/dev/null)
    [ -n "$active" ] && echo "ACTIVE_WINDOW=$active NAME=\"$(xdotool getwindowname "$active" 2>/dev/null)\""
    echo "WINDOW_COUNT=$(xdotool search --all "" 2>/dev/null | wc -l)"
    echo "========================="
}

# Command: stop
cmd_stop() {
    local pid_file=$(get_pid_file)
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        kill "$pid" 2>/dev/null || true
        rm -f "$pid_file"
    fi
    rm -f "$(get_display_file)" "$(get_auth_file)" "$(get_auth_path_file)" 2>/dev/null || true
    log "Cleanup complete"
}

# Help
show_help() { cat <<'EOF'
Xvfb GUI Loop - Claude Code Skill

Usage: xvfb-gui-loop.sh <command> [args...]

Commands:
  start [resolution] <cmd>   Start Xvfb and run command
  attach <display>           Attach to existing display
  use [display]              Use saved/specified display
  stop                       Stop Xvfb and cleanup
  capture [file.png]         Take screenshot
  record [file.mp4]          Start/stop video recording
  type <text>                Type text
  key <keysym>               Press key (e.g., Return, ctrl+s)
  click [button]             Click mouse (1=left, 2=mid, 3=right)
  clickat <x> <y> [button]   Click at position
  move <x> <y>               Move mouse
  search <pattern>           Find window by name
  activate <window_id>       Focus window
  list                       List all windows
  info                       Show session info

Examples:
  xvfb-gui-loop.sh start firefox
  xvfb-gui-loop.sh start 1280x720x24 gedit
  xvfb-gui-loop.sh capture screen.png
  xvfb-gui-loop.sh type "Hello"
  xvfb-gui-loop.sh key ctrl+s
EOF
}

# Main
[ $# -eq 0 ] && { show_help; exit 0; }
cmd="$1"; shift || true
case "$cmd" in
    start) cmd_start "$@" ;; attach) cmd_attach "$@" ;; use) cmd_use "$@" ;;
    stop) cmd_stop ;; capture) cmd_capture "$@" ;; record) cmd_record "$@" ;;
    type) cmd_type "$@" ;; key) cmd_key "$@" ;; click) cmd_click "$@" ;;
    clickat) cmd_clickat "$@" ;; move) cmd_move "$@" ;; drag) cmd_drag "$@" ;;
    search) cmd_search "$@" ;; activate) cmd_activate "$@" ;; list) cmd_list ;;
    info) cmd_info ;; help|--help|-h) show_help ;;
    *) error "Unknown command: $cmd"; show_help; exit 1 ;;
esac
