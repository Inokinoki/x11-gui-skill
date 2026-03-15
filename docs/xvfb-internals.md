# Xvfb Internals: Low-Level Technical Reference

## 1. Framebuffer Memory Layout

### 1.1 Default Xvfb Memory Behavior (Without Shadow)

By default, Xvfb uses a **Pixmap-based storage** model:

```c
// Xvfb internal structure (simplified from hw/xfree86/fbdev/)
typedef struct {
    PixmapPtr *pPixmap;      // Array of pixmap pointers
    int        depth;        // Color depth (usually 24)
    int        bitsPerPixel; // Bytes per pixel aligned (usually 32)

    // Each pixmap contains:
    // - devPrivate.ptr: pointer to actual pixel data
    // - drawable.width, drawable.height: dimensions
    // - devKind: row stride in bytes
} FbDevScreenPrivRec;
```

**Key insight**: Without Shadow extension, each window has its own Pixmap. There's no single "framebuffer" - the screen is composited on-demand when GetImage is called.

### 1.2 Shadow Framebuffer Layout

When Shadow extension is enabled (`-shadow` option or via Xfixes), Xvfb maintains a contiguous framebuffer:

```
┌────────────────────────────────────────────────────────────┐
│                  Shadow Framebuffer Memory                  │
├────────────────────────────────────────────────────────────┤
│                                                             │
│  Base Address (pShadow->pShadowArea->ptr)                  │
│  │                                                          │
│  ▼                                                          │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Row 0: [P0][P1][P2][P3]...[P(width-1)]               │  │
│  │      ↑                                               │  │
│  │      stride bytes (pShadow->pShadowArea->devKind)   │  │
│  │                                                      │  │
│  ├──────────────────────────────────────────────────────┤  │
│  │ Row 1: [P0][P1][P2][P3]...[P(width-1)]               │  │
│  │      ↑                                               │  │
│  │      stride bytes                                    │  │
│  │                                                      │  │
│  ├──────────────────────────────────────────────────────┤  │
│  │ ...                                                  │  │
│  │                                                      │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  Total size = stride × height bytes                        │
└────────────────────────────────────────────────────────────┘

Pixel format (depth=24, bitsPerPixel=32):
┌────────┬────────┬────────┬────────┐
│  Alpha |  Red   | Green  | Blue   |  (Big endian)
│  0x00  |  0xRR  | 0xGG   | 0xBB   │
└────────┴────────┴────────┴────────┘

Or (Little endian - most common on x86):
┌────────┬────────┬────────┬────────┐
│  Blue  | Green  |  Red   |  Alpha |
│  0xBB  | 0xGG   | 0xRR   | 0x00   │
└────────┴────────┴────────┴────────┘
```

### 1.3 Accessing the Shadow Framebuffer

The Shadow extension provides a callback mechanism:

```c
// From X server source: Xext/shadow.c
typedef struct _shadowScrPriv {
    ShadowUpdateProc update;  // Called when screen changes
    void *callbackData;
    PixmapPtr pPixmap;        // The shadow pixmap
} shadowScrPrivRec;

// To access the raw buffer:
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

Display *dpy = XOpenDisplay(":99");
Window root = DefaultRootWindow(dpy);

// Get the root window's pixmap via XFixes
// Note: This requires the X server to support Xfixes extension
XFixesGetCursorImage(dpy);  // Example Xfixes call

// For actual shadow access, you need to use the server-side API
// This is typically done in an X server module, not a client
```

**Critical limitation**: Client applications cannot directly mmap Xvfb's memory. The framebuffer exists in the X server's address space. You have three options:

1. **XGetImage** - X11 protocol call (slow, involves copying)
2. **MIT-SHM (XShm)** - Shared memory extension (faster, zero-copy after setup)
3. **X server module** - Kernel/driver level access (requires server modification)

## 2. MIT-SHM: The Fast Capture Path

MIT Shared Memory Extension (MIT-SHM / XShm) is the primary method for efficient framebuffer access:

