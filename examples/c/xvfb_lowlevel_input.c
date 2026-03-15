/**
 * xvfb_lowlevel_input.c
 *
 * Direct input simulation using XTest extension.
 * This demonstrates the lowest-level method for injecting keyboard and
 * mouse events into an Xvfb display.
 *
 * Compile:
 *   gcc -o xvfb_input xvfb_lowlevel_input.c -lX11 -lXtst -O2
 *
 * Usage:
 *   DISPLAY=:99 ./xvfb_input <command> [args...]
 *
 * Commands:
 *   key <keysym>           - Press and release a key
 *   keydown <keysym>       - Press a key (no release)
 *   keyup <keysym>         - Release a key
 *   type <text>            - Type a string
 *   move <x> <y>           - Move pointer to absolute position
 *   click [button]         - Click mouse button (default: 1)
 *   clickat <x> <y> [btn]  - Click at position
 *   drag <x1> <y1> <x2> <y2> - Drag from one position to another
 *   info                   - Show current pointer state and windows
 *
 * Technical details:
 * - XTestFakeKeyEvent injects into server's input queue
 * - Events go through normal input processing (grabs, focus, etc.)
 * - Cannot be easily distinguished from real hardware events
 * - Updates server's keyboard/pointer state
 */

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

typedef struct {
    Display *display;
    int screen;
    Window root;

    /* XTest extension info */
    int xtest_major;
    int xtest_minor;
    int xtest_present;

    /* Current state */
    int ptr_x, ptr_y;
    unsigned int button_mask;
    Window focus_window;

} XTestInput;

/**
 * Initialize XTest input controller
 */
int xtest_init(XTestInput *input, const char *display_name) {
    memset(input, 0, sizeof(XTestInput));

    input->display = XOpenDisplay(display_name);
    if (!input->display) {
        fprintf(stderr, "ERROR: Cannot open display '%s'\n",
                display_name ? display_name : "default");
        return -1;
    }

    input->screen = DefaultScreen(input->display);
    input->root = RootWindow(input->display, input->screen);

    /* Query XTest extension */
    input->xtest_present = XTestQueryExtension(input->display,
                                                &input->xtest_major,
                                                &input->xtest_minor);
    if (!input->xtest_present) {
        fprintf(stderr, "ERROR: XTest extension not available\n");
        fprintf(stderr, "Ensure libxtst is installed\n");
        XCloseDisplay(input->display);
        return -1;
    }

    printf("XTest version: %d.%d\n", input->xtest_major, input->xtest_minor);

    /* Update current state */
    xtest_update_state(input);

    return 0;
}

/**
 * Update internal state from X server
 */
int xtest_update_state(XTestInput *input) {
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;

    if (XQueryPointer(input->display, input->root,
                      &root, &child,
                      &root_x, &root_y,
                      &win_x, &win_y,
                      &mask)) {
        input->ptr_x = win_x;
        input->ptr_y = win_y;
        input->button_mask = mask;
    }

    /* Get current input focus */
    int revert_to;
    XGetInputFocus(input->display, &input->focus_window, &revert_to);

    return 0;
}

/**
 * Convert keysym to keycode
 */
KeyCode keysym_to_keycode(Display *dpy, KeySym keysym) {
    return XKeysymToKeycode(dpy, keysym);
}

/**
 * Press a key (keydown only)
 */
int xtest_key_down(XTestInput *input, KeySym keysym) {
    KeyCode keycode = keysym_to_keycode(input->display, keysym);
    if (keycode == 0) {
        fprintf(stderr, "WARNING: No keycode for keysym 0x%lx\n", keysym);
        return -1;
    }

    /*
     * XTestFakeKeyEvent internals:
     *
     * 1. Creates a core KeyEvent structure
     * 2. Sets the keycode and event type (KeyPress)
     * 3. Sends to X server's input processing queue
     * 4. Server processes through normal input path:
     *    - Checks for keyboard grabs
     *    - Determines target window (usually focus window)
     *    - Updates keyboard state map
     *    - Delivers event to target client's event queue
     *
     * Key difference from XSendEvent:
     * - XSendEvent bypasses normal input processing
     * - XTestFakeKeyEvent goes through the full input pipeline
     */
    XTestFakeKeyEvent(input->display, keycode, True, CurrentTime);
    XFlush(input->display);

    /* Small delay to allow event processing */
    usleep(10000);  /* 10ms */

    return 0;
}

/**
 * Release a key (keyup only)
 */
int xtest_key_up(XTestInput *input, KeySym keysym) {
    KeyCode keycode = keysym_to_keycode(input->display, keysym);
    if (keycode == 0) return -1;

    XTestFakeKeyEvent(input->display, keycode, False, CurrentTime);
    XFlush(input->display);
    usleep(10000);

    return 0;
}

