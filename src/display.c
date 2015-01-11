/*      $Id$

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation; either version 2, or (at your option)
        any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program; if not, write to the Free Software
        Foundation, Inc., Inc., 51 Franklin Street, Fifth Floor, Boston,
        MA 02110-1301, USA.


        xfwm4    - (c) 2002-2015 Olivier Fourdan

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>
#ifdef HAVE_RENDER
#include <X11/extensions/Xrender.h>
#endif

#include "spinning_cursor.h"
#include "display.h"
#include "screen.h"
#include "client.h"
#include "compositor.h"

#ifndef MAX_HOSTNAME_LENGTH
#define MAX_HOSTNAME_LENGTH 512
#endif /* MAX_HOSTNAME_LENGTH */

#ifndef CURSOR_ROOT
#define CURSOR_ROOT XC_left_ptr
#endif

#ifndef CURSOR_MOVE
#define CURSOR_MOVE XC_fleur
#endif

static int
handleXError (Display * dpy, XErrorEvent * err)
{
#if DEBUG
    char buf[64];

    XGetErrorText (dpy, err->error_code, buf, 63);
    g_print ("XError: %s\n", buf);
    g_print ("==>  XID 0x%lx, Request %d, Error %d <==\n",
              err->resourceid, err->request_code, err->error_code);
#endif
    return 0;
}

static gboolean
myDisplayInitAtoms (DisplayInfo *display_info)
{
    static const char *atom_names[] = {
        "COMPOSITING_MANAGER",
        "_GTK_FRAME_EXTENTS",
        "_GTK_HIDE_TITLEBAR_WHEN_MAXIMIZED",
        "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR",
        "KWM_WIN_ICON",
        "_MOTIF_WM_HINTS",
        "_MOTIF_WM_INFO",
        "_NET_ACTIVE_WINDOW",
        "_NET_CLIENT_LIST",
        "_NET_CLIENT_LIST_STACKING",
        "_NET_CLOSE_WINDOW",
        "_NET_CURRENT_DESKTOP",
        "_NET_DESKTOP_GEOMETRY",
        "_NET_DESKTOP_LAYOUT",
        "_NET_DESKTOP_NAMES",
        "_NET_DESKTOP_VIEWPORT",
        "_NET_FRAME_EXTENTS",
        "_NET_MOVERESIZE_WINDOW",
        "_NET_NUMBER_OF_DESKTOPS",
        "_NET_REQUEST_FRAME_EXTENTS",
        "_NET_SHOWING_DESKTOP",
        "_NET_STARTUP_ID",
        "_NET_SUPPORTED",
        "_NET_SUPPORTING_WM_CHECK",
        "_NET_SYSTEM_TRAY_OPCODE",
        "_NET_WM_ACTION_ABOVE",
        "_NET_WM_ACTION_BELOW",
        "_NET_WM_ACTION_CHANGE_DESKTOP",
        "_NET_WM_ACTION_CLOSE",
        "_NET_WM_ACTION_FULLSCREEN",
        "_NET_WM_ACTION_MAXIMIZE_HORZ",
        "_NET_WM_ACTION_MAXIMIZE_VERT",
        "_NET_WM_ACTION_MINIMIZE",
        "_NET_WM_ACTION_MOVE",
        "_NET_WM_ACTION_RESIZE",
        "_NET_WM_ACTION_SHADE",
        "_NET_WM_ACTION_STICK",
        "_NET_WM_ALLOWED_ACTIONS",
        "_NET_WM_CONTEXT_HELP",
        "_NET_WM_DESKTOP",
        "_NET_WM_FULLSCREEN_MONITORS",
        "_NET_WM_ICON",
        "_NET_WM_ICON_GEOMETRY",
        "_NET_WM_ICON_NAME",
        "_NET_WM_MOVERESIZE",
        "_NET_WM_NAME",
        "_NET_WM_PID",
        "_NET_WM_PING",
        "_NET_WM_WINDOW_OPACITY",
        "_NET_WM_WINDOW_OPACITY_LOCKED",
        "_NET_WM_STATE",
        "_NET_WM_STATE_ABOVE",
        "_NET_WM_STATE_BELOW",
        "_NET_WM_STATE_DEMANDS_ATTENTION",
        "_NET_WM_STATE_FOCUSED",
        "_NET_WM_STATE_FULLSCREEN",
        "_NET_WM_STATE_HIDDEN",
        "_NET_WM_STATE_MAXIMIZED_HORZ",
        "_NET_WM_STATE_MAXIMIZED_VERT",
        "_NET_WM_STATE_MODAL",
        "_NET_WM_STATE_SHADED",
        "_NET_WM_STATE_SKIP_PAGER",
        "_NET_WM_STATE_SKIP_TASKBAR",
        "_NET_WM_STATE_STICKY",
        "_NET_WM_STRUT",
        "_NET_WM_STRUT_PARTIAL",
        "_NET_WM_SYNC_REQUEST",
        "_NET_WM_SYNC_REQUEST_COUNTER",
        "_NET_WM_USER_TIME",
        "_NET_WM_USER_TIME_WINDOW",
        "_NET_WM_WINDOW_TYPE",
        "_NET_WM_WINDOW_TYPE_DESKTOP",
        "_NET_WM_WINDOW_TYPE_DIALOG",
        "_NET_WM_WINDOW_TYPE_DOCK",
        "_NET_WM_WINDOW_TYPE_MENU",
        "_NET_WM_WINDOW_TYPE_NORMAL",
        "_NET_WM_WINDOW_TYPE_SPLASH",
        "_NET_WM_WINDOW_TYPE_TOOLBAR",
        "_NET_WM_WINDOW_TYPE_UTILITY",
        "_NET_WORKAREA",
        "MANAGER",
        "PIXMAP",
        "SM_CLIENT_ID",
        "UTF8_STRING",
        "WM_CHANGE_STATE",
        "WM_CLIENT_LEADER",
        "WM_CLIENT_MACHINE",
        "WM_COLORMAP_WINDOWS",
        "WM_DELETE_WINDOW",
        "WM_HINTS",
        "WM_PROTOCOLS",
        "WM_STATE",
        "WM_TAKE_FOCUS",
        "WM_TRANSIENT_FOR",
        "WM_WINDOW_ROLE",
        "XFWM4_COMPOSITING_MANAGER",
        "XFWM4_TIMESTAMP_PROP",
        "_XROOTPMAP_ID",
        "_XSETROOT_ID"
    };

    g_assert (ATOM_COUNT == G_N_ELEMENTS (atom_names));
    return (XInternAtoms (display_info->dpy,
                          (char **) atom_names,
                          ATOM_COUNT,
                          FALSE, display_info->atoms) != 0);
}