```c
#define _XOPEN_SOURCE 500
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    Display *dpy;
    Window root;
    XShmSegmentInfo shminfo;
    XImage *ximage;
    int width, height;
} XvfbCapture;

int xshmb_capture_init(XvfbCapture *cap, const char *display) {
    cap->dpy = XOpenDisplay(display);
    if (!cap->dpy) return -1;

    // Check for MIT-SHM extension
    if (!XShmQueryExtension(cap->dpy)) {
        fprintf(stderr, "MIT-SHM not available\n");
        XCloseDisplay(cap->dpy);
        return -1;
    }

    // Get root window info
    cap->root = DefaultRootWindow(cap->dpy);
    XWindowAttributes attrs;
    XGetWindowAttributes(cap->dpy, cap->root, &attrs);
    cap->width = attrs.width;
    cap->height = attrs.height;

    // Create shared memory segment
    cap->shminfo.shmid = shmget(IPC_PRIVATE,
        cap->width * cap->height * 4,  // 32 bits per pixel
        IPC_CREAT | 0777);

    if (cap->shminfo.shmid == -1) {
        perror("shmget");
        XCloseDisplay(cap->dpy);
        return -1;
    }

    // Attach shared memory to our address space
    cap->shminfo.shmaddr = cap->ximage->data = shmat(cap->shminfo.shmid, NULL, 0);
    if (cap->shminfo.shmaddr == (char *)-1) {
        perror("shmat");
        shmctl(cap->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(cap->dpy);
        return -1;
    }

    cap->shminfo.readOnly = False;

    // Create XImage backed by shared memory
    cap->ximage = XShmCreateImage(cap->dpy,
        DefaultVisual(cap->dpy, DefaultScreen(cap->dpy)),
        DefaultDepth(cap->dpy, DefaultScreen(cap->dpy)),
        cap->width, cap->height,
        &cap->shminfo, ZPixmap, 0);

    if (!cap->ximage) {
        fprintf(stderr, "XShmCreateImage failed\n");
        shmdt(cap->shminfo.shmaddr);
        shmctl(cap->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(cap->dpy);
        return -1;
    }

    // Tell X server to attach to the same shared memory
    if (!XShmAttach(cap->dpy, &cap->shminfo)) {
        fprintf(stderr, "XShmAttach failed\n");
        XDestroyImage(cap->ximage);
        shmdt(cap->shminfo.shmaddr);
        shmctl(cap->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(cap->dpy);
        return -1;
    }

    return 0;
}

// Capture framebuffer - this triggers the X server to copy pixels
// into our shared memory segment
int xshmb_capture_frame(XvfbCapture *cap, void *output_buffer) {
    // XShmGetImage is the key call - it's synchronous and copies
    // the framebuffer into the shared memory segment
    XShmGetImage(cap->dpy, cap->root, cap->ximage, 0, 0, AllPlanes);

    // Copy from shared memory to output buffer
    memcpy(output_buffer, cap->ximage->data,
           cap->width * cap->height * 4);

    return 0;
}

void xshmb_capture_cleanup(XvfbCapture *cap) {
    XShmDetach(cap->dpy, &cap->shminfo);
    XDestroyImage(cap->ximage);
    shmdt(cap->shminfo.shmaddr);
    shmctl(cap->shminfo.shmid, IPC_RMID, NULL);
    XCloseDisplay(cap->dpy);
}
```

### 2.1 MIT-SHM Memory Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                      MIT-SHM Architecture                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────┐         ┌─────────────┐                    │
│  │  X Client   │         │   X Server  │                    │
│  │  (Your App) │         │   (Xvfb)    │                    │
│  │             │         │             │                    │
│  │  ┌───────┐  │         │  ┌───────┐  │                    │
│  │  │shmat()│  │         │  │Render │  │                    │
│  │  │       │  │         │  │Engine │  │                    │
│  │  │ data  │◄─┼─────────┼──│       │  │                    │
│  │  │ ptr   │  │  SHM    │  │       │  │                    │
│  │  └───▲───┘  │  memory │  └───▲───┘  │                    │
│  │      │      │  segment│      │      │                    │
│  │      │      │         │      │      │                    │
│  │  ┌───┴────┐ │         │  ┌───┴────┐ │                    │
│  │  │shmid   │ │         │  │pShadow │ │                    │
│  │  │ IPC   │ │         │  │ pixmap │ │                    │
│  │  └────────┘ │         │  └────────┘ │                    │
│  └─────────────┘         └─────────────┘                    │
│                                                              │
│  Data flow during XShmGetImage:                             │
│  1. Client sends XShmGetImage request                       │
│  2. Server renders current state to shadow pixmap           │
│  3. Server copies pixels → SHM segment (server-side mapping)│
│  4. Client reads directly from shmat() pointer              │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## 3. X11 Window System Under Xvfb

### 3.1 Window Hierarchy and Identification

