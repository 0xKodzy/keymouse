#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h> /* we share subpixel information */
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <X11/Xft/Xft.h>
#include <sys/time.h>

#define GRID_SIZE 26
#define SUBGRID_WIDTH 8
#define SUBGRID_HEIGHT 3
#define LETTERS "abcdefghijklmnopqrstuvwxyz"
int subs[24] = { // Keyboard keys that allow for subgrid selection (better precision)
    24, 25, 26, 27, 30, 31, 32, 33,
    38, 39, 40, 41, 44, 45, 46, 47,
    52, 53, 54, 55, 58, 59, 60, 61};

int screen_width, screen_height;
Window window;

int SHIFT_MOD = 0;
int CTRL_MOD = 0;
int ALT_MOD = 0;
int shifted = 0;

Display *dpy = NULL;
int recent_timestamp = 0;
int current_monitor = 0;

void draw_grid(Display *display, Window window, int width, int height, XVisualInfo vinfo, XSetWindowAttributes attrs)
{
    fprintf(stderr, "Drawing grid %d %d\n", width, height);
    GC gc = XCreateGC(display, window, 0, NULL);

    GC sub_gc = XCreateGC(display, window, 0, NULL);
    // Setting line width to 1 for thinner lines.
    XSetLineAttributes(display, gc, 2, LineSolid, CapButt, JoinMiter);
    XSetLineAttributes(display, sub_gc, 1, LineSolid, CapButt, JoinMiter);
    // Set a lighter grey for subgrid; adjust the color value if desired.
    // Here, we create a color with RGB components that look lighter.
    XColor subColor;
    subColor.pixel = 0;
    subColor.red = 0xaaaa;
    subColor.green = 0xaaaa;
    subColor.blue = 0xaaaa;
    subColor.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(display, DefaultColormap(display, DefaultScreen(display)), &subColor);

    XSetForeground(display, sub_gc, subColor.pixel);
    XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
    int i;
    int j;

    for (i = 0; i <= 26; i++)
    {
        int x = (int)((double)i * (width - 1) / 26.0 + 0.5);
        for (j = 1; j <= 7; j++)
        {
            int _x = x + j * (width - 1) / 26 / 8;
            // XDrawLine(display, window, sub_gc, _x, 0, _x, height - 1);
        }
        XDrawLine(display, window, gc, x, 0, x, height - 1);
    }

    // Draw horizontal lines similarly adjusted
    for (i = 0; i <= 26; i++)
    {
        int y = (int)((double)i * (height - 1) / 26.0 + 0.5);
        for (j = 1; j <= 2; j++)
        {
            int _y = y + j * (height - 1) / 26 / 3;
            // XDrawLine(display, window, sub_gc, 0, _y, width - 1, _y);
        }
        XDrawLine(display, window, gc, 0, y, width - 1, y);
    }

    // Create an XftDraw for your window after it's mapped
    XftDraw *draw = XftDrawCreate(display, window, vinfo.visual, attrs.colormap);
    if (!draw)
    {
        fprintf(stderr, "Unable to create XftDraw\n");
        return;
    }

    // Open a scalable font (the size is part of the font name)
    XftFont *xftFont = XftFontOpenName(display, DefaultScreen(dpy), "JetBrainsMonoNL NFP:size=12:style=Bold");
    if (!xftFont)
    {
        fprintf(stderr, "Unable to load scalable font 'Sans-24'\n");
        return;
    }

    XftColor color;
    XRenderColor renderColor = {0xffff, 0xffff, 0xffff, 0xffff}; // White color
    if (!XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), "white", &color))
    {
        fprintf(stderr, "Unable to allocate color\n");
        return;
    }

    double cellWidth = (double)(width) / GRID_SIZE;
    double cellHeight = (double)(height) / GRID_SIZE;
    for (int row = 0; row < GRID_SIZE; row++)
    {
        for (int col = 0; col < GRID_SIZE; col++)
        {
            char label[3] = {'A' + row, 'A' + col, '\0'};
            int x = col * cellWidth;
            int y = row * cellHeight;
            XGlyphInfo extents;
            XftTextExtents8(display, xftFont, (FcChar8 *)label, 2, &extents);
            // Center the text horizontally and vertically using extents metrics.
            int textX = x + (cellWidth - extents.width) / 2;
            int textY = y + (cellHeight - (xftFont->ascent + xftFont->descent)) / 2 + xftFont->ascent;
            XftDrawString8(draw, &color, xftFont, textX, textY, (FcChar8 *)label, 2);
        }
    }

    XFlush(display);
    XFreeGC(display, gc);
    XFreeGC(display, sub_gc);
}