static void
myDisplayCreateTimestampWin (DisplayInfo *display_info)
{
    XSetWindowAttributes attributes;

    attributes.event_mask = PropertyChangeMask;
    attributes.override_redirect = TRUE;
    display_info->timestamp_win =
        XCreateWindow (display_info->dpy, DefaultRootWindow (display_info->dpy),
                       -100, -100, 10, 10, 0, 0, CopyFromParent, CopyFromParent,
                       CWEventMask | CWOverrideRedirect, &attributes);
}

DisplayInfo *
myDisplayInit (GdkDisplay *gdisplay)
{
    DisplayInfo *display;
    int major, minor;
    int dummy;
    gchar *hostnametmp;

    display = g_new0 (DisplayInfo, 1);

    display->gdisplay = gdisplay;
    display->dpy = (Display *) gdk_x11_display_get_xdisplay (gdisplay);

    display->session = NULL;
    display->quit = FALSE;
    display->reload = FALSE;

    XSetErrorHandler (handleXError);

    /* Initialize internal atoms */
    if (!myDisplayInitAtoms (display))
    {
        g_warning ("Some internal atoms were not properly created.");
    }

    /* Test XShape extension support */
    major = 0;
    minor = 0;
    display->shape_version = 0;
    if (XShapeQueryExtension (display->dpy,
                              &display->shape_event_base,
                              &dummy))
    {
        display->have_shape = TRUE;
        if (XShapeQueryVersion (display->dpy, &major, &minor))
        {
            display->shape_version = major * 1000 + minor;
        }
    }
    else
    {
        g_warning ("The display does not support the XShape extension.");
        display->have_shape = FALSE;
        display->shape_event_base = 0;
    }

#ifdef HAVE_XSYNC
    display->have_xsync = FALSE;

    display->xsync_error_base = 0;
    display->xsync_event_base = 0;

    major = SYNC_MAJOR_VERSION;
    minor = SYNC_MINOR_VERSION;

    if (XSyncQueryExtension (display->dpy,
                              &display->xsync_event_base,
                              &display->xsync_error_base)
         && XSyncInitialize (display->dpy,
                             &major,
                             &minor))
    {
        display->have_xsync = TRUE;
    }
    else
    {
        g_warning ("The display does not support the XSync extension.");
        display->have_xsync = FALSE;
        display->xsync_event_base = 0;
        display->xsync_error_base = 0;
    }
#endif /* HAVE_XSYNC */

#ifdef HAVE_RENDER
    if (XRenderQueryExtension (display->dpy,
                               &display->render_event_base,
                               &display->render_error_base))
    {
        display->have_render = TRUE;
    }
    else
    {
        g_warning ("The display does not support the XRender extension.");
        display->have_render = FALSE;
        display->render_event_base = 0;
        display->render_error_base = 0;
    }
#else  /* HAVE_RENDER */
    display->have_render = FALSE;
#endif /* HAVE_RENDER */

#ifdef HAVE_RANDR
    if (XRRQueryExtension (display->dpy,
                            &display->xrandr_event_base,
                            &display->xrandr_error_base))
    {
        display->have_xrandr = TRUE;
    }
    else
    {
        g_warning ("The display does not support the XRandr extension.");
        display->have_xrandr = FALSE;
        display->xrandr_event_base = 0;
        display->xrandr_error_base = 0;
    }
#else  /* HAVE_RANDR */
    display->have_xrandr = FALSE;
#endif /* HAVE_RANDR */

    myDisplayCreateCursor (display);

    myDisplayCreateTimestampWin (display);

    display->xfilter = NULL;
    display->screens = NULL;
    display->clients = NULL;
    display->xgrabcount = 0;
    display->double_click_time = 250;
    display->double_click_distance = 5;
    display->nb_screens = 0;
    display->current_time = CurrentTime;

    hostnametmp = g_new0 (gchar, (size_t) MAX_HOSTNAME_LENGTH + 1);
    if (gethostname ((char *) hostnametmp, MAX_HOSTNAME_LENGTH))
    {
        g_warning ("The display's hostname could not be determined.");
        display->hostname = NULL;
    } else {
        hostnametmp[MAX_HOSTNAME_LENGTH] = '\0';
        display->hostname = g_strdup(hostnametmp);
    }
    g_free (hostnametmp);

    compositorInitDisplay (display);

    return display;
}

