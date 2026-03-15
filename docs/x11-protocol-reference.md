# X11 Protocol Reference for Xvfb Automation

This document provides protocol-level details for direct X11 interaction with Xvfb.

## 1. X11 Protocol Message Formats

### 1.1 Core Request Format

All X11 requests share this header:

```
┌─────────────────────────────────────────────────────────────┐
│                    X11 Request Header                        │
├─────────────────────────────────────────────────────────────┤
│ Byte 0:  Opcode        - Request type (e.g., 73 = GetImage) │
│ Byte 1:  Data/Flags   - Request-specific flags              │
│ Bytes 2-3: Length     - Total size in 4-byte units (incl. hdr)│
│ Bytes 4+:  Request-specific data                            │
└─────────────────────────────────────────────────────────────┘

Minimum request size: 4 bytes
Maximum request size: 262140 bytes (client dependent)
```

### 1.2 Key Request Opcodes

| Opcode | Request | Size | Description |
|--------|---------|------|-------------|
| 42 | QueryTree | 12 | Get window hierarchy |
| 43 | InternAtom | 8+ | Get atom ID for name |
| 44 | GetAtomName | 8 | Get name for atom ID |
| 45 | GetProperty | 20 | Read window property |
| 46 | SetProperty | 16+ | Set window property |
| 55 | GetGeometry | 8 | Get drawable dimensions |
| 73 | GetImage | 20 | Read pixel data |
| 135 | SendEvent | 36 | Send event to window |
| 139 | GrabPointer | 20 | Grab mouse input |
| 140 | GrabButton | 32 | Grab mouse button |
| 141 | GrabKeyboard | 16 | Grab keyboard |
| 142 | GrabKey | 20 | Grab key combination |

## 2. GetImage Request/Reply

### 2.1 GetImage Request (Opcode 73)

```
┌─────────────────────────────────────────────────────────────┐
│                    GetImage Request                          │
├─────────────────────────────────────────────────────────────┤
│ Offset | Size | Field           | Value                     │
├─────────────────────────────────────────────────────────────┤
│   0    |  1   | opcode          | 73                        │
│   1    |  1   | format          | 0=Pixmap, 1=Bitmap        │
│   2    |  2   | length          | 5 (20 bytes total)        │
│   4    |  4   | drawable        | Window or Pixmap ID       │
│   8    |  2   | x               | X coordinate (int16)      │
│  10    |  2   | y               | Y coordinate (int16)      │
│  12    |  2   | width           | Width (uint16)            │
│  14    |  2   | height          | Height (uint16)           │
│  16    |  4   | plane-mask      | Which planes to read      │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 GetImage Reply

```
┌─────────────────────────────────────────────────────────────┐
│                    GetImage Reply                            │
├─────────────────────────────────────────────────────────────┤
│ Offset | Size | Field           | Value                     │
├─────────────────────────────────────────────────────────────┤
│   0    |  1   | type            | 1 (reply)                 │
│   1    |  1   | visual type     | Visual class              │
│   2    |  2   | sequence        | Request sequence number   │
│   4    |  4   | length          | Data length / 4           │
│   8    |  4   | size            | Data size in bytes        │
│  12    |  4   | visual id       | Visual ID                 │
│  16    |  2   | x               | Actual X (int16)          │
│  18    |  2   | y               | Actual Y (int16)          │
│  20    |  2   | width           | Actual width (uint16)     │
│  22    |  2   | height          | Actual height (uint16)    │
│  24    |  2   | depth           | Color depth (uint16)      │
│  26    |  6   | pad             | Padding                   │
│  32    |  N   | data            | Pixel data                │
└─────────────────────────────────────────────────────────────┘
```

## 3. XTest Extension Protocol

### 3.1 XTest Extension Requests

| Minor Opcode | Request | Description |
|--------------|---------|-------------|
| 0 | GetVersion | Query version |
| 1 | CompareCursor | Compare cursors |
| 2 | FakeInput | Generate synthetic input |
| 3 | GrabControl | Control input grabbing |
| 4 | GetDeviceInfo | Query device info |

### 3.2 FakeInput Request (XTest Minor Opcode 2)

This is the single request used for all XTest input simulation:

```
┌─────────────────────────────────────────────────────────────┐
│                   XTest FakeInput Request                    │
├─────────────────────────────────────────────────────────────┤
│ Offset | Size | Field           | Value                     │
├─────────────────────────────────────────────────────────────┤
│   0    |  1   | major opcode    | XTest major opcode        │
│   1    |  1   | minor opcode    | 2 (FakeInput)             │
│   2    |  2   | length          | Variable                  │
│   4    |  1   | type            | Event type                │
│   5    |  1   | detail          | Keycode/button            │
│   6    |  4   | time            | Time (or 0 for CurrentTime)│
│   10   |  4   | root            | Root window               │
│   14   |  4   | strideX         | X stride (motion only)    │
│   18   |  4   | strideY         | Y stride (motion only)    │
│   22   |  4   | deviceid        | Device ID (XI only)       │
│   26   |  2   | numdevices      | Number of devices         │
└─────────────────────────────────────────────────────────────┘