int change_monitor(int i, XRRMonitorInfo *m, int n)
{
    current_monitor = (current_monitor + i) % n;
    if (current_monitor < 0)
        current_monitor = n - 1;
    screen_width = m[current_monitor].width;
    screen_height = m[current_monitor].height;
    fprintf(stderr, "Switching to monitor %d: %d %d %d %d\n",
            current_monitor,
            m[current_monitor].x,
            m[current_monitor].y,
            m[current_monitor].width,
            m[current_monitor].height);
    XMoveResizeWindow(dpy, window,
                      m[current_monitor].x,
                      m[current_monitor].y,
                      screen_width,
                      screen_height);
    // XMoveWindow(dpy, window, -999, -999);
    XClearWindow(dpy, window);
    XRaiseWindow(dpy, window);
    XSetInputFocus(dpy, window, RevertToPointerRoot, CurrentTime);
}

int main()
{
    dpy = XOpenDisplay(NULL);
    XEvent event;
    XSetWindowAttributes attrs;
    int screen;

    if (dpy == NULL)
    {
        fprintf(stderr, "Unable to open display\n");
        return EXIT_FAILURE;
    }

    screen = DefaultScreen(dpy);

    XRRMonitorInfo *m;
    int n;

    Window root = RootWindow(dpy, screen);

    m = XRRGetMonitors(dpy, DefaultRootWindow(dpy), 0, &n);
    if (n == -1)
        return fprintf(stderr, "Unable to open display\n");

    int di, nx, ny;
    unsigned int dui;
    Window dummy;

    if (!XQueryPointer(dpy, root, &dummy, &dummy, &di, &di, &nx, &ny, &dui))
        return 1;

    for (int i = 0; i < n; i++)
    {
        fprintf(stderr, "Monitor %d: %d %d %d %d\n", i, m[i].x, m[i].y, m[i].width, m[i].height);
        if (nx >= m[i].x && nx <= m[i].x + m[i].width && ny >= m[i].y && ny <= m[i].y + m[i].height)
        {
            current_monitor = i;
            break;
        }
    }

    fprintf(stderr, "di: %d nx: %d ny: %d dui: %d\n", di, nx, ny, dui);

    screen_width = m[current_monitor].width;
    screen_height = m[current_monitor].height;

    fprintf(stderr, "%d Monitors Detected\n", n);
    fprintf(stderr, "Active Screen is %d\n", screen);

    attrs.override_redirect = True;
    attrs.background_pixel = 0; // Transparent background

    fprintf(stderr, "Screen width: %d, Screen height: %d\n", screen_width, screen_height);

    // Select an ARGB visual (32-bit) for transparency.
    XVisualInfo vinfo;
    if (!XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo))
    {
        fprintf(stderr, "No ARGB visual found\n");
        return EXIT_FAILURE;
    }
    // Create colormap for the ARGB visual.
    attrs.colormap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);

    // Set the border to 0 and use a transparent background pixmap.
    attrs.background_pixmap = None; // fully transparent background
    attrs.event_mask = ExposureMask | KeyPressMask;
    attrs.border_pixel = 0;

    // Include CWColormap, CWBorderPixel, and CWBackPixmap in the mask.
    unsigned long valuemask = CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixmap | CWEventMask;

    // Create the window with the ARGB visual and depth.
    window = XCreateWindow(dpy, root, m[current_monitor].x, m[current_monitor].y, screen_width, screen_height, 0,
                           vinfo.depth, InputOutput, vinfo.visual,
                           valuemask, &attrs);

    // window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, WIDTH, HEIGHT, 1, BlackPixel(display, DefaultScreen(display)), BlackPixel(display, DefaultScreen(display)));

    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, window, &wmDelete, 1);

    typedef struct
    {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long inputMode;
        unsigned long status;
    } MotifWmHints;

    MotifWmHints hints;
    hints.flags = 2;       // MWM_HINTS_DECORATIONS
    hints.decorations = 0; // 0 (no decorations)
    hints.functions = 0;
    hints.inputMode = 0;
    hints.status = 0;

    XClassHint class_hint;
    class_hint.res_name = "c-screen-overlay"; // instance name
    class_hint.res_class = "super_overlay";   // class name
    XSetClassHint(dpy, window, &class_hint);

    Atom mwmHintsProperty = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy, window, mwmHintsProperty, mwmHintsProperty, 32,
                    PropModeReplace, (unsigned char *)&hints, 5);

    // Set window type to DOCK to hint a floating window
    Atom wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    // Atom wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    // Atom wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    XChangeProperty(dpy, window, wm_window_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&wm_window_type_dock, 1);

    // Request fullscreen and above status //NetWMWindowTypeDialog
    Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    Atom states[] = {fullscreen};
    XChangeProperty(dpy, window, wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, 1);

    unsigned long opacity = (unsigned long)(0.6 * 0xffffffffu);
    Atom netWmOpacity = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(dpy, window, netWmOpacity, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&opacity, 1);

    int keys[2];
    int i = 0;
    int stage = 0;

    XMapWindow(dpy, window);
    XFlush(dpy);
    // XDeleteProperty(dpy, window, XInternAtom(dpy, "WM_STATE", False));
    XRaiseWindow(dpy, window);
    XSetInputFocus(dpy, window, RevertToPointerRoot, CurrentTime);

    while (1)
    {
        fprintf(stderr, "Waiting for event\n");
        XNextEvent(dpy, &event);
        if (event.type == ClientMessage &&
            (Atom)event.xclient.data.l[0] == wmDelete)
        {
            fprintf(stderr, "Exiting\n");
            break;
        }
        if (event.type == Expose)
        {
            fprintf(stderr, "Expose event\n");
            XRenderColor bg = {0, 0, 0, 0.3 * 0xffff}; // red, green, blue = 0; alpha ≈20% opaque
            XRenderPictFormat *pictFormat = XRenderFindVisualFormat(dpy, vinfo.visual);
            Picture picture = XRenderCreatePicture(dpy, window, pictFormat, 0, NULL);
            XRenderFillRectangle(dpy, PictOpSrc, picture, &bg, 0, 0, screen_width, screen_height);
            XRenderFreePicture(dpy, picture);
            draw_grid(dpy, window, screen_width, screen_height, vinfo, attrs);
        }
        if (event.type == KeyRelease)
        {
            KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Shift_L)
                SHIFT_MOD = 0;

            if (key == XK_Control_L)
                CTRL_MOD = 0;

            if (key == XK_Alt_L)
                ALT_MOD = 0;
        }
        if (event.type == KeyPress)
        {
            KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Escape)
                break;

            if (key == XK_Shift_L)
                SHIFT_MOD = 1;

            if (key == XK_Control_L)
                CTRL_MOD = 1;

            if (stage == 0 && key == XK_Left)
            {
                change_monitor(1, m, n);
                continue;
            }

            if (stage == 0 && key == XK_Right)
            {
                change_monitor(-1, m, n);
                continue;
            }

            fprintf(stderr, "Key: %s Code: %d %ld\n", XKeysymToString(key), event.xkey.keycode, key);

            if (stage == 1)
            {

                int x = keys[1] * (screen_width - 1) / 26 + m[current_monitor].x;
                int y = keys[0] * (screen_height - 1) / 26 + m[current_monitor].y;

                if (key == XK_Return)
                {
                    XWarpPointer(dpy, None, root, 0, 0, 0, 0, x + (screen_width - 1) / 26 / 2, y + (screen_height - 1) / 26 / 2);
                    break;
                }

                int prev_key = 0;

                if (event.xkey.keycode == 65)
                {
                    prev_key = 65;
                    XWarpPointer(dpy, None, root, 0, 0, 0, 0, x + (screen_width - 1) / 26 / 2, y + (screen_height - 1) / 26 / 2);

                    fprintf(stderr, "Position: %d %d\n", x, y);
                }
                else
                {
                    int found = 0;
                    int num = 0;
                    for (int k = 0; k < 24; k++)
                    {
                        if (subs[k] == event.xkey.keycode)
                        {
                            found = 1;
                            num = k;
                            break;
                        }
                    }

                    if (found)
                    {
                        prev_key = subs[num];
                        int _x = x + (num % 8) * ((screen_width - 1) / 208) + (screen_width - 1) / 208 / 2;
                        int _y = y + (num / 8) * ((screen_height - 1) / 78) + (screen_height - 1) / 78 / 2;
                        XWarpPointer(dpy, None, root, 0, 0, 0, 0, _x, _y);
                        fprintf(stderr, "Subgrid: %d\n", num);
                    }
                    else
                    {
                        continue;
                    }
                }

                if (!prev_key)
                    break;

                if (ALT_MOD)
                {
                    fprintf(stderr, "Alt key pressed\n");
                    XUnmapWindow(dpy, window);
                    XTestFakeButtonEvent(dpy, 3, True, CurrentTime);  // Button press
                    XTestFakeButtonEvent(dpy, 3, False, CurrentTime); // Button release
                    XMapWindow(dpy, window);
                    if (shifted)
                    {
                        fprintf(stderr, "Shift key released2\n");
                        XUnmapWindow(dpy, window);
                        XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button press
                        XMapWindow(dpy, window);
                        shifted = 0;
                        break;
                    }
                    break;
                }

                if (shifted)
                {
                    fprintf(stderr, "Shift key released\n");
                    XUnmapWindow(dpy, window);
                    XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button press
                    XMapWindow(dpy, window);
                    shifted = 0;
                    break;
                }

                if (CTRL_MOD)
                {
                    fprintf(stderr, "CTRL key pressed\n");
                    XUnmapWindow(dpy, window);
                    XTestFakeButtonEvent(dpy, 1, True, CurrentTime); // Button press
                    XMapWindow(dpy, window);
                    stage = 0;
                    shifted = 1;
                    i = 0;
                    continue;
                }

                struct timeval tv;
                struct timeval tv2;
                gettimeofday(&tv, NULL);

                int loops = 0;
                XUnmapWindow(dpy, window);
                XTestFakeButtonEvent(dpy, 1, True, CurrentTime);  // Button press
                XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button release
                XMapWindow(dpy, window);
                while (1)
                {
                    XEvent event;

                    if (XPending(dpy))
                    {
                        fprintf(stderr, "Event found\n");
                        XNextEvent(dpy, &event);
                        fprintf(stderr, "Event type: %d %d\n", event.xkey.keycode, prev_key);
                        if (event.type == KeyPress && event.xkey.keycode == prev_key)
                        {
                            fprintf(stderr, "Key pressed\n");
                            XUnmapWindow(dpy, window);
                            XTestFakeButtonEvent(dpy, 1, True, CurrentTime);  // Button press
                            XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button release
                            XMapWindow(dpy, window);

                            if (++loops == 2)
                                break;
                        }
                    }

                    gettimeofday(&tv2, NULL);
                    if ((tv2.tv_usec < tv.tv_usec && 1000000 - tv.tv_usec + tv2.tv_usec > 250000 * (loops + 1)) || tv2.tv_usec - tv.tv_usec > 250000 * (loops + 1))
                    {
                        fprintf(stderr, "Time ran out \n");
                        break;
                    }
                }
                break;
            }

            if (97 <= key && key <= 122)
            {
                keys[i] = key - 97;
                i += 1;
                if (i == 2)
                {
                    fprintf(stderr, "Keys: %d %d\n", keys[0], keys[1]);
                    stage = 1;
                }
            }
        }
    }

    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}