DisplayInfo *
myDisplayClose (DisplayInfo *display)
{
    myDisplayFreeCursor (display);
    XDestroyWindow (display->dpy, display->timestamp_win);
    display->timestamp_win = None;

    if (display->hostname)
    {
        g_free (display->hostname);
        display->hostname = NULL;
    }

    g_slist_free (display->clients);
    display->clients = NULL;

    g_slist_free (display->screens);
    display->screens = NULL;

    return display;
}

gboolean
myDisplayHaveShape (DisplayInfo *display)
{
    g_return_val_if_fail (display != NULL, FALSE);

    return (display->have_shape);
}

gboolean
myDisplayHaveShapeInput (DisplayInfo *display)
{
    g_return_val_if_fail (display != NULL, FALSE);

    return ((display->have_shape) && (display->shape_version >= 1001));
}

gboolean
myDisplayHaveRender (DisplayInfo *display)
{
    g_return_val_if_fail (display != NULL, FALSE);

    return (display->have_render);
}

void
myDisplayCreateCursor (DisplayInfo *display)
{
    display->root_cursor =
        XCreateFontCursor (display->dpy, CURSOR_ROOT);
    display->move_cursor =
        XCreateFontCursor (display->dpy, CURSOR_MOVE);
    display->busy_cursor =
        cursorCreateSpinning (display->dpy);
    display->resize_cursor[CORNER_TOP_LEFT] =
        XCreateFontCursor (display->dpy, XC_top_left_corner);
    display->resize_cursor[CORNER_TOP_RIGHT] =
        XCreateFontCursor (display->dpy, XC_top_right_corner);
    display->resize_cursor[CORNER_BOTTOM_LEFT] =
        XCreateFontCursor (display->dpy, XC_bottom_left_corner);
    display->resize_cursor[CORNER_BOTTOM_RIGHT] =
        XCreateFontCursor (display->dpy, XC_bottom_right_corner);
    display->resize_cursor[CORNER_COUNT + SIDE_LEFT] =
        XCreateFontCursor (display->dpy, XC_left_side);
    display->resize_cursor[CORNER_COUNT + SIDE_RIGHT] =
        XCreateFontCursor (display->dpy, XC_right_side);
    display->resize_cursor[CORNER_COUNT + SIDE_TOP] =
        XCreateFontCursor (display->dpy, XC_top_side);
    display->resize_cursor[CORNER_COUNT + SIDE_BOTTOM] =
        XCreateFontCursor (display->dpy, XC_bottom_side);
}