/**
 * Press and release a key
 */
int xtest_key_press(XTestInput *input, KeySym keysym) {
    xtest_key_down(input, keysym);
    xtest_key_up(input, keysym);
    return 0;
}

/**
 * Type a string character by character
 */
int xtest_type_string(XTestInput *input, const char *str) {
    for (const char *p = str; *p != '\0'; p++) {
        KeySym keysym;

        /* Handle special characters */
        if (*p == '\n') {
            keysym = XK_Return;
        } else if (*p == '\t') {
            keysym = XK_Tab;
        } else if (*p == ' ') {
            keysym = XK_space;
        } else {
            /* Convert character to keysym */
            keysym = (KeySym)*p;
        }

        xtest_key_press(input, keysym);
    }

    return 0;
}

/**
 * Move pointer to absolute position
 */
int xtest_move_to(XTestInput *input, int x, int y) {
    /*
     * XTestFakeMotionEvent:
     *
     * - First argument is screen number (-1 for current)
     * - Generates MotionNotify events along the path
     * - Updates server's pointer position immediately
     * - Triggers enter/leave events if crossing window boundaries
     */
    XTestFakeMotionEvent(input->display, input->screen, x, y, CurrentTime);
    XFlush(input->display);

    input->ptr_x = x;
    input->ptr_y = y;

    usleep(10000);
    return 0;
}

/**
 * Move pointer relative to current position
 */
int xtest_move_by(XTestInput *input, int dx, int dy) {
    return xtest_move_to(input, input->ptr_x + dx, input->ptr_y + dy);
}

/**
 * Press mouse button
 */
int xtest_button_down(XTestInput *input, int button) {
    /*
     * XTestFakeButtonEvent:
     *
     * - button: 1=left, 2=middle, 3=right, 4=wheel up, 5=wheel down
     * - is_press: True for button press, False for release
     * - Updates server's button state mask
     * - Generates ButtonPress/ButtonRelease events
     */
    XTestFakeButtonEvent(input->display, button, True, CurrentTime);
    XFlush(input->display);

    input->button_mask |= (1 << (button - 1));
    usleep(10000);

    return 0;
}

/**
 * Release mouse button
 */
int xtest_button_up(XTestInput *input, int button) {
    XTestFakeButtonEvent(input->display, button, False, CurrentTime);
    XFlush(input->display);

    input->button_mask &= ~(1 << (button - 1));
    usleep(10000);

    return 0;
}

/**
 * Click (press + release)
 */
int xtest_click(XTestInput *input, int button) {
    xtest_button_down(input, button);
    xtest_button_up(input, button);
    return 0;
}

/**
 * Click at specific position
 */
int xtest_click_at(XTestInput *input, int x, int y, int button) {
    xtest_move_to(input, x, y);
    usleep(50000);  /* 50ms for motion to settle */
    xtest_click(input, button);
    return 0;
}

/**
 * Double-click at position
 */
int xtest_double_click_at(XTestInput *input, int x, int y, int button) {
    xtest_click_at(input, x, y, button);
    usleep(150000);  /* 150ms - typical double-click interval */
    xtest_click_at(input, x, y, button);
    return 0;
}

/**
 * Drag from one position to another
 */
int xtest_drag(XTestInput *input, int x1, int y1, int x2, int y2, int button) {
    xtest_move_to(input, x1, y1);
    usleep(50000);
    xtest_button_down(input, button);
    usleep(50000);

    /* Move in small steps for smooth drag */
    int dx = x2 - x1;
    int dy = y2 - y1;
    int steps = 10;

    for (int i = 1; i <= steps; i++) {
        int x = x1 + (dx * i / steps);
        int y = y1 + (dy * i / steps);
        xtest_move_to(input, x, y);
        usleep(10000);
    }

    xtest_button_up(input, button);
    return 0;
}

/**
 * Get window at position
 */
Window xtest_window_at_pos(XTestInput *input, int x, int y) {
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;

    if (XQueryPointer(input->display, input->root,
                      &root, &child,
                      &root_x, &root_y,
                      &win_x, &win_y,
                      &mask)) {
        return (child != None) ? child : root;
    }
    return None;
}

/**
 * Print window tree recursively
 */
