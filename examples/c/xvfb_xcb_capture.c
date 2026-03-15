/**
 * xvfb_xcb_capture.c
 *
 * Low-level framebuffer capture using XCB (X Protocol C Binding).
 * XCB provides direct access to X11 protocol messages with less
 * abstraction than Xlib.
 *
 * Compile:
 *   gcc -o xvfb_xcb_capture xvfb_xcb_capture.c -lxcb -lxcb-shm -O2
 *
 * Usage:
 *   DISPLAY=:99 ./xvfb_xcb_capture [output.ppm]
 *
 * Technical details:
 * - Uses XCB's direct protocol access (no Xlib wrapping)
 * - MIT-SHM via XCB-SHM extension
 * - Cookie/reply pattern for async protocol handling
 * - More efficient than Xlib for high-frequency operations
 */

#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xproto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/* Byte order */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

typedef struct {
    xcb_connection_t *conn;
    int screen_num;
    xcb_screen_t *screen;

    /* Shared memory */
    xcb_shm_seg_t shm_seg;
    int shm_id;
    void *shm_data;

    /* Framebuffer info */
    xcb_window_t window;
    uint16_t width, height;
    uint8_t depth;
    uint16_t bytes_per_pixel;
    uint16_t stride;

    /* Visual info */
    uint32_t red_mask, green_mask, blue_mask;

} XcbCapture;

/**
 * Calculate mask shift and bits
 */
static void calc_mask(uint32_t mask, int *shift, int *bits) {
    if (mask == 0) {
        *shift = 0;
        *bits = 0;
        return;
    }
    *shift = __builtin_ctz(mask);
    uint32_t tmp = mask >> *shift;
    *bits = 0;
    while (tmp & 1) {
        (*bits)++;
        tmp >>= 1;
    }
}

/**
 * Initialize XCB capture
 */
int xcb_capture_init(XcbCapture *cap, const char *display_name) {
    memset(cap, 0, sizeof(XcbCapture));

    /* Open XCB connection */
    int screen_num;
    cap->conn = xcb_connect(display_name, &screen_num);
    if (xcb_connection_has_error(cap->conn)) {
        fprintf(stderr, "ERROR: Cannot open XCB connection\n");
        return -1;
    }
    cap->screen_num = screen_num;

    /* Get screen */
    cap->screen = xcb_setup_roots_iterator(xcb_get_setup(cap->conn)).data;
    if (!cap->screen) {
        fprintf(stderr, "ERROR: No screen available\n");
        xcb_disconnect(cap->conn);
        return -1;
    }

    cap->window = cap->screen->root;
    cap->width = cap->screen->width_in_pixels;
    cap->height = cap->screen->height_in_pixels;
    cap->depth = cap->screen->root_depth;

    printf("Screen: %dx%d, depth=%d\n", cap->width, cap->height, cap->depth);

    /* Get visual info for color masks */
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(cap->screen);
    xcb_visualtype_t *visual_type = NULL;

    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        if (depth_iter.data->depth != cap->depth) continue;

        xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
        for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
            if (visual_iter.data->visual_id == cap->screen->root_visual) {
                visual_type = visual_iter.data;
                break;
            }
        }
        break;
    }

    if (visual_type) {
        cap->red_mask = visual_type->red_mask;
        cap->green_mask = visual_type->green_mask;
        cap->blue_mask = visual_type->blue_mask;

        int r_shift, r_bits, g_shift, g_bits, b_shift, b_bits;
        calc_mask(cap->red_mask, &r_shift, &r_bits);
        calc_mask(cap->green_mask, &g_shift, &g_bits);
        calc_mask(cap->blue_mask, &b_shift, &b_bits);

        printf("Visual masks: R=0x%08x(%db) G=0x%08x(%db) B=0x%08x(%db)\n",
               cap->red_mask, r_bits, cap->green_mask, g_bits,
               cap->blue_mask, b_bits);
    }

    /* Calculate bytes per pixel */
    cap->bytes_per_pixel = (cap->depth + 7) / 8;
    if (cap->bytes_per_pixel < 4) cap->bytes_per_pixel = 4;
    cap->stride = cap->width * cap->bytes_per_pixel;

    /* Check for SHM extension */
    xcb_shm_query_cookie_t shm_cookie = xcb_shm_query(cap->conn);
    xcb_shm_query_reply_t *shm_reply = xcb_shm_query_reply(cap->conn, shm_cookie, NULL);

    if (!shm_reply) {
        fprintf(stderr, "ERROR: SHM extension not available\n");
        xcb_disconnect(cap->conn);
        return -1;
    }

    printf("SHM extension: present=%d, pixmaps=%d\n",
           xcb_shm_query_shared_pixmap_support(shm_reply),
           xcb_shm_query_shared_pixmaps(shm_reply));

    cap->shm_seg = shm_reply->shared;
    free(shm_reply);

    /* Create shared memory segment */
    size_t shmsize = cap->width * cap->height * cap->bytes_per_pixel;
    printf("Creating SHM segment: %zu bytes\n", shmsize);

    cap->shm_id = shmget(IPC_PRIVATE, shmsize, IPC_CREAT | 0777);
    if (cap->shm_id < 0) {
        perror("shmget");
        xcb_disconnect(cap->conn);
        return -1;
    }

    cap->shm_data = shmat(cap->shm_id, NULL, 0);
    if (cap->shm_data == (void *)-1) {
        perror("shmat");
        shmctl(cap->shm_id, IPC_RMID, NULL);
        xcb_disconnect(cap->conn);
        return -1;
    }

    printf("SHM attached at: %p\n", cap->shm_data);

    /* Attach SHM segment to XCB */
    xcb_void_cookie_t attach_cookie =
        xcb_shm_attach_checked(cap->conn, cap->shm_seg, cap->shm_id, 0);

    xcb_generic_error_t *error = xcb_request_check(cap->conn, attach_cookie);
    if (error) {
        fprintf(stderr, "ERROR: xcb_shm_attach failed (code %d)\n", error->error_code);
        free(error);
        shmdt(cap->shm_data);
        shmctl(cap->shm_id, IPC_RMID, NULL);
        xcb_disconnect(cap->conn);
        return -1;
    }

    printf("XCB capture initialized\n");
    return 0;
}