void
myDisplayFreeCursor (DisplayInfo *display)
{
    int i;

    XFreeCursor (display->dpy, display->busy_cursor);
    display->busy_cursor = None;
    XFreeCursor (display->dpy, display->move_cursor);
    display->move_cursor = None;
    XFreeCursor (display->dpy, display->root_cursor);
    display->root_cursor = None;

    for (i = 0; i < SIDE_COUNT + CORNER_COUNT; i++)
    {
        XFreeCursor (display->dpy, display->resize_cursor[i]);
        display->resize_cursor[i] = None;
    }
}

Cursor
myDisplayGetCursorBusy (DisplayInfo *display)
{
    g_return_val_if_fail (display, None);

    return display->busy_cursor;
}

Cursor
myDisplayGetCursorMove  (DisplayInfo *display)
{
    g_return_val_if_fail (display, None);

    return display->move_cursor;
}

Cursor
myDisplayGetCursorRoot (DisplayInfo *display)
{
    g_return_val_if_fail (display, None);

    return display->root_cursor;
}

Cursor
myDisplayGetCursorResize (DisplayInfo *display, guint list)
{
    g_return_val_if_fail (display, None);
    g_return_val_if_fail (list < 8, None);

    return display->resize_cursor [list];
}


void
myDisplayGrabServer (DisplayInfo *display)
{
    g_return_if_fail (display);

    DBG ("entering myDisplayGrabServer");
    if (display->xgrabcount == 0)
    {
        DBG ("grabbing server");
        XGrabServer (display->dpy);
    }
    display->xgrabcount++;
    DBG ("grabs : %i", display->xgrabcount);
}

void
myDisplayUngrabServer (DisplayInfo *display)
{
    g_return_if_fail (display);

    DBG ("entering myDisplayUngrabServer");
    display->xgrabcount = display->xgrabcount - 1;
    if (display->xgrabcount < 0)       /* should never happen */
    {
        display->xgrabcount = 0;
    }
    if (display->xgrabcount == 0)
    {
        DBG ("ungrabbing server");
        XUngrabServer (display->dpy);
        XFlush (display->dpy);
    }
    DBG ("grabs : %i", display->xgrabcount);
}

void
myDisplayAddClient (DisplayInfo *display, Client *c)
{
    g_return_if_fail (c != None);
    g_return_if_fail (display != NULL);

    display->clients = g_slist_append (display->clients, c);
}

void
myDisplayRemoveClient (DisplayInfo *display, Client *c)
{
    g_return_if_fail (c != None);
    g_return_if_fail (display != NULL);

    display->clients = g_slist_remove (display->clients, c);
}

Client *
myDisplayGetClientFromWindow (DisplayInfo *display, Window w, unsigned short mode)
{
    GSList *list;

    g_return_val_if_fail (w != None, NULL);
    g_return_val_if_fail (display != NULL, NULL);

    for (list = display->clients; list; list = g_slist_next (list))
    {
        Client *c = (Client *) list->data;
        if (clientGetFromWindow (c, w, mode))
        {
            return (c);
        }
    }
    TRACE ("no client found");

    return NULL;
}

