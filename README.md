# tinybar
> a tiny X11 status bar

## usage and configuration
- `tinybar` is a simple status bar for X11
- it is designed to be maximally configurable
- configuration is done in C by editing the source
- here is the default configuration:
```c
#define BAR_HEIGHT 40
#define WIDGETS 3
#define BACKGROUND 0x123456
#define FOREGROUND 0xFFFFFF
#define BORDER 0xFF0000
#define PADDING 0
#define FONT_SIZE 12
#define FONT "Classic Console"
#define BORDER_WIDTH 2

void get_time(char *buf, int *_fg, int *_bg, int *_border) {
    (void)_fg;
    (void)_bg;
    (void)_border;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, 64, "%H:%M:%S", tm);
}

void get_date(char *buf, int *_fg, int *_bg, int *_border) {
    (void)_fg;
    (void)_bg;
    (void)_border;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, 64, "%Y-%m-%d", tm);
}

void get_battery(char *buf, int *_fg, int *_bg, int *border) {
    (void)_fg;
    (void)_bg;

    FILE *fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (!fp) {
        perror("fopen");
        return;
    }

    char buf2[64];
    fgets(buf2, sizeof(buf2), fp);

    int capacity = atoi(buf2);
    if (capacity < 20) {
        *border = 0xFF0000; // red
    } else if (capacity < 50) {
        *border = 0xFFFF00; // yellow
    } else {
        *border = 0x00FF00; // green
    }
    snprintf(buf, 64, "Battery: %d%%", capacity);

    fclose(fp);
}

Widget widgets[WIDGETS] = {
    {.width = 8,
     .callback = get_time,
     .interval = 1000},
    {.width = 8,
     .callback = get_date,
     .interval = 1000,},
    {.width = 13,
     .callback = get_battery,
     .interval = 60000,
     .border_width = 3},
};
```
- it will display the time, date, and battery percentage, which is colour coded based on the battery level
- for some reason, not all fonts seem to work, so try any listed by `fc-list`. Most seemed to work, but a few didn't (I think when you specify a font family instead of a specific font)

## building
- to build, simply run `make`