Event types:
  2 = KeyPress
  3 = KeyRelease
  4 = ButtonPress
  5 = ButtonRelease
  6 = MotionNotify
```

## 4. MIT-SHM Extension Protocol

### 4.1 SHM Request Opcodes

| Minor | Request | Description |
|-------|---------|-------------|
| 0 | Query | Check SHM support |
| 1 | Attach | Attach SHM segment |
| 2 | Detach | Detach SHM segment |
| 3 | PutImage | Draw from SHM |
| 4 | GetImage | Capture to SHM |
| 5 | CreatePixmap | Create SHM pixmap |

### 4.2 SHM Attach Request

```
┌─────────────────────────────────────────────────────────────┐
│                    SHM Attach Request                        │
├─────────────────────────────────────────────────────────────┤
│ Offset | Size | Field           | Value                     │
├─────────────────────────────────────────────────────────────┤
│   0    |  1   | major opcode    | SHM major opcode          │
│   1    |  1   | minor opcode    | 1 (Attach)                │
│   2    |  2   | length          | 3 (12 bytes)              │
│   4    |  4   | shmseg          | SHM segment ID (X side)   │
│   8    |  4   | shmid           | SysV shmget ID            │
│  12    |  1   | read-only       | 0=RW, 1=RO                │
└─────────────────────────────────────────────────────────────┘
```

### 4.3 SHM GetImage Request

```
┌─────────────────────────────────────────────────────────────┐
│                   SHM GetImage Request                       │
├─────────────────────────────────────────────────────────────┤
│ Offset | Size | Field           | Value                     │
├─────────────────────────────────────────────────────────────┤
│   0    |  1   | major opcode    | SHM major opcode          │
│   1    |  1   | minor opcode    | 4 (GetImage)              │
│   2    |  2   | length          | 5 (20 bytes)              │
│   4    |  4   | drawable        | Window/Pixmap ID          │
│   8    |  2   | x               | X coordinate              │
│  10    |  2   | y               | Y coordinate              │
│  12    |  2   | width           | Width                     │
│  14    |  2   | height          | Height                    │
│  16    |  4   | plane-mask      | Plane mask                │
│  20    |  1   | format          | Image format              │
│  21    |  1   | pad             | Padding                   │
│  22    |  4   | shmseg          | SHM segment ID            │
│  26    |  4   | offset          | Offset in SHM             │
└─────────────────────────────────────────────────────────────┘
```

## 5. Window Property Atoms

### 5.1 Common Atoms

| Atom Name | Description |
|-----------|-------------|
| WM_NAME | Window name (legacy) |
| WM_CLASS | Application class |
| WM_PROTOCOLS | Window manager protocols |
| WM_DELETE_WINDOW | Close window protocol |
| _NET_WM_NAME | UTF-8 window name (EWMH) |
| _NET_WM_PID | Process ID |
| _NET_WM_WINDOW_TYPE | Window type (EWMH) |
| _NET_WM_STATE | Window state (maximized, etc.) |
| _NET_ACTIVE_WINDOW | Currently active window |
| _NET_CLIENT_LIST | List of client windows |
| _NET_SUPPORTED | Supported EWMH features |
| _NET_SUPPORTING_WM_CHECK | WM compatibility check |
| _NET_WM_MOVED_RESIZE | Move/resize request |
| UTF8_STRING | UTF-8 text encoding |

### 5.2 Property Access via Xlib

```c
// Get window title (UTF-8)
Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", True);
Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", True);

