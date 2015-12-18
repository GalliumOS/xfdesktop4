/*
 *  xfdesktop - xfce4's desktop manager
 *
 *  Copyright (c) 2006-2009 Brian Tarricone, <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_MATH_H
#include <math.h>
#endif

#include <glib-object.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <exo/exo.h>

#include "xfdesktop-icon-view.h"
#include "xfdesktop-file-icon-manager.h"
#include "xfdesktop-marshal.h"
#include "xfce-desktop.h"
#include "xfdesktop-volume-icon.h"
#include "xfdesktop-common.h"
#include "gtkcairoblurprivate.h"

#include <libwnck/libwnck.h>
#include <libxfce4ui/libxfce4ui.h>
#include <xfconf/xfconf.h>

#define DEFAULT_FONT_SIZE     12
#define DEFAULT_TOOLTIP_SIZE 128
#define MAX_TOOLTIP_SIZE     512

#define ICON_SIZE         (icon_view->priv->icon_size)
#define TEXT_WIDTH        ((icon_view->priv->cell_text_width_proportion) * ICON_SIZE)
#define ICON_WIDTH        (TEXT_WIDTH)
#define CELL_PADDING      (icon_view->priv->cell_padding)
#define CELL_SIZE         (TEXT_WIDTH + CELL_PADDING * 2)
#define SPACING           (icon_view->priv->cell_spacing)
#define LABEL_RADIUS      (icon_view->priv->label_radius)
#define SHADOW_EXTENTS    (icon_view->priv->shadow_extents)
#define SHADOW_X_OFFSET   MAX(icon_view->priv->shadow_x_offset, icon_view->priv->selected_shadow_x_offset)
#define SHADOW_Y_OFFSET   MAX(icon_view->priv->shadow_y_offset, icon_view->priv->selected_shadow_y_offset)
#define TEXT_HEIGHT       (CELL_SIZE - ICON_SIZE - SPACING - (CELL_PADDING * 2) - LABEL_RADIUS)
#define MIN_MARGIN        8
#define DEFAULT_RUBBERBAND_ALPHA  64

#if defined(DEBUG) && DEBUG > 0
#define DUMP_GRID_LAYOUT(icon_view) \
{\
    gint my_i, my_maxi;\
    \
    DBG("grid layout dump:"); \
    my_maxi = icon_view->priv->nrows * icon_view->priv->ncols;\
    for(my_i = 0; my_i < my_maxi; my_i++)\
        g_printerr("%c ", icon_view->priv->grid_layout[my_i] ? '1' : '0');\
    g_printerr("\n\n");\
}
#else
#define DUMP_GRID_LAYOUT(icon_view)
#endif

enum
{
    SIG_ICON_SELECTION_CHANGED = 0,
    SIG_ICON_ACTIVATED,
    SIG_SELECT_ALL,
    SIG_UNSELECT_ALL,
    SIG_SELECT_CURSOR_ITEM,
    SIG_TOGGLE_CURSOR_ITEM,
    SIG_MOVE_CURSOR,
    SIG_ACTIVATE_CURSOR_ITEM,
    SIG_RESIZE_EVENT,
    SIG_N_SIGNALS,
};

typedef struct
{
    XfdesktopIconView *icon_view;
    XfdesktopIcon *icon;
    GdkRectangle area;
    guint source_id;
} XfdesktopIdleRepaintData;

struct _XfdesktopIconViewPrivate
{
    XfdesktopIconViewManager *manager;
    
    GtkWidget *parent_window;
    
    guint icon_size;
    gdouble font_size;
    
    WnckScreen *wnck_screen;
    PangoLayout *playout;
    
    GList *pending_icons;
    GList *icons;
    GList *selected_icons;
    
    gint xorigin;
    gint yorigin;
    gint width;
    gint height;

    gint xmargin;
    gint ymargin;
    gint xspacing;
    gint yspacing;
    
    gint16 nrows;
    gint16 ncols;
    XfdesktopIcon **grid_layout;
    
    guint grid_resize_timeout;
    
    GtkSelectionMode sel_mode;
    guint maybe_begin_drag:1,
          definitely_dragging:1,
          allow_rubber_banding:1,
          definitely_rubber_banding:1;
    gint press_start_x;
    gint press_start_y;
    GdkRectangle band_rect;

    XfconfChannel *channel;

    GdkColor *selection_box_color;
    guchar selection_box_alpha;
    
    XfdesktopIcon *cursor;
    XfdesktopIcon *first_clicked_item;
    XfdesktopIcon *item_under_pointer;
    
    GtkTargetList *native_targets;
    GtkTargetList *source_targets;
    GtkTargetList *dest_targets;
    
    gboolean drag_source_set;
    GdkDragAction foreign_source_actions;
    GdkModifierType foreign_source_mask;
    
    gboolean drag_dest_set;
    GdkDragAction foreign_dest_actions;

    gboolean dropped;
    GdkDragAction proposed_drop_action;
    gint16 hover_row, hover_col;
    
    guchar    label_alpha;
    guchar    selected_label_alpha;

    gchar     shadow_x_offset;
    gchar     shadow_y_offset;
    GdkColor *shadow_color;
    gchar     selected_shadow_x_offset;
    gchar     selected_shadow_y_offset;
    GdkColor *selected_shadow_color;
    gchar     shadow_blur_radius;

    gint cell_padding;
    gint cell_spacing;
    gdouble label_radius;
    gdouble cell_text_width_proportion;
    gint shadow_extents;

    gboolean ellipsize_icon_labels;

    /* tooltip options - There's a style and an xfconf property.
     * The order is xfconf, style, with a fall back of DEFAULT_TOOLTIP_SIZE.
     * xfdesktop_icon_view_get_tooltip_size will return the correct size. */
    gboolean show_tooltips;
    gint   tooltip_size_from_style;
    double tooltip_size_from_xfconf;

    gboolean single_click;
};

static void xfce_icon_view_set_property(GObject *object,
                                        guint property_id,
                                        const GValue *value,
                                        GParamSpec *pspec);
static void xfce_icon_view_get_property(GObject *object,
                                        guint property_id,
                                        GValue *value,
                                        GParamSpec *pspec);

static gboolean xfdesktop_icon_view_button_press(GtkWidget *widget,
                                                 GdkEventButton *evt,
                                                 gpointer user_data);
static gboolean xfdesktop_icon_view_button_release(GtkWidget *widget,
                                                   GdkEventButton *evt,
                                                   gpointer user_data);
static gboolean xfdesktop_icon_view_key_press(GtkWidget *widget,
                                              GdkEventKey *evt,
                                              gpointer user_data);
static gboolean xfdesktop_icon_view_focus_in(GtkWidget *widget,
                                             GdkEventFocus *evt,
                                             gpointer user_data);
static gboolean xfdesktop_icon_view_focus_out(GtkWidget *widget,
                                              GdkEventFocus *evt,
                                              gpointer user_data);
static gboolean xfdesktop_icon_view_motion_notify(GtkWidget *widget,
                                                  GdkEventMotion *evt,
                                                  gpointer user_data);
static gboolean xfdesktop_icon_view_leave_notify(GtkWidget *widget,
                                                 GdkEventCrossing *evt,
                                                 gpointer user_data);
static void xfdesktop_icon_view_style_set(GtkWidget *widget,
                                          GtkStyle *previous_style);
static void xfdesktop_icon_view_realize(GtkWidget *widget);
static void xfdesktop_icon_view_unrealize(GtkWidget *widget);
static gboolean xfdesktop_icon_view_expose(GtkWidget *widget,
                                           GdkEventExpose *evt);
static void xfdesktop_icon_view_drag_begin(GtkWidget *widget,
                                           GdkDragContext *contest);
static gboolean xfdesktop_icon_view_drag_motion(GtkWidget *widget,
                                                GdkDragContext *context,
                                                gint x,
                                                gint y,
                                                guint time_);
static gboolean xfdesktop_icon_view_drag_drop(GtkWidget *widget,
                                              GdkDragContext *context,
                                              gint x,
                                              gint y,
                                              guint time_);
static void xfdesktop_icon_view_drag_data_get(GtkWidget *widget,
                                              GdkDragContext *context,
                                              GtkSelectionData *data,
                                              guint info,
                                              guint time_);
static void xfdesktop_icon_view_drag_data_received(GtkWidget *widget,
                                                   GdkDragContext *context,
                                                   gint x,
                                                   gint y,
                                                   GtkSelectionData *data,
                                                   guint info,
                                                   guint time_);
                                                      
static void xfdesktop_icon_view_finalize(GObject *obj);

static void xfdesktop_icon_view_add_move_binding(GtkBindingSet *binding_set,
                                                 guint keyval,
                                                 guint modmask,
                                                 GtkMovementStep step,
                                                 gint count);

static gboolean xfdesktop_icon_view_update_icon_extents(XfdesktopIconView *icon_view,
                                                        XfdesktopIcon *icon,
                                                        GdkRectangle *pixbuf_extents,
                                                        GdkRectangle *text_extents,
                                                        GdkRectangle *total_extents,
                                                        gint *rtl_offset);
static void xfdesktop_icon_view_invalidate_icon(XfdesktopIconView *icon_view,
                                                XfdesktopIcon *icon,
                                                gboolean recalc_extents);
static void xfdesktop_icon_view_icon_changed(XfdesktopIcon *icon,
                                             gpointer user_data);

static void xfdesktop_icon_view_invalidate_icon_pixbuf(XfdesktopIconView *icon_view,
                                                       XfdesktopIcon *icon);

static void xfdesktop_icon_view_paint_icon(XfdesktopIconView *icon_view,
                                           XfdesktopIcon *icon,
                                           GdkRectangle *area);
static void xfdesktop_icon_view_repaint_icons(XfdesktopIconView *icon_view,
                                              GdkRectangle *area);
                                  
static void xfdesktop_setup_grids(XfdesktopIconView *icon_view);
static gboolean xfdesktop_grid_get_next_free_position(XfdesktopIconView *icon_view,
                                                      gint16 *row,
                                                      gint16 *col);
static inline gboolean xfdesktop_grid_is_free_position(XfdesktopIconView *icon_view,
                                                       gint16 row,
                                                       gint16 col);
static inline void xfdesktop_grid_set_position_free(XfdesktopIconView *icon_view,
                                                    gint16 row,
                                                    gint16 col);
static inline gboolean xfdesktop_grid_unset_position_free_raw(XfdesktopIconView *icon_view,
                                                              gint16 row,
                                                              gint16 col,
                                                              gpointer data);
static inline gboolean xfdesktop_grid_unset_position_free(XfdesktopIconView *icon_view,
                                                          XfdesktopIcon *icon);
static inline XfdesktopIcon *xfdesktop_icon_view_icon_in_cell_raw(XfdesktopIconView *icon_view,
                                                                  gint idx);
static inline XfdesktopIcon *xfdesktop_icon_view_icon_in_cell(XfdesktopIconView *icon_view,
                                                              gint16 row,
                                                              gint16 col);
static gint xfdesktop_check_icon_clicked(gconstpointer data,
                                         gconstpointer user_data);
static void xfdesktop_list_foreach_invalidate(gpointer data,
                                              gpointer user_data);

static inline void xfdesktop_xy_to_rowcol(XfdesktopIconView *icon_view,
                                          gint x,
                                          gint y,
                                          gint16 *row,
                                          gint16 *col);
static gboolean xfdesktop_grid_resize_timeout(gpointer user_data);
static void xfdesktop_screen_size_changed_cb(GdkScreen *gscreen,
                                             gpointer user_data);
static GdkFilterReturn xfdesktop_rootwin_watch_workarea(GdkXEvent *gxevent,
                                                        GdkEvent *event,
                                                        gpointer user_data);
static void xfdesktop_move_all_icons_to_pending_icons_list(XfdesktopIconView *icon_view);
static void xfdesktop_move_all_pending_icons_to_desktop(XfdesktopIconView* icon_view);
static void xfdesktop_grid_do_resize(XfdesktopIconView *icon_view);
static inline gboolean xfdesktop_rectangle_contains_point(GdkRectangle *rect,
                                                          gint x,
                                                          gint y);
static void xfdesktop_icon_view_modify_font_size(XfdesktopIconView *icon_view,
                                                 gdouble size);
static void xfdesktop_icon_view_add_item_internal(XfdesktopIconView *icon_view,
                                                  XfdesktopIcon *icon);
static gboolean xfdesktop_icon_view_icon_find_position(XfdesktopIconView *icon_view,
                                                       XfdesktopIcon *icon);
static gboolean xfdesktop_icon_view_shift_area_to_cell(XfdesktopIconView *icon_view,
                                                       XfdesktopIcon *icon,
                                                       GdkRectangle *text_area);
static gint xfdesktop_icon_view_get_tooltip_size(XfdesktopIconView *icon_view);
static gboolean xfdesktop_icon_view_show_tooltip(GtkWidget *widget,
                                                 gint x,
                                                 gint y,
                                                 gboolean keyboard_tooltip,
                                                 GtkTooltip *tooltip,
                                                 gpointer user_data);

static gboolean xfdesktop_icon_view_is_icon_selected(XfdesktopIconView *icon_view,
                                                     XfdesktopIcon *icon);
static void xfdesktop_icon_view_real_select_all(XfdesktopIconView *icon_view);
static void xfdesktop_icon_view_real_unselect_all(XfdesktopIconView *icon_view);
static void xfdesktop_icon_view_real_select_cursor_item(XfdesktopIconView *icon_view);
static void xfdesktop_icon_view_real_toggle_cursor_item(XfdesktopIconView *icon_view);
static gboolean xfdesktop_icon_view_real_activate_cursor_item(XfdesktopIconView *icon_view);
static gboolean xfdesktop_icon_view_real_move_cursor(XfdesktopIconView *icon_view,
                                                     GtkMovementStep step,
                                                     gint count);

static void xfdesktop_icon_view_move_cursor_left_right(XfdesktopIconView *icon_view,
                                                       gint count,
                                                       GdkModifierType modmask);
static void xfdesktop_icon_view_select_between(XfdesktopIconView *icon_view,
                                               XfdesktopIcon *start_icon,
                                               XfdesktopIcon *end_icon);

enum
{
    TARGET_XFDESKTOP_ICON = 9999,
};

enum
{
    PROP_0 = 0,
    PROP_SINGLE_CLICK,
    PROP_SHOW_TOOLTIPS,
    PROP_TOOLTIP_SIZE,
};


static const GtkTargetEntry icon_view_targets[] = {
    { "XFDESKTOP_ICON", GTK_TARGET_SAME_APP, TARGET_XFDESKTOP_ICON }
};
static const gint icon_view_n_targets = 1;

static guint __signals[SIG_N_SIGNALS] = { 0, };

static GQuark xfdesktop_cell_highlight_quark = 0;


G_DEFINE_TYPE(XfdesktopIconView, xfdesktop_icon_view, GTK_TYPE_WIDGET)