```
┌─────────────────────────────────────────────────────────────┐
│                    X11 Window Hierarchy                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Root Window (ID: 0xXXX)                                    │
│  │   - Covers entire screen                                 │
│  │   - Created at server start                              │
│  │   - Parent of all top-level windows                      │
│  │                                                          │
│  ├─── Window A (ID: 0xYYY)                                  │
│  │    │   - _NET_WM_NAME: "Application A"                   │
│  │    │   - WM_CLASS: "app-a", "AppA"                       │
│  │    │   - _NET_WM_PID: 12345                              │
│  │    │                                                      │
│  │    └─── Child A.1 (ID: 0xZZZ)                            │
│  │         │   - Widget/Subview                             │
│  │         └─── Child A.2                                   │
│  │                                                          │
│  └─── Window B (ID: 0xAAA)                                  │
│       │   - _NET_WM_NAME: "Application B"                   │
│       │   - Overrides redirect: true (popup)                │
│       │                                                      │
│       └─── Child B.1                                        │
│                                                              │
└─────────────────────────────────────────────────────────────┘

Window ID: 32-bit XID (X Identifier)
- Bits 0-7:   Index into server's client resource table
- Bits 8-31:  Client ID (which connection created it)
```

### 3.2 Querying Window Information - Low Level

```c
#define _XOPEN_SOURCE 500
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Window tree traversal
typedef struct {
    Display *dpy;
    Window *windows;
    int count;
    int capacity;
} WindowList;

int get_all_windows(Display *dpy, WindowList *list, Window parent) {
    Window root, parent_win;
    Window *children;
    unsigned int nchildren;

    if (!XQueryTree(dpy, parent, &root, &parent_win, &children, &nchildren)) {
        return -1;
    }

    for (unsigned int i = 0; i < nchildren; i++) {
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            list->windows = realloc(list->windows,
                                   list->capacity * sizeof(Window));
        }
        list->windows[list->count++] = children[i];

        // Recurse into children
        get_all_windows(dpy, list, children[i]);
    }

    XFree(children);
    return 0;
}

// Get window properties
typedef struct {
    Window id;
    int x, y, width, height, border_width, depth;
    char *wm_name;
    char *wm_class;
    pid_t pid;
    Window parent;
} WindowInfo;

int get_window_info(Display *dpy, Window win, WindowInfo *info) {
    memset(info, 0, sizeof(WindowInfo));
    info->id = win;

    // Get geometry
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(dpy, win, &attrs)) {
        return -1;
    }

    info->x = attrs.x;
    info->y = attrs.y;
    info->width = attrs.width;
    info->height = attrs.height;
    info->border_width = attrs.border_width;
    info->depth = attrs.depth;

    // Get parent
    Window root, parent;
    Window *children;
    unsigned int nchildren;
    if (XQueryTree(dpy, win, &root, &parent, &children, &nchildren)) {
        info->parent = parent;
        XFree(children);
    }

    // Get _NET_WM_NAME (UTF-8 window title)
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", True);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    if (net_wm_name != None &&
        XGetWindowProperty(dpy, win, net_wm_name, 0, 1024, False,
                          utf8_string, &actual_type, &actual_format,
                          &nitems, &bytes_after, &prop) == Success &&
        prop != NULL) {
        info->wm_name = strdup((char *)prop);
        XFree(prop);
    }

    // Get WM_CLASS
    if (XGetWindowProperty(dpy, win, XA_WM_CLASS, 0, 1024, False,
                          XA_STRING, &actual_type, &actual_format,
                          &nitems, &bytes_after, &prop) == Success &&
        prop != NULL) {
        info->wm_class = strdup((char *)prop);
        XFree(prop);
    }

    // Get _NET_WM_PID
    Atom net_wm_pid = XInternAtom(dpy, "_NET_WM_PID", True);
    if (net_wm_pid != None &&
        XGetWindowProperty(dpy, win, net_wm_pid, 0, 1, False,
                          XA_CARDINAL, &actual_type, &actual_format,
                          &nitems, &bytes_after, &prop) == Success &&
        prop != NULL) {
        info->pid = *(pid_t *)prop;
        XFree(prop);
    }

    return 0;
}

// Find window by name pattern
Window find_window_by_name(Display *dpy, const char *pattern) {
    WindowList list = { .count = 0, .capacity = 100 };
    list.windows = malloc(list.capacity * sizeof(Window));
    list.dpy = dpy;

    get_all_windows(dpy, &list, DefaultRootWindow(dpy));

    Window found = None;
    for (int i = 0; i < list.count; i++) {
        WindowInfo info;
        if (get_window_info(dpy, list.windows[i], &info) == 0) {
            if (info.wm_name && strstr(info.wm_name, pattern)) {
                found = list.windows[i];
                free(info.wm_name);
                free(info.wm_class);
                break;
            }
            free(info.wm_name);
            free(info.wm_class);
        }
    }

    free(list.windows);
    return found;
}
```

## 4. XTest Extension: Event Injection Internals

### 4.1 How XTest Works at the Protocol Level

