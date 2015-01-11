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


        oroborus - (c) 2001 Ken Lynch
        xfwm4    - (c) 2002-2011 Olivier Fourdan

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>

#include "cycle.h"
#include "client.h"
#include "focus.h"
#include "frame.h"
#include "settings.h"
#include "stacking.h"
#include "tabwin.h"
#include "transients.h"
#include "wireframe.h"
#include "workspaces.h"
#include "event_filter.h"

typedef struct _ClientCycleData ClientCycleData;
struct _ClientCycleData
{
    Tabwin *tabwin;
    Window wireframe;
    gboolean inside;
};

static gint
clientCompareModal (gconstpointer a, gconstpointer b)
{
    return !clientIsModalFor ((Client *) a, (Client *) b);
}

static guint
clientGetCycleRange (ScreenInfo *screen_info)
{
    guint range;

    g_return_val_if_fail (screen_info != NULL, 0);
    TRACE ("entering clientGetCycleRange");

    range = 0;
    if (screen_info->params->cycle_hidden)
    {
        range |= SEARCH_INCLUDE_HIDDEN;
    }
    if (!screen_info->params->cycle_minimum)
    {
        range |= SEARCH_INCLUDE_SKIP_TASKBAR | SEARCH_INCLUDE_SKIP_PAGER;
    }
    if (screen_info->params->cycle_workspaces)
    {
        range |= SEARCH_INCLUDE_ALL_WORKSPACES;
    }

    return range;
}

static GList *
clientCycleCreateList (Client *c)
{
    ScreenInfo *screen_info;
    Client *c2;
    guint range, search_range,   i;
    GList *client_list;

    g_return_val_if_fail (c, NULL);
    TRACE ("entering clientCycleCreateList");

    screen_info = c->screen_info;
    range = clientGetCycleRange (screen_info);
    client_list = NULL;

    for (c2 = c, i = 0; c && i < screen_info->client_count; i++, c2 = c2->next)
    {
        search_range = range;
        /*
         *  We want to include modals even if skip pager/taskbar because
         *  modals are supposed to be focused
         */
        if (clientIsModal(c2))
        {
            search_range |= (SEARCH_INCLUDE_SKIP_TASKBAR | SEARCH_INCLUDE_SKIP_PAGER);
        }
        if (!clientSelectMask (c2, NULL, search_range, WINDOW_REGULAR_FOCUSABLE))
        {
            TRACE ("%s not in select mask", c2->name);
            continue;
        }
        if (screen_info->params->cycle_apps_only)
        {
            /*
             *  For apps only cycling, it's a tad more complicated
             * - We want "fake" dialogs, ie without a parent window
             * - We do not want dialogs but we want modals
             * - If a modal was added,we do not want to add
             *   its parent again
             */

            if (c2->type & WINDOW_TYPE_DIALOG)
            {
                if (clientIsValidTransientOrModal (c2))
                {
                    if (!clientIsModal(c2))
                    {
                        TRACE ("%s is not modal", c2->name);
                        continue;
                    }
                }
            }
            else if (!(c2->type & WINDOW_NORMAL))
            {
                {
                    TRACE ("%s is not normal", c2->name);
                    continue;
                }
            }
            else
            {
                if (g_list_find_custom (client_list, c2, clientCompareModal))
                {
                    TRACE ("%s found as modal list", c2->name);
                    continue;
                }
            }
        }

        TRACE ("clientCycleCreateList: adding %s", c2->name);
        client_list = g_list_append (client_list, c2);
    }

    return client_list;
}

static void
clientCycleFocusAndRaise (Client *c)
{
    ScreenInfo *screen_info;
    DisplayInfo *display_info;
    Client *sibling;

    g_return_if_fail (c != NULL);
    TRACE ("entering clientFocusAndRaise");

    screen_info = c->screen_info;
    display_info = screen_info->display_info;

    sibling = clientGetTransientFor(c);
    clientRaise (sibling, None);
    clientShow (sibling, TRUE);
    clientUnshade (c);
    clientSetFocus (screen_info, c, myDisplayGetCurrentTime (display_info), NO_FOCUS_FLAG);
    clientSetLastRaise (c);
}