static void
xfdesktop_icon_view_class_init(XfdesktopIconViewClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;
    GtkBindingSet *binding_set;

    binding_set = gtk_binding_set_by_class(klass);
    
    g_type_class_add_private(klass, sizeof(XfdesktopIconViewPrivate));
    
    gobject_class->finalize = xfdesktop_icon_view_finalize;
    gobject_class->set_property = xfce_icon_view_set_property;
    gobject_class->get_property = xfce_icon_view_get_property;
    
    widget_class->style_set = xfdesktop_icon_view_style_set;
    widget_class->realize = xfdesktop_icon_view_realize;
    widget_class->unrealize = xfdesktop_icon_view_unrealize;
    widget_class->expose_event = xfdesktop_icon_view_expose;
    widget_class->drag_begin = xfdesktop_icon_view_drag_begin;
    widget_class->drag_motion = xfdesktop_icon_view_drag_motion;
    widget_class->drag_drop = xfdesktop_icon_view_drag_drop;
    widget_class->drag_data_get = xfdesktop_icon_view_drag_data_get;
    widget_class->drag_data_received = xfdesktop_icon_view_drag_data_received;
    
    klass->select_all = xfdesktop_icon_view_real_select_all;
    klass->unselect_all = xfdesktop_icon_view_real_unselect_all;
    klass->select_cursor_item = xfdesktop_icon_view_real_select_cursor_item;
    klass->toggle_cursor_item = xfdesktop_icon_view_real_toggle_cursor_item;
    klass->activate_cursor_item = xfdesktop_icon_view_real_activate_cursor_item;  
    klass->move_cursor = xfdesktop_icon_view_real_move_cursor;

    __signals[SIG_ICON_SELECTION_CHANGED] = g_signal_new("icon-selection-changed",
                                                         XFDESKTOP_TYPE_ICON_VIEW,
                                                         G_SIGNAL_RUN_LAST,
                                                         G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                                         icon_selection_changed),
                                                         NULL, NULL,
                                                         g_cclosure_marshal_VOID__VOID,
                                                         G_TYPE_NONE, 0);
    
    __signals[SIG_ICON_ACTIVATED] = g_signal_new("icon-activated",
                                                 XFDESKTOP_TYPE_ICON_VIEW,
                                                 G_SIGNAL_RUN_LAST,
                                                 G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                                 icon_activated),
                                                 NULL, NULL,
                                                 g_cclosure_marshal_VOID__VOID,
                                                 G_TYPE_NONE, 0);

    __signals[SIG_SELECT_ALL] = g_signal_new(I_("select-all"),
                                             XFDESKTOP_TYPE_ICON_VIEW,
                                             G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                             G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                             select_all),
                                             NULL, NULL,
                                             g_cclosure_marshal_VOID__VOID,
                                             G_TYPE_NONE, 0);

    __signals[SIG_UNSELECT_ALL] = g_signal_new(I_("unselect-all"),
                                               XFDESKTOP_TYPE_ICON_VIEW,
                                               G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                               G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                               unselect_all),
                                               NULL, NULL,
                                               g_cclosure_marshal_VOID__VOID,
                                               G_TYPE_NONE, 0);

    __signals[SIG_SELECT_CURSOR_ITEM] = g_signal_new(I_("select-cursor-item"),
                                                     XFDESKTOP_TYPE_ICON_VIEW,
                                                     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                     G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                                     select_cursor_item),
                                                     NULL, NULL,
                                                     g_cclosure_marshal_VOID__VOID,
                                                     G_TYPE_NONE, 0);

    __signals[SIG_TOGGLE_CURSOR_ITEM] = g_signal_new(I_("toggle-cursor-item"),
                                                     XFDESKTOP_TYPE_ICON_VIEW,
                                                     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                     G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                                     toggle_cursor_item),
                                                     NULL, NULL,
                                                     g_cclosure_marshal_VOID__VOID,
                                                     G_TYPE_NONE, 0);

    __signals[SIG_ACTIVATE_CURSOR_ITEM] = g_signal_new(I_("activate-cursor-item"),
                                                       XFDESKTOP_TYPE_ICON_VIEW,
                                                       G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                       G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                                       activate_cursor_item),
                                                       NULL, NULL,
                                                       xfdesktop_marshal_BOOLEAN__VOID,
                                                       G_TYPE_BOOLEAN, 0);
  
    __signals[SIG_MOVE_CURSOR] = g_signal_new(I_("move-cursor"),
                                              XFDESKTOP_TYPE_ICON_VIEW,
                                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                              G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                              move_cursor),
                                              NULL, NULL,
                                              xfdesktop_marshal_BOOLEAN__ENUM_INT,
                                              G_TYPE_BOOLEAN, 2,
                                              GTK_TYPE_MOVEMENT_STEP,
                                              G_TYPE_INT);

    __signals[SIG_RESIZE_EVENT] = g_signal_new(I_("resize-event"),
                                               XFDESKTOP_TYPE_ICON_VIEW,
                                               G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                               G_STRUCT_OFFSET(XfdesktopIconViewClass,
                                                               resize_event),
                                               NULL, NULL,
                                               g_cclosure_marshal_VOID__VOID,
                                               G_TYPE_NONE, 0);

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_uchar("label-alpha",
                                                               "Label alpha",
                                                               "Alpha value for the text label's background",
                                                               0, 255, 155,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_uchar("selected-label-alpha",
                                                               "Selected label alpha",
                                                               "Alpha value for the selected text label's background",
                                                               0, 255, 155,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_char("shadow-x-offset",
                                                               "Shadow X offset",
                                                               "Shadow X offset for label text",
                                                               G_MININT8, G_MAXINT8, 0,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_char("shadow-y-offset",
                                                               "Shadow Y offset",
                                                               "Shadow Y offset for label text",
                                                               G_MININT8, G_MAXINT8, 0,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_char("selected-shadow-x-offset",
                                                               "Selected shadow X offset",
                                                               "Shadow X offset for selected label text",
                                                               G_MININT8, G_MAXINT8, 0,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_char("selected-shadow-y-offset",
                                                               "Selected shadow Y offset",
                                                               "Shadow Y offset for selected label text",
                                                               G_MININT8, G_MAXINT8, 0,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_char("shadow-blur-radius",
                                                              "shadow blur radius",
                                                              "Blur radius for label text",
                                                              G_MININT8, G_MAXINT8, 0,
                                                              G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_boxed("shadow-color",
                                                               "Shadow color",
                                                               "Color for label text shadows",
                                                               GDK_TYPE_COLOR,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_boxed("selected-shadow-color",
                                                               "Selected shadow color",
                                                               "Color for selected label text shadows",
                                                               GDK_TYPE_COLOR,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_int("cell-spacing",
                                                             "Cell spacing",
                                                             "Spacing between desktop icon cells",
                                                             0, 255, 2,
                                                             G_PARAM_READABLE));
    
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_int("cell-padding",
                                                             "Cell padding",
                                                             "Padding in desktop icon cell",
                                                             0, 255, 6,
                                                             G_PARAM_READABLE));
    
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_double("cell-text-width-proportion",
                                                                "Cell text width proportion",
                                                                "Width of text in desktop icon cell, "
                                                                "calculated as multiplier of the icon size",
                                                                1.0, 10.0, 1.9,
                                                                G_PARAM_READABLE));
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_boolean("ellipsize-icon-labels",
                                                                 "Ellipsize Icon Labels",
                                                                 "Ellipzize labels of unselected icons on desktop",
                                                                 TRUE,
                                                                 G_PARAM_READABLE));
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_double("label-radius",
                                                                "Label radius",
                                                                "The radius of the rounded corners of the text background",
                                                                0.0, 50.0, 4.0,
                                                                G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_int("tooltip-size",
                                                             "Tooltip Image Size",
                                                             "The size of the tooltip image preview",
                                                             -1, MAX_TOOLTIP_SIZE, -1,
                                                             G_PARAM_READABLE));

#define XFDESKTOP_PARAM_FLAGS  (G_PARAM_READWRITE \
                                | G_PARAM_CONSTRUCT \
                                | G_PARAM_STATIC_NAME \
                                | G_PARAM_STATIC_NICK \
                                | G_PARAM_STATIC_BLURB)

    g_object_class_install_property(gobject_class, PROP_SINGLE_CLICK,
                                    g_param_spec_boolean("single-click",
                                                         "single-click",
                                                         "single-click",
                                                         FALSE,
                                                         XFDESKTOP_PARAM_FLAGS));

    g_object_class_install_property(gobject_class, PROP_SHOW_TOOLTIPS,
                                    g_param_spec_boolean("show-tooltips",
                                                         "show tooltips",
                                                         "show tooltips for icons on the desktop",
                                                         TRUE,
                                                         XFDESKTOP_PARAM_FLAGS));

    g_object_class_install_property(gobject_class, PROP_TOOLTIP_SIZE,
                                    g_param_spec_double("tooltip-size",
                                                        "tooltip size",
                                                        "size of the tooltip image preview",
                                                        -1, MAX_TOOLTIP_SIZE, -1,
                                                        XFDESKTOP_PARAM_FLAGS));

#undef XFDESKTOP_PARAM_FLAGS

    /* same binding entries as GtkIconView */
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_a, GDK_CONTROL_MASK,
                                 "select-all", 0);
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_a,
                                 GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                                 "unselect-all", 0);
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_space, GDK_CONTROL_MASK,
                                 "toggle-cursor-item", 0);
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_KP_Space, GDK_CONTROL_MASK,
                                 "toggle-cursor-item", 0);

    gtk_binding_entry_add_signal(binding_set, GDK_KEY_space, 0,
                                 "activate-cursor-item", 0);
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_KP_Space, 0,
                                 "activate-cursor-item", 0);
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_Return, 0,
                                 "activate-cursor-item", 0);
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_ISO_Enter, 0,
                                 "activate-cursor-item", 0);
    gtk_binding_entry_add_signal(binding_set, GDK_KEY_KP_Enter, 0,
                                 "activate-cursor-item", 0);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_Up, 0,
                                         GTK_MOVEMENT_DISPLAY_LINES, -1);
    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_KP_Up, 0,
                                         GTK_MOVEMENT_DISPLAY_LINES, -1);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_Down, 0,
                                         GTK_MOVEMENT_DISPLAY_LINES, 1);
    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_KP_Down, 0,
                                         GTK_MOVEMENT_DISPLAY_LINES, 1);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_p, GDK_CONTROL_MASK,
                                         GTK_MOVEMENT_DISPLAY_LINES, -1);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_n, GDK_CONTROL_MASK,
                                         GTK_MOVEMENT_DISPLAY_LINES, 1);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_Home, 0,
                                         GTK_MOVEMENT_BUFFER_ENDS, -1);
    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KP_Home, 0,
                                         GTK_MOVEMENT_BUFFER_ENDS, -1);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_End, 0,
                                         GTK_MOVEMENT_BUFFER_ENDS, 1);
    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_KP_End, 0,
                                         GTK_MOVEMENT_BUFFER_ENDS, 1);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_Right, 0,
                                         GTK_MOVEMENT_VISUAL_POSITIONS, 1);
    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_Left, 0,
                                         GTK_MOVEMENT_VISUAL_POSITIONS, -1);

    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_KP_Right, 0,
                                         GTK_MOVEMENT_VISUAL_POSITIONS, 1);
    xfdesktop_icon_view_add_move_binding(binding_set, GDK_KEY_KP_Left, 0,
                                         GTK_MOVEMENT_VISUAL_POSITIONS, -1);

    xfdesktop_cell_highlight_quark = g_quark_from_static_string("xfdesktop-icon-view-cell-highlight");
}

static void
xfdesktop_icon_view_init(XfdesktopIconView *icon_view)
{
    icon_view->priv = G_TYPE_INSTANCE_GET_PRIVATE(icon_view,
                                                  XFDESKTOP_TYPE_ICON_VIEW,
                                                  XfdesktopIconViewPrivate);
    
    icon_view->priv->icon_size = DEFAULT_ICON_SIZE;
    icon_view->priv->font_size = DEFAULT_FONT_SIZE;

    icon_view->priv->allow_rubber_banding = TRUE;
    icon_view->priv->selection_box_alpha = DEFAULT_RUBBERBAND_ALPHA;
    
    icon_view->priv->native_targets = gtk_target_list_new(icon_view_targets,
                                                          icon_view_n_targets);
    
    icon_view->priv->source_targets = gtk_target_list_new(icon_view_targets,
                                                          icon_view_n_targets);
    gtk_drag_source_set(GTK_WIDGET(icon_view), 0, NULL, 0, GDK_ACTION_MOVE);
    
    icon_view->priv->dest_targets = gtk_target_list_new(icon_view_targets,
                                                        icon_view_n_targets);
    gtk_drag_dest_set(GTK_WIDGET(icon_view), 0, NULL, 0, GDK_ACTION_MOVE);
    
    g_object_set(G_OBJECT(icon_view), "has-tooltip", TRUE, NULL);
    g_signal_connect(G_OBJECT(icon_view), "query-tooltip",
                     G_CALLBACK(xfdesktop_icon_view_show_tooltip), NULL);
    
    gtk_widget_set_has_window(GTK_WIDGET(icon_view), FALSE);
    gtk_widget_set_can_focus(GTK_WIDGET(icon_view), TRUE);
}

static void
xfdesktop_icon_view_finalize(GObject *obj)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(obj);
    
    if(icon_view->priv->manager) {
        xfdesktop_icon_view_manager_fini(icon_view->priv->manager);
        g_object_unref(G_OBJECT(icon_view->priv->manager));
    }
    
    gtk_target_list_unref(icon_view->priv->native_targets);
    gtk_target_list_unref(icon_view->priv->source_targets);
    gtk_target_list_unref(icon_view->priv->dest_targets);
    
    g_list_foreach(icon_view->priv->pending_icons, (GFunc)g_object_unref, NULL);
    g_list_free(icon_view->priv->pending_icons);
    /* icon_view->priv->icons should be cleared in _unrealize() */

    if (icon_view->priv->channel)
        icon_view->priv->channel = NULL;

    G_OBJECT_CLASS(xfdesktop_icon_view_parent_class)->finalize(obj);
}

static void
xfce_icon_view_set_property(GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(object);

    switch(property_id) {
        case PROP_SINGLE_CLICK:
            icon_view->priv->single_click = g_value_get_boolean (value);
            break;

        case PROP_SHOW_TOOLTIPS:
            icon_view->priv->show_tooltips = g_value_get_boolean(value);
            break;

        case PROP_TOOLTIP_SIZE:
            icon_view->priv->tooltip_size_from_xfconf = g_value_get_double(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
xfce_icon_view_get_property(GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(object);

    switch(property_id) {
        case PROP_SINGLE_CLICK:
            g_value_set_boolean(value, icon_view->priv->single_click);
            break;

        case PROP_SHOW_TOOLTIPS:
            g_value_set_boolean(value, icon_view->priv->show_tooltips);
            break;

        case PROP_TOOLTIP_SIZE:
            g_value_set_double(value, icon_view->priv->tooltip_size_from_xfconf);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
xfdesktop_icon_view_add_move_binding(GtkBindingSet *binding_set,
                                     guint keyval,
                                     guint modmask,
                                     GtkMovementStep step,
                                     gint count)
{
    gtk_binding_entry_add_signal(binding_set, keyval, modmask,
                                 I_("move-cursor"), 2,
                                 G_TYPE_ENUM, step,
                                 G_TYPE_INT, count);

    gtk_binding_entry_add_signal(binding_set, keyval, GDK_SHIFT_MASK,
                                 I_("move-cursor"), 2,
                                 G_TYPE_ENUM, step,
                                 G_TYPE_INT, count);

    if(modmask & GDK_CONTROL_MASK)
        return;

    gtk_binding_entry_add_signal(binding_set, keyval,
                                 GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                                 I_("move-cursor"), 2,
                                 G_TYPE_ENUM, step,
                                 G_TYPE_INT, count);

    gtk_binding_entry_add_signal(binding_set, keyval, GDK_CONTROL_MASK,
                                 I_("move-cursor"), 2,
                                 G_TYPE_ENUM, step,
                                 G_TYPE_INT, count);
}

static gboolean
xfdesktop_icon_view_button_press(GtkWidget *widget,
                                 GdkEventButton *evt,
                                 gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    XfdesktopIcon *icon;

    TRACE("entering");

    if(evt->type == GDK_BUTTON_PRESS) {
        GList *icon_l;

        /* Let xfce-desktop handle button 2 */
        if(evt->button == 2) {
            /* If we had the grab release it so the desktop gets the event */
            if(gtk_widget_has_grab(widget))
                gtk_grab_remove(widget);

            return FALSE;
        }

        /* Grab the focus, release on button release */
        if(!gtk_widget_has_grab(widget))
            gtk_grab_add(widget);

        icon_l = g_list_find_custom(icon_view->priv->icons, evt,
                                    (GCompareFunc)xfdesktop_check_icon_clicked);
        if(icon_l && (icon = icon_l->data)) {
            if(xfdesktop_icon_view_is_icon_selected(icon_view, icon)) {
                /* clicked an already-selected icon */
                
                if(evt->state & GDK_CONTROL_MASK) {
                    /* unselect */
                    xfdesktop_icon_view_unselect_item(icon_view, icon);
                }

                icon_view->priv->cursor = icon;
            } else {
                /* clicked a non-selected icon */
                if(icon_view->priv->sel_mode != GTK_SELECTION_MULTIPLE
                   || !(evt->state & GDK_CONTROL_MASK))
                {
                    /* unselect all of the other icons if we haven't held
                     * down the ctrl key.  we'll handle shift in the next block,
                     * but for shift we do need to unselect everything */
                    xfdesktop_icon_view_unselect_all(icon_view);
                    
                    if(!(evt->state & GDK_SHIFT_MASK))
                        icon_view->priv->first_clicked_item = NULL;
                }
                
                icon_view->priv->cursor = icon;
                
                if(!icon_view->priv->first_clicked_item)
                    icon_view->priv->first_clicked_item = icon;
                
                if(icon_view->priv->sel_mode == GTK_SELECTION_MULTIPLE
                   && evt->state & GDK_SHIFT_MASK
                   && icon_view->priv->first_clicked_item
                   && icon_view->priv->first_clicked_item != icon)
                {
                    xfdesktop_icon_view_select_between(icon_view,
                                                       icon_view->priv->first_clicked_item,
                                                       icon);
                } else
                    xfdesktop_icon_view_select_item(icon_view, icon);
            }
            
            if(evt->button == 1 || evt->button == 3) {
                /* we might be the start of a drag */
                DBG("setting stuff");
                icon_view->priv->maybe_begin_drag = TRUE;
                icon_view->priv->definitely_dragging = FALSE;
                icon_view->priv->definitely_rubber_banding = FALSE;
                icon_view->priv->press_start_x = evt->x;
                icon_view->priv->press_start_y = evt->y;
            }
            
            return TRUE;
        } else {
            /* Button press wasn't over any icons */
            /* unselect previously selected icons if we didn't click one */
            if(icon_view->priv->sel_mode != GTK_SELECTION_MULTIPLE
               || !(evt->state & GDK_CONTROL_MASK))
            {
                xfdesktop_icon_view_unselect_all(icon_view);
            }
            
            icon_view->priv->cursor = NULL;
            icon_view->priv->first_clicked_item = NULL;

            if(icon_view->priv->allow_rubber_banding && evt->button == 1) {
                icon_view->priv->maybe_begin_drag = TRUE;
                icon_view->priv->definitely_dragging = FALSE;
                icon_view->priv->press_start_x = evt->x;
                icon_view->priv->press_start_y = evt->y;
            }

            /* Since we're not over any icons this won't be the start of a
             * drag so we can pop up menu */
            if(evt->button == 3 || (evt->button == 1 && (evt->state & GDK_SHIFT_MASK))) {
                xfce_desktop_popup_root_menu(XFCE_DESKTOP(widget), evt->button, evt->time);
                return TRUE;
            }
        }
    } else if(evt->type == GDK_2BUTTON_PRESS) {
        /* be sure to cancel any pending drags that might have snuck through.
         * this shouldn't happen, but it does sometimes (bug 3426).  */
        icon_view->priv->definitely_dragging = FALSE;
        icon_view->priv->maybe_begin_drag = FALSE;
        icon_view->priv->definitely_rubber_banding = FALSE;
        
        if(evt->button == 1) {
            GList *icon_l = g_list_find_custom(icon_view->priv->icons, evt,
                                               (GCompareFunc)xfdesktop_check_icon_clicked);
            if(icon_l && (icon = icon_l->data)) {
                icon_view->priv->cursor = icon;
                g_signal_emit(G_OBJECT(icon_view), __signals[SIG_ICON_ACTIVATED],
                              0, NULL);
                xfdesktop_icon_activated(icon);
                xfdesktop_icon_view_unselect_item(icon_view, icon);
            }
        }
        
        return TRUE;
    }
    
    return FALSE;
}

static gboolean
xfdesktop_icon_view_get_single_click(XfdesktopIconView *icon_view)
{
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view), FALSE);

    return icon_view->priv->single_click;
}

static gint
xfdesktop_icon_view_get_tooltip_size(XfdesktopIconView *icon_view)
{
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view), DEFAULT_TOOLTIP_SIZE);

    /* Check if we're showing tooltips at all */
    if(!icon_view->priv->show_tooltips)
        return 0;

    /* If the xfconf size is set and sane, use it */
    if(icon_view->priv->tooltip_size_from_xfconf >= 0)
        return icon_view->priv->tooltip_size_from_xfconf;

    /* if the style size is set and sane, use it */
    if(icon_view->priv->tooltip_size_from_style >= 0)
        return icon_view->priv->tooltip_size_from_style;

    /* Fall back */
    return DEFAULT_TOOLTIP_SIZE;
}

static gboolean
xfdesktop_icon_view_button_release(GtkWidget *widget,
                                   GdkEventButton *evt,
                                   gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    XfdesktopIcon *icon;
    GList *icon_l = NULL;

    TRACE("entering btn=%d", evt->button);

    /* single-click */
    if(xfdesktop_icon_view_get_single_click(icon_view)
       && evt->button == 1
       && !(evt->state & GDK_SHIFT_MASK)
       && !(evt->state & GDK_CONTROL_MASK)
       && !icon_view->priv->definitely_dragging
       && !icon_view->priv->definitely_rubber_banding) {
        /* Find out if we clicked on an icon */
        icon_l = g_list_find_custom(icon_view->priv->icons, evt,
                                    (GCompareFunc)xfdesktop_check_icon_clicked);
        if(icon_l && (icon = icon_l->data)) {
            /* We did, activate it */
            icon_view->priv->cursor = icon;
            g_signal_emit(G_OBJECT(icon_view), __signals[SIG_ICON_ACTIVATED],
                          0, NULL);
            xfdesktop_icon_activated(icon);
            xfdesktop_icon_view_unselect_item(icon_view, icon);
        }
    }

    if((evt->button == 3 || (evt->button == 1 && (evt->state & GDK_SHIFT_MASK))) &&
       icon_view->priv->definitely_dragging == FALSE &&
       icon_view->priv->definitely_rubber_banding == FALSE)
    {
        /* If we're in single click mode we may already have the icon, don't
         * find it again. */
        if(icon_l == NULL) {
            icon_l = g_list_find_custom(icon_view->priv->icons, evt,
                                        (GCompareFunc)xfdesktop_check_icon_clicked);
        }

        /* If we clicked an icon then we didn't pop up the menu during the
         * button press in order to support right click DND, pop up the menu
         * now.
         * We pass 0 as the button because the docs say that you must use 0
         * for pop ups other than button press events. */
        if(icon_l && (icon = icon_l->data))
            xfce_desktop_popup_root_menu(XFCE_DESKTOP(widget), 0, evt->time);
    }

    if(evt->button == 1 || evt->button == 3) {
        DBG("unsetting stuff");
        icon_view->priv->definitely_dragging = FALSE;
        icon_view->priv->maybe_begin_drag = FALSE;
        if(icon_view->priv->definitely_rubber_banding) {
            /* Remove the rubber band selection box */
            icon_view->priv->definitely_rubber_banding = FALSE;
            gtk_widget_queue_draw_area(widget, icon_view->priv->band_rect.x,
                                       icon_view->priv->band_rect.y,
                                       icon_view->priv->band_rect.width,
                                       icon_view->priv->band_rect.height);
        }
    }

    gtk_grab_remove(widget);

    return FALSE;
}

static gboolean
xfdesktop_icon_view_key_press(GtkWidget *widget,
                              GdkEventKey *evt,
                              gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);

    TRACE("entering");

    /* since we're NO_WINDOW, events don't get delivered to us normally,
     * so we have to activate the bindings manually */
    return gtk_bindings_activate_event(GTK_OBJECT(icon_view), evt);
}

