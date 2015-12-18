/*
 * A GtkImageMenuItem subclass that handles menu items that are
 * intended to represent launchable applications.
 *
 * Copyright (c) 2004-2007,2009 Brian Tarricone <bjt23@cornell.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <exo/exo.h>
#include <xfconf/xfconf.h>

#include "xfdesktop-app-menu-item.h"
#include "xfdesktop-common.h"

struct _XfdesktopAppMenuItem
{
    GtkImageMenuItem parent;

    GarconMenuItem *item;

    GtkWidget *accel_label;
};

typedef struct _XfdesktopAppMenuItemClass
{
    GtkImageMenuItemClass parent;
} XfdesktopAppMenuItemClass;

enum
{
    PROP_0,
    PROP_ITEM
};


static void xfdesktop_app_menu_item_set_property(GObject *object,
                                                 guint prop_id,
                                                 const GValue *value,
                                                 GParamSpec *pspec);
static void xfdesktop_app_menu_item_get_property(GObject *object,
                                                 guint prop_id,
                                                 GValue *value,
                                                 GParamSpec *pspec);
static void xfdesktop_app_menu_item_finalize(GObject *object);

static void xfdesktop_app_menu_item_changed(XfdesktopAppMenuItem *app_menu_item);

static void xfdesktop_app_menu_item_activate(XfdesktopAppMenuItem *app_menu_item);


G_DEFINE_TYPE(XfdesktopAppMenuItem, xfdesktop_app_menu_item, GTK_TYPE_IMAGE_MENU_ITEM)



static gboolean global_show_icons = TRUE;



static void
xfdesktop_app_menu_item_class_init(XfdesktopAppMenuItemClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;

    gobject_class->finalize = xfdesktop_app_menu_item_finalize;
    gobject_class->set_property = xfdesktop_app_menu_item_set_property;
    gobject_class->get_property = xfdesktop_app_menu_item_get_property;

    g_object_class_install_property(gobject_class, PROP_ITEM,
                                    g_param_spec_object("item", NULL, NULL,
                                                        GARCON_TYPE_MENU_ITEM,
                                                        G_PARAM_STATIC_STRINGS
                                                        | G_PARAM_READWRITE
                                                        | G_PARAM_CONSTRUCT_ONLY));
}

static void
xfdesktop_app_menu_item_init(XfdesktopAppMenuItem *app_menu_item)
{
    g_signal_connect(G_OBJECT(app_menu_item), "activate",
                     G_CALLBACK(xfdesktop_app_menu_item_activate), NULL);
}

static void
xfdesktop_app_menu_item_set_property(GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
    XfdesktopAppMenuItem *app_menu_item = XFDESKTOP_APP_MENU_ITEM(object);

    switch(prop_id) {
        case PROP_ITEM:
            if(app_menu_item->item) {
                g_signal_handlers_disconnect_by_func(G_OBJECT(app_menu_item->item),
                     G_CALLBACK(xfdesktop_app_menu_item_changed), app_menu_item);
                g_object_unref(G_OBJECT(app_menu_item->item));
            }
            app_menu_item->item = g_value_dup_object(value);
            g_signal_connect_swapped(G_OBJECT(app_menu_item->item), "changed",
                                     G_CALLBACK(xfdesktop_app_menu_item_changed), app_menu_item);
            xfdesktop_app_menu_item_changed (app_menu_item);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
xfdesktop_app_menu_item_get_property(GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
    XfdesktopAppMenuItem *app_menu_item = XFDESKTOP_APP_MENU_ITEM(object);

    switch(prop_id) {
        case PROP_ITEM:
            g_value_set_object(value, app_menu_item->item);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
xfdesktop_app_menu_item_finalize(GObject *object)
{
    XfdesktopAppMenuItem *app_menu_item = XFDESKTOP_APP_MENU_ITEM(object);

    g_return_if_fail(app_menu_item != NULL);

    if(app_menu_item->item) {
        g_signal_handlers_disconnect_by_func(G_OBJECT(app_menu_item->item),
                     G_CALLBACK(xfdesktop_app_menu_item_changed), app_menu_item);
        g_object_unref(G_OBJECT(app_menu_item->item));
    }

    G_OBJECT_CLASS(xfdesktop_app_menu_item_parent_class)->finalize(object);
}

static void
xfdesktop_app_menu_item_set_icon(XfdesktopAppMenuItem *app_menu_item)
{
    const gchar *icon_name;
    gint w, h, size;
    GdkPixbuf *pixbuf = NULL;
    GtkWidget *image = NULL;
    GtkIconTheme *icon_theme;
    gchar *p, *name = NULL;
    gchar *filename;

    icon_name = garcon_menu_item_get_icon_name(app_menu_item->item);
    icon_theme = gtk_icon_theme_get_default();

    if(G_LIKELY(icon_name)) {
        gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &w, &h);
        size = MIN(w, h);

        if(gtk_icon_theme_has_icon(icon_theme, icon_name))
            image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
        else {
            if (g_path_is_absolute(icon_name)) {
                pixbuf = gdk_pixbuf_new_from_file_at_scale(icon_name, w, h, TRUE, NULL);
            } else {
                /* try to lookup names like application.png in the theme */
                p = strrchr(icon_name, '.');
                if (p) {
                    name = g_strndup(icon_name, p - icon_name);
                    pixbuf = gtk_icon_theme_load_icon(icon_theme, name, size, 0, NULL);
                    g_free (name);
                    name = NULL;
                }

                /* maybe they point to a file in the pixbufs folder */
                if (G_UNLIKELY(pixbuf == NULL)) {
                    filename = g_build_filename("pixmaps", icon_name, NULL);
                    name = xfce_resource_lookup(XFCE_RESOURCE_DATA, filename);
                    g_free(filename);
                }

                if(name) {
                    pixbuf = gdk_pixbuf_new_from_file_at_scale(name, w, h, TRUE, NULL);
                    g_free(name);
                }
            }

            /* Turn the pixbuf into a gtk_image */
            if(G_LIKELY(pixbuf)) {
                /* scale the pixbuf down if it needs it */
                GdkPixbuf *tmp = exo_gdk_pixbuf_scale_down(pixbuf, TRUE, w, h);
                g_object_unref(pixbuf);
                pixbuf = tmp;

                image = gtk_image_new_from_pixbuf(pixbuf);
                g_object_unref(G_OBJECT(pixbuf));
            }
        }
    }

    if(!GTK_IS_IMAGE(image))
        image = gtk_image_new();

    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(app_menu_item), image);
}