/**
 * Capture framebuffer using XCB SHM GetImage
 */
int xcb_capture_frame(XcbCapture *cap, uint8_t *output_buffer) {
    /*
     * XCB SHM GetImage:
     *
     * The cookie/reply pattern:
     * 1. xcb_shm_get_image() sends the request and returns a cookie
     * 2. xcb_shm_get_image_reply() blocks until reply is received
     * 3. Server copies pixels to our SHM segment
     * 4. We read directly from shm_data
     */
    xcb_shm_get_image_cookie_t img_cookie =
        xcb_shm_get_image(cap->conn,
                          cap->window,
                          0, 0,                    /* x, y */
                          cap->width, cap->height, /* size */
                          0xFFFFFFFF,              /* plane mask (all planes) */
                          XCB_IMAGE_FORMAT_Z_PIXMAP,
                          cap->shm_seg,
                          0);                      /* offset in SHM */

    xcb_shm_get_image_reply_t *img_reply =
        xcb_shm_get_image_reply(cap->conn, img_cookie, NULL);

    if (!img_reply) {
        fprintf(stderr, "ERROR: SHM GetImage failed\n");
        return -1;
    }

    /* The image data is now in our SHM segment */
    uint32_t *src = (uint32_t *)cap->shm_data;
    int src_stride = cap->stride / sizeof(uint32_t);

    /* Calculate shift values for color extraction */
    int r_shift, r_bits, g_shift, g_bits, b_shift, b_bits;
    calc_mask(cap->red_mask, &r_shift, &r_bits);
    calc_mask(cap->green_mask, &g_shift, &g_bits);
    calc_mask(cap->blue_mask, &b_shift, &b_bits);

    /* Convert to RGB888 */
    for (int y = 0; y < cap->height; y++) {
        uint32_t *src_row = src + (y * src_stride);
        uint8_t *dst_row = output_buffer + (y * cap->width * 3);

        for (int x = 0; x < cap->width; x++) {
            uint32_t pixel = src_row[x];

            uint32_t r = (pixel & cap->red_mask) >> r_shift;
            uint32_t g = (pixel & cap->green_mask) >> g_shift;
            uint32_t b = (pixel & cap->blue_mask) >> b_shift;

            /* Scale to 8-bit if needed */
            if (r_bits < 8 && r_bits > 0) {
                r = (r << (8 - r_bits)) | (r >> (2 * r_bits - 8));
            }
            if (g_bits < 8 && g_bits > 0) {
                g = (g << (8 - g_bits)) | (g >> (2 * g_bits - 8));
            }
            if (b_bits < 8 && b_bits > 0) {
                b = (b << (8 - b_bits)) | (b >> (2 * b_bits - 8));
            }

            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;

            dst_row[x * 3 + 0] = (uint8_t)r;
            dst_row[x * 3 + 1] = (uint8_t)g;
            dst_row[x * 3 + 2] = (uint8_t)b;
        }
    }

    free(img_reply);
    return 0;
}

/**
 * Get pointer position using XCB
 */
int xcb_get_pointer(XcbCapture *cap, int *x, int *y, uint16_t *mask) {
    xcb_query_pointer_cookie_t ptr_cookie =
        xcb_query_pointer(cap->conn, cap->window);

    xcb_query_pointer_reply_t *ptr_reply =
        xcb_query_pointer_reply(cap->conn, ptr_cookie, NULL);

    if (!ptr_reply) {
        return -1;
    }

    if (x) *x = ptr_reply->win_x;
    if (y) *y = ptr_reply->win_y;
    if (mask) *mask = ptr_reply->mask;

    free(ptr_reply);
    return 0;
}

/**
 * List all windows using XCB
 */