static gboolean
xfdesktop_icon_view_focus_in(GtkWidget *widget,
                             GdkEventFocus *evt,
                             gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    GList *l;
    
    gtk_widget_grab_focus(GTK_WIDGET(icon_view));
    DBG("GOT FOCUS");
    
    for(l = icon_view->priv->selected_icons; l; l = l->next) {
        xfdesktop_icon_view_invalidate_icon(icon_view, l->data, FALSE);
    }
    
    return FALSE;
}

static gboolean
xfdesktop_icon_view_focus_out(GtkWidget *widget,
                              GdkEventFocus *evt,
                              gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    GList *l;
    
    DBG("LOST FOCUS");

    for(l = icon_view->priv->selected_icons; l; l = l->next) {
        xfdesktop_icon_view_invalidate_icon(icon_view, l->data, FALSE);
    }

    if(G_UNLIKELY(icon_view->priv->single_click)) {
        if(G_LIKELY(gtk_widget_get_window(GTK_WIDGET(icon_view->priv->parent_window)) != NULL)) {
            gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(icon_view->priv->parent_window)), NULL);
        }
    }

    return FALSE;
}

static gboolean
xfdesktop_icon_view_maybe_begin_drag(XfdesktopIconView *icon_view,
                                     GdkEventMotion *evt)
{
    GdkDragAction actions;
    
    /* sanity check */
    g_return_val_if_fail(icon_view->priv->cursor, FALSE);
    
    if(!gtk_drag_check_threshold(GTK_WIDGET(icon_view),
                                 icon_view->priv->press_start_x,
                                 icon_view->priv->press_start_y,
                                 evt->x, evt->y))
    {
        return FALSE;
    }
    
    actions = GDK_ACTION_MOVE | (icon_view->priv->drag_source_set ?
                                 icon_view->priv->foreign_source_actions : 0);
    
    if(evt->state != GDK_BUTTON3_MASK) {
    gtk_drag_begin(GTK_WIDGET(icon_view),
                   icon_view->priv->source_targets,
                   actions, 1, (GdkEvent *)evt);
    } else {
        gtk_drag_begin(GTK_WIDGET(icon_view),
                   icon_view->priv->source_targets,
                   actions | GDK_ACTION_ASK, 3, (GdkEvent *)evt);
    }
    
    DBG("DRAG BEGIN!");
    
    return TRUE;
}

static gboolean
xfdesktop_icon_view_show_tooltip(GtkWidget *widget,
                                 gint x,
                                 gint y,
                                 gboolean keyboard_tooltip,
                                 GtkTooltip *tooltip,
                                 gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    const gchar *tip_text;
    gchar *padded_tip_text = NULL;
    gint tooltip_size;
    
    if(!icon_view->priv->item_under_pointer
       || icon_view->priv->definitely_dragging)
    {
        return FALSE;
    }

    /* not showing tooltips */
    if(!icon_view->priv->show_tooltips)
        return FALSE;

    tooltip_size = xfdesktop_icon_view_get_tooltip_size(icon_view);

    tip_text = xfdesktop_icon_peek_tooltip(icon_view->priv->item_under_pointer);
    if(!tip_text)
        return FALSE;

    padded_tip_text = g_strdup_printf("%s\t", tip_text);

    if(tooltip_size > 0) {
        gtk_tooltip_set_icon(tooltip,
                xfdesktop_icon_peek_tooltip_pixbuf(icon_view->priv->item_under_pointer,
                                                   tooltip_size * 1.5f,
                                                   tooltip_size));
    }

    gtk_tooltip_set_text(tooltip, padded_tip_text);

    g_free(padded_tip_text);

    return TRUE;
}

static gboolean
xfdesktop_icon_view_motion_notify(GtkWidget *widget,
                                  GdkEventMotion *evt,
                                  gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    gboolean ret = FALSE;
    
    if(icon_view->priv->maybe_begin_drag
       && icon_view->priv->item_under_pointer
       && !icon_view->priv->definitely_dragging)
    {
        /* we might have the start of an icon click + drag here */
        icon_view->priv->definitely_dragging = xfdesktop_icon_view_maybe_begin_drag(icon_view,
                                                                                    evt);
        if(icon_view->priv->definitely_dragging)
            ret = TRUE;
    } else if(icon_view->priv->maybe_begin_drag
              && ((!icon_view->priv->item_under_pointer
                   && !icon_view->priv->definitely_rubber_banding)
                  || icon_view->priv->definitely_rubber_banding))
    {
        GdkRectangle old_rect, *new_rect, intersect;
        GdkRegion *region;
        GList *l;

        /* we're dragging with no icon under the cursor -> rubber band start
         * OR, we're already doin' the band -> update it */

        new_rect = &icon_view->priv->band_rect;

        if(!icon_view->priv->definitely_rubber_banding) {
            icon_view->priv->definitely_rubber_banding = TRUE;
            old_rect.x = icon_view->priv->press_start_x;
            old_rect.y = icon_view->priv->press_start_y;
            old_rect.width = old_rect.height = 1;
        } else
            memcpy(&old_rect, new_rect, sizeof(old_rect));

        new_rect->x = MIN(icon_view->priv->press_start_x, evt->x);
        new_rect->y = MIN(icon_view->priv->press_start_y, evt->y);
        new_rect->width = ABS(evt->x - icon_view->priv->press_start_x) + 1;
        new_rect->height = ABS(evt->y - icon_view->priv->press_start_y) + 1;

        region = gdk_region_rectangle(&old_rect);
        gdk_region_union_with_rect(region, new_rect);

        if(gdk_rectangle_intersect(&old_rect, new_rect, &intersect)
           && intersect.width > 2 && intersect.height > 2)
        {
            GdkRegion *region_intersect;

            /* invalidate border too */
            intersect.x += 1;
            intersect.width -= 2;
            intersect.y += 1;
            intersect.height -= 2;

            region_intersect = gdk_region_rectangle(&intersect);
            gdk_region_subtract(region, region_intersect);
            gdk_region_destroy(region_intersect);
        }

        gdk_window_invalidate_region(gtk_widget_get_window(widget), region, TRUE);
        gdk_region_destroy(region);

        /* update list of selected icons */

        /* first pass: if the rubber band area got smaller at least in
         * one dimension, we can try first to just remove icons that
         * aren't in the band anymore */
        if(old_rect.width > new_rect->width
           || old_rect.height > new_rect->height)
        {
            l = icon_view->priv->selected_icons;
            while(l) {
                GdkRectangle extents, dummy;
                XfdesktopIcon *icon = l->data;

                /* To be removed, it must intersect the old rectangle and
                 * not intersect the new one. This way CTRL + rubber band
                 * works properly (Bug 10275) */
                if(xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)
                   && gdk_rectangle_intersect(&extents, &old_rect, NULL)
                   && !gdk_rectangle_intersect(&extents, new_rect, &dummy))
                {
                    /* remove the icon from the selected list */
                    l = l->next;
                    xfdesktop_icon_view_unselect_item(icon_view, icon);
                } else
                    l = l->next;
            }
        }

        /* second pass: if at least one dimension got larger, unfortunately
         * we have to figure out what icons to add to the selected list */
        if(old_rect.width < new_rect->width
           || old_rect.height < new_rect->height)
        {
            for(l = icon_view->priv->icons; l; l = l->next) {
                GdkRectangle extents, dummy;
                XfdesktopIcon *icon = l->data;

                if(xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)
                   && gdk_rectangle_intersect(&extents, new_rect, &dummy)
                   && !xfdesktop_icon_view_is_icon_selected(icon_view, icon))
                {
                    /* since _select_item() prepends to the list, we
                     * should be ok just calling this */
                    xfdesktop_icon_view_select_item(icon_view, icon);
                }
            }
        }
    } else {
        XfdesktopIcon *icon;
        GdkRectangle extents;

        /* normal movement; highlight icons as they go under the pointer */
        
        if(icon_view->priv->item_under_pointer) {
            if(G_UNLIKELY(icon_view->priv->single_click)) {
                GdkCursor *cursor = gdk_cursor_new(GDK_HAND2);
                gdk_window_set_cursor(evt->window, cursor);
                gdk_cursor_unref(cursor);
            }
            if(!xfdesktop_icon_get_extents(icon_view->priv->item_under_pointer,
                                           NULL, NULL, &extents)
               || !xfdesktop_rectangle_contains_point(&extents, evt->x, evt->y))
            {
                icon = icon_view->priv->item_under_pointer;
                icon_view->priv->item_under_pointer = NULL;

                xfdesktop_icon_view_invalidate_icon_pixbuf(icon_view, icon);
            }
        } else {
            if(G_UNLIKELY(icon_view->priv->single_click)) {
                gdk_window_set_cursor(evt->window, NULL);
            }
            icon = xfdesktop_icon_view_widget_coords_to_item(icon_view,
                                                             evt->x,
                                                             evt->y);
            if(icon && xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)
               && xfdesktop_rectangle_contains_point(&extents, evt->x, evt->y))
            {
                icon_view->priv->item_under_pointer = icon;

                xfdesktop_icon_view_invalidate_icon_pixbuf(icon_view, icon);
            }
        }
    }
    
    gdk_event_request_motions(evt);

    return ret;
}

static gboolean
xfdesktop_icon_view_leave_notify(GtkWidget *widget,
                                 GdkEventCrossing *evt,
                                 gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    
    if(icon_view->priv->item_under_pointer) {
        XfdesktopIcon *icon = icon_view->priv->item_under_pointer;
        icon_view->priv->item_under_pointer = NULL;

        xfdesktop_icon_view_invalidate_icon(icon_view, icon, FALSE);
    }

    if(G_UNLIKELY(icon_view->priv->single_click)) {
        if(gtk_widget_get_realized(widget)) {
            gdk_window_set_cursor(gtk_widget_get_window(widget), NULL);
        }
    }
    
    return FALSE;
}

static void
xfdesktop_icon_view_drag_begin(GtkWidget *widget,
                               GdkDragContext *context)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    XfdesktopIcon *icon;
    GdkRectangle extents;
    
    icon = icon_view->priv->cursor;
    g_return_if_fail(icon);
    
    if(xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)) {
        GdkPixbuf *pix;
        
        pix = xfdesktop_icon_peek_pixbuf(icon, ICON_WIDTH, ICON_SIZE);
        if(pix)
            gtk_drag_set_icon_pixbuf(context, pix, 0, 0);
    }
}

static inline void
xfdesktop_xy_to_rowcol(XfdesktopIconView *icon_view,
                       gint x,
                       gint y,
                       gint16 *row,
                       gint16 *col)
{
    g_return_if_fail(row && col);
    
    *row = (y - icon_view->priv->yorigin - icon_view->priv->ymargin) / (CELL_SIZE + icon_view->priv->yspacing);
    *col = (x - icon_view->priv->xorigin - icon_view->priv->xmargin) / (CELL_SIZE + icon_view->priv->xspacing);
}

static gboolean
xfdesktop_icon_view_drag_motion(GtkWidget *widget,
                                GdkDragContext *context,
                                gint x,
                                gint y,
                                guint time_)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    GdkAtom target;
    gint16 hover_row = 0, hover_col = 0;
    XfdesktopIcon *icon_on_dest = NULL;
    GdkDragAction our_action = 0;
    gboolean is_local_drag;
    
    target = gtk_drag_dest_find_target(widget, context, 
                                       icon_view->priv->native_targets);
    if(target == GDK_NONE) {
        target = gtk_drag_dest_find_target(widget, context,
                                           icon_view->priv->dest_targets);
        if(target == GDK_NONE)
            return FALSE;
    }
    
    /* can we drop here? */
    xfdesktop_xy_to_rowcol(icon_view, x, y, &hover_row, &hover_col);
    if(hover_row >= icon_view->priv->nrows || hover_col >= icon_view->priv->ncols)
        return FALSE;
    icon_on_dest = xfdesktop_icon_view_icon_in_cell(icon_view, hover_row,
                                                    hover_col);
    if(icon_on_dest) {
        if(!xfdesktop_icon_get_allowed_drop_actions(icon_on_dest, NULL))
            return FALSE;
    } else if(!xfdesktop_grid_is_free_position(icon_view, hover_row, hover_col))
        return FALSE;
    
    is_local_drag = (target == gdk_atom_intern("XFDESKTOP_ICON", FALSE));
    
    /* at this point there are four cases to account for:
     * 1.  local drag, empty space -> MOVE
     * 2.  local drag, icon is there -> depends on icon_on_dest
     * 3.  foreign drag, empty space -> depends on the source
     * 4.  foreign drag, icon is there -> depends on source and icon_on_dest
     */
    
    if(!icon_on_dest) {
        if(is_local_drag)  /* # 1 */
            our_action = GDK_ACTION_MOVE;
        else  /* #3 */
            our_action = gdk_drag_context_get_suggested_action(context);
    } else {
        /* start with all available actions (may be filtered by modifier keys) */
        GdkDragAction allowed_actions = gdk_drag_context_get_actions(context);

        if(is_local_drag) {  /* #2 */
            /* check to make sure we aren't just hovering over ourself */
            GList *l;
            gint16 sel_row, sel_col;
            
            for(l = icon_view->priv->selected_icons; l; l = l->next) {
                XfdesktopIcon *sel_icon = l->data;
                if(xfdesktop_icon_get_position(sel_icon, &sel_row, &sel_col)
                   && sel_row == hover_row && sel_col == hover_col)
                {
                    return FALSE;
                }
            }
            
            allowed_actions &= xfdesktop_icon_get_allowed_drag_actions(icon_view->priv->cursor);

            /* for local drags, let the dest icon decide */
            allowed_actions &= xfdesktop_icon_get_allowed_drop_actions(icon_on_dest, &our_action);
        } else {  /* #4 */
            allowed_actions &= xfdesktop_icon_get_allowed_drop_actions(icon_on_dest, NULL);

            /* for foreign drags, take the action suggested by the source */
            our_action = gdk_drag_context_get_suggested_action(context);
        }

        /* #2 or #4 */

        /* fallback actions if the suggested action is not allowed,
         * priority: move, copy, link */
        if(!(our_action & allowed_actions)) {
            if(allowed_actions & GDK_ACTION_MOVE)
                our_action = GDK_ACTION_MOVE;
            else if(allowed_actions & GDK_ACTION_COPY)
                our_action = GDK_ACTION_COPY;
            else if(allowed_actions & GDK_ACTION_LINK)
                our_action = GDK_ACTION_LINK;
            else
                our_action = 0;
        }
    }

    /* allow the drag dest to override the selected action based on the drag data */
    icon_view->priv->hover_row = hover_row;
    icon_view->priv->hover_col = hover_col;
    icon_view->priv->proposed_drop_action = our_action;
    icon_view->priv->dropped = FALSE;
    g_object_set_data(G_OBJECT(context), "--xfdesktop-icon-view-drop-icon",
                      icon_on_dest);
    gtk_drag_get_data(widget, context, target, time_);

    /* the actual call to gdk_drag_status() is deferred to
     * xfdesktop_icon_view_drag_data_received() */

    return TRUE;
}