The XTEST extension (part of X Extensions) allows synthetic event generation. Here's what happens under the hood:

```
┌─────────────────────────────────────────────────────────────┐
│                  XTest Event Injection Flow                  │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Client App                    X Server (Xvfb)              │
│  ┌────────────┐                                             │
│  │ XTTest     │  XTestFakeKeyEvent request                  │
│  │ FakeKeyPress│ ──────────────────────────────────►        │
│  │ (libXtst)  │                                             │
│  └────────────┘         ┌──────────────────┐                │
│                         │  XTest Extension │                │
│                         │  Handler         │                │
│                         │                  │                │
│                         │  1. Validate     │                │
│                         │     keycode      │                │
│                         │  2. Create       │                │
│                         │     CoreProtocol  │                │
│                         │     KeyEvent    ◄────────┐        │
│                         │  3. Inject into  │        │        │
│                         │     input queue   │        │        │
│                         └────────┬─────────┘        │        │
│                                  │                  │        │
│                                  ▼                  │        │
│                         ┌──────────────────┐        │        │
│                         │  Input Thread    │        │        │
│                         │  - Processes Q   │        │        │
│                         │  - Determines    │        │        │
│                         │    target window │        │        │
│                         └────────┬─────────┘        │        │
│                                  │                  │        │
│                                  ▼                  │        │
│                         ┌──────────────────┐        │        │
│                         │  Delivery        │        │        │
│                         │  - Apply grabs   │        │        │
│                         │  - Check masks   │        │        │
│                         │  - Queue to      │        │        │
│                         │    client event Q│        │        │
│                         └──────────────────┘        │        │
│                                                      │        │
│  ◄───────────────────────────────────────────────────┘        │
│  Event reaches target application's event queue              │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 XTest vs. XSendEvent: Critical Differences

```c
// Method 1: XSendEvent (NOT recommended for input simulation)
// This sends an event DIRECTLY to a specific window
// - Bypasses normal input processing
// - Doesn't trigger grabs
// - Applications can filter out synthetic events
// - Doesn't update pointer/keyboard state
XSendEvent(dpy, target_window, False, KeyPressMask, &event);

// Method 2: XTestFakeKeyEvent (CORRECT for input simulation)
// This injects an event into the server's input queue
// - Goes through normal input processing
// - Triggers grabs and keyboard shortcuts
// - Updates server's keyboard state
// - Respects input focus
// - Can't be easily filtered by applications
XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);

// Method 3: XTestFakeMotionEvent
// Moves the pointer and generates motion events
XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);

// Method 4: XTestFakeButtonEvent
// Simulates mouse button press/release
XTestFakeButtonEvent(dpy, button, True, CurrentTime);
```

### 4.3 Complete XTest Input Controller

```c
#define _XOPEN_SOURCE 500
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    Display *dpy;
    int screen;
    Window root;
    int xtest_major, xtest_minor;
    // Current pointer position
    int ptr_x, ptr_y;
    // Button state bitmask
    unsigned int button_mask;
} XTestController;

int xtest_controller_init(XTestController *ctrl, const char *display) {
    memset(ctrl, 0, sizeof(XTestController));

    ctrl->dpy = XOpenDisplay(display);
    if (!ctrl->dpy) {
        fprintf(stderr, "Cannot open display: %s\n", display);
        return -1;
    }

    ctrl->screen = DefaultScreen(ctrl->dpy);
    ctrl->root = RootWindow(ctrl->dpy, ctrl->screen);

    // Query XTest extension version
    if (!XTestQueryExtension(ctrl->dpy, &ctrl->xtest_major,
                             &ctrl->xtest_minor)) {
        fprintf(stderr, "XTest extension not available\n");
        XCloseDisplay(ctrl->dpy);
        return -1;
    }

    // Get initial pointer position
    Window child;
    XQueryPointer(ctrl->dpy, ctrl->root, &ctrl->root, &child,
                  &ctrl->ptr_x, &ctrl->ptr_y,
                  &ctrl->ptr_x, &ctrl->ptr_y,
                  &ctrl->button_mask);

    return 0;
}

// Get keycode from keysym
KeyCode keysym_to_keycode(Display *dpy, KeySym keysym) {
    return XKeysymToKeycode(dpy, keysym);
}

// Get keysym from string name (e.g., "Return", "Control_L")
KeySym keyname_to_keysym(const char *name) {
    return XStringToKeysym(name);
}