int xcb_list_windows(XcbCapture *cap) {
    printf("\n=== Window List ===\n");

    /* Query tree of root window */
    xcb_query_tree_cookie_t tree_cookie =
        xcb_query_tree(cap->conn, cap->window);

    xcb_query_tree_reply_t *tree_reply =
        xcb_query_tree_reply(cap->conn, tree_cookie, NULL);

    if (!tree_reply) {
        return -1;
    }

    xcb_window_t *children = xcb_query_tree_children(tree_reply);
    int nchildren = xcb_query_tree_children_length(tree_reply);

    for (int i = 0; i < nchildren; i++) {
        xcb_window_t win = children[i];

        /* Get WM_NAME */
        xcb_get_property_cookie_t prop_cookie =
            xcb_get_property(cap->conn, 0, win,
                             XCB_ATOM_WM_NAME,
                             XCB_ATOM_STRING,
                             0, 64);

        xcb_get_property_reply_t *prop_reply =
            xcb_get_property_reply(cap->conn, prop_cookie, NULL);

        if (prop_reply && xcb_get_property_value_length(prop_reply) > 0) {
            char *name = xcb_get_property_value(prop_reply);
            printf("Window 0x%08x: %.*s\n", win,
                   xcb_get_property_value_length(prop_reply), name);
        } else {
            printf("Window 0x%08x (unnamed)\n", win);
        }

        if (prop_reply) free(prop_reply);
    }

    free(tree_reply);
    printf("===================\n");
    return 0;
}

/**
 * Send key event using XCB core protocol
 * Note: This is NOT the same as XTest - it bypasses normal input processing
 */
int xcb_send_key_event(XcbCapture *cap, xcb_keycode_t keycode, int press) {
    /*
     * XCB core protocol key event:
     *
     * This uses XSendEvent semantics - it sends directly to a window
     * and bypasses normal input processing. For proper input simulation,
     * you should use XTest extension instead.
     */
    xcb_key_press_event_t event;
    memset(&event, 0, sizeof(event));

    event.response_type = press ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
    event.detail = keycode;
    event.time = XCB_CURRENT_TIME;
    event.root = cap->window;
    event.event = cap->window;
    event.child = XCB_NONE;
    event.root_x = 0;
    event.root_y = 0;
    event.event_x = 0;
    event.event_y = 0;
    event.state = 0;
    event.same_screen = 1;

    xcb_send_event(cap->conn, 0, cap->window,
                   XCB_EVENT_MASK_KEY_PRESS,
                   (char *)&event);

    xcb_flush(cap->conn);
    return 0;
}

/**
 * Cleanup
 */
void xcb_capture_cleanup(XcbCapture *cap) {
    if (cap->conn) {
        /* Detach SHM */
        xcb_shm_detach(cap->conn, cap->shm_seg);
        xcb_flush(cap->conn);
        xcb_aux_sync(cap->conn);
    }

    if (cap->shm_data && cap->shm_data != (void *)-1) {
        shmdt(cap->shm_data);
    }

    if (cap->shm_id >= 0) {
        shmctl(cap->shm_id, IPC_RMID, NULL);
    }

    if (cap->conn) {
        xcb_disconnect(cap->conn);
    }

    memset(cap, 0, sizeof(XcbCapture));
}

/**
 * Save as PPM
 */
int save_ppm(const char *filename, uint8_t *data, int width, int height) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(data, 1, width * height * 3, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    const char *display = getenv("DISPLAY");
    const char *output = (argc > 1) ? argv[1] : "/tmp/xcb_capture.ppm";
    int frames = (argc > 2) ? atoi(argv[2]) : 1;

    printf("XCB Low-Level Framebuffer Capture\n");
    printf("==================================\n");
    printf("Display: %s\n", display ? display : "(default)");
    printf("Output: %s\n", output);
    printf("Frames: %d\n\n", frames);

    XcbCapture cap;
    if (xcb_capture_init(&cap, display) != 0) {
        return 1;
    }

    /* List windows */
    xcb_list_windows(&cap);

    /* Allocate RGB buffer */
    size_t bufsize = cap.width * cap.height * 3;
    uint8_t *framebuffer = malloc(bufsize);
    if (!framebuffer) {
        perror("malloc");
        xcb_capture_cleanup(&cap);
        return 1;
    }

    /* Capture frames */
    for (int i = 0; i < frames; i++) {
        int px, py;
        uint16_t mask;
        xcb_get_pointer(&cap, &px, &py, &mask);

        printf("Frame %d: pointer=(%d,%d), mask=0x%04x\n",
               i, px, py, mask);

        if (xcb_capture_frame(&cap, framebuffer) != 0) {
            fprintf(stderr, "Capture failed\n");
            free(framebuffer);
            xcb_capture_cleanup(&cap);
            return 1;
        }
    }

    /* Save */
    if (save_ppm(output, framebuffer, cap.width, cap.height) == 0) {
        printf("Saved to %s\n", output);
    }

    free(framebuffer);
    xcb_capture_cleanup(&cap);

    printf("Done.\n");
    return 0;
}