static gboolean
xfdesktop_icon_view_drag_drop(GtkWidget *widget,
                              GdkDragContext *context,
                              gint x,
                              gint y,
                              guint time_)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    GdkAtom target;
    XfdesktopIcon *icon;
    gint16 old_row, old_col, row, col;
    GList *l;
    XfdesktopIcon *icon_on_dest = NULL;
    
    TRACE("entering: (%d,%d)", x, y);

    icon_view->priv->dropped = TRUE;
    
    target = gtk_drag_dest_find_target(widget, context, 
                                       icon_view->priv->native_targets);
    if(target == GDK_NONE) {
        target = gtk_drag_dest_find_target(widget, context,
                                           icon_view->priv->dest_targets);
        if(target == GDK_NONE)
            return FALSE;
    }
    XF_DEBUG("target=%ld (%s)", (glong)target, gdk_atom_name(target));
    
    xfdesktop_xy_to_rowcol(icon_view, x, y, &row, &col);
    icon_on_dest = xfdesktop_icon_view_icon_in_cell(icon_view, row, col);
    
    if(target == gdk_atom_intern("XFDESKTOP_ICON", FALSE)) {
        if(icon_on_dest) {
            gboolean ret = FALSE;
            
            for(l = icon_view->priv->selected_icons; l; l = l->next) {
                if(xfdesktop_icon_do_drop_dest(icon_on_dest,
                                               XFDESKTOP_ICON(l->data),
                                               gdk_drag_context_get_selected_action(context)))
                {
                    ret = TRUE;
                }
            }
            
            gtk_drag_finish(context, ret, FALSE, time_);
            
            return ret;
        }
        
        icon = icon_view->priv->cursor;
        g_return_val_if_fail(icon, FALSE);
        
        /* 1: Remove all the icons that are going to be moved from
         *    the desktop. That's in case the icons being moved
         *    want to rearrange themselves there.
         * 2: We need to move the icon that's being dragged since the
         *    user explicitly wants to drop it in that spot.
         * 3: We just append all the other icons in any spot that's
         *    open. */
        for(l = icon_view->priv->selected_icons; l; l = l->next) {
            /* clear out old position */
            xfdesktop_icon_view_invalidate_icon(icon_view, l->data, FALSE);
            if(xfdesktop_icon_get_position(l->data, &old_row, &old_col))
                xfdesktop_grid_set_position_free(icon_view, old_row, old_col);
        }

        /* set new position */
        xfdesktop_icon_set_position(icon, row, col);
        xfdesktop_grid_unset_position_free(icon_view, icon);
        
        /* clear out old extents, if any */
        /* FIXME: is this right? */
        xfdesktop_icon_view_invalidate_icon(icon_view, icon, TRUE);

        /* Now that we have moved the icon the user selected,
         * append all the other selected icons after it. */
        for(l = icon_view->priv->selected_icons; l; l = l->next) {
            if(l->data == icon)
                continue;

            /* Find the next available spot for an icon */
            do {
                if(row + 1 >= icon_view->priv->nrows) {
                    ++col;
                    row = 0;
                } else {
                    ++row;
                }
            } while(!xfdesktop_grid_is_free_position(icon_view, row, col));

            /* set new position */
            xfdesktop_icon_set_position(l->data, row, col);
            xfdesktop_grid_unset_position_free(icon_view, l->data);

            /* clear out old extents, if any */
            /* FIXME: is this right? */
            xfdesktop_icon_view_invalidate_icon(icon_view, l->data, TRUE);
        }
        
        XF_DEBUG("drag succeeded");
        
        gtk_drag_finish(context, TRUE, FALSE, time_);
    } else {
        g_object_set_data(G_OBJECT(context), "--xfdesktop-icon-view-drop-icon",
                          icon_on_dest);
        return xfdesktop_icon_view_manager_drag_drop(icon_view->priv->manager,
                                                     icon_on_dest,
                                                     context,
                                                     row, col, time_);
    }
    
    return TRUE;
}

static void
xfdesktop_icon_view_drag_data_get(GtkWidget *widget,
                                  GdkDragContext *context,
                                  GtkSelectionData *data,
                                  guint info,
                                  guint time_)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    
    TRACE("entering");

    /* Sometimes during a ctrl+drag this is NULL
     * but works when the file(s) are dropped */
    if(icon_view->priv->selected_icons == NULL)
        return;
    
    xfdesktop_icon_view_manager_drag_data_get(icon_view->priv->manager,
                                              icon_view->priv->selected_icons,
                                              context, data, info, time_);
}

static void
xfdesktop_icon_view_drag_data_received(GtkWidget *widget,
                                       GdkDragContext *context,
                                       gint x,
                                       gint y,
                                       GtkSelectionData *data,
                                       guint info,
                                       guint time_)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    gint16 row, col;
    XfdesktopIcon *icon_on_dest;
    
    TRACE("entering");
    
    icon_on_dest = g_object_get_data(G_OBJECT(context),
                                     "--xfdesktop-icon-view-drop-icon");
    
    if(icon_view->priv->dropped) {
        xfdesktop_xy_to_rowcol(icon_view, x, y, &row, &col);
        if(row >= icon_view->priv->nrows || col >= icon_view->priv->ncols)
            return;

        xfdesktop_icon_view_manager_drag_data_received(icon_view->priv->manager,
                                                       icon_on_dest,
                                                       context, row, col, data,
                                                       info, time_);
    } else {
        /* FIXME: cannot use x and y here, for they don't seem to have any
         * meaningful value */

        GdkDragAction action = icon_view->priv->proposed_drop_action;
        action = xfdesktop_icon_view_manager_propose_drop_action(icon_view->priv->manager,
                                                                 icon_on_dest,
                                                                 action,
                                                                 context, data,
                                                                 info);

        gdk_drag_status(context, action, time_);
    }
}

#ifdef ENABLE_FILE_ICONS
static gint
xfdesktop_icon_view_compare_icons(gconstpointer *a,
                                  gconstpointer *b)
{
    XfdesktopIcon *a_icon, *b_icon;
    const gchar *a_str, *b_str;

    a_icon = XFDESKTOP_ICON(a);
    b_icon = XFDESKTOP_ICON(b);

    a_str = xfdesktop_icon_peek_label(a_icon);
    b_str = xfdesktop_icon_peek_label(b_icon);

    if(a_str == NULL)
        a_str = "";
    if(b_str == NULL)
        b_str = "";

    return g_utf8_collate(a_str, b_str);
}
#endif /* ENABLE_FILE_ICONS */

#ifdef ENABLE_FILE_ICONS
static void
xfdesktop_icon_view_append_icons(XfdesktopIconView *icon_view,
                                 GList *icon_list,
                                 gint16 *row,
                                 gint16 *col)
{
    GList *l = NULL;
    for(l = icon_list; l != NULL; l = g_list_next(l)) {

        /* Find the next available spot for an icon */
        do {
            if(*row + 1 >= icon_view->priv->nrows) {
                ++*col;
                *row = 0;
            } else {
                ++*row;
            }
        } while(!xfdesktop_grid_is_free_position(icon_view, *row, *col));

        /* set new position */
        xfdesktop_icon_set_position(l->data, *row, *col);
        xfdesktop_grid_unset_position_free(icon_view, l->data);

        xfdesktop_icon_view_invalidate_icon(icon_view, l->data, TRUE);
    }
}
#endif /* ENABLE_FILE_ICONS */

void
xfdesktop_icon_view_sort_icons(XfdesktopIconView *icon_view)
{
#ifdef ENABLE_FILE_ICONS
    GList *l = NULL;
    GList *special_icons = NULL;
    GList *folder_icons = NULL;
    GList *regular_icons = NULL;
    gint16 row = 0;
    gint16 col = -1; /* start at -1 because we'll increment it */

    for(l = icon_view->priv->icons; l; l = l->next) {
        gint16 old_row, old_col;

        /* clear out old position */
        xfdesktop_icon_view_invalidate_icon(icon_view, l->data, FALSE);

        if(xfdesktop_icon_get_position(l->data, &old_row, &old_col))
            xfdesktop_grid_set_position_free(icon_view, old_row, old_col);

        /* Add it to the correct list */
        if(XFDESKTOP_IS_SPECIAL_FILE_ICON(l->data) ||
           XFDESKTOP_IS_VOLUME_ICON(l->data)) {
            special_icons = g_list_insert_sorted(special_icons,
                                                 l->data,
                                                 (GCompareFunc)xfdesktop_icon_view_compare_icons);
        } else if(XFDESKTOP_IS_FILE_ICON(l->data) &&
                  g_file_query_file_type(xfdesktop_file_icon_peek_file(l->data),
                                         G_FILE_QUERY_INFO_NONE,
                                         NULL) == G_FILE_TYPE_DIRECTORY) {
            folder_icons = g_list_insert_sorted(folder_icons,
                                                 l->data,
                                                 (GCompareFunc)xfdesktop_icon_view_compare_icons);
        } else {
            regular_icons = g_list_insert_sorted(regular_icons,
                                                 l->data,
                                                 (GCompareFunc)xfdesktop_icon_view_compare_icons);
        }
    }

    /* Append the icons: special, folder, then regular */
    xfdesktop_icon_view_append_icons(icon_view, special_icons, &row, &col);
    xfdesktop_icon_view_append_icons(icon_view, folder_icons, &row, &col);
    xfdesktop_icon_view_append_icons(icon_view, regular_icons, &row, &col);


    g_list_free(special_icons);
    g_list_free(folder_icons);
    g_list_free(regular_icons);
#endif
}

static void
xfdesktop_icon_view_icon_theme_changed(GtkIconTheme *icon_theme,
                                       gpointer user_data)
{
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
}    

static void
xfdesktop_icon_view_style_set(GtkWidget *widget,
                              GtkStyle *previous_style)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    GtkWidget *dummy;

    gtk_widget_style_get(GTK_WIDGET(icon_view),
                         "label-alpha",   &icon_view->priv->label_alpha,
                         "shadow-x-offset", &icon_view->priv->shadow_x_offset,
                         "shadow-y-offset", &icon_view->priv->shadow_y_offset,
                         "shadow-blur-radius", &icon_view->priv->shadow_blur_radius,
                         "shadow-color",  &icon_view->priv->shadow_color,
                         NULL);

    icon_view->priv->shadow_extents =
      (icon_view->priv->shadow_blur_radius > 1) ?
      _gtk_cairo_blur_compute_pixels(icon_view->priv->shadow_blur_radius) : 0;

    /* default the shadow color to the inverse of the text color */
    if (!icon_view->priv->shadow_color) {
        icon_view->priv->shadow_color = gdk_color_copy(&gtk_widget_get_style(widget)->fg[GTK_STATE_NORMAL]);
        icon_view->priv->shadow_color->red   ^= 0xffff;
        icon_view->priv->shadow_color->green ^= 0xffff;
        icon_view->priv->shadow_color->blue  ^= 0xffff;
    }

    XF_DEBUG("label alpha is %d\n",   (gint)(icon_view->priv->label_alpha));
    XF_DEBUG("shadow x offset is %d\n", (gint)(icon_view->priv->shadow_x_offset));
    XF_DEBUG("shadow y offset is %d\n", (gint)(icon_view->priv->shadow_y_offset));
#if defined(DEBUG) && (DEBUG > 0)
    {
        gchar *color = gdk_color_to_string(icon_view->priv->shadow_color);
        XF_DEBUG("shadow color is %s\n", color);
        g_free(color);
    }
#endif

    gtk_widget_style_get(GTK_WIDGET(icon_view),
                         "selected-label-alpha", &icon_view->priv->selected_label_alpha,
                         "selected-shadow-x-offset", &icon_view->priv->selected_shadow_x_offset,
                         "selected-shadow-y-offset", &icon_view->priv->selected_shadow_y_offset,
                         "selected-shadow-color", &icon_view->priv->selected_shadow_color,
                         NULL);

    /* default the shadow color to the inverse of the text color */
    if (!icon_view->priv->selected_shadow_color) {
        icon_view->priv->selected_shadow_color = gdk_color_copy(&gtk_widget_get_style(widget)->fg[GTK_STATE_SELECTED]);
        icon_view->priv->selected_shadow_color->red   ^= 0xffff;
        icon_view->priv->selected_shadow_color->green ^= 0xffff;
        icon_view->priv->selected_shadow_color->blue  ^= 0xffff;
    }

    XF_DEBUG("selected label alpha is %d\n",
             (gint)(icon_view->priv->selected_label_alpha));
    XF_DEBUG("selected shadow x offset is %d\n",
             (gint)(icon_view->priv->selected_shadow_x_offset));
    XF_DEBUG("selected shadow y offset is %d\n",
             (gint)(icon_view->priv->selected_shadow_y_offset));
#if defined(DEBUG) && (DEBUG > 0)
    {
        gchar *color = gdk_color_to_string(icon_view->priv->selected_shadow_color);
        XF_DEBUG("shadow color is %s\n", color);
        g_free(color);
    }
#endif
    
    gtk_widget_style_get(widget,
                         "cell-spacing", &icon_view->priv->cell_spacing,
                         "cell-padding", &icon_view->priv->cell_padding,
                         "cell-text-width-proportion", &icon_view->priv->cell_text_width_proportion,
                         "ellipsize-icon-labels", &icon_view->priv->ellipsize_icon_labels,
                         "tooltip-size", &icon_view->priv->tooltip_size_from_style,
                         "label-radius", &icon_view->priv->label_radius,
                         NULL);

    XF_DEBUG("cell spacing is %d", icon_view->priv->cell_spacing);
    XF_DEBUG("cell padding is %d", icon_view->priv->cell_padding);
    XF_DEBUG("cell text width proportion is %f", icon_view->priv->cell_text_width_proportion);
    XF_DEBUG("ellipsize icon label is %s", icon_view->priv->ellipsize_icon_labels?"true":"false");
    XF_DEBUG("tooltip size is %d", icon_view->priv->tooltip_size_from_style);
    XF_DEBUG("label radius is %f", icon_view->priv->label_radius);

    if(icon_view->priv->selection_box_color) {
        gdk_color_free(icon_view->priv->selection_box_color);
        icon_view->priv->selection_box_color = NULL;
    }
    icon_view->priv->selection_box_alpha = DEFAULT_RUBBERBAND_ALPHA;

    /* this is super lame */
    dummy = gtk_icon_view_new();
    gtk_widget_ensure_style(dummy);
    gtk_widget_style_get(dummy,
                         "selection-box-color", &icon_view->priv->selection_box_color,
                         "selection-box-alpha", &icon_view->priv->selection_box_alpha,
                         NULL);
    gtk_widget_destroy(dummy);

    GTK_WIDGET_CLASS(xfdesktop_icon_view_parent_class)->style_set(widget,
                                                                  previous_style);

    /* do this after we're sure we have a style set */
    if(!icon_view->priv->selection_box_color) {
        GtkStyle *style = gtk_widget_get_style(widget);
        icon_view->priv->selection_box_color = gdk_color_copy(&style->base[GTK_STATE_SELECTED]);
    }
}

static void
xfdesktop_icon_view_realize(GtkWidget *widget)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    PangoContext *pctx;
    GdkScreen *gscreen;
    GdkWindow *groot;

    icon_view->priv->parent_window = gtk_widget_get_toplevel(widget);
    g_return_if_fail(icon_view->priv->parent_window);
    gtk_widget_set_window(widget, gtk_widget_get_window(icon_view->priv->parent_window));
    
    gtk_widget_set_style(widget,
                         gtk_style_attach(gtk_widget_get_style(widget),
                                          gtk_widget_get_window(widget)));
    
    /* there's no reason to start up the manager before we're realized,
     * but we do NOT shut it down if we unrealize, since there may not be
     * a reason to do so.  shutdown occurs in finalize. */
    xfdesktop_icon_view_manager_init(icon_view->priv->manager, icon_view);
    
    gtk_widget_set_realized(widget, TRUE);
    
    gtk_window_set_accept_focus(GTK_WINDOW(icon_view->priv->parent_window),
                                TRUE);
    gtk_window_set_focus_on_map(GTK_WINDOW(icon_view->priv->parent_window),
                                FALSE);
    
    pctx = gtk_widget_get_pango_context(GTK_WIDGET(icon_view));
    icon_view->priv->playout = pango_layout_new(pctx);

    if(icon_view->priv->font_size > 0) {
        xfdesktop_icon_view_modify_font_size(icon_view,
                                             icon_view->priv->font_size);
    }
    
    xfdesktop_setup_grids(icon_view);
    
    /* unfortunately GTK_NO_WINDOW widgets don't receive events, with the
     * exception of expose events. */
    gtk_widget_add_events(icon_view->priv->parent_window,
                          GDK_POINTER_MOTION_HINT_MASK | GDK_KEY_PRESS_MASK
                          | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                          | GDK_FOCUS_CHANGE_MASK | GDK_EXPOSURE_MASK
                          | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(G_OBJECT(icon_view->priv->parent_window),
                     "motion-notify-event",
                     G_CALLBACK(xfdesktop_icon_view_motion_notify), icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->parent_window),
                     "leave-notify-event",
                     G_CALLBACK(xfdesktop_icon_view_leave_notify), icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->parent_window),
                     "key-press-event",
                     G_CALLBACK(xfdesktop_icon_view_key_press), icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->parent_window),
                     "button-press-event",
                     G_CALLBACK(xfdesktop_icon_view_button_press), icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->parent_window),
                     "button-release-event",
                     G_CALLBACK(xfdesktop_icon_view_button_release), icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->parent_window),
                     "focus-in-event",
                     G_CALLBACK(xfdesktop_icon_view_focus_in), icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->parent_window),
                     "focus-out-event",
                     G_CALLBACK(xfdesktop_icon_view_focus_out), icon_view);
    
    /* watch for _NET_WORKAREA changes */
    gscreen = gtk_widget_get_screen(widget);
    groot = gdk_screen_get_root_window(gscreen);
    gdk_window_set_events(groot, gdk_window_get_events(groot)
                                 | GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(groot, xfdesktop_rootwin_watch_workarea, icon_view);
    
    g_signal_connect(G_OBJECT(gscreen), "size-changed",
                     G_CALLBACK(xfdesktop_screen_size_changed_cb), icon_view);
    
    g_signal_connect_after(G_OBJECT(gtk_icon_theme_get_for_screen(gscreen)),
                           "changed",
                           G_CALLBACK(xfdesktop_icon_view_icon_theme_changed),
                           icon_view);
    
    xfdesktop_move_all_pending_icons_to_desktop(icon_view);
}

static void
xfdesktop_icon_view_unrealize(GtkWidget *widget)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    GdkScreen *gscreen;
    GdkWindow *groot;
    
    gtk_window_set_accept_focus(GTK_WINDOW(icon_view->priv->parent_window), FALSE);
    
    gscreen = gtk_widget_get_screen(widget);
    groot = gdk_screen_get_root_window(gscreen);
    gdk_window_remove_filter(groot, xfdesktop_rootwin_watch_workarea, icon_view);
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(gtk_icon_theme_get_for_screen(gscreen)),
                     G_CALLBACK(xfdesktop_icon_view_icon_theme_changed),
                     icon_view);
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->parent_window),
                     G_CALLBACK(xfdesktop_icon_view_motion_notify), icon_view);
    g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->parent_window),
                     G_CALLBACK(xfdesktop_icon_view_leave_notify), icon_view);
    g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->parent_window),
                     G_CALLBACK(xfdesktop_icon_view_key_press), icon_view);
    g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->parent_window),
                     G_CALLBACK(xfdesktop_icon_view_button_press), icon_view);
    g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->parent_window),
                     G_CALLBACK(xfdesktop_icon_view_button_release), icon_view);
    g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->parent_window),
                     G_CALLBACK(xfdesktop_icon_view_focus_in), icon_view);
    g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->parent_window),
                     G_CALLBACK(xfdesktop_icon_view_focus_out), icon_view);
    
    if(icon_view->priv->grid_resize_timeout) {
        g_source_remove(icon_view->priv->grid_resize_timeout);
        icon_view->priv->grid_resize_timeout = 0;
    }
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(gscreen),
                                         G_CALLBACK(xfdesktop_screen_size_changed_cb),
                                         icon_view);
    
    /* FIXME: really clear these? */
    g_list_free(icon_view->priv->selected_icons);
    icon_view->priv->selected_icons = NULL;

    xfdesktop_move_all_icons_to_pending_icons_list(icon_view);

    g_free(icon_view->priv->grid_layout);
    icon_view->priv->grid_layout = NULL;
    
    g_object_unref(G_OBJECT(icon_view->priv->playout));
    icon_view->priv->playout = NULL;
    
    if(icon_view->priv->selection_box_color) {
        gdk_color_free(icon_view->priv->selection_box_color);
        icon_view->priv->selection_box_color = NULL;
    }

    if(icon_view->priv->shadow_color) {
        gdk_color_free(icon_view->priv->shadow_color);
        icon_view->priv->shadow_color = NULL;
    }

    if(icon_view->priv->selected_shadow_color) {
        gdk_color_free(icon_view->priv->selected_shadow_color);
        icon_view->priv->selected_shadow_color = NULL;
    }
    
    gtk_widget_set_window(widget, NULL);
    gtk_widget_set_realized(widget, FALSE);
}

