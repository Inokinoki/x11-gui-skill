/**
 * xvfb_lowlevel_capture.c
 *
 * Direct framebuffer capture from Xvfb using MIT-SHM (XShm) extension.
 * This demonstrates the lowest-level practical method for capturing
 * pixels from an Xvfb display.
 *
 * Compile:
 *   gcc -o xvfb_capture xvfb_lowlevel_capture.c -lX11 -lXext -O2
 *
 * Usage:
 *   DISPLAY=:99 ./xvfb_capture [output.ppm]
 *
 * Technical details:
 * - Uses XShmCreateImage to create an XImage backed by SysV shared memory
 * - X server writes directly to shared memory segment
 * - Client reads directly from shmat() mapped address
 * - Zero-copy after initial setup (no protocol data transfer for pixels)
 */

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* Byte order detection */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

typedef struct {
    Display *display;
    Window window;
    int screen;

    /* Shared memory info */
    XShmSegmentInfo shminfo;
    XImage *image;

    /* Framebuffer dimensions and format */
    int width, height, depth;
    int bytes_per_pixel;
    int row_stride;

    /* Visual/color information for RGB extraction */
    uint32_t red_mask, green_mask, blue_mask;
    int red_shift, green_shift, blue_shift;
    int red_bits, green_bits, blue_bits;

} FbCapture;

/**
 * Calculate shift and bit count for a color mask
 * E.g., mask=0x00FF0000 returns shift=16, bits=8
 */
static void calc_mask_info(uint32_t mask, int *shift, int *bits) {
    if (mask == 0) {
        *shift = 0;
        *bits = 0;
        return;
    }

    /* Count trailing zeros for shift */
    *shift = __builtin_ctz(mask);

    /* Count 1-bits for bit depth */
    uint32_t tmp = mask >> *shift;
    *bits = 0;
    while (tmp & 1) {
        (*bits)++;
        tmp >>= 1;
    }
}

/**
 * Initialize framebuffer capture
 */
int fb_capture_init(FbCapture *fb, const char *display_name, Window window) {
    memset(fb, 0, sizeof(FbCapture));

    /* Open display */
    fb->display = XOpenDisplay(display_name);
    if (!fb->display) {
        fprintf(stderr, "ERROR: Cannot open display '%s'\n",
                display_name ? display_name : "default");
        return -1;
    }

    fb->screen = DefaultScreen(fb->display);
    fb->window = (window == None) ? RootWindow(fb->display, fb->screen) : window;

    /* Check for MIT-SHM extension support */
    if (!XShmQueryExtension(fb->display)) {
        fprintf(stderr, "ERROR: MIT-SHM extension not available\n");
        fprintf(stderr, "Ensure libxext is installed and X server supports SHM\n");
        XCloseDisplay(fb->display);
        return -1;
    }

    /* Check if XShm pixmaps are supported (optional optimization) */
    int shm_major, shm_minor;
    Bool pixmaps_supported;
    XShmQueryVersion(fb->display, &shm_major, &shm_minor, &pixmaps_supported);
    printf("MIT-SHM version: %d.%d, pixmaps: %s\n",
           shm_major, shm_minor, pixmaps_supported ? "yes" : "no");

    /* Get window attributes */
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(fb->display, fb->window, &attrs)) {
        fprintf(stderr, "ERROR: Cannot get window attributes\n");
        XCloseDisplay(fb->display);
        return -1;
    }

    fb->width = attrs.width;
    fb->height = attrs.height;
    fb->depth = attrs.depth;

    printf("Window: %dx%d, depth=%d\n", fb->width, fb->height, fb->depth);
    printf("Visual: ID=0x%lx, class=%d\n", attrs.visual->visualid, attrs.visual->class);

    /* Calculate bytes per pixel and stride */
    fb->bytes_per_pixel = (attrs.depth + 7) / 8;
    if (fb->bytes_per_pixel < 4) {
        fb->bytes_per_pixel = 4;  /* Pad to 32-bit for alignment */
    }
    fb->row_stride = fb->width * fb->bytes_per_pixel;

    /* Extract color masks from visual */
    fb->red_mask = attrs.visual->red_mask;
    fb->green_mask = attrs.visual->green_mask;
    fb->blue_mask = attrs.visual->blue_mask;

    calc_mask_info(fb->red_mask, &fb->red_shift, &fb->red_bits);
    calc_mask_info(fb->green_mask, &fb->green_shift, &fb->green_bits);
    calc_mask_info(fb->blue_mask, &fb->blue_shift, &fb->blue_bits);

    printf("Color masks: R=0x%08x(%d bits) G=0x%08x(%d bits) B=0x%08x(%d bits)\n",
           fb->red_mask, fb->red_bits,
           fb->green_mask, fb->green_bits,
           fb->blue_mask, fb->blue_bits);

    /* Create shared memory segment */
    size_t shmsize = fb->width * fb->height * fb->bytes_per_pixel;
    printf("Allocating SHM segment: %zu bytes\n", shmsize);

    fb->shminfo.shmid = shmget(IPC_PRIVATE, shmsize, IPC_CREAT | 0777);
    if (fb->shminfo.shmid < 0) {
        perror("shmget");
        XCloseDisplay(fb->display);
        return -1;
    }

    /* Attach shared memory to our address space */
    fb->shminfo.shmaddr = shmat(fb->shminfo.shmid, NULL, 0);
    if (fb->shminfo.shmaddr == (void *)-1) {
        perror("shmat");
        shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(fb->display);
        return -1;
    }
    fb->shminfo.readOnly = False;

    printf("SHM attached at: %p\n", fb->shminfo.shmaddr);

    /* Create XImage structure backed by shared memory */
    fb->image = XShmCreateImage(fb->display,
                                attrs.visual,
                                attrs.depth,
                                ZPixmap,
                                fb->shminfo.shmaddr,
                                &fb->shminfo,
                                fb->width, fb->height);
    if (!fb->image) {
        fprintf(stderr, "ERROR: XShmCreateImage failed\n");
        shmdt(fb->shminfo.shmaddr);
        shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(fb->display);
        return -1;
    }

    /* Tell X server to attach to the same shared memory segment */
    if (!XShmAttach(fb->display, &fb->shminfo)) {
        fprintf(stderr, "ERROR: XShmAttach failed\n");
        XDestroyImage(fb->image);
        shmdt(fb->shminfo.shmaddr);
        shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
        XCloseDisplay(fb->display);
        return -1;
    }

    printf("Capture initialized successfully\n");
    return 0;
}

