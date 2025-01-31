/*******************************************************************************
 * thunar-dropbox
 *
 * tdp-provider.c
 *
 * Copyright © 2010-2018 Maato
 * Copyright © 2019 Jeinzi
 *
 * Authors:
 *    Maato <maato@softwarebakery.com>
 *    Jeinzi <jeinzi@gmx.de>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>

#include "tdp-provider.h"
#include "dropbox-communication.h"


/***************************** Function prototypes ****************************/

static void tdp_provider_menu_provider_init(ThunarxMenuProviderIface * iface);
static void tdp_provider_finalize(GObject * object);
static GList * tdp_provider_get_file_actions(
    ThunarxMenuProvider * menu_provider,
    GtkWidget * window,
    GList * files
);


/*********************************** GObject **********************************/

struct _TdpProviderClass
{
    GObjectClass __parent__;
};

struct _TdpProvider
{
    GObject __parent__;
};

/*********************************** Thunarx **********************************/

THUNARX_DEFINE_TYPE_WITH_CODE (
    TdpProvider,
    tdp_provider,
    G_TYPE_OBJECT,
    THUNARX_IMPLEMENT_INTERFACE (
        THUNARX_TYPE_MENU_PROVIDER,
        tdp_provider_menu_provider_init
    )
)


/************************************ Other ***********************************/

static void tdp_provider_class_init(TdpProviderClass * class)
{
    GObjectClass * gobject_class;
    gobject_class = G_OBJECT_CLASS(class);
    gobject_class->finalize = tdp_provider_finalize;
}

static void tdp_provider_menu_provider_init(ThunarxMenuProviderIface * iface)
{
    iface->get_file_menu_items = tdp_provider_get_file_actions;
}

static void tdp_provider_init(TdpProvider * tdp_provider)
{
    // Suppress -Wunused-parameter warning.
    (void)tdp_provider;
}

static void tdp_provider_finalize(GObject * object)
{
    TDP_PROVIDER(object);
    (*G_OBJECT_CLASS(tdp_provider_parent_class)->finalize)(object);
}

static void tdp_callback(ThunarxMenuItem * item, gpointer data)
{
    // Suppress -Wunused-parameter warning.
    (void)item;

    GList * actioninfo = (GList*)data;
    gchar * verb = NULL;

    if (actioninfo == NULL)
        return;

    verb = actioninfo->data;
    actioninfo = actioninfo->next;

    dropbox_do_verb(verb, actioninfo);
}

static void tdp_closure_destroy_notify(gpointer data, GClosure * closure)
{
    // Suppress -Wunused-parameter warning.
    (void)closure;

    GList * actioninfo = (GList*)data;
    GList * lp;

    for (lp = actioninfo; lp != NULL; lp = lp->next) {
        g_free(lp->data);
    }

    g_list_free(actioninfo);
}

static void add_action(ThunarxMenu * menu, GList * filelist, gchar * str)
{
    ThunarxMenuItem * item = NULL;
    gchar ** argval;
    guint len;
    GList * actioninfo = NULL;
    GList * iter;

    for (iter = filelist; iter != NULL; iter = iter->next) {
        actioninfo = g_list_append(actioninfo, g_strdup(iter->data));
    }

    argval = g_strsplit(str, "~", 0);
    len = g_strv_length(argval);

    if (len == 3) {
        gchar unique_name[128];
        g_sprintf(unique_name, "Tdp::%s", argval[2]);

        item = thunarx_menu_item_new(
            unique_name,
            argval[0],
            argval[1],
            "thunar-dropbox"
        );

        actioninfo = g_list_prepend(actioninfo, g_strdup(argval[2]));

        GClosure * closure = g_cclosure_new(
            G_CALLBACK(tdp_callback),
            (gpointer)actioninfo,
            tdp_closure_destroy_notify
        );

        g_signal_connect_closure(G_OBJECT(item), "activate", closure, TRUE);
    }

    g_strfreev(argval);
    thunarx_menu_append_item(menu, item);
}

static GList * tdp_provider_get_file_actions(
    ThunarxMenuProvider * menu_provider,
    GtkWidget * window,
    GList * files)
{
    // Suppress -Wunused-parameter warning.
    (void)menu_provider;
    (void)window;

    ThunarxMenu * menu = thunarx_menu_new();
    gchar * path;
    GFile * file;
    GList * actions = NULL;
    GList * lp;
    GList * filelist = NULL;

    int socket;
    if (!dropbox_connect(&socket))
        return NULL;

    GIOChannel * io_channel = g_io_channel_unix_new(socket);
    g_io_channel_set_close_on_unref(io_channel, TRUE);
    g_io_channel_set_line_term(io_channel, "\n", -1);

    dropbox_write(io_channel, "icon_overlay_context_options\n");
    dropbox_write(io_channel, "paths");

    for (lp = files; lp != NULL; lp = lp->next) {
        file = thunarx_file_info_get_location(lp->data);
        path = g_file_get_path(file);
        g_object_unref (file);
        if (path == NULL)
            continue;

        if (!g_utf8_validate(path, -1, NULL))
            continue;

        char *real_path = realpath(path, NULL);
        if (real_path) {
            dropbox_write(io_channel, "\t");
            dropbox_write(io_channel, real_path);
            free(real_path);
            real_path = NULL;
        } else {
            dropbox_write(io_channel, "\t");
            dropbox_write(io_channel, path);
        }

        filelist = g_list_append(filelist, path);
    }

    dropbox_write(io_channel, "\ndone\n");
    g_io_channel_flush(io_channel, NULL);

    int n_items = 0;
    for (;;) {
        gchar * line;
        GIOStatus status = g_io_channel_read_line(io_channel, &line,
            NULL, NULL, NULL);

        if (status == G_IO_STATUS_NORMAL) {
            if (g_strcmp0(line, "done\n") == 0) {
                g_free(line);
                break;
            }
            else if (g_strcmp0(line, "notok\n") == 0) {}
            else if (g_strcmp0(line, "ok\n") == 0) {}
            else {
                gchar ** argval;
                guint len;

                argval = g_strsplit(line, "\t", 0);
                len = g_strv_length(argval);

                if (len > 1) {
                    // First array element is an "options"-tag.
                    guint i;
                    for (i = 1; i < len; ++i) {
                        add_action(menu, filelist, argval[i]);
                        ++n_items;
                    }
                }

                g_strfreev(argval);
            }

            g_free(line);
        }
        else if (status == G_IO_STATUS_AGAIN) {
            continue;
        }
        else if (status == G_IO_STATUS_ERROR) {
            break;
        }
    }

    if (n_items > 1) {
        ThunarxMenuItem * menu_root = thunarx_menu_item_new(
            "Tdp::menu_root",
            "Dropbox",
            "",
            "thunar-dropbox"
        );
        thunarx_menu_item_set_menu(menu_root, menu);
        actions = g_list_append(actions, menu_root);
    }
    else if (n_items == 1) {
        actions = thunarx_menu_get_items(menu);
    }


    for (lp = filelist; lp != NULL; lp = lp->next) {
        g_free(lp->data);
    }
    g_list_free(filelist);

    g_io_channel_unref(io_channel);
    return actions;
}