static gboolean
xfdesktop_icon_view_expose(GtkWidget *widget,
                           GdkEventExpose *evt)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(widget);
    GdkRectangle *rects = NULL;
    GdkRectangle clipbox;
    gint n_rects = 0, i;

    /*TRACE("entering");*/
    
    if(evt->count != 0)
        return FALSE;

    gdk_region_get_rectangles(evt->region, &rects, &n_rects);
    gdk_region_get_clipbox(evt->region, &clipbox);

    xfdesktop_icon_view_repaint_icons(icon_view, &clipbox);

    if(icon_view->priv->definitely_rubber_banding) {
        GdkRectangle intersect;
        cairo_t *cr;

        cr = gdk_cairo_create(GDK_DRAWABLE(gtk_widget_get_window(widget)));
        cairo_set_line_width(cr, 1);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(cr,
                              icon_view->priv->selection_box_color->red / 65535.,
                              icon_view->priv->selection_box_color->green / 65535.,
                              icon_view->priv->selection_box_color->blue / 65535.,
                              icon_view->priv->selection_box_alpha / 255.);

        /* paint each rectangle in the expose region with the rubber
         * band color, semi-transparently */
        for(i = 0; i < n_rects; ++i) {
            if(!gdk_rectangle_intersect(&rects[i],
                                        &icon_view->priv->band_rect,
                                        &intersect))
            {
                continue;
            }

            cairo_save(cr);

            /* paint the inner rubber band area.  we clip to the
             * rectangle in order to properly handle the border below */
            gdk_cairo_rectangle(cr, &intersect);
            cairo_clip_preserve(cr);
            cairo_fill(cr);

            /* paint whatever part of the rubber band border is inside
             * this rectangle */
            gdk_cairo_set_source_color(cr, icon_view->priv->selection_box_color);
            cairo_rectangle(cr, icon_view->priv->band_rect.x + 0.5,
                            icon_view->priv->band_rect.y + 0.5,
                            icon_view->priv->band_rect.width - 1,
                            icon_view->priv->band_rect.height - 1);
            cairo_stroke(cr);

            cairo_restore(cr);
        }

        cairo_destroy(cr);
    }

    g_free(rects);

    return FALSE;
}

static void
xfdesktop_icon_view_real_select_all(XfdesktopIconView *icon_view)
{
    TRACE("entering");

    xfdesktop_icon_view_select_all(icon_view);
}

static void
xfdesktop_icon_view_real_unselect_all(XfdesktopIconView *icon_view)
{
    TRACE("entering");

    xfdesktop_icon_view_unselect_all(icon_view);
}

static void
xfdesktop_icon_view_real_select_cursor_item(XfdesktopIconView *icon_view)
{
    TRACE("entering");

    if(icon_view->priv->cursor)
        xfdesktop_icon_view_select_item(icon_view, icon_view->priv->cursor);
}

static void
xfdesktop_icon_view_real_toggle_cursor_item(XfdesktopIconView *icon_view)
{
    TRACE("entering");

    if(!icon_view->priv->cursor)
        return;

    if(g_list_find(icon_view->priv->selected_icons, icon_view->priv->cursor))
        xfdesktop_icon_view_unselect_item(icon_view, icon_view->priv->cursor);
    else
        xfdesktop_icon_view_select_item(icon_view, icon_view->priv->cursor);
}

static gboolean
xfdesktop_icon_view_real_activate_cursor_item(XfdesktopIconView *icon_view)
{
    TRACE("entering");

    if(!icon_view->priv->cursor)
        return FALSE;

    g_signal_emit(G_OBJECT(icon_view), __signals[SIG_ICON_ACTIVATED], 0, NULL);
    xfdesktop_icon_activated(icon_view->priv->cursor);
    xfdesktop_icon_view_unselect_item(icon_view, icon_view->priv->cursor);
    icon_view->priv->cursor = NULL;

    return TRUE;
}

static void
xfdesktop_icon_view_select_between(XfdesktopIconView *icon_view,
                                   XfdesktopIcon *start_icon,
                                   XfdesktopIcon *end_icon)
{
    gint16 start_row, start_col, end_row, end_col;
    gint i, j;
    XfdesktopIcon *icon;

    if(xfdesktop_icon_get_position(start_icon, &start_row, &start_col)
       && xfdesktop_icon_get_position(end_icon, &end_row, &end_col))
    {
        if(start_row > end_row || (start_row == end_row && start_col > end_col)) {
            /* flip start and end */
            gint16 tmpr = start_row, tmpc = start_col;

            start_row = end_row;
            start_col = end_col;
            end_row = tmpr;
            end_col = tmpc;
        }

        for(i = start_row; i <= end_row; ++i) {
            for(j = (i == start_row ? start_col : 0);
                (i == end_row ? j <= end_col : j < icon_view->priv->ncols);
                ++j)
            {
                icon = xfdesktop_icon_view_icon_in_cell(icon_view, i, j);
                if(icon) {
                    xfdesktop_icon_view_select_item(icon_view, icon);
                }
            }
        }
    }
}

static XfdesktopIcon *
xfdesktop_icon_view_find_first_icon(XfdesktopIconView *icon_view)
{
    gint i, j;
    XfdesktopIcon *icon = NULL;

    if(!icon_view->priv->icons)
        return NULL;

    for(i = 0; i < icon_view->priv->nrows && !icon; ++i) {
        for(j = 0; j < icon_view->priv->ncols; ++j) {
            icon = xfdesktop_icon_view_icon_in_cell(icon_view, i, j);
            if(icon)
                break;
        }
    }

    return icon;
}

static XfdesktopIcon *
xfdesktop_icon_view_find_last_icon(XfdesktopIconView *icon_view)
{
    XfdesktopIcon *icon = NULL;
    gint i, j;

    if(!icon_view->priv->icons)
        return NULL;

    for(i = icon_view->priv->nrows - 1; i >= 0 && !icon; --i) {
        for(j = icon_view->priv->ncols - 1; j >= 0; --j) {
            icon = xfdesktop_icon_view_icon_in_cell(icon_view, i, j);
            if(icon)
                break;
        }
    }

    return icon;
}

static void
xfdesktop_icon_view_move_cursor_left_right(XfdesktopIconView *icon_view,
                                           gint count,
                                           GdkModifierType modmask)
{
    gint16 row, col;
    gint i, j;
    guint left = (count < 0 ? -count : count);
    gint step = (count < 0 ? -1 : 1);
    XfdesktopIcon *icon = NULL;

    if(!icon_view->priv->cursor) {
        /* choose first or last item depending on left or right */
        if(count < 0)
            icon = xfdesktop_icon_view_find_last_icon(icon_view);
        else
            icon = xfdesktop_icon_view_find_first_icon(icon_view);

        if(icon) {
            if(!(modmask & GDK_CONTROL_MASK))
                xfdesktop_icon_view_unselect_all(icon_view);
            icon_view->priv->cursor = icon;
            xfdesktop_icon_view_select_item(icon_view, icon);
        }
    } else {
        if(!xfdesktop_icon_get_position(icon_view->priv->cursor, &row, &col))
            return;

        if(!(modmask & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)))
            xfdesktop_icon_view_unselect_all(icon_view);

        for(i = row;
            (count < 0 ? i >= 0 : i < icon_view->priv->nrows) && left > 0;
            i += step)
        {
            for(j = (i == row ? col + step : (count < 0) ? icon_view->priv->ncols - 1 : 0);
                (count < 0 ? j >= 0 : j < icon_view->priv->ncols) && left > 0;
                j += step)
            {
                icon = xfdesktop_icon_view_icon_in_cell(icon_view, i, j);
                if(icon) {
                    icon_view->priv->cursor = icon;
                    if((modmask & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)) || left == 1)
                        xfdesktop_icon_view_select_item(icon_view, icon);
                    left--;
                }
            }
        }

        if(!icon_view->priv->selected_icons) {
            if(count < 0)
                icon = xfdesktop_icon_view_find_first_icon(icon_view);
            else
                icon = xfdesktop_icon_view_find_last_icon(icon_view);
            
            if(icon) {
                xfdesktop_icon_view_select_item(icon_view, icon);
                icon_view->priv->cursor = icon;
            }
        }
    }
}

static void
xfdesktop_icon_view_move_cursor_up_down(XfdesktopIconView *icon_view,
                                        gint count,
                                        GdkModifierType modmask)
{
    gint16 row, col;
    gint i, j;
    guint left = (count < 0 ? -count : count);
    gint step = (count < 0 ? -1 : 1);
    XfdesktopIcon *icon = NULL;

    if(!icon_view->priv->cursor) {
        /* choose first or last item depending on up or down */
        if(count < 0)
            icon = xfdesktop_icon_view_find_last_icon(icon_view);
        else
            icon = xfdesktop_icon_view_find_first_icon(icon_view);

        if(icon) {
            if(!(modmask & GDK_CONTROL_MASK))
                xfdesktop_icon_view_unselect_all(icon_view);
            icon_view->priv->cursor = icon;
            xfdesktop_icon_view_select_item(icon_view, icon);
        }
    } else {
        if(!xfdesktop_icon_get_position(icon_view->priv->cursor, &row, &col))
            return;

        if(!(modmask & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)))
            xfdesktop_icon_view_unselect_all(icon_view);

        for(j = col;
            (count < 0 ? j >= 0 : j < icon_view->priv->ncols) && left > 0;
            j += step)
        {
        for(i = (j == col ? row + step : (count < 0) ? icon_view->priv->nrows - 1 : 0);
            (count < 0 ? i >= 0 : i < icon_view->priv->nrows) && left > 0;
            i += step)
        {
                icon = xfdesktop_icon_view_icon_in_cell(icon_view, i, j);
                if(icon) {
                    icon_view->priv->cursor = icon;
                    if((modmask & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)) || left == 1)
                        xfdesktop_icon_view_select_item(icon_view, icon);
                    left--;
                }
            }
        }

        if(!icon_view->priv->selected_icons) {
            if(count < 0)
                icon = xfdesktop_icon_view_find_first_icon(icon_view);
            else
                icon = xfdesktop_icon_view_find_last_icon(icon_view);
            
            if(icon) {
                xfdesktop_icon_view_select_item(icon_view, icon);
                icon_view->priv->cursor = icon;
            }
        }
    }
}

static void
xfdesktop_icon_view_move_cursor_begin_end(XfdesktopIconView *icon_view,
                                          gint count,
                                          GdkModifierType modmask)
{
    XfdesktopIcon *icon = NULL, *old_cursor;

    if(count < 0)
        icon = xfdesktop_icon_view_find_first_icon(icon_view);
    else
        icon = xfdesktop_icon_view_find_last_icon(icon_view);

    if(!icon)
        return;

    old_cursor = icon_view->priv->cursor;
    icon_view->priv->cursor = icon;

    if(!old_cursor || !(modmask & (GDK_SHIFT_MASK|GDK_CONTROL_MASK))) {
        xfdesktop_icon_view_unselect_all(icon_view);
        xfdesktop_icon_view_select_item(icon_view, icon);
    } else if(old_cursor) {
        if(modmask & GDK_SHIFT_MASK) {
            /* select everything between the cursor and the old_cursor */
            xfdesktop_icon_view_select_between(icon_view, old_cursor, icon);
        } else if(modmask & GDK_CONTROL_MASK) {
            /* add the icon to the selection */
            xfdesktop_icon_view_select_item(icon_view, icon);
        }

    }
}

static gboolean
xfdesktop_icon_view_real_move_cursor(XfdesktopIconView *icon_view,
                                     GtkMovementStep step,
                                     gint count)
{
    GdkModifierType modmask = 0;

    g_return_val_if_fail(step == GTK_MOVEMENT_VISUAL_POSITIONS
                         || step == GTK_MOVEMENT_DISPLAY_LINES
                         || step == GTK_MOVEMENT_BUFFER_ENDS, FALSE);

    if(count == 0)
        return FALSE;

    gtk_widget_grab_focus(GTK_WIDGET(icon_view));
    gtk_get_current_event_state(&modmask);

    switch(step) {
        case GTK_MOVEMENT_VISUAL_POSITIONS:
            xfdesktop_icon_view_move_cursor_left_right(icon_view, count, modmask);
            break;

        case GTK_MOVEMENT_DISPLAY_LINES:
            xfdesktop_icon_view_move_cursor_up_down(icon_view, count, modmask);
            break;

        case GTK_MOVEMENT_BUFFER_ENDS:
            xfdesktop_icon_view_move_cursor_begin_end(icon_view, count, modmask);
            break;

        default:
            g_assert_not_reached();
    }

    return TRUE;
}


static void
xfdesktop_screen_size_changed_cb(GdkScreen *gscreen,
                                 gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    
   /* this is kinda icky.  we want to use _NET_WORKAREA to reset the size of
     * the grid, but we can never be sure it'll actually change.  so let's
     * give it 7 seconds, and then fix it manually */
    if(icon_view->priv->grid_resize_timeout)
        g_source_remove(icon_view->priv->grid_resize_timeout);
    icon_view->priv->grid_resize_timeout = g_timeout_add(7000,
                                                         xfdesktop_grid_resize_timeout,
                                                         icon_view);   
}

static void
xfdesktop_icon_view_repaint_icons(XfdesktopIconView *icon_view,
                                  GdkRectangle *area)
{
    GdkRectangle extents, dummy;
    GList *l;
    XfdesktopIcon *icon;
    
    /* fist paint non-selected items, then paint selected items */
    for(l = icon_view->priv->icons; l; l = l->next) {
        icon = (XfdesktopIcon *)l->data;
        if (xfdesktop_icon_view_is_icon_selected(icon_view, icon))
            continue;

        if(!xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)
           || gdk_rectangle_intersect(area, &extents, &dummy))
        {
            xfdesktop_icon_view_paint_icon(icon_view, icon, area);
        }
    }
    
    for(l = icon_view->priv->icons; l; l = l->next) {
        icon = (XfdesktopIcon *)l->data;
        if (!xfdesktop_icon_view_is_icon_selected(icon_view, icon))
            continue;

        if(!xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)
           || gdk_rectangle_intersect(area, &extents, &dummy))
        {
            xfdesktop_icon_view_paint_icon(icon_view, icon, area);
        }
    }
}

static inline gboolean
xfdesktop_rectangle_equal(GdkRectangle *rect1, GdkRectangle *rect2)
{
    return (rect1->x == rect2->x && rect1->y == rect2->y
            && rect1->width == rect2->width && rect1->height == rect2->height);
}

static inline gboolean
xfdesktop_rectangle_is_bounded_by(GdkRectangle *rect,
                                  GdkRectangle *bounds)
{
    GdkRectangle intersection;
    
    if(gdk_rectangle_intersect(rect, bounds, &intersection)) {
        if(xfdesktop_rectangle_equal(rect, &intersection))
            return TRUE;
    }
    
    return FALSE;
}

/* FIXME: add a cache for this so we don't have to compute this EVERY time */
static void
xfdesktop_icon_view_setup_grids_xinerama(XfdesktopIconView *icon_view)
{
    GdkScreen *gscreen;
    GdkRectangle *monitor_geoms, cell_rect;
    gint nmonitors, i, row, col;
    
    TRACE("entering");
    
    gscreen = gtk_widget_get_screen(GTK_WIDGET(icon_view));
    
    nmonitors = gdk_screen_get_n_monitors(gscreen);
    if(nmonitors == 1)  /* optimisation */
        return;
    
    monitor_geoms = g_new0(GdkRectangle, nmonitors);
    for(i = 0; i < nmonitors; ++i)
        gdk_screen_get_monitor_geometry(gscreen, i, &monitor_geoms[i]);

    /* cubic time; w00t! */
    cell_rect.width = cell_rect.height = CELL_SIZE;
    for(row = 0; row < icon_view->priv->nrows; ++row) {
        for(col = 0; col < icon_view->priv->ncols; ++col) {
            gboolean bounded = FALSE;

            cell_rect.x = icon_view->priv->xorigin + icon_view->priv->xmargin + col * CELL_SIZE + col * icon_view->priv->xspacing;
            cell_rect.y = icon_view->priv->yorigin + icon_view->priv->ymargin + row * CELL_SIZE + row * icon_view->priv->yspacing;

            for(i = 0; i < nmonitors; ++i) {
                if(xfdesktop_rectangle_is_bounded_by(&cell_rect,
                                                     &monitor_geoms[i]))
                {
                    bounded = TRUE;
                    break;
                }
            }
            
            if(!bounded) {
                xfdesktop_grid_unset_position_free_raw(icon_view, row, col,
                                                       (gpointer)0xdeadbeef);
            }
        }
    }
    
    g_free(monitor_geoms);
    
    TRACE("exiting");
}
    