static void
xfdesktop_app_menu_item_changed(XfdesktopAppMenuItem *app_menu_item)
{
    const gchar *label;

    g_return_if_fail(XFCE_IS_APP_MENU_ITEM(app_menu_item));
    g_return_if_fail(GARCON_IS_MENU_ITEM(app_menu_item->item));

    if(global_show_icons)
        xfdesktop_app_menu_item_set_icon(app_menu_item);

    label = garcon_menu_item_get_name(app_menu_item->item);
    if (G_UNLIKELY (label == NULL))
      label = "";

    gtk_menu_item_set_label(GTK_MENU_ITEM(app_menu_item), label);
}

static void
xfdesktop_app_menu_item_append_quote (GString     *string,
                                      const gchar *unquoted)
{
  gchar *quoted;

  quoted = g_shell_quote(unquoted);
  g_string_append(string, quoted);
  g_free(quoted);
}

static gchar *
xfdesktop_app_menu_item_command(XfdesktopAppMenuItem *app_menu_item)
{
    GString *newstr;
    const gchar *p;
    const gchar *command;
    const gchar *var;
    gchar *uri, *filename;

    command = garcon_menu_item_get_command(app_menu_item->item);
    if(command == NULL)
        return NULL;

    newstr = g_string_sized_new(100);

    for(p = command; *p; ++p) {
        if('%' == *p) {
            ++p;
            switch(*p) {
                /* we don't care about these since we aren't passing filenames */
                case 'f':
                case 'F':
                case 'u':
                case 'U':
                /* these are all deprecated */
                case 'd':
                case 'D':
                case 'n':
                case 'N':
                case 'v':
                case 'm':
                    break;

                case 'i':
                    var = garcon_menu_item_get_icon_name(app_menu_item->item);
                    if(G_LIKELY(var)) {
                        g_string_append(newstr, "--icon ");
                        xfdesktop_app_menu_item_append_quote(newstr, var);
                    }
                    break;

                case 'c':
                    var = garcon_menu_item_get_name(app_menu_item->item);
                    if(G_LIKELY(var))
                        xfdesktop_app_menu_item_append_quote(newstr, var);
                    break;

                case 'k':
                    uri = garcon_menu_item_get_uri(app_menu_item->item);
                    if(G_LIKELY(uri)) {
                        filename = g_filename_from_uri(uri, NULL, NULL);
                        xfdesktop_app_menu_item_append_quote(newstr, filename);
                        g_free(filename);
                    }
                    g_free(uri);
                    break;

                case '%':
                    g_string_append_c(newstr, '%');
                    break;

                default:
                    g_warning("Invalid field code in Exec line: %%%c", *p);
                    break;
            }
        } else
            g_string_append_c(newstr, *p);
    }

    return g_string_free(newstr, FALSE);
}

