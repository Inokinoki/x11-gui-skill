# Makefile for Xvfb Low-Level Examples
#
# These examples demonstrate direct access to Xvfb framebuffer
# and input simulation using X11 extensions.
#
# Requirements:
#   - libx11-dev (X11, Xlib)
#   - libxext-dev (X extensions)
#   - libxtst-dev (XTest extension)
#   - libxcb-dev (XCB)
#   - libxcb-shm0-dev (XCB SHM extension)
#
# On Debian/Ubuntu:
#   sudo apt install libx11-dev libxext-dev libxtst-dev libxcb-dev libxcb-shm0-dev
#
# On Fedora/RHEL:
#   sudo dnf install libX11-devel libXext-devel libXtst-devel libxcb-devel
#

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

# Xlib/XTest examples
XLIB_LIBS = -lX11 -lXext
XTEST_LIBS = $(XLIB_LIBS) -lXtst

# XCB examples
XCB_LIBS = -lxcb -lxcb-shm

TARGETS = \
	xvfb_lowlevel_capture \
	xvfb_lowlevel_input \
	xvfb_xcb_capture

.PHONY: all clean test

all: $(TARGETS)

xvfb_lowlevel_capture: xvfb_lowlevel_capture.c
	$(CC) $(CFLAGS) -o $@ $< $(XLIB_LIBS)

xvfb_lowlevel_input: xvfb_lowlevel_input.c
	$(CC) $(CFLAGS) -o $@ $< $(XTEST_LIBS)

xvfb_xcb_capture: xvfb_xcb_capture.c
	$(CC) $(CFLAGS) -o $@ $< $(XCB_LIBS)

clean:
	rm -f $(TARGETS) *.o /tmp/framebuffer.ppm /tmp/capture.ppm /tmp/xcb_capture.ppm

# Test target - runs a simple capture test
# Requires Xvfb running on :99
test: all
	@echo "Starting Xvfb on :99..."
	Xvfb :99 -screen 0 1024x768x24 &
	XVFB_PID=$$!
	sleep 1

	@echo "\n=== Testing framebuffer capture ==="
	DISPLAY=:99 ./xvfb_lowlevel_capture /tmp/test_capture.ppm

	@echo "\n=== Testing XCB capture ==="
	DISPLAY=:99 ./xvfb_xcb_capture /tmp/test_xcb_capture.ppm

	@echo "\n=== Testing input simulation ==="
	DISPLAY=:99 ./xvfb_lowlevel_input info

	@echo "\n=== Stopping Xvfb ==="
	kill $$XVFB_PID 2>/dev/null || true

	@echo "\nTest complete. Check /tmp/test_*.ppm for output."

# Help target
help:
	@echo "Xvfb Low-Level Examples"
	@echo "======================="
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build all examples (default)"
	@echo "  clean   - Remove built files"
	@echo "  test    - Run tests with Xvfb (requires Xvfb installed)"
	@echo "  help    - Show this help"
	@echo ""
	@echo "Examples:"
	@echo "  xvfb_lowlevel_capture - MIT-SHM framebuffer capture (Xlib)"
	@echo "  xvfb_lowlevel_input   - XTest input simulation"
	@echo "  xvfb_xcb_capture      - MIT-SHM framebuffer capture (XCB)"
	@echo ""
	@echo "Usage:"
	@echo "  DISPLAY=:99 ./xvfb_lowlevel_capture [output.ppm]"
	@echo "  DISPLAY=:99 ./xvfb_lowlevel_input <command> [args]"
	@echo "  DISPLAY=:99 ./xvfb_xcb_capture [output.ppm]"