static void
xfdesktop_setup_grids(XfdesktopIconView *icon_view)
{
    gint xorigin = 0, yorigin = 0, xrest = 0, yrest = 0, width = 0, height = 0;
    gsize old_size, new_size;
    
    old_size = (guint)icon_view->priv->nrows * icon_view->priv->ncols
               * sizeof(XfdesktopIcon *);
    
    if(!xfdesktop_get_workarea_single(icon_view, 0,
                                      &xorigin, &yorigin,
                                      &width, &height))
    {
        GdkScreen *gscreen = gtk_widget_get_screen(GTK_WIDGET(icon_view));
        width = gdk_screen_get_width(gscreen);
        height = gdk_screen_get_height(gscreen);
        xorigin = yorigin = 0;
    }
    
    icon_view->priv->xorigin = xorigin;
    icon_view->priv->yorigin = yorigin;
    icon_view->priv->width = width;
    icon_view->priv->height = height;

    icon_view->priv->nrows = (height - MIN_MARGIN * 2) / CELL_SIZE;
    icon_view->priv->ncols = (width - MIN_MARGIN * 2) / CELL_SIZE;

    xrest = icon_view->priv->width - icon_view->priv->ncols * CELL_SIZE;
    icon_view->priv->xspacing = (xrest - MIN_MARGIN * 2) / (icon_view->priv->ncols - 1);
    icon_view->priv->xmargin = (xrest - (icon_view->priv->ncols - 1) * icon_view->priv->xspacing) / 2;

    yrest = icon_view->priv->height - icon_view->priv->nrows * CELL_SIZE;
    icon_view->priv->yspacing = (yrest - MIN_MARGIN * 2) / (icon_view->priv->nrows - 1);
    icon_view->priv->ymargin = (yrest - (icon_view->priv->nrows - 1) * icon_view->priv->yspacing) / 2;

    new_size = (guint)icon_view->priv->nrows * icon_view->priv->ncols
               * sizeof(XfdesktopIcon *);

    if(old_size == new_size) {
        DBG("old_size == new_size exiting");
        return;
    }

    XF_DEBUG("CELL_SIZE=%0.3f, TEXT_WIDTH=%0.3f, ICON_SIZE=%u", CELL_SIZE, TEXT_WIDTH, ICON_SIZE);
    XF_DEBUG("grid size is %dx%d", icon_view->priv->nrows, icon_view->priv->ncols);

    if(icon_view->priv->grid_layout) {
        icon_view->priv->grid_layout = g_realloc(icon_view->priv->grid_layout,
                                                 new_size);
        
        if(new_size > old_size) {
            memset(((guint8 *)icon_view->priv->grid_layout) + old_size, 0,
                   new_size - old_size);
        }
    } else
        icon_view->priv->grid_layout = g_malloc0(new_size);
    
    XF_DEBUG("created grid_layout with %lu positions", (gulong)(new_size/sizeof(gpointer)));
    DUMP_GRID_LAYOUT(icon_view);
    
    xfdesktop_icon_view_setup_grids_xinerama(icon_view);
}

static GdkFilterReturn
xfdesktop_rootwin_watch_workarea(GdkXEvent *gxevent,
                                 GdkEvent *event,
                                 gpointer user_data)
{
    XfdesktopIconView *icon_view = user_data;
    XPropertyEvent *xevt = (XPropertyEvent *)gxevent;
    
    if(xevt->type == PropertyNotify
       && XInternAtom(xevt->display, "_NET_WORKAREA", False) == xevt->atom)
    {
        XF_DEBUG("got _NET_WORKAREA change on rootwin!");
        if(icon_view->priv->grid_resize_timeout) {
            g_source_remove(icon_view->priv->grid_resize_timeout);
            icon_view->priv->grid_resize_timeout = 0;
        }
        xfdesktop_grid_do_resize(icon_view);
    }
    
    return GDK_FILTER_CONTINUE;
}

static void
xfdesktop_icon_view_invalidate_icon(XfdesktopIconView *icon_view,
                                    XfdesktopIcon *icon,
                                    gboolean recalc_extents)
{
    GdkRectangle extents;
    gboolean invalidated_something = FALSE;
    gint rtl_offset;
    
    g_return_if_fail(icon);
    
    /*TRACE("entering (recalc=%s)", recalc_extents?"true":"false");*/

    /* we always have to invalidate the old extents */
    if(xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)) {
        if(gtk_widget_get_realized(GTK_WIDGET(icon_view))) {
            gtk_widget_queue_draw_area(GTK_WIDGET(icon_view), extents.x,
                                       extents.y, extents.width,
                                       extents.height);
        }
        invalidated_something = TRUE;
    } else
        recalc_extents = TRUE;
    
    if(recalc_extents) {
        GdkRectangle pixbuf_extents, text_extents, total_extents;

        if(!xfdesktop_icon_view_update_icon_extents(icon_view, icon,
                                                    &pixbuf_extents,
                                                    &text_extents,
                                                    &total_extents,
                                                    &rtl_offset))
        {
            g_warning("Trying to invalidate icon, but can't recalculate extents");
        } else if(gtk_widget_get_realized(GTK_WIDGET(icon_view))) {
            gtk_widget_queue_draw_area(GTK_WIDGET(icon_view),
                                       total_extents.x, total_extents.y,
                                       total_extents.width, total_extents.height);
            invalidated_something = TRUE;
        }
    }

    if(!invalidated_something) {
        DBG("Icon '%s' doesn't have extents: need to call paint some other way",
            xfdesktop_icon_peek_label(icon));
    }
}

static void
xfdesktop_icon_view_invalidate_icon_pixbuf(XfdesktopIconView *icon_view,
                                           XfdesktopIcon *icon)
{
    GdkPixbuf *pix;
    
    pix = xfdesktop_icon_peek_pixbuf(icon, ICON_WIDTH, ICON_SIZE);
    if(pix) {
        GdkRectangle rect = { 0, };
        
        rect.width = gdk_pixbuf_get_width(pix);
        rect.height = gdk_pixbuf_get_height(pix);

        if(!xfdesktop_icon_view_shift_area_to_cell(icon_view, icon, &rect))
            return;
        
        rect.x += CELL_PADDING + ((CELL_SIZE - 2 * CELL_PADDING) - rect.width) / 2;
        rect.y += CELL_PADDING;
    
        if(gtk_widget_get_realized(GTK_WIDGET(icon_view))) {
            gtk_widget_queue_draw_area(GTK_WIDGET(icon_view), rect.x, rect.y,
                                       rect.width, rect.height);
        }
    }
}

static void
xfdesktop_paint_rounded_box(XfdesktopIconView *icon_view,
                            GtkStateType state,
                            GdkRectangle *text_area,
                            GdkRectangle *expose_area)
{
    GdkRectangle box_area, intersection;
    
    box_area = *text_area;
    box_area.x -= LABEL_RADIUS;
    box_area.y -= LABEL_RADIUS;
    box_area.width += LABEL_RADIUS * 2;
    box_area.height += LABEL_RADIUS * 2;
    
    if(gdk_rectangle_intersect(&box_area, expose_area, &intersection)) {
        cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(GTK_WIDGET(icon_view)));
        GtkStyle *style = gtk_widget_get_style(GTK_WIDGET(icon_view));
        double alpha;

        if(state == GTK_STATE_NORMAL)
            alpha = icon_view->priv->label_alpha / 255.;
        else
            alpha = icon_view->priv->selected_label_alpha / 255.;

        cairo_set_source_rgba(cr, style->base[state].red / 65535.,
                              style->base[state].green / 65535.,
                              style->base[state].blue / 65535.,
                              alpha);

        /* restrict painting to expose area */
        gdk_cairo_rectangle(cr, expose_area);
        cairo_clip(cr);

        if(LABEL_RADIUS < 0.1)
            gdk_cairo_rectangle(cr, &box_area);
        else {
            cairo_move_to(cr, box_area.x, box_area.y + LABEL_RADIUS);
            cairo_arc(cr, box_area.x + LABEL_RADIUS,
                      box_area.y + LABEL_RADIUS, LABEL_RADIUS,
                      M_PI, 3.0*M_PI/2.0);
            cairo_line_to(cr, box_area.x + box_area.width - LABEL_RADIUS,
                          box_area.y);
            cairo_arc(cr, box_area.x + box_area.width - LABEL_RADIUS,
                      box_area.y + LABEL_RADIUS, LABEL_RADIUS,
                      3.0+M_PI/2.0, 0.0);
            cairo_line_to(cr, box_area.x + box_area.width,
                          box_area.y + box_area.height - LABEL_RADIUS);
            cairo_arc(cr, box_area.x + box_area.width - LABEL_RADIUS,
                      box_area.y + box_area.height - LABEL_RADIUS,
                      LABEL_RADIUS,
                      0.0, M_PI/2.0);
            cairo_line_to(cr, box_area.x + LABEL_RADIUS,
                          box_area.y + box_area.height);
            cairo_arc(cr, box_area.x + LABEL_RADIUS,
                      box_area.y + box_area.height - LABEL_RADIUS,
                      LABEL_RADIUS,
                      M_PI/2.0, M_PI);
            cairo_close_path(cr);
        }

        cairo_fill(cr);

        cairo_destroy(cr);
    }
}

static gboolean
xfdesktop_icon_view_calculate_icon_pixbuf_area(XfdesktopIconView *icon_view,
                                               XfdesktopIcon *icon,
                                               GdkRectangle *pixbuf_area)
{
    GdkPixbuf *pix;

    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view)
                         && XFDESKTOP_IS_ICON(icon)
                         && pixbuf_area, FALSE);

    pixbuf_area->x = 0;
    pixbuf_area->y = 0;

    pix = xfdesktop_icon_peek_pixbuf(icon, ICON_WIDTH, ICON_SIZE);
    if(G_LIKELY(pix)) {
        pixbuf_area->width = gdk_pixbuf_get_width(pix);
        pixbuf_area->height = gdk_pixbuf_get_height(pix);
    } else {
        /* presumably this should never happen, but... */
        pixbuf_area->width = ICON_SIZE;
        pixbuf_area->height = ICON_SIZE;
    }

    return TRUE;
}

static void
xfdesktop_icon_view_setup_pango_layout(XfdesktopIconView *icon_view,
                                       XfdesktopIcon *icon,
                                       PangoLayout *playout)
{
    const gchar *label = xfdesktop_icon_peek_label(icon);

    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view)
                     && XFDESKTOP_IS_ICON(icon));

    pango_layout_set_ellipsize(playout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_wrap(playout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(playout, TEXT_WIDTH * PANGO_SCALE);
    pango_layout_set_text(playout, label, -1);

    if(!xfdesktop_icon_view_is_icon_selected(icon_view, icon)
       && icon_view->priv->ellipsize_icon_labels) {
        /* constrain the text area */
        pango_layout_set_height(playout, TEXT_HEIGHT * PANGO_SCALE);
        pango_layout_set_ellipsize(playout, PANGO_ELLIPSIZE_END);
    }
}

static gboolean
xfdesktop_icon_view_calculate_icon_text_area(XfdesktopIconView *icon_view,
                                             XfdesktopIcon *icon,
                                             GdkRectangle *text_area)
{
    PangoLayout *playout;
    PangoRectangle prect;

    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view)
                         && XFDESKTOP_IS_ICON(icon)
                         && text_area, FALSE);

    playout = icon_view->priv->playout;
    xfdesktop_icon_view_setup_pango_layout(icon_view, icon, playout);
    pango_layout_get_pixel_extents(playout, NULL, &prect);

    text_area->x = prect.x - SHADOW_EXTENTS - SHADOW_X_OFFSET;
    text_area->y = prect.y - SHADOW_EXTENTS - SHADOW_Y_OFFSET;
    text_area->width = prect.width + 2 * (SHADOW_EXTENTS + SHADOW_X_OFFSET);
    text_area->height = prect.height + 2 * (SHADOW_EXTENTS + SHADOW_Y_OFFSET);

    return TRUE;
}

static gboolean
xfdesktop_icon_view_shift_area_to_cell(XfdesktopIconView *icon_view,
                                       XfdesktopIcon *icon,
                                       GdkRectangle *area)
{
    gint16 row, col;

    if(!xfdesktop_icon_get_position(icon, &row, &col)) {
        g_warning("trying to calculate without a position for icon '%s'",
                  xfdesktop_icon_peek_label(icon));
        return FALSE;
    }

    area->x = icon_view->priv->xorigin + icon_view->priv->xmargin + col * CELL_SIZE + col * icon_view->priv->xspacing;
    area->y = icon_view->priv->yorigin + icon_view->priv->ymargin + row * CELL_SIZE + row * icon_view->priv->yspacing;

    return TRUE;
}

static gboolean
xfdesktop_icon_view_update_icon_extents(XfdesktopIconView *icon_view,
                                        XfdesktopIcon *icon,
                                        GdkRectangle *pixbuf_extents,
                                        GdkRectangle *text_extents,
                                        GdkRectangle *total_extents,
                                        gint *rtl_offset)
{
    GdkRectangle tmp_text;

    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view)
                         && XFDESKTOP_IS_ICON(icon)
                         && pixbuf_extents && text_extents
                         && total_extents && rtl_offset, FALSE);

    if(!xfdesktop_icon_view_calculate_icon_pixbuf_area(icon_view, icon,
                                                       pixbuf_extents)
       || !xfdesktop_icon_view_shift_area_to_cell(icon_view, icon,
                                                  pixbuf_extents))
    {
        return FALSE;
    }
    pixbuf_extents->x += CELL_PADDING + ((CELL_SIZE - CELL_PADDING * 2) - pixbuf_extents->width) / 2;
    pixbuf_extents->y += CELL_PADDING;

    if(!xfdesktop_icon_view_calculate_icon_text_area(icon_view, icon, text_extents))
        return FALSE;

    /* text_extents->x right now includes the padding needed for rtl languages
     * to display properly if it's set */
    *rtl_offset = text_extents->x;

    if(!xfdesktop_icon_view_shift_area_to_cell(icon_view, icon, text_extents))
        return FALSE;

    text_extents->x += (CELL_SIZE - text_extents->width) / 2 - *rtl_offset;
    text_extents->y += ICON_SIZE + SPACING + LABEL_RADIUS + CELL_PADDING;

    tmp_text = *text_extents;
    tmp_text.x -= LABEL_RADIUS + (SHADOW_EXTENTS + SHADOW_X_OFFSET);
    tmp_text.y -= LABEL_RADIUS - (SHADOW_EXTENTS + SHADOW_Y_OFFSET);
    tmp_text.width += LABEL_RADIUS * 2;
    tmp_text.height += LABEL_RADIUS * 2;
    gdk_rectangle_union(pixbuf_extents, &tmp_text, total_extents);

    xfdesktop_icon_set_extents(icon, pixbuf_extents, text_extents, total_extents);

    return TRUE;
}

static void
xfdesktop_icon_view_draw_image(cairo_t *cr, GdkPixbuf *pix, GdkRectangle *rect)
{
    cairo_save(cr);

    gdk_cairo_set_source_pixbuf(cr, pix, rect->x, rect->y);
    cairo_paint(cr);

    cairo_restore(cr);
}

static void
xfdesktop_icon_view_draw_text(cairo_t *cr, PangoLayout *playout, GdkRectangle *text_area,
                              gint x_offset, gint y_offset, gint rtl_offset,
                              gint blur_radius, GdkColor *color)
{
    cairo_save(cr);

    /*  Clip the cairo area to blur the minimum surface */
    gdk_cairo_rectangle(cr, text_area);
    cairo_clip(cr);

    cairo_move_to(cr,
                  text_area->x + x_offset - rtl_offset,
                  text_area->y + y_offset);

    if (blur_radius > 1) {
        cr = gtk_css_shadow_value_start_drawing (cr, blur_radius);
        pango_cairo_show_layout (cr, playout);
        cairo_set_line_width(cr, 1);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_BEVEL);
        pango_cairo_layout_path(cr, playout);
        cairo_stroke(cr);
        cr = gtk_css_shadow_value_finish_drawing (cr, blur_radius, color);
    }
    else {
        gdk_cairo_set_source_color(cr, color);
        pango_cairo_show_layout(cr, playout);
    }

    cairo_restore(cr);
}

static void
xfdesktop_icon_view_paint_icon(XfdesktopIconView *icon_view,
                               XfdesktopIcon *icon,
                               GdkRectangle *area)
{
    GtkWidget *widget = GTK_WIDGET(icon_view);
    gint state, rtl_offset;
    PangoLayout *playout;
    GdkRectangle pixbuf_extents, text_extents, total_extents;
    GdkRectangle intersection;
    gchar x_offset = 0, y_offset = 0;
    GdkColor *sh_text_col = NULL;
    cairo_t *cr;
#ifdef G_ENABLE_DEBUG
    gint16 row, col;
#endif

    TRACE("entering, (%s)(area=%dx%d+%d+%d)", xfdesktop_icon_peek_label(icon),
          area->width, area->height, area->x, area->y);

    playout = icon_view->priv->playout;

    cr = gdk_cairo_create(GDK_DRAWABLE(gtk_widget_get_window(widget)));
    
    if(!xfdesktop_icon_get_extents(icon, &pixbuf_extents,
                                   &text_extents, &total_extents))
    {
        g_warning("Can't get extents for icon '%s'", xfdesktop_icon_peek_label(icon));
    }

    if(!xfdesktop_icon_view_update_icon_extents(icon_view, icon,
                                                &pixbuf_extents,
                                                &text_extents,
                                                &total_extents,
                                                &rtl_offset))
    {
        g_warning("Can't update extents for icon '%s'",
                  xfdesktop_icon_peek_label(icon));
    }

    if(xfdesktop_icon_view_is_icon_selected(icon_view, icon)) {
        if(gtk_widget_has_focus(widget))
            state = GTK_STATE_SELECTED;
        else
            state = GTK_STATE_ACTIVE;
    } else
        state = GTK_STATE_NORMAL;
    
    if(gdk_rectangle_intersect(area, &pixbuf_extents, &intersection)) {
        GdkPixbuf *pix = xfdesktop_icon_peek_pixbuf(icon, ICON_WIDTH, ICON_SIZE);
        GdkPixbuf *pix_free = NULL;

        if(state != GTK_STATE_NORMAL) {
            pix_free = exo_gdk_pixbuf_colorize(pix, &gtk_widget_get_style(widget)->base[state]);
            pix = pix_free;
        }

        if(icon_view->priv->item_under_pointer == icon) {
            GdkPixbuf *tmp = exo_gdk_pixbuf_spotlight(pix);
            if(pix_free)
                g_object_unref(G_OBJECT(pix_free));
            pix = tmp;
            pix_free = tmp;
        }

#ifdef G_ENABLE_DEBUG
        xfdesktop_icon_get_position(icon, &row, &col);

        TRACE("painting pixbuf at %dx%d+%d+%d (row %d ,col %d)",
              pixbuf_extents.width, pixbuf_extents.height,
              pixbuf_extents.x, pixbuf_extents.y,
              row, col);
#endif

        xfdesktop_icon_view_draw_image(cr, pix, &pixbuf_extents);
        
        if(pix_free)
            g_object_unref(G_OBJECT(pix_free));
    }

    /* Only redraw the text if the text area requires it.  */
    if(gdk_rectangle_intersect(area, &text_extents, &intersection)
       && icon_view->priv->font_size > 0)
    {
        text_extents.x += rtl_offset;
        xfdesktop_paint_rounded_box(icon_view, state, &text_extents, area);

        if (state == GTK_STATE_NORMAL) {
            x_offset = icon_view->priv->shadow_x_offset;
            y_offset = icon_view->priv->shadow_y_offset;
            sh_text_col = icon_view->priv->shadow_color;
        } else {
            x_offset = icon_view->priv->selected_shadow_x_offset;
            y_offset = icon_view->priv->selected_shadow_y_offset;
            sh_text_col = icon_view->priv->selected_shadow_color;
        }

        /* draw text shadow for the label text if an offset was defined */
        if(x_offset || y_offset || (icon_view->priv->shadow_blur_radius > 1)) {
            /* Draw the shadow */
            xfdesktop_icon_view_draw_text(cr, playout,
                                          &text_extents,
                                          x_offset,
                                          y_offset,
                                          rtl_offset,
                                          icon_view->priv->shadow_blur_radius,
                                          sh_text_col);
        }

        text_extents.x -= rtl_offset;

        TRACE("painting text at %dx%d+%d+%d",
              text_extents.width, text_extents.height,
              text_extents.x, text_extents.y);

        gtk_paint_layout(gtk_widget_get_style(widget), gtk_widget_get_window(widget),
                         state, FALSE, area, widget, "label",
                         text_extents.x, text_extents.y, playout);

    }


#if 0 /*def DEBUG*/
    {
        GdkRectangle cell = { 0, };
        gint16 row, col;

        if(!xfdesktop_icon_get_position(icon, &row, &col))
            DBG("can't get icon position for '%s'", xfdesktop_icon_peek_label(icon));
        else
            DBG("for icon at (%hu,%hu) (%s)", row, col, xfdesktop_icon_peek_label(icon));

        cairo_set_line_width(cr, 1.0);

        cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0);
        cairo_rectangle(cr, area->x, area->y, area->width, area->height);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 1.0);
        cairo_rectangle(cr, text_extents.x, text_extents.y,
                        text_extents.width, text_extents.height);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 1.0);
        cairo_rectangle(cr, total_extents.x, total_extents.y,
                        total_extents.width, total_extents.height);
        cairo_stroke(cr);

        /* this might not totally paint, but that's ok */
        cell.width = cell.height = CELL_SIZE;
        xfdesktop_icon_view_shift_area_to_cell(icon_view, icon, &cell);

        cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, 1.0);
        cairo_rectangle(cr, cell.x, cell.y, cell.width, cell.height);
        cairo_stroke(cr);


        DBG("cell extents:       %dx%d+%d+%d", cell.width, cell.height, cell.x, cell.y);
        DBG("new pixbuf extents: %dx%d+%d+%d", pixbuf_extents.width, pixbuf_extents.height, pixbuf_extents.x, pixbuf_extents.y);
        DBG("new text extents:   %dx%d+%d+%d", text_extents.width, text_extents.height, text_extents.x, text_extents.y);
        DBG("new total extents:  %dx%d+%d+%d", total_extents.width, total_extents.height, total_extents.x, total_extents.y);
    }
