#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>
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
Window win;

int SHIFT_MOD = 0;
int CTRL_MOD = 0;
int ALT_MOD = 0;
int shifted = 0;

Display *dpy = NULL;
int recent_timestamp = 0;
int current_monitor = 0;

// Global variables to track the selected cell (-1 means “none”)
int selected_row = -1;
int selected_col = -1;

void draw_grid(Display *display, Window window, int width, int height, XVisualInfo vinfo, XSetWindowAttributes attrs)
{
    fprintf(stderr, "Drawing grid %d %d\n", width, height);
    GC gc = XCreateGC(display, window, 0, NULL);
    GC sub_gc = XCreateGC(display, window, 0, NULL);
    XSetLineAttributes(display, gc, 2, LineSolid, CapButt, JoinMiter);
    XSetLineAttributes(display, sub_gc, 1, LineSolid, CapButt, JoinMiter);

    // Set up colors for the main grid and subgrid.
    XColor subColor;
    subColor.pixel = 0;
    subColor.red = 0xaaaa;
    subColor.green = 0xaaaa;
    subColor.blue = 0xaaaa;
    subColor.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(display, DefaultColormap(display, DefaultScreen(display)), &subColor);
    XSetForeground(display, sub_gc, subColor.pixel);
    XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));

    // Create an XftDraw for text rendering.
    XftDraw *draw = XftDrawCreate(display, window, vinfo.visual, attrs.colormap);
    if (!draw)
    {
        fprintf(stderr, "Unable to create XftDraw\n");
        return;
    }
    XftFont *xftFont = XftFontOpenName(display, DefaultScreen(dpy), "JetBrainsMonoNL NFP:size=12:style=Bold");
    if (!xftFont)
    {
        fprintf(stderr, "Unable to load scalable font\n");
        return;
    }
    XftColor color;
    if (!XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)),
                           DefaultColormap(display, DefaultScreen(display)), "white", &color))
    {
        fprintf(stderr, "Unable to allocate color\n");
        return;
    }

    double cellWidth = (double)width / GRID_SIZE;
    double cellHeight = (double)height / GRID_SIZE;

    // Loop over all grid cells.
    for (int row = 0; row < GRID_SIZE; row++)
    {
        for (int col = 0; col < GRID_SIZE; col++)
        {
            int x = col * cellWidth;
            int y = row * cellHeight;

            // Check if this cell is the selected one.
            if (row == selected_row && col == selected_col)
            {
                // Optionally, clear the cell background to remove any previous letters.
                XSetForeground(display, gc, BlackPixel(display, DefaultScreen(display)));
                XFillRectangle(display, window, gc, x, y, cellWidth, cellHeight);

                // Draw the cell border.
                XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
                XDrawRectangle(display, window, gc, x, y, cellWidth, cellHeight);

                // Draw subgrid lines inside the selected cell.
                for (int i = 1; i < SUBGRID_WIDTH; i++)
                {
                    int sub_x = x + i * cellWidth / SUBGRID_WIDTH;
                    XDrawLine(display, window, sub_gc, sub_x, y, sub_x, y + cellHeight);
                }
                for (int j = 1; j < SUBGRID_HEIGHT; j++)
                {
                    int sub_y = y + j * cellHeight / SUBGRID_HEIGHT;
                    XDrawLine(display, window, sub_gc, x, sub_y, x + cellWidth, sub_y);
                }
            }
            else
            {
                // Draw the standard grid cell border.
                XDrawRectangle(display, window, gc, x, y, cellWidth, cellHeight);

                // Draw the label (for example, "AB" where A and B are based on row and col)
                char label[3] = {'A' + row, 'A' + col, '\0'};
                XGlyphInfo extents;
                XftTextExtents8(display, xftFont, (FcChar8 *)label, 2, &extents);
                int textX = x + (cellWidth - extents.width) / 2;
                int textY = y + (cellHeight - (xftFont->ascent + xftFont->descent)) / 2 + xftFont->ascent;
                XftDrawString8(draw, &color, xftFont, textX, textY, (FcChar8 *)label, 2);
            }
        }
    }

    XFlush(display);
    XFreeGC(display, gc);
    XFreeGC(display, sub_gc);
    XftDrawDestroy(draw);
}