static void
clientCycleActivate (Client *c)
{
    ScreenInfo *screen_info;
    DisplayInfo *display_info;
    Client *focused;
    guint workspace;

    if (c == NULL)
    {
        return;
    }

    screen_info = c->screen_info;
    display_info = screen_info->display_info;
    workspace = c->win_workspace;
    focused = clientGetFocus ();

    if ((focused) && (c != focused))
    {
        /* We might be able to avoid this if we are about to switch workspace */
        clientAdjustFullscreenLayer (focused, FALSE);
    }
    if (FLAG_TEST (c->xfwm_flags, XFWM_FLAG_WAS_SHOWN))
    {
        /* We are explicitely activating a window that was shown before show-desktop */
        clientClearAllShowDesktop (screen_info);
    }
    if (workspace != screen_info->current_ws)
    {
        workspaceSwitch (screen_info, workspace, c, FALSE, myDisplayGetCurrentTime (display_info));
    }

    clientCycleFocusAndRaise (c);
}

static eventFilterStatus
clientCycleEventFilter (XEvent * xevent, gpointer data)
{
    ScreenInfo *screen_info;
    DisplayInfo *display_info;
    ClientCycleData *passdata;
    Client *c, *removed;
    Client *c2 = NULL;
    eventFilterStatus status;
    KeyCode cancel, left, right, up, down;
    int key, modifiers;
    gboolean key_pressed, cycling, gone;
    GList *li;

    TRACE ("entering clientCycleEventFilter");

    passdata = (ClientCycleData *) data;
    c = tabwinGetSelected(passdata->tabwin);
    if (c == NULL)
    {
        return EVENT_FILTER_CONTINUE;
    }

    screen_info = c->screen_info;
    display_info = screen_info->display_info;
    cancel = screen_info->params->keys[KEY_CANCEL].keycode;
    left = screen_info->params->keys[KEY_LEFT].keycode;
    right = screen_info->params->keys[KEY_RIGHT].keycode;
    up = screen_info->params->keys[KEY_UP].keycode;
    down = screen_info->params->keys[KEY_DOWN].keycode;
    modifiers = (screen_info->params->keys[KEY_CYCLE_WINDOWS].modifier |
                 screen_info->params->keys[KEY_CYCLE_REVERSE_WINDOWS].modifier);
    status = EVENT_FILTER_STOP;
    removed = NULL;
    cycling = TRUE;
    gone = FALSE;

    /* Update the display time */
    myDisplayUpdateCurrentTime (display_info, xevent);

    switch (xevent->type)
    {
        case DestroyNotify:
            status = EVENT_FILTER_CONTINUE;
            if ((removed = myScreenGetClientFromWindow (screen_info, ((XDestroyWindowEvent *) xevent)->window, SEARCH_WINDOW)) == NULL)
                break; /* No need to go any further */
            gone |= (c == removed);
            /* Walk through */
        case UnmapNotify:
            status = EVENT_FILTER_CONTINUE;
            if (!removed && (removed = myScreenGetClientFromWindow (screen_info, ((XUnmapEvent *) xevent)->window, SEARCH_WINDOW)) == NULL)
                break; /* No need to go any further */
            gone |= (c == removed);
            c = tabwinRemoveClient(passdata->tabwin, removed);
            /* Walk through */
        case KeyPress:
            key_pressed = (xevent->type == KeyPress);
            if (gone || key_pressed)
            {
                if (key_pressed)
                {
                    key = myScreenGetKeyPressed (screen_info, (XKeyEvent *) xevent);
                    /*
                     * We cannot simply check for key == KEY_CANCEL here because of the
                     * modidier being pressed, so we need to look at the keycode directly.
                     */
                    if (xevent->xkey.keycode == cancel)
                    {
                        c2 = tabwinSelectHead (passdata->tabwin);
                        cycling = FALSE;
                    }
                    else if (xevent->xkey.keycode == up)
                    {
                        c2 = tabwinSelectDelta(passdata->tabwin, -1, 0);
                    }
                    else if (xevent->xkey.keycode == down)
                    {
                        c2 = tabwinSelectDelta(passdata->tabwin, 1, 0);
                    }
                    else if (xevent->xkey.keycode == left)
                    {
                        c2 = tabwinSelectDelta(passdata->tabwin, 0, -1);
                    }
                    else if (xevent->xkey.keycode == right)
                    {
                        c2 = tabwinSelectDelta(passdata->tabwin, -0, 1);
                    }
                    else if (key == KEY_CYCLE_REVERSE_WINDOWS)
                    {
                        TRACE ("Cycle: previous");
                        c2 = tabwinSelectPrev(passdata->tabwin);
                    }
                    else if (key == KEY_CYCLE_WINDOWS)
                    {
                        TRACE ("Cycle: next");
                        c2 = tabwinSelectNext(passdata->tabwin);
                    }
                    if (c2)
                    {
                        c = c2;
                    }

                    /* If last key press event had not our modifiers pressed, finish cycling */
                    if (!(xevent->xkey.state & modifiers))
                    {
                        cycling = FALSE;
                    }
                }
            }
            break;
        case KeyRelease:
            {
                int keysym = XLookupKeysym (&xevent->xkey, 0);

                if (IsModifierKey(keysym))
                {
                    if (!(myScreenGetModifierPressed (screen_info) & modifiers))
                    {
                        cycling = FALSE;
                    }
                }
            }
            break;
        case ButtonPress:
            /* only accept events for the tab windows */
            for (li = passdata->tabwin->tabwin_list; li != NULL; li = li->next)
            {
                if (GDK_WINDOW_XID (gtk_widget_get_window (li->data)) == xevent->xbutton.window)
                {
                    if  (xevent->xbutton.button == Button1)
                    {
                        c2 = tabwinSelectHovered (passdata->tabwin);
                        cycling = FALSE;
                        break;
                    }
                    else if  (xevent->xbutton.button == Button4)
                    {
                        /* Mouse wheel scroll up */
                        TRACE ("Cycle: previous");
                        c2 = tabwinSelectPrev(passdata->tabwin);
                    }
                    else if (xevent->xbutton.button == Button5)
                    {
                        /* Mouse wheel scroll down */
                        TRACE ("Cycle: next");
                        c2 = tabwinSelectNext(passdata->tabwin);
                    }
                }
            }
            if (c2)
            {
                c = c2;
            }
            break;
        case EnterNotify:
        case LeaveNotify:
            /* Track whether the pointer is inside one of the tab-windows */
            for (li = passdata->tabwin->tabwin_list; li != NULL; li = li->next)
            {
                if (GDK_WINDOW_XID (gtk_widget_get_window (li->data)) == xevent->xcrossing.window)
                {
                    passdata->inside = (xevent->xcrossing.type == EnterNotify);
                }
            }
            break;
        default:
            status = EVENT_FILTER_CONTINUE;
            break;
    }

    if (!cycling)
    {
        TRACE ("event loop now finished");
        gtk_main_quit ();
    }

    if (status == EVENT_FILTER_STOP)
    {
        if (cycling)
        {
            if (c)
            {
                if (passdata->wireframe)
                {
                    wireframeUpdate (c, passdata->wireframe);
                }
            }
            else
            {
                cycling = FALSE;
            }
        }
    }

    return status;
}