void
myDisplayAddScreen (DisplayInfo *display, ScreenInfo *screen)
{
    g_return_if_fail (screen != NULL);
    g_return_if_fail (display != NULL);

    display->screens = g_slist_append (display->screens, screen);
    display->nb_screens = display->nb_screens + 1;
}

void
myDisplayRemoveScreen (DisplayInfo *display, ScreenInfo *screen)
{
    g_return_if_fail (screen != NULL);
    g_return_if_fail (display != NULL);

    display->screens = g_slist_remove (display->screens, screen);
    display->nb_screens = display->nb_screens - 1;
    if (display->nb_screens < 0)
    {
        display->nb_screens = 0;
    }
}

ScreenInfo *
myDisplayGetScreenFromRoot (DisplayInfo *display, Window root)
{
    GSList *list;

    g_return_val_if_fail (root != None, NULL);
    g_return_val_if_fail (display != NULL, NULL);

    for (list = display->screens; list; list = g_slist_next (list))
    {
        ScreenInfo *screen = (ScreenInfo *) list->data;
        if (screen->xroot == root)
        {
            return screen;
        }
    }
    TRACE ("myDisplayGetScreenFromRoot: no screen found");

    return NULL;
}

ScreenInfo *
myDisplayGetScreenFromNum (DisplayInfo *display, int num)
{
    GSList *list;

    g_return_val_if_fail (display != NULL, NULL);

    for (list = display->screens; list; list = g_slist_next (list))
    {
        ScreenInfo *screen = (ScreenInfo *) list->data;
        if (screen->screen == num)
        {
            return screen;
        }
    }
    TRACE ("myDisplayGetScreenFromNum: no screen found");

    return NULL;
}

Window
myDisplayGetRootFromWindow(DisplayInfo *display, Window w)
{
    XWindowAttributes attributes;

    g_return_val_if_fail (w != None, None);
    g_return_val_if_fail (display != NULL, None);

    if (!XGetWindowAttributes(display->dpy, w, &attributes))
    {
        TRACE ("myDisplayGetRootFromWindow: no root found for 0x%lx", w);
        return None;
    }
    return attributes.root;
}

ScreenInfo *
myDisplayGetScreenFromWindow (DisplayInfo *display, Window w)
{
    Window root;

    g_return_val_if_fail (w != None, NULL);
    g_return_val_if_fail (display != NULL, NULL);

    root = myDisplayGetRootFromWindow (display, w);
    if (root != None)
    {
        return myDisplayGetScreenFromRoot (display, root);
    }
    TRACE ("myDisplayGetScreenFromWindow: no screen found");

    return NULL;
}

#ifdef ENABLE_KDE_SYSTRAY_PROXY
ScreenInfo *
myDisplayGetScreenFromSystray (DisplayInfo *display, Window w)
{
    GSList *list;

    g_return_val_if_fail (w != None, NULL);
    g_return_val_if_fail (display != NULL, NULL);

    for (list = display->screens; list; list = g_slist_next (list))
    {
        ScreenInfo *screen = (ScreenInfo *) list->data;
        if (screen->systray == w)
        {
            return screen;
        }
    }
    TRACE ("myDisplayGetScreenFromSystray: no screen found");

    return NULL;
}
#endif /* ENABLE_KDE_SYSTRAY_PROXY */

#ifdef HAVE_XSYNC
Client *
myDisplayGetClientFromXSyncAlarm (DisplayInfo *display, XSyncAlarm xalarm)
{
    GSList *list;

    g_return_val_if_fail (xalarm != None, NULL);
    g_return_val_if_fail (display != NULL, NULL);

    for (list = display->clients; list; list = g_slist_next (list))
    {
        Client *c = (Client *) list->data;
        if (xalarm == c->xsync_alarm)
        {
            return (c);
        }
    }
    TRACE ("no client found");

    return NULL;
}
#endif /* HAVE_XSYNC */