// Press a key by keysym
int xtest_key_press(XTestController *ctrl, KeySym keysym) {
    KeyCode keycode = keysym_to_keycode(ctrl->dpy, keysym);
    if (keycode == 0) {
        fprintf(stderr, "Unknown keysym: %ld\n", keysym);
        return -1;
    }

    XTestFakeKeyEvent(ctrl->dpy, keycode, True, CurrentTime);
    XFlush(ctrl->dpy);
    return 0;
}

// Release a key by keysym
int xtest_key_release(XTestController *ctrl, KeySym keysym) {
    KeyCode keycode = keysym_to_keycode(ctrl->dpy, keysym);
    if (keycode == 0) return -1;

    XTestFakeKeyEvent(ctrl->dpy, keycode, False, CurrentTime);
    XFlush(ctrl->dpy);
    return 0;
}

// Type a full string
int xtest_type_string(XTestController *ctrl, const char *str) {
    for (const char *p = str; *p; p++) {
        KeySym keysym = XStringToKeysym((char[]){*p, '\0'});
        if (keysym == NoSymbol) {
            // Try ASCII directly
            keysym = *p;
        }

        KeyCode keycode = keysym_to_keycode(ctrl->dpy, keysym);
        if (keycode == 0) continue;

        XTestFakeKeyEvent(ctrl->dpy, keycode, True, CurrentTime);
        XTestFakeKeyEvent(ctrl->dpy, keycode, False, CurrentTime);
    }
    XFlush(ctrl->dpy);
    return 0;
}

// Move pointer to absolute position
int xtest_move_to(XTestController *ctrl, int x, int y) {
    ctrl->ptr_x = x;
    ctrl->ptr_y = y;

    // Screen number -1 means "same screen as current"
    XTestFakeMotionEvent(ctrl->dpy, ctrl->screen, x, y, CurrentTime);
    XFlush(ctrl->dpy);
    return 0;
}

// Move pointer relative to current position
int xtest_move_by(XTestController *ctrl, int dx, int dy) {
    return xtest_move_to(ctrl, ctrl->ptr_x + dx, ctrl->ptr_y + dy);
}

// Press mouse button (1=left, 2=middle, 3=right)
int xtest_button_press(XTestController *ctrl, int button) {
    ctrl->button_mask |= (1 << (button - 1));
    XTestFakeButtonEvent(ctrl->dpy, button, True, CurrentTime);
    XFlush(ctrl->dpy);
    return 0;
}

// Release mouse button
int xtest_button_release(XTestController *ctrl, int button) {
    ctrl->button_mask &= ~(1 << (button - 1));
    XTestFakeButtonEvent(ctrl->dpy, button, False, CurrentTime);
    XFlush(ctrl->dpy);
    return 0;
}

// Click (press + release)
int xtest_click(XTestController *ctrl, int button) {
    xtest_button_press(ctrl, button);
    xtest_button_release(ctrl, button);
    return 0;
}

// Double-click
int xtest_double_click(XTestController *ctrl, int button) {
    xtest_click(ctrl, button);
    usleep(150000);  // 150ms between clicks
    xtest_click(ctrl, button);
    return 0;
}

// Click at specific position
int xtest_click_at(XTestController *ctrl, int x, int y, int button) {
    xtest_move_to(ctrl, x, y);
    usleep(10000);  // Small delay for motion to process
    xtest_click(ctrl, button);
    return 0;
}

void xtest_controller_cleanup(XTestController *ctrl) {
    XCloseDisplay(ctrl->dpy);
}

// Example usage
int main() {
    XTestController ctrl;

    if (xtest_controller_init(&ctrl, ":99") != 0) {
        return 1;
    }

    // Get pointer position
    printf("Current pointer: (%d, %d)\n", ctrl.ptr_x, ctrl.ptr_y);

    // Type "Hello World"
    xtest_type_string(&ctrl, "Hello World");

    // Press Enter
    xtest_key_press(&ctrl, XK_Return);
    xtest_key_release(&ctrl, XK_Return);

    // Click at position (100, 100)
    xtest_click_at(&ctrl, 100, 100, 1);

    // Ctrl+C (key chord)
    xtest_key_press(&ctrl, XK_Control_L);
    xtest_key_press(&ctrl, XK_c);
    usleep(50000);
    xtest_key_release(&ctrl, XK_c);
    xtest_key_release(&ctrl, XK_Control_L);

    xtest_controller_cleanup(&ctrl);
    return 0;
}
```

### 4.4 XTest Event Processing Inside X Server

Here's what happens inside the X server when XTestFakeKeyEvent is called:

```c
// Simplified from X server source: Xext/xtest.c

