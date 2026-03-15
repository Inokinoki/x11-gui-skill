# Xvfb Low-Level Access: Executive Summary

This document summarizes the key mechanisms for direct framebuffer capture and input simulation in Xvfb.

## Quick Reference

| Goal | Method | Speed | Complexity |
|------|--------|-------|------------|
| Capture framebuffer | MIT-SHM (XShmGetImage) | Fast | Medium |
| Capture framebuffer | XGetImage | Slow | Low |
| Inject keyboard | XTestFakeKeyEvent | N/A | Low |
| Inject mouse | XTestFakeMotionEvent/ButtonEvent | N/A | Low |
| Query windows | XQueryTree + XGetWindowProperty | Fast | Low |
| Direct protocol | XCB | Fastest | High |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      Xvfb Architecture                       │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌─────────────────────────────────────────────────────┐    │
│  │                  X Server (Xvfb)                     │    │
│  │                                                       │    │
│  │  ┌───────────────┐  ┌───────────────┐               │    │
│  │  │  PixmapStore  │  │  Window Tree  │               │    │
│  │  │  (per-window  │  │  (metadata,   │               │    │
│  │  │   pixmaps)    │  │   properties) │               │    │
│  │  └───────────────┘  └───────────────┘               │    │
│  │                                                       │    │
│  │  ┌───────────────┐  ┌───────────────┐               │    │
│  │  │  Input Queue  │  │  SHM Segments │               │    │
│  │  │  (XTest +     │  │  (client-     │               │    │
│  │  │   hardware)   │  │   shared)     │               │    │
│  │  └───────────────┘  └───────────────┘               │    │
│  │                                                       │    │
│  │  ┌─────────────────────────────────────────┐         │    │
│  │  │        Extension Handlers                │         │    │
│  │  │  - XTest (input injection)              │         │    │
│  │  │  - MIT-SHM (shared memory)              │         │    │
│  │  │  - XFixes (cursor, damage)              │         │    │
│  │  │  - XRandR (resolution)                  │         │    │
│  │  └─────────────────────────────────────────┘         │    │
│  └─────────────────────────────────────────────────────┘    │
│                         │                                    │
│         ════════════════╪════════════════════                │
│         X11 Protocol    │                                    │
│         (TCP/Unix sock) │                                    │
│                         │                                    │
│  ┌──────────────────────┴───────────────────────────────┐   │
│  │                  Client Applications                  │   │
│  │                                                       │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │   │
│  │  │   Xlib      │  │    XCB      │  │   XTest     │  │   │
│  │  │  (high-lvl) │  │ (low-level) │  │  (input)    │  │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  │   │
│  └─────────────────────────────────────────────────────┘    │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

## Key Insights

### 1. There Is No Direct Framebuffer Device

Xvfb does **not** create `/dev/fb0` or similar. The framebuffer exists only in the X server's memory space. Access methods:

- **XGetImage**: Protocol-based, copies pixels over X11 connection (slow)
- **MIT-SHM**: Shared memory segment mapped by both client and server (fast)
- **Server module**: Kernel-level access (requires X server modification)

### 2. MIT-SHM Is The Practical Fast Path

```c
// Create shared memory
int shmid = shmget(IPC_PRIVATE, width * height * 4, IPC_CREAT | 0777);
void *shmaddr = shmat(shmid, NULL, 0);

// Tell X server to use this segment
XShmAttach(display, &shminfo);

// Capture - server writes directly to shared memory
XShmGetImage(display, window, ximage, 0, 0, AllPlanes);

// Read directly from mapped memory
uint32_t *pixels = (uint32_t *)shmaddr;
```

**Why it's fast:**
- No protocol data transfer for pixel data
- Server writes directly to client-mapped memory
- Zero-copy after initial setup

### 3. XTest Injects At Server Input Queue

XTestFakeKeyEvent doesn't send events to a specific window. It injects into the server's global input queue:

```
Client → XTestFakeKeyEvent → Server Input Queue → Normal Processing → Target Window
```

This means:
- Events go through grabs, keyboard shortcuts, etc.
- Target is determined by current focus (not specified by client)
- Applications cannot easily filter synthetic events
- Server's keyboard/pointer state is updated

### 4. XSendEvent vs XTestFakeKeyEvent

| Aspect | XSendEvent | XTestFakeKeyEvent |
|--------|------------|-------------------|
| Target | Specific window | Global input queue |
| Processing | Bypasses normal flow | Full normal processing |
| Grabs | Not triggered | Triggered |
| State update | No | Yes |
| Filterable | Yes (applications can block) | No |

**Always use XTest for input simulation.**

### 5. Window Hierarchy Is A Tree

```
Root Window (screen)
├── Application Window A
│   ├── Titlebar (WM decorator)
│   ├── Client area
│   │   ├── Widget 1
│   │   └── Widget 2
│   └── Popup
└── Application Window B
    └── Dialog
```

Query with `XQueryTree` recursively. Find windows by:
- WM_NAME (legacy ASCII title)
- _NET_WM_NAME (UTF-8 title)
- WM_CLASS (application class)
- _NET_WM_PID (process ID)