#endif

    cairo_destroy(cr);
}

static void
xfdesktop_move_all_icons_to_pending_icons_list(XfdesktopIconView *icon_view)
{
    GList *l = NULL;
    
    /* move all icons into the pending_icons list and remove from the desktop */
    for(l = icon_view->priv->icons; l; l = l->next) {
        gint16 old_row, old_col;

        if(xfdesktop_icon_get_position(XFDESKTOP_ICON(l->data), &old_row, &old_col))
            xfdesktop_grid_set_position_free(icon_view, old_row, old_col);

        g_signal_handlers_disconnect_by_func(G_OBJECT(l->data),
                                             G_CALLBACK(xfdesktop_icon_view_icon_changed),
                                             icon_view);
    }
    icon_view->priv->pending_icons = g_list_concat(icon_view->priv->icons,
                                                   icon_view->priv->pending_icons);
    icon_view->priv->icons = NULL;

    memset(icon_view->priv->grid_layout, 0,
           (guint)icon_view->priv->nrows * icon_view->priv->ncols
           * sizeof(XfdesktopIcon *));
    
    xfdesktop_setup_grids(icon_view);
}

/* When changing resolutions this moves all the icons that are in the rc file
 * for the new resolution to the desktop and removes them from the pending
 * icons list */
static void
xfdesktop_move_all_cached_icons_to_desktop(XfdesktopIconView *icon_view)
{
#ifdef ENABLE_FILE_ICONS
    GList *l, *leftovers = NULL;
    XfdesktopFileIconManager *fmanager = NULL;

    TRACE("entering");

    if(XFDESKTOP_IS_FILE_ICON_MANAGER(icon_view->priv->manager))
        fmanager = XFDESKTOP_FILE_ICON_MANAGER(icon_view->priv->manager);

    if(fmanager == NULL)
        return;

    xfdesktop_file_icon_save(fmanager);

    /* add all cached icons back */
    for(l = icon_view->priv->pending_icons; l; l = l->next) {
        gint16 row, col;
        XfdesktopIcon *icon = XFDESKTOP_ICON(l->data);
        gchar *identifier = xfdesktop_icon_get_identifier(icon);

        if(!XFDESKTOP_IS_FILE_ICON(icon))
            continue;

        /* Try to get the cached position for the new resolution */
        if(xfdesktop_file_icon_manager_get_cached_icon_position(
                                                            fmanager,
                                                            xfdesktop_icon_peek_label(icon),
                                                            identifier,
                                                            &row,
                                                            &col))
        {
            XF_DEBUG("icon %s setting position row%dxcol%d",
                     xfdesktop_icon_peek_label(icon), row, col);

            /* Make sure the spot is available */
            if(xfdesktop_grid_is_free_position(icon_view, row, col)) {
                xfdesktop_icon_set_position(icon, row, col);
                xfdesktop_icon_view_add_item_internal(icon_view, icon);
            } else {
                leftovers = g_list_prepend(leftovers, icon);
            }
        } else {
            leftovers = g_list_prepend(leftovers, icon);
        }

        if(identifier)
            g_free(identifier);
    }

    g_list_free(icon_view->priv->pending_icons);
    icon_view->priv->pending_icons = g_list_reverse(leftovers);
#endif
}

/* Takes any icons in the pending icons list that has their original spot open.
 * This way icons stay somewhat stable during minor resolution changes */
static void
xfdesktop_move_all_previous_icons_to_desktop(XfdesktopIconView *icon_view)
{
    GList *l, *leftovers = NULL;

    TRACE("entering");

    /* add all pending icons back if their space is still available */
    for(l = icon_view->priv->pending_icons; l; l = l->next) {
        gint16 row, col;
        XfdesktopIcon *icon = XFDESKTOP_ICON(l->data);

        if(!xfdesktop_icon_get_position(icon, &row, &col)) {
            g_warning("Trying to set previous position of an icon with no position");
        }

        if(xfdesktop_grid_is_free_position(icon_view, row, col)) {
            XF_DEBUG("adding icon %s position row %d x col %d",
                     xfdesktop_icon_peek_label(icon), row, col);
            xfdesktop_icon_view_add_item_internal(icon_view, icon);
        } else {
            leftovers = g_list_prepend(leftovers, icon);
        }
    }

    g_list_free(icon_view->priv->pending_icons);
    icon_view->priv->pending_icons = g_list_reverse(leftovers);
}

/* Takes any icons in the pending icons list and adds them where there is space */
static void
xfdesktop_append_all_pending_icons(XfdesktopIconView *icon_view)
{
    GList *l, *leftovers = NULL;

    TRACE("entering");

    /* add all pending icons back if space is available */
    for(l = icon_view->priv->pending_icons; l; l = l->next) {
        gint16 row, col;
        XfdesktopIcon *icon = XFDESKTOP_ICON(l->data);

        if(xfdesktop_grid_get_next_free_position(icon_view, &row, &col)) {
            xfdesktop_icon_set_position(icon, row, col);
            xfdesktop_icon_view_add_item_internal(icon_view, icon);
        } else {
            leftovers = g_list_prepend(leftovers, icon);
        }
    }

    g_list_free(icon_view->priv->pending_icons);
    icon_view->priv->pending_icons = g_list_reverse(leftovers);
}

static void
xfdesktop_move_all_pending_icons_to_desktop(XfdesktopIconView *icon_view)
{
    if(!XFDESKTOP_IS_ICON_VIEW(icon_view))
        return;

    if(icon_view->priv->grid_layout == NULL)
        return;

    xfdesktop_move_all_cached_icons_to_desktop(icon_view);
    xfdesktop_move_all_previous_icons_to_desktop(icon_view);
    xfdesktop_append_all_pending_icons(icon_view);
}

static void
xfdesktop_grid_do_resize(XfdesktopIconView *icon_view)
{
    gint xorigin = 0, yorigin = 0, width = 0, height = 0;
    gint16 new_rows, new_cols;
    gsize old_size, new_size;
    GdkScreen *gscreen;

    /* First check to see if the grid actaully did change. This way
     * we don't remove all the icons just to put them back again */
    old_size = (guint)icon_view->priv->nrows * icon_view->priv->ncols
               * sizeof(XfdesktopIcon *);

    if(!xfdesktop_get_workarea_single(icon_view, 0,
                                      &xorigin, &yorigin,
                                      &width, &height))
    {
        gscreen = gtk_widget_get_screen(GTK_WIDGET(icon_view));
        width = gdk_screen_get_width(gscreen);
        height = gdk_screen_get_height(gscreen);
    }

    new_rows = (height - MIN_MARGIN * 2) / CELL_SIZE;
    new_cols = (width - MIN_MARGIN * 2) / CELL_SIZE;

    new_size = (guint)new_rows * new_cols * sizeof(XfdesktopIcon *);

    if(old_size != new_size) {
        DBG("old_size != new_size use cache icon list");
        #if 0 /*def DEBUG*/
            DUMP_GRID_LAYOUT(icon_view);
        #endif

        /* Grid size did change */
        xfdesktop_move_all_icons_to_pending_icons_list(icon_view);
        xfdesktop_move_all_pending_icons_to_desktop(icon_view);

        #if 0 /*def DEBUG*/
            DUMP_GRID_LAYOUT(icon_view);
        #endif

        /* Fire off an event to notify others of the change */
        g_signal_emit(G_OBJECT(icon_view), __signals[SIG_RESIZE_EVENT], 0, NULL);
    }
    else {
        DBG("old_size == new_size updating grid");
        xfdesktop_setup_grids (icon_view);
    }

    gtk_widget_queue_draw(GTK_WIDGET(icon_view));
}

static gboolean
xfdesktop_grid_resize_timeout(gpointer user_data)
{
    XfdesktopIconView *icon_view = user_data;
    
    xfdesktop_grid_do_resize(icon_view);
    
    icon_view->priv->grid_resize_timeout = 0;
    return FALSE;
}


gboolean
xfdesktop_get_workarea_single(XfdesktopIconView *icon_view,
                              guint ws_num,
                              gint *xorigin,
                              gint *yorigin,
                              gint *width,
                              gint *height)
{
    gboolean ret = FALSE;
    GdkScreen *gscreen;
    Display *dpy;
    Window root;
    Atom property, actual_type = None;
    gint actual_format = 0, first_id;
    gulong nitems = 0, bytes_after = 0, offset = 0, tmp_size = 0;
    unsigned char *data_p = NULL;
    
    g_return_val_if_fail(xorigin && yorigin
                         && width && height, FALSE);
    
    gscreen = gtk_widget_get_screen(GTK_WIDGET(icon_view));
    dpy = GDK_DISPLAY_XDISPLAY(gdk_screen_get_display(gscreen));
    root = GDK_WINDOW_XID(gdk_screen_get_root_window(gscreen));
    property = XInternAtom(dpy, "_NET_WORKAREA", False);
    
    first_id = ws_num * 4;
    
    gdk_error_trap_push();
    
    do {
        if(Success == XGetWindowProperty(dpy, root, property, offset,
                                         G_MAXULONG, False, XA_CARDINAL,
                                         &actual_type, &actual_format, &nitems,
                                         &bytes_after, &data_p))
        {
            gint i;
            gulong *data;

            if(actual_format != 32 || actual_type != XA_CARDINAL) {
                XFree(data_p);
                break;
            }

            tmp_size = (actual_format / 8) * nitems;
            if(actual_format == 32) {
                tmp_size *= sizeof(long)/4;
            }

            data = g_malloc(tmp_size);
            memcpy(data, data_p, tmp_size);
            
            i = offset / 32;  /* first element id in this batch */
            
            /* there's probably a better way to do this. */
            if(i + (glong)nitems >= first_id && first_id - (glong)offset >= 0)
                *xorigin = data[first_id - offset] + MIN_MARGIN;
            if(i + (glong)nitems >= first_id + 1 && first_id - (glong)offset + 1 >= 0)
                *yorigin = data[first_id - offset + 1] + MIN_MARGIN;
            if(i + (glong)nitems >= first_id + 2 && first_id - (glong)offset + 2 >= 0)
                *width = data[first_id - offset + 2] - 2 * MIN_MARGIN;
            if(i + (glong)nitems >= first_id + 3 && first_id - (glong)offset + 3 >= 0) {
                *height = data[first_id - offset + 3] - 2 * MIN_MARGIN;
                ret = TRUE;
                XFree(data_p);
                g_free(data);
                break;
            }
            
            offset += actual_format * nitems;
            XFree(data_p);
            g_free(data);
        } else
            break;
    } while(bytes_after > 0);
    
    gdk_error_trap_pop();
    
    return ret;
}

static inline gboolean
xfdesktop_grid_is_free_position(XfdesktopIconView *icon_view,
                                gint16 row,
                                gint16 col)
{
    g_return_val_if_fail(icon_view->priv->grid_layout != NULL, FALSE);

    if(row >= icon_view->priv->nrows
       || col >= icon_view->priv->ncols)
    {
        return FALSE;
    }
    
    return !icon_view->priv->grid_layout[col * icon_view->priv->nrows + row];
}


static gboolean
xfdesktop_grid_get_next_free_position(XfdesktopIconView *icon_view,
                                      gint16 *row,
                                      gint16 *col)
{
    gint i, maxi;
    
    g_return_val_if_fail(row && col, FALSE);
    
    maxi = icon_view->priv->nrows * icon_view->priv->ncols;
    for(i = 0; i < maxi; ++i) {
        if(!icon_view->priv->grid_layout[i]) {
            *row = i % icon_view->priv->nrows;
            *col = i / icon_view->priv->nrows;
            return TRUE;
        }
    }
    
    return FALSE;
}


static inline void
xfdesktop_grid_set_position_free(XfdesktopIconView *icon_view,
                                 gint16 row,
                                 gint16 col)
{
    g_return_if_fail(row < icon_view->priv->nrows
                     && col < icon_view->priv->ncols);
    
#if 0 /*def DEBUG*/
    DUMP_GRID_LAYOUT(icon_view);
#endif

    icon_view->priv->grid_layout[col * icon_view->priv->nrows + row] = NULL;

#if 0 /*def DEBUG*/
    DUMP_GRID_LAYOUT(icon_view);
#endif
}

static inline gboolean
xfdesktop_grid_unset_position_free_raw(XfdesktopIconView *icon_view,
                                       gint16 row,
                                       gint16 col,
                                       gpointer data)
{
    gint idx;
    
    g_return_val_if_fail(row < icon_view->priv->nrows
                         && col < icon_view->priv->ncols, FALSE);
    
    idx = col * icon_view->priv->nrows + row;
    if(icon_view->priv->grid_layout[idx])
        return FALSE;

#if 0 /*def DEBUG*/
    DUMP_GRID_LAYOUT(icon_view);
#endif

    icon_view->priv->grid_layout[idx] = data;

#if 0 /*def DEBUG*/
    DUMP_GRID_LAYOUT(icon_view);
#endif

    return TRUE;
}

static inline gboolean
xfdesktop_grid_unset_position_free(XfdesktopIconView *icon_view,
                                   XfdesktopIcon *icon)
{
    gint16 row, col;
    
    if(!xfdesktop_icon_get_position(icon, &row, &col)) {
        g_warning("Trying to set free position of an icon with no position");
        return FALSE;
    }
    
    return xfdesktop_grid_unset_position_free_raw(icon_view, row, col, icon);
}

static inline XfdesktopIcon *
xfdesktop_icon_view_icon_in_cell_raw(XfdesktopIconView *icon_view,
                                     gint idx)
{
    XfdesktopIcon *icon = icon_view->priv->grid_layout[idx];
    
    if((gpointer)0xdeadbeef == icon)
        return NULL;
    
    return icon;
}

static inline XfdesktopIcon *
xfdesktop_icon_view_icon_in_cell(XfdesktopIconView *icon_view,
                                 gint16 row,
                                 gint16 col)
{
    gint idx;
    
    g_return_val_if_fail(row < icon_view->priv->nrows
                         && col < icon_view->priv->ncols, NULL);
    
    idx = col * icon_view->priv->nrows + row;
    return xfdesktop_icon_view_icon_in_cell_raw(icon_view, idx);
}

static inline gboolean
xfdesktop_rectangle_contains_point(GdkRectangle *rect, gint x, gint y)
{
    if(x > rect->x + rect->width
            || x < rect->x
            || y > rect->y + rect->height
            || y < rect->y)
    {
        return FALSE;
    }
    
    return TRUE;
}

static gint
xfdesktop_check_icon_clicked(gconstpointer data,
                             gconstpointer user_data)
{
    XfdesktopIcon *icon = XFDESKTOP_ICON(data);
    GdkEventButton *evt = (GdkEventButton *)user_data;
    GdkRectangle extents;
    
    if(xfdesktop_icon_get_extents(icon, NULL, NULL, &extents)
       && xfdesktop_rectangle_contains_point(&extents, evt->x, evt->y))
    {
        return 0;
    } else
        return 1;
}

static void
xfdesktop_list_foreach_invalidate(gpointer data,
                                  gpointer user_data)
{
    XfdesktopIconView *icon_view = XFDESKTOP_ICON_VIEW(user_data);
    XfdesktopIcon *icon = XFDESKTOP_ICON(data);
    xfdesktop_icon_view_invalidate_icon(icon_view, icon, TRUE);
}

static void
xfdesktop_icon_view_modify_font_size(XfdesktopIconView *icon_view,
                                     gdouble size)
{
    const PangoFontDescription *pfd;
    PangoFontDescription *pfd_new;
    
    pfd = pango_layout_get_font_description(icon_view->priv->playout);
    if(pfd)
        pfd_new = pango_font_description_copy(pfd);
    else
        pfd_new = pango_font_description_new();
    
    pango_font_description_set_size(pfd_new, (gint)(size * PANGO_SCALE));
    
    pango_layout_set_font_description(icon_view->priv->playout, pfd_new);
    
    pango_font_description_free(pfd_new);
}

static void
xfdesktop_icon_view_icon_changed(XfdesktopIcon *icon,
                                 gpointer user_data)
{
    /* maybe can pass FALSE here */
    xfdesktop_icon_view_invalidate_icon(XFDESKTOP_ICON_VIEW(user_data),
                                        icon, TRUE);
}

static gboolean
xfdesktop_icon_view_is_icon_selected(XfdesktopIconView *icon_view,
                                     XfdesktopIcon *icon)
{
    return (g_list_find(icon_view->priv->selected_icons, icon)) == NULL ? FALSE : TRUE;
}


/* public api */