// The XTestFakeKeyEvent handler
static int
ProcXTestFakeKeyEvent(ClientPtr client)
{
    REQUEST(xTestFakeKeyEventReq);

    // 1. Parse request
    xTestFakeKeyEventReq *req = (xTestFakeKeyEventReq *) client->requestBuffer;

    // 2. Validate keycode
    if (stuff->detail > 255) {
        return BadValue;
    }

    // 3. Create a core keyboard event
    EventRec event;
    memset(&event, 0, sizeof(EventRec));

    event.u.u.type = (stuff->keyType == XTest_KeyPress) ?
                     KeyPress : KeyRelease;
    event.u.u.detail = stuff->detail;
    event.u.keyButtonPointer.time = stuff->time;

    // 4. Get the master keyboard device
    DeviceIntPtr keybd = GetMasterKeyboard(inputInfo.pointer);

    // 5. Update the keyboard state
    keybd->key->map[keybd->key->xkbInfo->desc->max_key_code] =
        (stuff->keyType == XTest_KeyPress) ? KeyPressed : KeyReleased;

    // 6. Set the focus window (usually the window with input focus)
    WindowPtr focusWin = inputInfo.focus->win;

    // 7. Deliver the event through normal channels
    // This is the KEY part - it goes through the same path as real input
    if (focusWin != NullWindow) {
        event.u.keyButtonPointer.root = inputInfo.pointer->spriteInfo->sprite->hotSpots[0];
        event.u.keyButtonPointer.event = focusWin;

        // Process any active keyboard grabs
        if (inputInfo.pointer->grab && inputInfo.pointer->grab->keybd == keybd) {
            // Deliver to grab window
            DeliverGrabbedEvent(&event, inputInfo.pointer->grab);
        } else {
            // Normal delivery
            DeliverEventsToWindow(focusWin, &event, 1,
                                 KeyPressMask | KeyReleaseMask,
                                 NULL, NULL);
        }
    }

    // 8. Wake up any clients waiting for events
    WakeupHandlers(&inputInfo);

    return Success;
}
```

## 5. Direct X11 Protocol Methods

### 5.1 Using XCB Instead of Xlib (Lower Level)

XCB (X Protocol C-language Binding) provides closer-to-protocol access:

```c
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xtest.h>
#include <stdio.h>

// XCB-based window query (more efficient than Xlib)
void list_windows_xcb(xcb_connection_t *conn, xcb_window_t window, int indent) {
    xcb_query_tree_cookie_t tree_cookie;
    xcb_query_tree_reply_t *tree;

    tree_cookie = xcb_query_tree(conn, window);
    tree = xcb_query_tree_reply(conn, tree_cookie, NULL);

    if (!tree) return;

    xcb_window_t *children = xcb_query_tree_children(tree);

    for (int i = 0; i < xcb_query_tree_children_length(tree); i++) {
        xcb_window_t child = children[i];

        // Get WM_NAME property
        xcb_get_property_cookie_t prop_cookie;
        xcb_get_property_reply_t *prop;

        prop_cookie = xcb_get_property(conn, 0, child,
                                       XCB_ATOM_WM_NAME,
                                       XCB_ATOM_STRING,
                                       0, 64);
        prop = xcb_get_property_reply(conn, prop_cookie, NULL);

        if (prop && xcb_get_property_value_length(prop) > 0) {
            char *name = xcb_get_property_value(prop);
            printf("%*sWindow 0x%08x: %.*s\n", indent, "", child,
                   xcb_get_property_value_length(prop), name);
        } else {
            printf("%*sWindow 0x%08x (no name)\n", indent, "", child);
        }

        free(prop);

        // Recurse
        list_windows_xcb(conn, child, indent + 2);
    }

    free(tree);
}