## Memory Layout Details

### TrueColor Pixel Format (32-bit)

Little-endian (x86):
```
Byte 0: Blue (bits 0-7)
Byte 1: Green (bits 8-15)
Byte 2: Red (bits 16-23)
Byte 3: Alpha/padding (bits 24-31)
```

Extract RGB:
```c
uint32_t pixel = buffer[y * stride + x];
uint8_t b = (pixel >> 0) & 0xFF;
uint8_t g = (pixel >> 8) & 0xFF;
uint8_t r = (pixel >> 16) & 0xFF;
```

Or with masks (portable):
```c
uint8_t r = (pixel & 0x00FF0000) >> 16;
uint8_t g = (pixel & 0x0000FF00) >> 8;
uint8_t b = (pixel & 0x000000FF) >> 0;
```

### Row Stride/Padding

Rows may have padding for alignment:
```
stride = width * bytes_per_pixel  // Often rounded to 4-byte boundary
```

Always use `image->bytes_per_line` from XImage, not calculated values.

## Common Pitfalls

### 1. Not Flushing After XTest Events

```c
XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);
// Missing: XFlush(dpy);  ← Events stay in buffer!
```

### 2. Using Relative Coordinates Wrong

```c
// Wrong: Using wrong screen parameter
XTestFakeMotionEvent(dpy, 0, x, y, CurrentTime);  // Screen 0

// Right: Use -1 for current screen or actual screen number
XTestFakeMotionEvent(dpy, screen_num, x, y, CurrentTime);
```

### 3. Not Handling Different Depths

```c
// Wrong: Assuming 32-bit pixels
uint32_t *pixels = (uint32_t *)image->data;

// Right: Check depth first
if (image->depth == 24 && image->bits_per_pixel == 32) {
    // 32-bit handling
} else if (image->depth == 16) {
    // 16-bit handling (RGB565 etc.)
}
```

### 4. Memory Leaks With XGetWindowProperty

```c
// Wrong: Not freeing property data
XGetWindowProperty(..., &prop);
// Use prop...
// Missing: XFree(prop);

// Right:
XGetWindowProperty(..., &prop);
// Use prop...
XFree(prop);
```

### 5. Not Checking SHM Availability

```c
// Always check before using MIT-SHM:
if (!XShmQueryExtension(display)) {
    fprintf(stderr, "MIT-SHM not available\n");
    // Fall back to XGetImage
}
```

## Performance Comparison

| Method | 1920x1080 Capture | Notes |
|--------|-------------------|-------|
| XGetImage | ~50-100ms | Protocol overhead |
| XShmGetImage | ~5-10ms | Shared memory |
| XShmGetImage + batching | ~2-5ms | Multiple frames |

## Security Considerations

1. **X11 has no authentication by default** - Any local user can:
   - Read your screen
   - Inject input
   - Log keystrokes

2. **Use XAUTHORITY** - Set `XAUTHORITY` environment variable:
   ```bash
   xauth add :99 . $(mcookie)
   ```

3. **XTest can be disabled** - Some systems disable XTest:
   ```bash
   xorg.conf: Option "Xtest" "off"
   ```

## Example: Complete Capture + Input Loop

```c
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/XTest.h>
#include <sys/shm.h>

int main() {
    Display *dpy = XOpenDisplay(":99");

    // Check extensions
    if (!XShmQueryExtension(dpy)) { /* handle */ }
    if (!XTestQueryExtension(dpy, NULL, NULL)) { /* handle */ }

    // Setup capture (see xvfb_lowlevel_capture.c)
    // ...

    // Main loop
    while (running) {
        // 1. Capture screen
        XShmGetImage(dpy, root, image, 0, 0, AllPlanes);

        // 2. Process image (find target, etc.)
        // ...

        // 3. Inject input based on analysis
        XTestFakeMotionEvent(dpy, screen, target_x, target_y, CurrentTime);
        XTestFakeButtonEvent(dpy, 1, True, CurrentTime);
        XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
        XFlush(dpy);

        usleep(16000);  // ~60 FPS
    }

    XCloseDisplay(dpy);
    return 0;
}
```

## Files In This Repository

| File | Description |
|------|-------------|
| `examples/c/xvfb_lowlevel_capture.c` | MIT-SHM framebuffer capture (Xlib) |
| `examples/c/xvfb_lowlevel_input.c` | XTest input simulation |
| `examples/c/xvfb_xcb_capture.c` | MIT-SHM capture (XCB) |
| `docs/xvfb-internals.md` | Detailed internals documentation |
| `docs/x11-protocol-reference.md` | Protocol-level reference |
| `examples/c/Makefile` | Build configuration |

## Further Reading

1. **X Protocol Specification**: https://www.x.org/releases/X11R7.6/doc/xproto/x11protocol.html
2. **Xlib Programming Manual**: https://tronche.com/gui/x/xlib/
3. **XCB Documentation**: https://xcb.freedesktop.org/
4. **XTest Extension**: https://www.x.org/releases/current/doc/extproto/XTestExt.txt
5. **MIT-SHM Extension**: https://www.x.org/releases/current/doc/extproto/shm.txt