ScreenInfo *
myDisplayGetDefaultScreen (DisplayInfo *display)
{
    GSList *list;

    g_return_val_if_fail (display != NULL, NULL);

    list = display->screens;
    if (list)
    {
        return (ScreenInfo *) list->data;
    }

    return NULL;
}

guint32
myDisplayUpdateCurrentTime (DisplayInfo *display, XEvent *ev)
{
    guint32 timestamp;

    g_return_val_if_fail (display != NULL, (guint32) CurrentTime);

    timestamp = (guint32) CurrentTime;
    switch (ev->type)
    {
        case KeyPress:
        case KeyRelease:
            timestamp = (guint32) ev->xkey.time;
            break;
        case ButtonPress:
        case ButtonRelease:
            timestamp = (guint32) ev->xbutton.time;
            break;
        case MotionNotify:
            timestamp = (guint32) ev->xmotion.time;
            break;
        case EnterNotify:
        case LeaveNotify:
            timestamp = (guint32) ev->xcrossing.time;
            break;
        case PropertyNotify:
            timestamp = (guint32) ev->xproperty.time;
            break;
        case SelectionClear:
            timestamp = (guint32) ev->xselectionclear.time;
            break;
        case SelectionRequest:
            timestamp = (guint32) ev->xselectionrequest.time;
            break;
        case SelectionNotify:
            timestamp = (guint32) ev->xselection.time;
            break;
        default:
#ifdef HAVE_XSYNC
            if ((display->have_xsync) && (ev->type == display->xsync_event_base + XSyncAlarmNotify))
            {
                timestamp = ((XSyncAlarmNotifyEvent*) ev)->time;
            }
#endif /* HAVE_XSYNC */
            break;
    }

    if ((timestamp != (guint32) CurrentTime) && TIMESTAMP_IS_BEFORE(display->current_time, timestamp))
    {
        display->current_time = timestamp;
    }

    return display->current_time;
}

guint32
myDisplayGetCurrentTime (DisplayInfo *display)
{
    g_return_val_if_fail (display != NULL, (guint32) CurrentTime);

    TRACE ("myDisplayGetCurrentTime gives timestamp=%u", (guint32) display->current_time);
    return display->current_time;
}

guint32
myDisplayGetTime (DisplayInfo * display, guint32 timestamp)
{
    guint32 display_timestamp;

    display_timestamp = timestamp;
    if (display_timestamp == (guint32) CurrentTime)
    {
        display_timestamp = getXServerTime (display);
    }

    TRACE ("myDisplayGetTime gives timestamp=%u", (guint32) display_timestamp);
    return display_timestamp;
}

guint32
myDisplayGetLastUserTime (DisplayInfo *display)
{
    g_return_val_if_fail (display != NULL, (guint32) CurrentTime);

    TRACE ("myDisplayGetLastUserTime gives timestamp=%u", (guint32) display->last_user_time);
    return display->last_user_time;
}

void
myDisplaySetLastUserTime (DisplayInfo *display, guint32 timestamp)
{
    g_return_if_fail (display != NULL);
    g_return_if_fail (timestamp != 0);

    if (TIMESTAMP_IS_BEFORE(timestamp, display->last_user_time))
    {
        g_warning ("Last user time set back to %u (was %u)", (unsigned int) timestamp, (unsigned int) display->last_user_time);
    }
    display->last_user_time = timestamp;
}

void
myDisplayUpdateLastUserTime (DisplayInfo *display, guint32 timestamp)
{
    g_return_if_fail (display != NULL);
    g_return_if_fail (timestamp != 0);

    if (TIMESTAMP_IS_BEFORE(display->last_user_time, timestamp))
    {
        display->last_user_time = timestamp;
    }
}

