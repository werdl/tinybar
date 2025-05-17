#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define COMMAND_WIDGET(name, width, interval, font, font_size, command)        \
    void name(char *buf, int *fg, int *bg) {                                   \
        FILE *fp = popen(command, "r");                                        \
        if (!fp) {                                                             \
            perror("popen");                                                   \
            return;                                                            \
        }                                                                      \
        fgets(buf, 256, fp);                                                   \
        pclose(fp);                                                            \
    }                                                                          \
    static const Widget name##_widget = {.width = width,                       \
                                         .callback = name,                     \
                                         .interval = interval,                 \
                                         .font = font,                         \
                                         .font_size = font_size}

// -------------- CONFIG STARTS --------------

typedef struct Widget {
    int width;

    // callback
    void (*callback)(char *buf, int *fg, int *bg);

    // update interval in ms (0 for no update)
    int interval;

    const char *font;
    int font_size;

} Widget;

#define BAR_HEIGHT 20
#define WIDGETS 3
#define BACKGROUND 0x123456

void get_time(char *buf, int *fg, int *bg) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, 64, "%H:%M:%S", tm);
}

void get_date(char *buf, int *fg, int *bg) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, 64, "%Y-%m-%d", tm);
}

void get_battery(char *buf, int *fg, int *bg) {
    FILE *fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (!fp) {
        perror("fopen");
        return;
    }
    
    char buf2[64];
    fgets(buf2, sizeof(buf2), fp);

    int capacity = atoi(buf2);
    if (capacity < 20) {
        *fg = 0xFF0000; // red
    } else if (capacity < 50) {
        *fg = 0xFFFF00; // yellow
    } else {
        *fg = 0x00FF00; // green
    }
    *bg = BACKGROUND;
    snprintf(buf, 64, "Battery: %d%%", capacity);

    fclose(fp);
}

Widget widgets[WIDGETS] = {
    {.width = 8,
     .callback = get_time,
     .interval = 1000,
     .font = "JetBrains Mono",
     .font_size = 12},
    {.width = 8,
     .callback = get_date,
     .interval = 1000,
     .font = "JetBrains Mono",
     .font_size = 12},
    {.width = 12,
     .callback = get_battery,
     .interval = 60000,
     .font = "JetBrains Mono",
     .font_size = 12},
};

// -------------- CONFIG ENDS --------------

int time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

void die(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    exit(1);
}