void print_window_tree(XTestInput *input, Window window, int indent) {
    Window root, parent;
    Window *children;
    unsigned int nchildren;

    /* Get window name */
    char *wm_name = NULL;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    Atom net_wm_name = XInternAtom(input->display, "_NET_WM_NAME", True);
    Atom utf8_string = XInternAtom(input->display, "UTF8_STRING", True);

    if (XGetWindowProperty(input->display, window, net_wm_name,
                           0, 256, False, utf8_string,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success && prop) {
        wm_name = strdup((char *)prop);
        XFree(prop);
    }

    /* Get geometry */
    XWindowAttributes attrs = {0};
    XGetWindowAttributes(input->display, window, &attrs);

    /* Print window info */
    printf("%*sWindow 0x%08x: [%d,%d] %dx%d",
           indent, "", window, attrs.x, attrs.y, attrs.width, attrs.height);

    if (wm_name) {
        printf(" \"%s\"", wm_name);
    }
    if (window == input->focus_window) {
        printf(" [FOCUS]");
    }
    printf("\n");

    free(wm_name);

    /* Recurse into children */
    if (XQueryTree(input->display, window, &root, &parent,
                   &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            print_window_tree(input, children[i], indent + 2);
        }
        XFree(children);
    }
}

/**
 * Find window by name pattern
 */
Window find_window_by_name(XTestInput *input, const char *pattern) {
    Window root, parent;
    Window *children;
    unsigned int nchildren;

    if (!XQueryTree(input->display, input->root,
                    &root, &parent, &children, &nchildren)) {
        return None;
    }

    Window found = None;

    for (unsigned int i = 0; i < nchildren && !found; i++) {
        Window win = children[i];

        /* Check WM_NAME */
        char *wm_name = NULL;
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;

        if (XGetWindowProperty(input->display, win, XA_WM_NAME,
                               0, 256, False, XA_STRING,
                               &actual_type, &actual_format,
                               &nitems, &bytes_after, &prop) == Success && prop) {
            wm_name = strdup((char *)prop);
            XFree(prop);
        }

        if (wm_name && strstr(wm_name, pattern)) {
            found = win;
        }

        free(wm_name);

        /* Search children recursively */
        if (!found) {
            /* Would need full recursive search here */
        }
    }

    XFree(children);
    return found;
}

/**
 * Print system status
 */
void xtest_print_info(XTestInput *input) {
    printf("\n=== XTest Input State ===\n");
    printf("Display: %s\n", DisplayString(input->display));
    printf("Screen: %d\n", input->screen);
    printf("Root window: 0x%08x\n", input->root);
    printf("\nPointer state:\n");
    printf("  Position: (%d, %d)\n", input->ptr_x, input->ptr_y);
    printf("  Button mask: 0x%04x\n", input->button_mask);
    printf("  Buttons: ");
    if (input->button_mask & Button1Mask) printf("L ");
    if (input->button_mask & Button2Mask) printf("M ");
    if (input->button_mask & Button3Mask) printf("R ");
    if (input->button_mask & Button4Mask) printf("WheelUp ");
    if (input->button_mask & Button5Mask) printf("WheelDown ");
    printf("\n");
    printf("\nFocus window: 0x%08x\n", input->focus_window);

    printf("\nWindow tree:\n");
    print_window_tree(input, input->root, 0);

    printf("\n=========================\n\n");
}

/**
 * Cleanup
 */
void xtest_cleanup(XTestInput *input) {
    if (input->display) {
        XCloseDisplay(input->display);
    }
    memset(input, 0, sizeof(XTestInput));
}

/* ============ Command parsing ============ */

void print_usage(const char *progname) {
    printf("Usage: %s <command> [args...]\n\n", progname);
    printf("Commands:\n");
    printf("  key <keysym>           Press and release a key\n");
    printf("  keydown <keysym>       Press a key (hold)\n");
    printf("  keyup <keysym>         Release a key\n");
    printf("  type <text>            Type a string\n");
    printf("  move <x> <y>           Move pointer to (x,y)\n");
    printf("  click [button]         Click mouse (1=left, 2=mid, 3=right)\n");
    printf("  clickat <x> <y> [btn]  Click at position\n");
    printf("  drag <x1> <y1> <x2> <y2> [btn]  Drag operation\n");
    printf("  info                   Show current state\n");
    printf("  find <pattern>         Find window by name\n");
    printf("\nExamples:\n");
    printf("  %s key Return          # Press Enter\n", progname);
    printf("  %s type \"Hello\"        # Type text\n", progname);
    printf("  %s move 100 200        # Move pointer\n", progname);
    printf("  %s click 1             # Left click\n", progname);
    printf("  %s clickat 500 300 3   # Right click at position\n", progname);
    printf("\nCommon keysyms:\n");
    printf("  Return, Tab, Escape, space\n");
    printf("  Control_L, Control_R, Shift_L, Shift_R, Alt_L, Alt_R\n");
    printf("  F1-F12, Up, Down, Left, Right\n");
    printf("  BackSpace, Delete, Home, End, Page_Up, Page_Down\n");
}

KeySym parse_keysym(const char *name) {
    /* Try string-to-keysym first */
    KeySym keysym = XStringToKeysym(name);
    if (keysym != NoSymbol) {
        return keysym;
    }

    /* Try as single character */
    if (strlen(name) == 1) {
        return (KeySym)name[0];
    }

    return NoSymbol;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *display = getenv("DISPLAY");
    const char *cmd = argv[1];

    XTestInput input;
    if (xtest_init(&input, display) != 0) {
        return 1;
    }

    int result = 0;

    if (strcmp(cmd, "info") == 0) {
        xtest_print_info(&input);
    }
    else if (strcmp(cmd, "key") == 0) {
        if (argc < 3) {
            fprintf(stderr, "ERROR: key requires keysym argument\n");
            result = 1;
        } else {
            KeySym keysym = parse_keysym(argv[2]);
            if (keysym == NoSymbol) {
                fprintf(stderr, "ERROR: Unknown keysym '%s'\n", argv[2]);
                result = 1;
            } else {
                xtest_key_press(&input, keysym);
                printf("Sent key: %s (keysym=0x%lx)\n", argv[2], keysym);
            }
        }
    }
    else if (strcmp(cmd, "keydown") == 0) {
        if (argc < 3) {
            fprintf(stderr, "ERROR: keydown requires keysym argument\n");
            result = 1;
        } else {
            KeySym keysym = parse_keysym(argv[2]);
            if (keysym == NoSymbol) {
                fprintf(stderr, "ERROR: Unknown keysym '%s'\n", argv[2]);
                result = 1;
            } else {
                xtest_key_down(&input, keysym);
                printf("Key down: %s\n", argv[2]);
            }
        }
    }
    else if (strcmp(cmd, "keyup") == 0) {
        if (argc < 3) {
            fprintf(stderr, "ERROR: keyup requires keysym argument\n");
            result = 1;
        } else {
            KeySym keysym = parse_keysym(argv[2]);
            if (keysym == NoSymbol) {
                fprintf(stderr, "ERROR: Unknown keysym '%s'\n", argv[2]);
                result = 1;
            } else {
                xtest_key_up(&input, keysym);
                printf("Key up: %s\n", argv[2]);
            }
        }
    }
    else if (strcmp(cmd, "type") == 0) {
        if (argc < 3) {
            fprintf(stderr, "ERROR: type requires text argument\n");
            result = 1;
        } else {
            xtest_type_string(&input, argv[2]);
            printf("Typed: %s\n", argv[2]);
        }
    }
    else if (strcmp(cmd, "move") == 0) {
        if (argc < 4) {
            fprintf(stderr, "ERROR: move requires x y arguments\n");
            result = 1;
        } else {
            int x = atoi(argv[2]);
            int y = atoi(argv[3]);
            xtest_move_to(&input, x, y);
            printf("Moved to: (%d, %d)\n", x, y);
        }
    }
    else if (strcmp(cmd, "click") == 0) {
        int button = (argc >= 3) ? atoi(argv[2]) : 1;
        xtest_click(&input, button);
        printf("Clicked button %d\n", button);
    }
    else if (strcmp(cmd, "clickat") == 0) {
        if (argc < 4) {
            fprintf(stderr, "ERROR: clickat requires x y [button] arguments\n");
            result = 1;
        } else {
            int x = atoi(argv[2]);
            int y = atoi(argv[3]);
            int button = (argc >= 5) ? atoi(argv[4]) : 1;
            xtest_click_at(&input, x, y, button);
            printf("Clicked at (%d, %d) button %d\n", x, y, button);
        }
    }
    else if (strcmp(cmd, "drag") == 0) {
        if (argc < 6) {
            fprintf(stderr, "ERROR: drag requires x1 y1 x2 y2 [button] arguments\n");
            result = 1;
        } else {
            int x1 = atoi(argv[2]);
            int y1 = atoi(argv[3]);
            int x2 = atoi(argv[4]);
            int y2 = atoi(argv[5]);
            int button = (argc >= 7) ? atoi(argv[6]) : 1;
            xtest_drag(&input, x1, y1, x2, y2, button);
            printf("Dragged from (%d,%d) to (%d,%d)\n", x1, y1, x2, y2);
        }
    }
    else if (strcmp(cmd, "find") == 0) {
        if (argc < 3) {
            fprintf(stderr, "ERROR: find requires pattern argument\n");
            result = 1;
        } else {
            Window found = find_window_by_name(&input, argv[2]);
            if (found != None) {
                printf("Found window: 0x%08x\n", found);
            } else {
                printf("No window matching '%s' found\n", argv[2]);
            }
        }
    }
    else {
        fprintf(stderr, "ERROR: Unknown command '%s'\n\n", cmd);
        print_usage(argv[0]);
        result = 1;
    }

    xtest_cleanup(&input);
    return result;
}