GtkWidget *
xfdesktop_icon_view_new(XfdesktopIconViewManager *manager)
{
    XfdesktopIconView *icon_view;
    
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW_MANAGER(manager), NULL);
    
    icon_view = g_object_new(XFDESKTOP_TYPE_ICON_VIEW, NULL);
    icon_view->priv->manager = manager;

    icon_view->priv->channel = xfconf_channel_get(XFDESKTOP_CHANNEL);

    xfconf_g_property_bind(icon_view->priv->channel,
                           "/desktop-icons/single-click",
                           G_TYPE_BOOLEAN,
                           G_OBJECT(icon_view),
                           "single_click");

    xfconf_g_property_bind(icon_view->priv->channel,
                           "/desktop-icons/show-tooltips",
                           G_TYPE_BOOLEAN,
                           G_OBJECT(icon_view),
                           "show_tooltips");

    xfconf_g_property_bind(icon_view->priv->channel,
                           "/desktop-icons/tooltip-size",
                           G_TYPE_DOUBLE,
                           G_OBJECT(icon_view),
                           "tooltip_size");
    
    return GTK_WIDGET(icon_view);
}

static void
xfdesktop_icon_view_add_item_internal(XfdesktopIconView *icon_view,
                                      XfdesktopIcon *icon)
{
    gint16 row, col;
    GdkRectangle fake_area;
    
    /* sanity check: at this point this should be taken care of */
    if(!xfdesktop_icon_get_position(icon, &row, &col)) {
        g_warning("Attempting to add item without a position");
        return;
    }
    
    xfdesktop_grid_unset_position_free(icon_view, icon);
    
    icon_view->priv->icons = g_list_prepend(icon_view->priv->icons, icon);
    
    g_signal_connect(G_OBJECT(icon), "pixbuf-changed",
                     G_CALLBACK(xfdesktop_icon_view_icon_changed),
                     icon_view);
    g_signal_connect(G_OBJECT(icon), "label-changed",
                     G_CALLBACK(xfdesktop_icon_view_icon_changed),
                     icon_view);

    fake_area.x = icon_view->priv->xorigin + icon_view->priv->xmargin + col * CELL_SIZE + col * icon_view->priv->xspacing;
    fake_area.y = icon_view->priv->yorigin + icon_view->priv->ymargin + row * CELL_SIZE + row * icon_view->priv->yspacing;
    fake_area.width = fake_area.height = CELL_SIZE;
    xfdesktop_icon_view_paint_icon(icon_view, icon, &fake_area);
}

static gboolean
xfdesktop_icon_view_icon_find_position(XfdesktopIconView *icon_view,
                                       XfdesktopIcon *icon)
{
    gint16 row, col;
    
    if(!xfdesktop_icon_get_position(icon, &row, &col)
       || !xfdesktop_grid_is_free_position(icon_view, row, col))
    {
        if(xfdesktop_grid_get_next_free_position(icon_view, &row, &col)) {
            XF_DEBUG("old position didn't exist or isn't free, got (%d,%d) instead",
                     row, col);
            xfdesktop_icon_set_position(icon, row, col);
        } else {
            XF_DEBUG("can't fit icon on screen");
            return FALSE;
        }
    }
    
    return TRUE;
}

void
xfdesktop_icon_view_add_item(XfdesktopIconView *icon_view,
                             XfdesktopIcon *icon)
{
    gint16 row, col;
    
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view)
                     && XFDESKTOP_IS_ICON(icon));
    
    /* ensure the icon isn't already in an icon view */
    g_return_if_fail(!g_object_get_data(G_OBJECT(icon),
                                        "--xfdesktop-icon-view"));
    
    g_object_set_data(G_OBJECT(icon), "--xfdesktop-icon-view", icon_view);
    g_object_ref(G_OBJECT(icon));
    
    if(!gtk_widget_get_realized(GTK_WIDGET(icon_view))) {
        /* if we aren't realized, we don't know what our grid looks like, so
         * just hang onto the icon for later */
        if(xfdesktop_icon_get_position(icon, &row, &col)) {
            icon_view->priv->pending_icons = g_list_prepend(icon_view->priv->pending_icons,
                                                            icon);
        } else {
            icon_view->priv->pending_icons = g_list_append(icon_view->priv->pending_icons,
                                                           icon);
        }
    } else {
        if(xfdesktop_icon_view_icon_find_position(icon_view, icon))
            xfdesktop_icon_view_add_item_internal(icon_view, icon);
        else {
            icon_view->priv->pending_icons = g_list_append(icon_view->priv->pending_icons,
                                                           icon);
        }
    }
}

void
xfdesktop_icon_view_remove_item(XfdesktopIconView *icon_view,
                                XfdesktopIcon *icon)
{
    gint16 row, col;
    GList *l;
    
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view)
                     && XFDESKTOP_IS_ICON(icon));
    
    l = g_list_find(icon_view->priv->icons, icon);
    if(l) {
        g_signal_handlers_disconnect_by_func(G_OBJECT(icon),
                                             G_CALLBACK(xfdesktop_icon_view_icon_changed),
                                             icon_view);
        
        if(xfdesktop_icon_get_position(icon, &row, &col)) {
            xfdesktop_icon_view_invalidate_icon(icon_view, icon, FALSE);
            xfdesktop_grid_set_position_free(icon_view, row, col);
        }
        icon_view->priv->icons = g_list_delete_link(icon_view->priv->icons, l);
        icon_view->priv->selected_icons = g_list_remove(icon_view->priv->selected_icons,
                                                        icon);
        if(icon_view->priv->cursor == icon) {
            icon_view->priv->cursor = NULL;
            if(icon_view->priv->selected_icons)
                icon_view->priv->cursor = icon_view->priv->selected_icons->data;
        }
        if(icon_view->priv->first_clicked_item == icon)
            icon_view->priv->first_clicked_item = NULL;
        if(icon_view->priv->item_under_pointer == icon)
            icon_view->priv->item_under_pointer = NULL;
    } else if((l = g_list_find(icon_view->priv->pending_icons, icon))) {
        icon_view->priv->pending_icons = g_list_delete_link(icon_view->priv->pending_icons,
                                                            l);
    } else {
        g_warning("Attempt to remove icon %p from XfdesktopIconView %p, but it's not in there.",
                  icon, icon_view);
        return;
    }
    
    g_object_set_data(G_OBJECT(icon), "--xfdesktop-icon-view", NULL);
    g_object_unref(G_OBJECT(icon));

    if(icon_view->priv->pending_icons != NULL) {
        /* Move in any pending icons to the space available */
        xfdesktop_move_all_pending_icons_to_desktop(icon_view);
    }
}

void
xfdesktop_icon_view_remove_all(XfdesktopIconView *icon_view)
{
    GList *l;
    gint16 row, col;
    
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(icon_view->priv->pending_icons) {
        g_list_foreach(icon_view->priv->pending_icons, (GFunc)g_object_unref,
                       NULL);
        g_list_free(icon_view->priv->pending_icons);
        icon_view->priv->pending_icons = NULL;
    }
    
    for(l = icon_view->priv->icons; l; l = l->next) {
        XfdesktopIcon *icon = XFDESKTOP_ICON(l->data);
        if(xfdesktop_icon_get_position(icon, &row, &col)) {
            xfdesktop_icon_view_invalidate_icon(icon_view, icon, FALSE);
            xfdesktop_grid_set_position_free(icon_view, row, col);
        }
        
        g_signal_handlers_disconnect_by_func(G_OBJECT(l->data),
                                             G_CALLBACK(xfdesktop_icon_view_icon_changed),
                                             icon_view);
        g_object_set_data(G_OBJECT(l->data), "--xfdesktop-icon-view", NULL);
        g_object_unref(G_OBJECT(l->data));
    }
    
    if(G_LIKELY(icon_view->priv->icons)) {
        g_list_free(icon_view->priv->icons);
        icon_view->priv->icons = NULL;
    }
    
    if(icon_view->priv->selected_icons) {
        g_list_free(icon_view->priv->selected_icons);
        icon_view->priv->selected_icons = NULL;
    }
    
    icon_view->priv->item_under_pointer = NULL;
    icon_view->priv->cursor = NULL;
    icon_view->priv->first_clicked_item = NULL;
}

void
xfdesktop_icon_view_set_selection_mode(XfdesktopIconView *icon_view,
                                       GtkSelectionMode mode)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    g_return_if_fail(mode <= GTK_SELECTION_MULTIPLE);
    
    if(mode == icon_view->priv->sel_mode)
        return;
    
    icon_view->priv->sel_mode = mode;
    
    switch(mode) {
        case GTK_SELECTION_NONE:
            g_warning("GTK_SELECTION_NONE is not implemented for " \
                      "XfdesktopIconView.  Falling back to " \
                      "GTK_SELECTION_SINGLE.");
            icon_view->priv->sel_mode = GTK_SELECTION_SINGLE;
            /* fall through */
        case GTK_SELECTION_SINGLE:
            if(g_list_length(icon_view->priv->selected_icons) > 1) {
                GList *l;
                /* TODO: enable later and make sure it works */
                /*gdk_window_freeze_updates(GTK_WIDGET(icon_view)->window);*/
                for(l = icon_view->priv->selected_icons->next; l; l = l->next) {
                    xfdesktop_icon_view_unselect_item(icon_view,
                                                      XFDESKTOP_ICON(l->data));
                }
                /*gdk_window_thaw_updates(GTK_WIDGET(icon_view)->window);*/
            }
            icon_view->priv->allow_rubber_banding = FALSE;
            break;
        
        case GTK_SELECTION_BROWSE:
            g_warning("GTK_SELECTION_BROWSE is not implemented for " \
                  "XfdesktopIconView.  Falling back to " \
                  "GTK_SELECTION_MULTIPLE.");
            icon_view->priv->sel_mode = GTK_SELECTION_MULTIPLE;
            /* fall through */
        default:
            icon_view->priv->allow_rubber_banding = TRUE;
            break;
    }
}

GtkSelectionMode
xfdesktop_icon_view_get_selection_mode(XfdesktopIconView *icon_view)
{
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view),
                         GTK_SELECTION_NONE);
    
    return icon_view->priv->sel_mode;
}

void
xfdesktop_icon_view_enable_drag_source(XfdesktopIconView *icon_view,
                                       GdkModifierType start_button_mask,
                                       const GtkTargetEntry *targets,
                                       gint n_targets,
                                       GdkDragAction actions)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(icon_view->priv->drag_source_set) {
        gtk_target_list_unref(icon_view->priv->source_targets);
        icon_view->priv->source_targets = gtk_target_list_new(icon_view_targets,
                                                              icon_view_n_targets);
    }
    
    icon_view->priv->foreign_source_actions = actions;
    icon_view->priv->foreign_source_mask = start_button_mask;
    
    gtk_target_list_add_table(icon_view->priv->source_targets, targets,
                              n_targets);
    
    gtk_drag_source_set(GTK_WIDGET(icon_view), start_button_mask, NULL, 0,
                        GDK_ACTION_MOVE | actions);
    gtk_drag_source_set_target_list(GTK_WIDGET(icon_view),
                                    icon_view->priv->source_targets);
    
    icon_view->priv->drag_source_set = TRUE;
}

void
xfdesktop_icon_view_enable_drag_dest(XfdesktopIconView *icon_view,
                                     const GtkTargetEntry *targets,
                                     gint n_targets,
                                     GdkDragAction actions)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(icon_view->priv->drag_dest_set) {
        gtk_target_list_unref(icon_view->priv->dest_targets);
        icon_view->priv->dest_targets = gtk_target_list_new(icon_view_targets,
                                                            icon_view_n_targets);
    }
    
    icon_view->priv->foreign_dest_actions = actions;
    
    gtk_target_list_add_table(icon_view->priv->dest_targets, targets,
                              n_targets);
    
    gtk_drag_dest_set(GTK_WIDGET(icon_view), 0, NULL, 0,
                      GDK_ACTION_MOVE | actions);
    gtk_drag_dest_set_target_list(GTK_WIDGET(icon_view),
                                  icon_view->priv->dest_targets);
    
    icon_view->priv->drag_dest_set = TRUE;
}

void
xfdesktop_icon_view_unset_drag_source(XfdesktopIconView *icon_view)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(!icon_view->priv->drag_source_set)
        return;
    
    if(icon_view->priv->source_targets)
        gtk_target_list_unref(icon_view->priv->source_targets);
    
    icon_view->priv->source_targets = gtk_target_list_new(icon_view_targets,
                                                              icon_view_n_targets);
    
    gtk_drag_source_set(GTK_WIDGET(icon_view), 0, NULL, 0, GDK_ACTION_MOVE);
    gtk_drag_source_set_target_list(GTK_WIDGET(icon_view),
                                    icon_view->priv->source_targets);
    
    icon_view->priv->drag_source_set = FALSE;
}

void
xfdesktop_icon_view_unset_drag_dest(XfdesktopIconView *icon_view)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(!icon_view->priv->drag_dest_set)
        return;
    
    if(icon_view->priv->dest_targets)
        gtk_target_list_unref(icon_view->priv->dest_targets);
    
    icon_view->priv->dest_targets = gtk_target_list_new(icon_view_targets,
                                                        icon_view_n_targets);
    
    gtk_drag_dest_set(GTK_WIDGET(icon_view), 0, NULL, 0, GDK_ACTION_MOVE);
    gtk_drag_dest_set_target_list(GTK_WIDGET(icon_view),
                                  icon_view->priv->dest_targets);
    
    icon_view->priv->drag_dest_set = FALSE;
}

XfdesktopIcon *
xfdesktop_icon_view_widget_coords_to_item(XfdesktopIconView *icon_view,
                                          gint wx,
                                          gint wy)
{
    gint16 row, col;
    
    xfdesktop_xy_to_rowcol(icon_view, wx, wy, &row, &col);
    if(row >= icon_view->priv->nrows
       || col >= icon_view->priv->ncols)
    {
        return NULL;
    }
    
    return xfdesktop_icon_view_icon_in_cell(icon_view, row, col);
}

GList *
xfdesktop_icon_view_get_selected_items(XfdesktopIconView *icon_view)
{
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view), NULL);
    
    return g_list_copy(icon_view->priv->selected_icons);
}

void
xfdesktop_icon_view_select_item(XfdesktopIconView *icon_view,
                                XfdesktopIcon *icon)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(xfdesktop_icon_view_is_icon_selected(icon_view, icon))
        return;
    
    if(icon_view->priv->sel_mode == GTK_SELECTION_SINGLE)
        xfdesktop_icon_view_unselect_all(icon_view);
    
    icon_view->priv->selected_icons = g_list_prepend(icon_view->priv->selected_icons,
                                                     icon);
    xfdesktop_icon_view_invalidate_icon(icon_view, icon, TRUE);
    
    g_signal_emit(G_OBJECT(icon_view),
                  __signals[SIG_ICON_SELECTION_CHANGED],
                  0, NULL);
    xfdesktop_icon_selected(icon);
}

void
xfdesktop_icon_view_select_all(XfdesktopIconView *icon_view)
{
    GList *l;

    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));

    if(!icon_view->priv->icons)
        return;

    if(icon_view->priv->selected_icons
       && g_list_length(icon_view->priv->icons)
          == g_list_length(icon_view->priv->selected_icons))
    {
        return;
    }

    /* simplify: just free the entire list and repopulate it */
    if(icon_view->priv->selected_icons) {
        g_list_free(icon_view->priv->selected_icons);
        icon_view->priv->selected_icons = NULL;
    }

    for(l = icon_view->priv->icons; l; l = l->next) {
        icon_view->priv->selected_icons = g_list_prepend(icon_view->priv->selected_icons, l->data);
        xfdesktop_icon_view_invalidate_icon(icon_view, l->data, TRUE);
        xfdesktop_icon_selected(l->data);
    }

    g_signal_emit(G_OBJECT(icon_view),
                  __signals[SIG_ICON_SELECTION_CHANGED],
                  0, NULL);
}

void
xfdesktop_icon_view_unselect_item(XfdesktopIconView *icon_view,
                                  XfdesktopIcon *icon)
{
    GList *l;

    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view)
                     && XFDESKTOP_IS_ICON(icon));
    
    l = g_list_find(icon_view->priv->selected_icons, icon);
    if(l) {
        icon_view->priv->selected_icons = g_list_delete_link(icon_view->priv->selected_icons,
                                                             l);
        xfdesktop_icon_view_invalidate_icon(icon_view, icon, TRUE);
        g_signal_emit(G_OBJECT(icon_view),
                      __signals[SIG_ICON_SELECTION_CHANGED],
                      0, NULL);
    }
}

void
xfdesktop_icon_view_unselect_all(XfdesktopIconView *icon_view)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(icon_view->priv->selected_icons) {
        GList *repaint_icons = icon_view->priv->selected_icons;
        icon_view->priv->selected_icons = NULL;
        g_list_foreach(repaint_icons, xfdesktop_list_foreach_invalidate,
                       icon_view);
        g_list_free(repaint_icons);
        g_signal_emit(G_OBJECT(icon_view),
                      __signals[SIG_ICON_SELECTION_CHANGED],
                      0, NULL);
    }
}

void
xfdesktop_icon_view_set_icon_size(XfdesktopIconView *icon_view,
                                  guint icon_size)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(icon_size == icon_view->priv->icon_size)
        return;
    
    icon_view->priv->icon_size = icon_size;
    
    if(gtk_widget_get_realized(GTK_WIDGET(icon_view))) {
        xfdesktop_grid_do_resize(icon_view);
        gtk_widget_queue_draw(GTK_WIDGET(icon_view));
    }
}

guint
xfdesktop_icon_view_get_icon_size(XfdesktopIconView *icon_view)
{
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view), 0);
    return icon_view->priv->icon_size;
}

void
xfdesktop_icon_view_set_font_size(XfdesktopIconView *icon_view,
                                  gdouble font_size_points)
{
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    if(font_size_points == icon_view->priv->font_size)
        return;
    
    icon_view->priv->font_size = font_size_points;
    
    if(gtk_widget_get_realized(GTK_WIDGET(icon_view))) {
        xfdesktop_icon_view_modify_font_size(icon_view, font_size_points);
        xfdesktop_grid_do_resize(icon_view);
        gtk_widget_queue_draw(GTK_WIDGET(icon_view));
    }
}

gdouble
xfdesktop_icon_view_get_font_size(XfdesktopIconView *icon_view)
{
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view), 0.0);
    return icon_view->priv->font_size;
}

GtkWidget *
xfdesktop_icon_view_get_window_widget(XfdesktopIconView *icon_view)
{
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW(icon_view), NULL);
    
    return icon_view->priv->parent_window;
}

#if defined(DEBUG) && DEBUG > 0
guint
_xfdesktop_icon_view_n_items(XfdesktopIconView *icon_view)
{
    return g_list_length(icon_view->priv->pending_icons) + g_list_length(icon_view->priv->icons);
}
#endif