/**
 * Capture a single frame from the framebuffer
 * Output buffer must be width * height * 3 bytes (RGB888)
 */
int fb_capture_frame(FbCapture *fb, uint8_t *output_buffer) {
    /*
     * XShmGetImage is the critical operation here.
     *
     * What happens internally:
     * 1. Client sends XShmGetImage request to server
     * 2. Server renders current screen state (if needed)
     * 3. Server copies pixels from its internal pixmap to SHM segment
     *    - Server has the SHM segment mapped in its address space too
     * 4. Server sends reply confirming completion
     * 5. Client can now read pixels directly from shmaddr
     *
     * This is synchronous - the call blocks until the image is captured.
     */
    if (!XShmGetImage(fb->display, fb->window, fb->image, 0, 0, AllPlanes)) {
        fprintf(stderr, "ERROR: XShmGetImage failed\n");
        return -1;
    }

    /*
     * Now convert from server's pixel format to RGB888.
     *
     * The server stores pixels in its native visual format.
     * For TrueColor visuals, this is typically:
     *   - 32-bit pixels with R/G/B masks
     *   - Possibly with padding/alignment bytes
     *
     * We need to extract R, G, B components and pack them as 3 bytes.
     */
    uint32_t *src_pixels = (uint32_t *)fb->image->data;
    int src_stride = fb->image->bytes_per_line / sizeof(uint32_t);

    for (int y = 0; y < fb->height; y++) {
        uint32_t *src_row = src_pixels + (y * src_stride);
        uint8_t *dst_row = output_buffer + (y * fb->width * 3);

        for (int x = 0; x < fb->width; x++) {
            uint32_t pixel = src_row[x];

            /* Extract color components using masks and shifts */
            uint32_t r = (pixel & fb->red_mask) >> fb->red_shift;
            uint32_t g = (pixel & fb->green_mask) >> fb->green_shift;
            uint32_t b = (pixel & fb->blue_mask) >> fb->blue_shift;

            /* Scale up to 8-bit if necessary (for 15/16-bit depths) */
            if (fb->red_bits < 8 && fb->red_bits > 0) {
                r = (r << (8 - fb->red_bits)) | (r >> (2 * fb->red_bits - 8));
            }
            if (fb->green_bits < 8 && fb->green_bits > 0) {
                g = (g << (8 - fb->green_bits)) | (g >> (2 * fb->green_bits - 8));
            }
            if (fb->blue_bits < 8 && fb->blue_bits > 0) {
                b = (b << (8 - fb->blue_bits)) | (b >> (2 * fb->blue_bits - 8));
            }

            /* Clamp to valid range */
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;

            dst_row[x * 3 + 0] = (uint8_t)r;
            dst_row[x * 3 + 1] = (uint8_t)g;
            dst_row[x * 3 + 2] = (uint8_t)b;
        }
    }

    return 0;
}