static eventFilterStatus
clientCycleFlushEventFilter (XEvent * xevent, gpointer data)
{
    DisplayInfo *display_info = (DisplayInfo *) data;

    /* Update the display time */
    myDisplayUpdateCurrentTime (display_info, xevent);

    switch (xevent->type)
    {
        case EnterNotify:
            gtk_main_quit ();
            return EVENT_FILTER_STOP;
            break;
        default:
            break;
    }
    return EVENT_FILTER_CONTINUE;
}

void
clientCycle (Client * c, XKeyEvent * ev)
{
    ScreenInfo *screen_info;
    DisplayInfo *display_info;
    ClientCycleData passdata;
    GList *client_list, *selected;
    gboolean g1, g2;
    int key, modifier;

    g_return_if_fail (c != NULL);
    TRACE ("entering clientCycle");

    screen_info = c->screen_info;
    display_info = screen_info->display_info;

    client_list = clientCycleCreateList (c);
    if (!client_list)
    {
        return;
    }

    modifier = 0;
    key = myScreenGetKeyPressed (screen_info, ev);
    if (key == KEY_CYCLE_REVERSE_WINDOWS)
    {
        selected = g_list_last (client_list);
        modifier = screen_info->params->keys[KEY_CYCLE_REVERSE_WINDOWS].modifier;
    }
    else
    {
        selected = g_list_next (client_list);
        modifier = screen_info->params->keys[KEY_CYCLE_WINDOWS].modifier;
    }
    if (!selected)
    {
        /* Only one element in list */
        selected = client_list;
    }

    if (!modifier)
    {
        /*
         * The shortcut has no modifier so there's no point in entering
         * the cycle loop, just select the next or previous window and
         * that's it...
         */
        clientCycleActivate ((Client *) selected->data);
        g_list_free (client_list);
        return;
    }

    g1 = myScreenGrabKeyboard (screen_info, ev->time);
    g2 = myScreenGrabPointer (screen_info, TRUE, LeaveWindowMask, None, ev->time);

    if (!g1 || !g2)
    {
        TRACE ("grab failed in clientCycle");

        gdk_beep ();
        myScreenUngrabKeyboard (screen_info, myDisplayGetCurrentTime (display_info));
        myScreenUngrabPointer (screen_info, myDisplayGetCurrentTime (display_info));
        g_list_free (client_list);

        return;
    }

    passdata.wireframe = None;
    passdata.inside = FALSE;

    TRACE ("entering cycle loop");
    if (screen_info->params->cycle_draw_frame)
    {
        passdata.wireframe = wireframeCreate ((Client *) selected->data);
    }
    passdata.tabwin = tabwinCreate (&client_list, selected, screen_info->params->cycle_workspaces);
    eventFilterPush (display_info->xfilter, clientCycleEventFilter, &passdata);

    gtk_main ();
    eventFilterPop (display_info->xfilter);
    TRACE ("leaving cycle loop");
    if (passdata.wireframe)
    {
        wireframeDelete (screen_info, passdata.wireframe);
    }
    updateXserverTime (display_info);

    c = tabwinGetSelected (passdata.tabwin);
    if (c)
    {
        clientCycleActivate (c);
    }

    tabwinDestroy (passdata.tabwin);
    g_free (passdata.tabwin);
    g_list_free (client_list);

    if (passdata.inside)
    {
        /* A bit of a hack, flush EnterNotify if the pointer is inside
         * the tabwin to defeat focus-follow-mouse tracking */
        eventFilterPush (display_info->xfilter, clientCycleFlushEventFilter, display_info);
        gtk_main ();
        eventFilterPop (display_info->xfilter);
    }

    myScreenUngrabKeyboard (screen_info, myDisplayGetCurrentTime (display_info));
    myScreenUngrabPointer (screen_info, myDisplayGetCurrentTime (display_info));
}

gboolean
clientSwitchWindow (void)
{
    Client *focus, *new;
    guint range;

    focus = clientGetFocus();
    if (!focus)
    {
        return FALSE;
    }

    range = clientGetCycleRange (focus->screen_info);
    new = clientGetPrevious(focus, range | SEARCH_SAME_APPLICATION, WINDOW_REGULAR_FOCUSABLE);
    if (new)
    {
        clientCycleFocusAndRaise (new);
        return TRUE;
    }
    return FALSE;
}

gboolean
clientSwitchApp (void)
{
    Client *focus, *new;
    guint range;

    TRACE ("entering clientSwitchApp");

    focus = clientGetFocus();
    if (!focus)
    {
        return FALSE;
    }

    range = clientGetCycleRange (focus->screen_info);
    /* We do not want dialogs, just toplevel app windows here */
    new = clientGetPrevious(focus, range | SEARCH_DIFFERENT_APPLICATION, WINDOW_NORMAL);
    if (new)
    {
        clientCycleFocusAndRaise (new);
        return TRUE;
    }
    return FALSE;
}