// XCB-based screen capture using GetImage
xcb_image_t *capture_screen_xcb(xcb_connection_t *conn, xcb_window_t window) {
    xcb_get_geometry_cookie_t geom_cookie;
    xcb_get_geometry_reply_t *geom;

    geom_cookie = xcb_get_geometry(conn, window);
    geom = xcb_get_geometry_reply(conn, geom_cookie, NULL);

    if (!geom) return NULL;

    // Get the image
    xcb_get_image_cookie_t img_cookie;
    xcb_get_image_reply_t *img;

    img_cookie = xcb_get_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP,
                               window, 0, 0,
                               geom->width, geom->height,
                               0xFFFFFFFF);  // All planes

    img = xcb_get_image_reply(conn, img_cookie, NULL);

    if (!img) {
        free(geom);
        return NULL;
    }

    // Extract raw data
    int length = xcb_get_image_data_length(img);
    uint8_t *data = xcb_get_image_data(img);

    // Note: data is in ZPixmap format, need to convert to RGB
    // based on the depth and visual type

    xcb_image_t *result = malloc(sizeof(xcb_image_t));
    result->data = malloc(length);
    memcpy(result->data, data, length);
    result->width = geom->width;
    result->height = geom->height;
    result->depth = geom->depth;

    free(img);
    free(geom);
    return result;
}
```

### 5.2 X11 Protocol Message Structure

Understanding the actual wire format:

```
┌─────────────────────────────────────────────────────────────┐
│              X11 Protocol: GetImage Request                  │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Byte offset  Field            Value                        │
│  0            opcode           73 (GetImage)                │
│  1            format           0 (Pixmap), 1 (Bitmap)       │
│  2-3          request length   (size in 4-byte units)       │
│  4-7          drawable         Window/Pixmap ID             │
│  8-9          x                X coordinate                 │
│  10-11        y                Y coordinate                 │
│  12-13        width            Width                        │
│  14-15        height           Height                       │
│  16-19        plane-mask       Which bit planes to get      │
│                                                              │
│  Total: 20 bytes minimum                                     │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│              X11 Protocol: GetImage Reply                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Byte offset  Field            Value                        │
│  0            type             1 (Reply)                    │
│  1            visual type      Visual class                 │
│  2-3          sequence number  Matching request             │
│  4-7          length           (data length / 4)            │
│  8-11         size             Data size in bytes           │
│  12-15        visual ID        Visual for this image        │
│  16-17        x                X coordinate                 │
│  18-19        y                Y coordinate                 │
│  20-21        width            Width                        │
│  22-23        height           Height                       │
│  24-25        depth            Color depth                  │
│  26-31        pad              Padding                      │
│  32+          data             Pixel data                   │
│                                                              │
│  Total: 32 bytes header + variable data                      │
└─────────────────────────────────────────────────────────────┘
```

## 6. Complete Example: Low-Level Screen Capture with MIT-SHM

```c
// xvfb_capture.c - Direct framebuffer capture using MIT-SHM
// Compile: gcc -o xvfb_capture xvfb_capture.c -lX11 -lXext

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    Display *display;
    Window window;
    XShmSegmentInfo shminfo;
    XImage *image;
    int width, height, depth;
    int bpp;  // bytes per pixel
    uint32_t rmask, gmask, bmask;
} FramebufferCapture;

int fb_capture_init(FramebufferCapture *fb, const char *display, Window window) {
    memset(fb, 0, sizeof(FramebufferCapture));

    fb->display = XOpenDisplay(display);
    if (!fb->display) {
        fprintf(stderr, "Cannot open display: %s\n", display);
        return -1;
    }

    fb->window = (window == None) ? DefaultRootWindow(fb->display) : window;

    // Check XShm extension
    int shm_major, shm_minor;
    Bool pixmaps;
    if (!XShmQueryExtension(fb->display)) {
        fprintf(stderr, "MIT-SHM not available\n");
        XCloseDisplay(fb->display);
        return -1;
    }

    // Get window attributes
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(fb->display, fb->window, &attrs)) {
        fprintf(stderr, "Cannot get window attributes\n");
        XCloseDisplay(fb->display);
        return -1;
    }

    fb->width = attrs.width;
    fb->height = attrs.height;
    fb->depth = attrs.depth;

    // Determine bytes per pixel
    fb->bpp = (attrs.depth + 7) / 8;
    if (fb->bpp < 4) fb->bpp = 4;  // Always use 32-bit for alignment

    // Create shared memory segment
    size_t shmsize = fb->width * fb->height * fb->bpp;
    fb->shminfo.shmid = shmget(IPC_PRIVATE, shmsize, IPC_CREAT | 0777);
    if (fb->shminfo.shmid < 0) {
        perror("shmget");
        XCloseDisplay(fb->display);
        return -1;
    }

    fb->shminfo.shmaddr = shmat(fb->shminfo.shmid, NULL, 0);
    if (fb->shminfo.shmaddr == (void *)-1) {
        perror("shmat");
        shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(fb->display);
        return -1;
    }
    fb->shminfo.readOnly = False;

    // Create XImage
    fb->image = XShmCreateImage(fb->display,
                                attrs.visual,
                                attrs.depth,
                                ZPixmap,
                                fb->shminfo.shmaddr,
                                &fb->shminfo,
                                fb->width, fb->height);
    if (!fb->image) {
        fprintf(stderr, "XShmCreateImage failed\n");
        shmdt(fb->shminfo.shmaddr);
        shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(fb->display);
        return -1;
    }

    // Attach to X server
    if (!XShmAttach(fb->display, &fb->shminfo)) {
        fprintf(stderr, "XShmAttach failed\n");
        XDestroyImage(fb->image);
        shmdt(fb->shminfo.shmaddr);
        shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(fb->display);
        return -1;
    }

    // Extract color masks for RGB conversion
    Visual *visual = attrs.visual;
    fb->rmask = visual->red_mask;
    fb->gmask = visual->green_mask;
    fb->bmask = visual->blue_mask;

    printf("Capture initialized: %dx%d, depth=%d, bpp=%d\n",
           fb->width, fb->height, fb->depth, fb->bpp);
    printf("Color masks: R=0x%08x G=0x%08x B=0x%08x\n",
           fb->rmask, fb->gmask, fb->bmask);

    return 0;
}