gboolean
myDisplayTestXrender (DisplayInfo *display, gdouble min_time)
{
#ifdef HAVE_RENDER
    GTimeVal t1, t2;
    gdouble dt;
    Display *dpy;
    Picture picture1, picture2, picture3;
    XRenderPictFormat *format_src, *format_dst;
    Pixmap fillPixmap, rootPixmap;
    XRenderPictureAttributes pa;
    XSetWindowAttributes attrs;
    XImage *ximage;
    Window output;
    XRenderColor c;
    Visual *visual;
    Screen *screen;
    int x, y, w, h;
    int screen_number;
    int depth;
    int iterations;

    g_return_val_if_fail (display != NULL, FALSE);
    TRACE ("entering myDisplayTesxrender");

    c.alpha = 0x7FFF;
    c.red   = 0xFFFF;
    c.green = 0xFFFF;
    c.blue  = 0xFFFF;

    dpy = display->dpy;
    screen_number = DefaultScreen (dpy);
    screen = DefaultScreenOfDisplay (dpy);
    visual = DefaultVisual (dpy, screen_number);
    depth = DefaultDepth (dpy, screen_number);

    w = WidthOfScreen(screen) / 16;
    h = HeightOfScreen(screen) / 16;
    x = (WidthOfScreen(screen) - w);
    y = (HeightOfScreen(screen) - h);

    format_dst = XRenderFindVisualFormat (dpy, visual);
    g_return_val_if_fail (format_dst != NULL , FALSE);

    format_src = XRenderFindStandardFormat (dpy, PictStandardA8);
    g_return_val_if_fail (format_src != NULL , FALSE);

    ximage = XGetImage (dpy,
                        DefaultRootWindow(dpy),
                        x, y, w, h,
                        AllPlanes, ZPixmap);
    g_return_val_if_fail (ximage != NULL , FALSE);

    rootPixmap = XCreatePixmap (dpy,
                                DefaultRootWindow(dpy),
                                w, h, depth);
    XPutImage (dpy, rootPixmap,
               DefaultGC (dpy, screen_number), ximage,
               0, 0, 0, 0, w, h);
    XDestroyImage (ximage);

    attrs.override_redirect = TRUE;
    output = XCreateWindow (dpy,
                            DefaultRootWindow(dpy),
                            x, y, w, h,
                            0, CopyFromParent, CopyFromParent,
                            (Visual *) CopyFromParent,
                            CWOverrideRedirect, &attrs);
    XMapRaised (dpy, output);

    fillPixmap = XCreatePixmap (dpy,
                                DefaultRootWindow(dpy),
                                1, 1, 8);

    g_get_current_time (&t1);

    pa.repeat = TRUE;
    picture1 = XRenderCreatePicture (dpy,
                                     rootPixmap,
                                     format_dst, 0, NULL);
    picture2 = XRenderCreatePicture (dpy,
                                     fillPixmap,
                                     format_src, CPRepeat, &pa);
    picture3 = XRenderCreatePicture (dpy,
                                     output,
                                     format_dst, 0, NULL);
    XRenderComposite (dpy, PictOpSrc,
                    picture1, None, picture3,
                    0, 0, 0, 0, 0, 0, w, h);
    XRenderFillRectangle (dpy, PictOpSrc,
                          picture2, &c, 0, 0,
                          1, 1);
    for (iterations = 0; iterations < 10; iterations++)
    {
        XRenderComposite (dpy, PictOpOver,
                        picture1, picture2, picture3,
                        0, 0, 0, 0, 0, 0, w, h);
        ximage = XGetImage (dpy, output,
                            0, 0, 1, 1,
                            AllPlanes, ZPixmap);
        if (ximage)
        {
                XDestroyImage (ximage);
        }
    }
    XRenderFreePicture (dpy, picture1);
    XRenderFreePicture (dpy, picture2);
    XRenderFreePicture (dpy, picture3);

    XFreePixmap (dpy, fillPixmap);
    XFreePixmap (dpy, rootPixmap);

    XDestroyWindow (dpy, output);

    g_get_current_time (&t2);

    dt = (gdouble) (t2.tv_sec - t1.tv_sec) * G_USEC_PER_SEC +
         (gdouble) (t2.tv_usec - t1.tv_usec) / 1000.0;

    if (dt < min_time)
    {
        TRACE ("XRender test passed (target %3.4f sec., measured %3.4f sec.).", min_time, dt);
        return TRUE;
    }
    g_print ("XRender test failed (target %3.4f sec., measured %3.4f sec.).\n", min_time, dt);
    return FALSE;
#else  /* HAVE_RENDER */
    return FALSE;
#endif /* HAVE_RENDER */
}
