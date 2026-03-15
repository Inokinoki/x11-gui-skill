#!/usr/bin/env bash
#
# Example: Xvfb-in-the-Loop workflow for Claude Code
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
XVFB_LOOP="${SCRIPT_DIR}/../xvfb-gui-loop/xvfb-gui-loop.sh"
CAPTURE_DIR="${CAPTURE_DIR:-/tmp/xvfb-captures}"

mkdir -p "$CAPTURE_DIR"

echo "=============================================="
echo "  Xvfb-in-the-Loop: Demo Workflow"
echo "=============================================="

# Step 1: Start Xvfb with application
echo "[Step 1] Starting Xvfb with application..."
"$XVFB_LOOP" start 1920x1080x24 xcalc &
sleep 3
echo ""

# Step 2: Capture initial state
echo "[Step 2] Capturing initial state..."
INITIAL_CAPTURE="$CAPTURE_DIR/initial_$(date +%Y%m%d_%H%M%S).png"
"$XVFB_LOOP" capture "$INITIAL_CAPTURE"
echo "Captured: $INITIAL_CAPTURE"
echo ""

# Step 3: Show session info
echo "[Step 3] Session info:"
"$XVFB_LOOP" info
echo ""

# Step 4: List windows
echo "[Step 4] Windows:"
"$XVFB_LOOP" list
echo ""

# Step 5: Interact (approximate calculator positions)
echo "[Step 5] Clicking calculator buttons..."
"$XVFB_LOOP" clickat 200 300 1
sleep 0.5
"$XVFB_LOOP" clickat 300 300 1
sleep 0.5
"$XVFB_LOOP" clickat 400 400 1
echo ""

# Step 6: Capture after interaction
echo "[Step 6] Capturing after interaction..."
AFTER_CAPTURE="$CAPTURE_DIR/after_$(date +%Y%m%d_%H%M%S).png"
sleep 1
"$XVFB_LOOP" capture "$AFTER_CAPTURE"
echo "Captured: $AFTER_CAPTURE"
echo ""

echo "=============================================="
echo "  Demo complete!"
echo "  Files:"
echo "    - $INITIAL_CAPTURE"
echo "    - $AFTER_CAPTURE"
echo "=============================================="