static gboolean
xfdesktop_app_menu_item_edit_launcher(XfdesktopAppMenuItem *app_menu_item)
{
    GFile *file;
    gchar *uri, *cmd;
    GError *error = NULL;
    gboolean ret = FALSE;

    file = garcon_menu_item_get_file(app_menu_item->item);

    if(file) {
        uri = g_file_get_uri(file);
        cmd = g_strdup_printf("exo-desktop-item-edit \"%s\"", uri);

        if(!xfce_spawn_command_line_on_screen(NULL, cmd, FALSE, FALSE, &error)) {
            xfce_message_dialog(NULL, _("Launch Error"),
                                GTK_STOCK_DIALOG_ERROR,
                                _("Unable to launch \"exo-desktop-item-edit\", which is required to create and edit launchers and links on the desktop."),
                                error->message, GTK_STOCK_CLOSE,
                                GTK_RESPONSE_ACCEPT, NULL);
            g_error_free(error);
        } else {
            ret = TRUE;
        }

        g_free(uri);
        g_free(cmd);
        g_object_unref(file);
    }

    return ret;
}

static void
xfdesktop_app_menu_item_activate (XfdesktopAppMenuItem *app_menu_item)
{
   XfconfChannel *channel;
   gchar *command;
   GdkEventButton *evt;
   guint button;
   gboolean right_click_edits;
   GError *error = NULL;

   TRACE("entering");

   command = xfdesktop_app_menu_item_command(app_menu_item);
   if (command == NULL)
       return;

   evt = (GdkEventButton *)gtk_get_current_event();

   channel = xfconf_channel_get (XFDESKTOP_CHANNEL);
   right_click_edits = xfconf_channel_get_bool (channel,
                                                "/desktop-icons/right-click-edits-menu",
                                                TRUE);

   /* See if we're trying to edit the launcher */
   if(evt && GDK_BUTTON_RELEASE == evt->type) {
       button = evt->button;

       /* right click can optionally edit launchers. Shift + left always will */
       if((button == 3 && right_click_edits) || (button == 1 && (evt->state & GDK_SHIFT_MASK))) {
            if(xfdesktop_app_menu_item_edit_launcher(app_menu_item)) {
                gdk_event_free((GdkEvent*)evt);
                return;
            }
       }
   }

   if(!xfce_spawn_command_line_on_screen(gtk_widget_get_screen(GTK_WIDGET(app_menu_item)),
                                         command,
                                         garcon_menu_item_requires_terminal(app_menu_item->item),
                                         garcon_menu_item_supports_startup_notification(app_menu_item->item),
                                         &error)) {
        g_warning("XfdesktopAppMenuItem: unable to spawn %s: %s",
                 command, error->message);
        g_error_free(error);
    }

    gdk_event_free((GdkEvent*)evt);
}

GtkWidget *
xfdesktop_app_menu_item_new (GarconMenuItem *item)
{
    g_return_val_if_fail(GARCON_IS_MENU_ITEM(item), NULL);

    return g_object_new (XFDESKTOP_TYPE_APP_MENU_ITEM,
                         "item", item, NULL);
}


void
xfdesktop_app_menu_item_set_show_icon (gboolean show_icon)
{
    global_show_icons = show_icon;
}