// Capture framebuffer to output buffer (RGB888 format)
int fb_capture_frame(FramebufferCapture *fb, uint8_t *output) {
    // This is the key operation - captures pixels into shared memory
    if (!XShmGetImage(fb->display, fb->window, fb->image, 0, 0, AllPlanes)) {
        fprintf(stderr, "XShmGetImage failed\n");
        return -1;
    }

    // Convert from server format to RGB888
    uint32_t *src = (uint32_t *)fb->image->data;
    int src_stride = fb->image->bytes_per_line / 4;

    for (int y = 0; y < fb->height; y++) {
        uint32_t *row = src + (y * src_stride);
        uint8_t *out_row = output + (y * fb->width * 3);

        for (int x = 0; x < fb->width; x++) {
            uint32_t pixel = row[x];

            // Extract RGB using color masks
            int r = (pixel & fb->rmask) >> __builtin_ctz(fb->rmask);
            int g = (pixel & fb->gmask) >> __builtin_ctz(fb->gmask);
            int b = (pixel & fb->bmask) >> __builtin_ctz(fb->bmask);

            // Scale to 8-bit if needed (for 15/16-bit depth)
            if (fb->depth <= 16) {
                int bits = (fb->depth + 2) / 3;  // Approx bits per channel
                r = (r << (8 - bits)) | (r >> (2 * bits - 8));
                g = (g << (8 - bits)) | (g >> (2 * bits - 8));
                b = (b << (8 - bits)) | (b >> (2 * bits - 8));
            }

            out_row[x * 3 + 0] = r;
            out_row[x * 3 + 1] = g;
            out_row[x * 3 + 2] = b;
        }
    }

    return 0;
}

void fb_capture_cleanup(FramebufferCapture *fb) {
    XShmDetach(fb->display, &fb->shminfo);
    XDestroyImage(fb->image);
    shmdt(fb->shminfo.shmaddr);
    shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
    XCloseDisplay(fb->display);
}

// Save as raw RGB file (for verification)
void save_rgb_raw(const char *filename, uint8_t *data, int width, int height) {
    FILE *f = fopen(filename, "wb");
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(data, 1, width * height * 3, f);
    fclose(f);
    printf("Saved %s (%dx%d RGB)\n", filename, width, height);
}

int main(int argc, char **argv) {
    const char *display = getenv("DISPLAY") ? : ":99";

    FramebufferCapture fb;
    if (fb_capture_init(&fb, display, None) != 0) {
        return 1;
    }

    uint8_t *framebuffer = malloc(fb.width * fb.height * 3);

    // Capture 10 frames
    for (int i = 0; i < 10; i++) {
        fb_capture_frame(&fb, framebuffer);
        printf("Frame %d captured\n", i);
    }

    // Save first frame as PPM
    save_rgb_raw("/tmp/capture.ppm", framebuffer, fb.width, fb.height);

    free(framebuffer);
    fb_capture_cleanup(&fb);
    return 0;
}
```

## 7. Summary: Access Methods Comparison

| Method | Direct Memory | Speed | Complexity | Notes |
|--------|--------------|-------|------------|-------|
| XGetImage | No (copies via protocol) | Slow | Low | Simple but slow |
| XShmGetImage | Yes (shared memory) | Fast | Medium | Best for capture |
| XFixes + Shadow | Yes (server internal) | Fastest | High | Requires server module |
| /dev/fb* | No (Xvfb has none) | N/A | N/A | Xvfb doesn't create framebuffer device |
| mmap X server | No | N/A | N/A | Not possible from userspace |

## 8. Key Takeaways

1. **Xvfb has no traditional framebuffer** - it's pixmap-based unless Shadow extension is used
2. **MIT-SHM is the practical fast path** - provides near-direct memory access via shared segments
3. **XTest injects at server input queue** - events go through normal processing, can't be easily distinguished from real input
4. **Window IDs are XIDs** - 32-bit identifiers with client ID encoded in upper bits
5. **No true direct memory access** - the X server owns all memory; clients must use X11 protocol or extensions