unsigned char *prop;
Atom actual_type;
int actual_format;
unsigned long nitems, bytes_after;

XGetWindowProperty(dpy, window, net_wm_name,
                   0, 1024,    // offset, length
                   False,      // delete
                   utf8_string,// required type
                   &actual_type, &actual_format,
                   &nitems, &bytes_after, &prop);

if (prop) {
    printf("Window name: %s\n", prop);
    XFree(prop);
}
```

### 5.3 Property Access via XCB

```c
// Get window title (XCB)
xcb_get_property_cookie_t cookie =
    xcb_get_property(conn, 0, window,
                     XCB_ATOM_WM_NAME,
                     XCB_ATOM_STRING,
                     0, 64);

xcb_get_property_reply_t *reply =
    xcb_get_property_reply(conn, cookie, NULL);

if (reply && xcb_get_property_value_length(reply) > 0) {
    char *name = xcb_get_property_value(reply);
    printf("Window name: %.*s\n",
           xcb_get_property_value_length(reply), name);
}
free(reply);
```

## 6. Event Processing

### 6.1 Core Event Types

| Code | Event | Size | Description |
|------|-------|------|-------------|
| 2 | KeyPress | 32 | Key pressed |
| 3 | KeyRelease | 32 | Key released |
| 4 | ButtonPress | 32 | Mouse button pressed |
| 5 | ButtonRelease | 32 | Mouse button released |
| 6 | MotionNotify | 32 | Mouse moved |
| 7 | EnterNotify | 32 | Pointer entered window |
| 8 | LeaveNotify | 32 | Pointer left window |
| 9 | FocusIn | 12 | Window gained focus |
| 10 | FocusOut | 12 | Window lost focus |
| 12 | Expose | 12 | Window needs redraw |
| 22 | PropertyNotify | 12 | Property changed |
| 28 | SelectionClear | 12 | Selection cleared |
| 29 | SelectionRequest | 24 | Selection requested |

### 6.2 KeyPress Event Structure

```
┌─────────────────────────────────────────────────────────────┐
│                    KeyPress Event (32 bytes)                 │
├─────────────────────────────────────────────────────────────┤
│ Offset | Size | Field                                       │
├─────────────────────────────────────────────────────────────┤
│   0    |  1   | response_type (2 = KeyPress)                │
│   1    |  1   | detail (keycode)                            │
│   2    |  2   | sequence number                             │
│   4    |  4   | time (milliseconds)                         │
│   8    |  4   | root (root window)                          │
│  12    |  4   | event (destination window)                  │
│  16    |  4   | child (child window if any)                 │
│  20    |  2   | root_x, root_y (pointer position)           │
│  24    |  2   | event_x, event_y (relative to event win)    │
│  28    |  2   | state (modifier mask)                       │
│  30    |  1   | same_screen flag                            │
│  31    |  1   | pad                                         │
└─────────────────────────────────────────────────────────────┘
```

### 6.3 Event Masks

| Mask | Value | Description |
|------|-------|-------------|
| KeyPressMask | 1 | Key press events |
| KeyReleaseMask | 2 | Key release events |
| ButtonPressMask | 4 | Button press events |
| ButtonReleaseMask | 8 | Button release events |
| PointerMotionMask | 16 | Pointer motion |
| EnterWindowMask | 16 | Pointer enter |
| LeaveWindowMask | 32 | Pointer leave |
| FocusChangeMask | 0x200 | Focus changes |
| PropertyChangeMask | 0x800 | Property changes |
| StructureNotifyMask | 0x10000 | Window structure |

## 7. Modifier Masks

| Modifier | Value | Keys |
|----------|-------|------|
| ShiftMask | 0x0001 | Shift |
| LockMask | 0x0002 | Caps Lock |
| ControlMask | 0x0004 | Control |
| Mod1Mask | 0x0008 | Alt/Meta |
| Mod2Mask | 0x0010 | Num Lock |
| Mod3Mask | 0x0020 | (custom) |
| Mod4Mask | 0x0040 | Super/Windows |
| Mod5Mask | 0x0080 | (custom) |
| Button1Mask | 0x0100 | Left button held |
| Button2Mask | 0x0200 | Middle button held |
| Button3Mask | 0x0400 | Right button held |
| Button4Mask | 0x0800 | Wheel up held |
| Button5Mask | 0x1000 | Wheel down held |

## 8. Common Keysyms

| Keysym | Value | Description |
|--------|-------|-------------|
| XK_Return | 0xFF0D | Enter/Return |
| XK_Tab | 0xFF09 | Tab |
| XK_Escape | 0xFF1B | Escape |
| XK_space | 0x0020 | Space |
| XK_BackSpace | 0xFF08 | Backspace |
| XK_Delete | 0xFFFF | Delete |
| XK_Home | 0xFF50 | Home |
| XK_End | 0xFF57 | End |
| XK_Page_Up | 0xFF55 | Page Up |
| XK_Page_Down | 0xFF56 | Page Down |
| XK_Left | 0xFF51 | Left arrow |
| XK_Up | 0xFF52 | Up arrow |
| XK_Right | 0xFF53 | Right arrow |
| XK_Down | 0xFF54 | Down arrow |
| XK_F1-XK_F12 | 0xFFBE-0xFFC9 | Function keys |
| XK_Shift_L | 0xFFE1 | Left Shift |
| XK_Shift_R | 0xFFE2 | Right Shift |
| XK_Control_L | 0xFFE3 | Left Control |
| XK_Control_R | 0xFFE4 | Right Control |
| XK_Alt_L | 0xFFE9 | Left Alt |
| XK_Alt_R | 0xFFEA | Right Alt |
| XK_Super_L | 0xFFEB | Left Super |
| XK_Super_R | 0xFFEC | Right Super |

## 9. Visual Classes

| Class | Value | Description |
|-------|-------|-------------|
| StaticGray | 0 | B/W, fixed colormap |
| GrayScale | 1 | B/W, writable colormap |
| StaticColor | 2 | Color, fixed colormap |
| PseudoColor | 3 | Color, writable colormap |
| TrueColor | 4 | Color, RGB direct (most common) |
| DirectColor | 5 | Color, RGB with colormap |

For TrueColor (most common in Xvfb):
- Pixel value directly encodes RGB
- Masks define bit positions
- No colormap lookup needed

## 10. Error Codes

| Code | Name | Description |
|------|------|-------------|
| 1 | BadRequest | Invalid opcode |
| 2 | BadValue | Parameter out of range |
| 3 | BadWindow | Invalid window ID |
| 4 | BadPixmap | Invalid pixmap ID |
| 5 | BadAtom | Invalid atom |
| 6 | BadCursor | Invalid cursor |
| 7 | BadFont | Invalid font |
| 8 | BadMatch | Parameter mismatch |
| 9 | BadDrawable | Invalid drawable |
| 10 | BadAccess | Permission denied |
| 11 | BadAlloc | Memory exhausted |