int main() {
    Display *d = XOpenDisplay(NULL);
    if (!d)
        return 1;
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);

    int event_base, error_base;
    if (!XRRQueryExtension(d, &event_base, &error_base)) {
        die("Xrandr extension not available");
    }

    XRRScreenResources *res = XRRGetScreenResourcesCurrent(d, root);
    if (!res) {
        die("Failed to get screen resources");
    }

    // Create one bar window per active output
    Window *bars = calloc(res->ncrtc, sizeof(Window));
    int bar_count = 0;

    for (int i = 0; i < res->ncrtc; i++) {
        XRRCrtcInfo *ci = XRRGetCrtcInfo(d, res, res->crtcs[i]);
        if (!ci)
            continue;
        if (ci->mode == 0 || ci->width == 0 || ci->height == 0) {
            XRRFreeCrtcInfo(ci);
            continue;
        }

        XSetWindowAttributes swa;
        swa.override_redirect = True;
        swa.background_pixel = BACKGROUND;
        swa.event_mask = ExposureMask;

        Window w =
            XCreateWindow(d, root, ci->x, ci->y, ci->width, BAR_HEIGHT, 0,
                          CopyFromParent, CopyFromParent, CopyFromParent,
                          CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);

        Atom window_type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
        Atom dock_type = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
        XChangeProperty(d, w, window_type, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&dock_type, 1);

        // Reserve screen space on this monitor
        Atom net_wm_strut_partial =
            XInternAtom(d, "_NET_WM_STRUT_PARTIAL", False);
        long strut[12] = {0};
        strut[2] = BAR_HEIGHT;            // top strut height
        strut[8] = ci->x;                 // top strut start (monitor left)
        strut[9] = ci->x + ci->width - 1; // top strut end (monitor right)
        XChangeProperty(d, w, net_wm_strut_partial, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)strut, 12);

        XMapWindow(d, w);

        bars[bar_count++] = w;
        XRRFreeCrtcInfo(ci);
    }

    XRRFreeScreenResources(res);

    GC gc = XCreateGC(d, root, 0, NULL);
    XSetForeground(d, gc, BlackPixel(d, screen));

    char buf[256];

    int last_update[WIDGETS] = {0};
    XftFont *fonts[WIDGETS];

    int shortest_interval = 0;
    for (int i = 0; i < WIDGETS; i++) {
        if (widgets[i].interval > 0) {
            if (shortest_interval == 0 ||
                widgets[i].interval < shortest_interval) {
                shortest_interval = widgets[i].interval;
            }
        }

        if (widgets[i].font) {
            // Load font

            char font_str[256];
            snprintf(font_str, sizeof(font_str), "%s:size=%d", widgets[i].font,
                     widgets[i].font_size);

            XftFont *font = XftFontOpenName(d, screen, font_str);
            if (!font) {
                die("Failed to load font: %s\n", font_str);
            }
            fonts[i] = font;
        } else {
            fonts[i] = XftFontOpenName(d, screen, "monospace:size=12");
            if (!fonts[i]) {
                die("Failed to load default font\n");
            }
        }
    }
    while (1) {
        int cur_x = 0;
        for (int i = 0; i < WIDGETS; i++) {
            // Check if the widget needs to be updated
            if (widgets[i].interval > 0) {
                int now = time_ms();
                if (last_update[i] == 0) {
                    last_update[i] = now;
                } else {
                    if ((now - last_update[i]) < widgets[i].interval) {
                        continue;
                    }
                    last_update[i] = now;
                }
            }

            int fg = 0, bg = BACKGROUND;
            widgets[i].callback(buf, &fg, &bg);
            printf("Widget %d: %s\n", i, buf);
            XSetForeground(d, gc, fg);
            XSetBackground(d, gc, bg);

            // parse in colors from fg and bg
            int r = (fg >> 16) & 0xFF;
            int g = (fg >> 8) & 0xFF;
            int b = fg & 0xFF;

            // Allocate Xft color
            XRenderColor xr = {.red = (fg >> 16 & 0xFF) * 257,
                               .green = (fg >> 8 & 0xFF) * 257,
                               .blue = (fg & 0xFF) * 257,
                               .alpha = 0xFFFF};
            XftColor color;
            XftColorAllocValue(d, DefaultVisual(d, 0), DefaultColormap(d, 0),
                               &xr, &color);

            XftFont *font = fonts[i];
            for (int j = 0; j < bar_count; j++) {
                Window w = bars[j];

                XSetForeground(d, gc, bg);

                XGlyphInfo extents;
                XftTextExtentsUtf8(d, font, (FcChar8 *)buf, strlen(buf),
                                   &extents);

                int x = cur_x + ((widgets[i].width * widgets[i].font_size) -
                                 extents.width) /
                                    2;
                int y = BAR_HEIGHT / 2 + extents.height / 2;

                XFillRectangle(d, w, gc, x, 0,
                               (widgets[i].width * widgets[i].font_size),
                               BAR_HEIGHT);

                XftDraw *draw = XftDrawCreate(d, w, DefaultVisual(d, 0),
                                              DefaultColormap(d, 0));

                XftDrawStringUtf8(draw, &color, font, x, y, (FcChar8 *)buf,
                                  strlen(buf));
                XftDrawDestroy(draw);
            }

            XFlush(d);

            cur_x += (widgets[i].width * widgets[i].font_size);
        }
        usleep(shortest_interval * 1000);
        XFlush(d);
    }

    free(bars);
    XFreeGC(d, gc);
    XCloseDisplay(d);
    return 0;
}