int change_monitor(int i, XRRMonitorInfo *m, int n)
{
    current_monitor = (current_monitor + i) % n;
    if (current_monitor < 0)
        current_monitor = n - 1;
    int x = m[current_monitor].x;
    int y = m[current_monitor].y;
    screen_width = m[current_monitor].width;
    screen_height = m[current_monitor].height;
    fprintf(stderr, "Switching to monitor %d: %d %d %d %d\n",
            current_monitor,
            x,
            y,
            m[current_monitor].width,
            m[current_monitor].height);
    XMoveResizeWindow(dpy, win,
                      x,
                      y,
                      screen_width,
                      screen_height);
    // XMoveWindow(dpy, window, -999, -999);
    XClearWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XSetInputFocus(dpy, win, RevertToPointerRoot, CurrentTime);

    return 0;
}

int main()
{
    dpy = XOpenDisplay(NULL);
    XEvent ev;
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
        XRRMonitorInfo mon = m[i];
        fprintf(stderr, "Monitor %d: %d %d %d %d\n", i, mon.x, mon.y, mon.width, mon.height);

        if (nx >= mon.x && nx <= mon.x + mon.width && ny >= mon.y && ny <= mon.y + mon.height)
        {
            current_monitor = i;
        }
    }

    fprintf(stderr, "di: %d nx: %d ny: %d dui: %d\n", di, nx, ny, dui);

    screen_width = m[current_monitor].width;
    screen_height = m[current_monitor].height;

    fprintf(stderr, "%d Monitors Detected\n", n);
    fprintf(stderr, "Active Screen is %d\n", screen);

    attrs.override_redirect = True;
    attrs.background_pixel = 0;

    fprintf(stderr, "Screen width: %d, Screen height: %d\n", screen_width, screen_height);

    XVisualInfo vinfo;
    if (!XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo))
    {
        fprintf(stderr, "No ARGB visual found\n");
        return EXIT_FAILURE;
    }
    attrs.colormap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);

    attrs.background_pixmap = None;
    attrs.event_mask = ExposureMask | KeyPressMask;
    attrs.border_pixel = 0;

    unsigned long valuemask = CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixmap | CWEventMask;

    win = XCreateWindow(dpy, root, m[current_monitor].x, m[current_monitor].y, m[current_monitor].width, m[current_monitor].height, 0,
                        vinfo.depth, InputOutput, vinfo.visual,
                        valuemask, &attrs);

    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);

    XWMHints *hints = XAllocWMHints();
    if (hints)
    {
        hints->flags = InputHint;
        hints->input = False;
        XSetWMHints(dpy, win, hints);
        XFree(hints);
    }

    Atom wmWindowType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom utilityType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(dpy, win, wmWindowType, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&utilityType, 1);

    Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XChangeProperty(dpy, win, wmState, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&fullscreen, 1);

    unsigned long opacity = (unsigned long)(0.6 * 0xffffffffu);
    Atom netWmOpacity = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(dpy, win, netWmOpacity, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&opacity, 1);

    XMapWindow(dpy, win);

    int keys[2];
    int i = 0;
    int stage = 0;

    int keycode = XKeysymToKeycode(dpy, XK_F1);
    XGrabKey(dpy, keycode, AnyModifier, root, True, GrabModeAsync, GrabModeAsync);

    XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    usleep(150000);
    int grabResult = XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    if (grabResult != GrabSuccess)
    {
        fprintf(stderr, "Failed to grab the keyboard\n");
        XCloseDisplay(dpy);
        return EXIT_FAILURE;
    }

    while (1)
    {
        XNextEvent(dpy, &ev);
        if (ev.type == ClientMessage &&
            (Atom)ev.xclient.data.l[0] == wmDelete)
        {
            fprintf(stderr, "Exiting\n");
            break;
        }
        if (ev.type == Expose)
        {
            XRenderColor bg = {0, 0, 0, 0.3 * 0xffff}; // red, green, blue = 0; alpha ≈20% opaque
            XRenderPictFormat *pictFormat = XRenderFindVisualFormat(dpy, vinfo.visual);
            Picture picture = XRenderCreatePicture(dpy, win, pictFormat, 0, NULL);
            XRenderFillRectangle(dpy, PictOpSrc, picture, &bg, 0, 0, screen_width, screen_height);
            XRenderFreePicture(dpy, picture);
            draw_grid(dpy, win, screen_width, screen_height, vinfo, attrs);
        }
        if (ev.type == KeyRelease)
        {
            KeySym key = XLookupKeysym(&ev.xkey, 0);
            if (key == XK_Shift_L)
                SHIFT_MOD = 0;

            if (key == XK_Control_L)
                CTRL_MOD = 0;

            if (key == XK_Alt_L)
                ALT_MOD = 0;
        }
        if (ev.type == KeyPress)
        {
            KeySym key = XLookupKeysym(&ev.xkey, 0);
            if (key == XK_Escape)
                break;

            if (key == XK_Shift_L)
                SHIFT_MOD = 1;

            if (key == XK_Control_L)
                CTRL_MOD = 1;

            if (key == XK_Alt_L)
                ALT_MOD = 1;

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

            fprintf(stderr, "Key: %s Code: %d %ld\n", XKeysymToString(key), ev.xkey.keycode, key);

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

                if (ev.xkey.keycode == 65)
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
                        if (subs[k] == ev.xkey.keycode)
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
                    XUnmapWindow(dpy, win);
                    XTestFakeButtonEvent(dpy, 3, True, CurrentTime);  // Button press
                    XTestFakeButtonEvent(dpy, 3, False, CurrentTime); // Button release
                    XMapWindow(dpy, win);
                    if (shifted)
                    {
                        fprintf(stderr, "Shift key released2\n");
                        XUnmapWindow(dpy, win);
                        XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button press
                        XMapWindow(dpy, win);
                        shifted = 0;
                        break;
                    }
                    break;
                }

                if (shifted)
                {
                    fprintf(stderr, "Shift key released\n");
                    XUnmapWindow(dpy, win);
                    XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button press
                    XMapWindow(dpy, win);
                    shifted = 0;
                    break;
                }

                if (CTRL_MOD)
                {
                    fprintf(stderr, "CTRL key pressed\n");
                    XUnmapWindow(dpy, win);
                    XTestFakeButtonEvent(dpy, 1, True, CurrentTime); // Button press
                    XMapWindow(dpy, win);
                    stage = 0;
                    shifted = 1;
                    i = 0;
                    continue;
                }

                struct timeval tv;
                struct timeval tv2;
                gettimeofday(&tv, NULL);

                int loops = 0;
                XUnmapWindow(dpy, win);
                XTestFakeButtonEvent(dpy, 1, True, CurrentTime);  // Button press
                XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button release
                XMapWindow(dpy, win);
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
                            XUnmapWindow(dpy, win);
                            XTestFakeButtonEvent(dpy, 1, True, CurrentTime);  // Button press
                            XTestFakeButtonEvent(dpy, 1, False, CurrentTime); // Button release
                            XMapWindow(dpy, win);

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
                    selected_row = keys[0];
                    selected_col = keys[1];
                    XClearWindow(dpy, win);
                    draw_grid(dpy, win, screen_width, screen_height, vinfo, attrs);
                    
                    stage = 1;
                }
            }
        }
    }

    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}