/**
 * Get pointer position (optional utility)
 */
int fb_get_pointer_pos(FbCapture *fb, int *x, int *y, unsigned int *mask) {
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int button_mask;

    if (!XQueryPointer(fb->display, fb->window,
                       &root, &child,
                       &root_x, &root_y,
                       &win_x, &win_y,
                       &button_mask)) {
        return -1;
    }

    if (x) *x = win_x;
    if (y) *y = win_y;
    if (mask) *mask = button_mask;

    return 0;
}

/**
 * Cleanup and release resources
 */
void fb_capture_cleanup(FbCapture *fb) {
    if (fb->display && fb->image) {
        /* Detach SHM from X server's perspective */
        XShmDetach(fb->display, &fb->shminfo);

        /* Destroy XImage (doesn't free shm data) */
        XDestroyImage(fb->image);
    }

    if (fb->shminfo.shmaddr && fb->shminfo.shmaddr != (char *)-1) {
        /* Detach from our address space */
        shmdt(fb->shminfo.shmaddr);
    }

    if (fb->shminfo.shmid >= 0) {
        /* Mark shared memory for deletion */
        shmctl(fb->shminfo.shmid, IPC_RMID, NULL);
    }

    if (fb->display) {
        XCloseDisplay(fb->display);
    }

    memset(fb, 0, sizeof(FbCapture));
}

/**
 * Save framebuffer as PPM file (simple RGB format)
 */
int save_as_ppm(const char *filename, uint8_t *data, int width, int height) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    /* PPM P6 format: binary RGB */
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(data, 1, width * height * 3, f);
    fclose(f);

    return 0;
}

/**
 * Print framebuffer format information
 */
void print_format_info(FbCapture *fb) {
    printf("\n=== Framebuffer Format ===\n");
    printf("Dimensions: %dx%d\n", fb->width, fb->height);
    printf("Depth: %d bits\n", fb->depth);
    printf("Bytes per pixel: %d\n", fb->bytes_per_pixel);
    printf("Row stride: %d bytes\n", fb->row_stride);
    printf("Total size: %d bytes\n", fb->width * fb->height * fb->bytes_per_pixel);
    printf("\nColor format:\n");
    printf("  Red:   mask=0x%08x, shift=%d, bits=%d\n",
           fb->red_mask, fb->red_shift, fb->red_bits);
    printf("  Green: mask=0x%08x, shift=%d, bits=%d\n",
           fb->green_mask, fb->green_shift, fb->green_bits);
    printf("  Blue:  mask=0x%08x, shift=%d, bits=%d\n",
           fb->blue_mask, fb->blue_shift, fb->blue_bits);
    printf("Byte order: %s\n", IS_BIG_ENDIAN ? "big-endian" : "little-endian");
    printf("==========================\n\n");
}

int main(int argc, char **argv) {
    const char *display = getenv("DISPLAY");
    const char *output_file = (argc > 1) ? argv[1] : "/tmp/framebuffer.ppm";
    int num_frames = (argc > 2) ? atoi(argv[2]) : 1;

    printf("Xvfb Low-Level Framebuffer Capture\n");
    printf("===================================\n");
    printf("Display: %s\n", display ? display : "(default)");
    printf("Output: %s\n", output_file);
    printf("Frames: %d\n\n", num_frames);

    FbCapture fb;
    if (fb_capture_init(&fb, display, None) != 0) {
        fprintf(stderr, "Failed to initialize capture\n");
        return 1;
    }

    print_format_info(&fb);

    /* Allocate output buffer (RGB888 format) */
    size_t bufsize = fb.width * fb.height * 3;
    uint8_t *framebuffer = malloc(bufsize);
    if (!framebuffer) {
        perror("malloc");
        fb_capture_cleanup(&fb);
        return 1;
    }

    /* Capture frames */
    for (int i = 0; i < num_frames; i++) {
        int px, py;
        unsigned int pmask;
        fb_get_pointer_pos(&fb, &px, &py, &pmask);

        printf("Frame %d: pointer=(%d,%d), buttons=0x%02x\n",
               i, px, py, pmask);

        if (fb_capture_frame(&fb, framebuffer) != 0) {
            fprintf(stderr, "Frame capture failed\n");
            free(framebuffer);
            fb_capture_cleanup(&fb);
            return 1;
        }
    }

    /* Save last captured frame */
    if (save_as_ppm(output_file, framebuffer, fb.width, fb.height) == 0) {
        printf("Saved framebuffer to %s\n", output_file);
    }

    free(framebuffer);
    fb_capture_cleanup(&fb);

    printf("Done.\n");
    return 0;
}
