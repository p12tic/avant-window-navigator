/*
 * Copyright (C) 2008 Neil Jagdish Patel <njpatel@gmail.com>
 * Copyright (C) 2009 Rodney Cryderman <rcryderman@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as 
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by Neil Jagdish Patel <njpatel@gmail.com>
 *             Hannes Verschore <hv1989@gmail.com>
 *             Rodney Cryderman <rcryderman@gmail.com>
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libwnck/libwnck.h>

#include <libdesktop-agnostic/vfs.h>
#include "libawn/gseal-transition.h"
#include <libawn/awn-utils.h>

#include "taskmanager-marshal.h"
#include "task-icon.h"

#include "task-launcher.h"
#include "task-settings.h"
#include "task-manager.h"

//#define DEBUG 1

enum{
  DESKTOP_COPY_ALL=0,
  DESKTOP_COPY_OWNER,
  DESKTOP_COPY_NONE
}DesktopCopy;

G_DEFINE_TYPE (TaskIcon, task_icon, AWN_TYPE_THEMED_ICON)

#if !GTK_CHECK_VERSION(2,14,0)
#define GTK_ICON_LOOKUP_FORCE_SIZE 0
#endif

#define TASK_ICON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj),\
  TASK_TYPE_ICON, \
  TaskIconPrivate))

struct _TaskIconPrivate
{
  //List containing the TaskItems
  GSList *items;

  //The number of TaskItems that get shown
  guint shown_items;

  //The number of TaskWindows (subclass of TaskItem) that needs attention
  guint needs_attention;

  //The number of TaskWindows (subclass of TaskItem) that have the active state.
  guint is_active;

  //The main item being used for the icon (and if alone for the text)
  TaskItem *main_item;

  //Whetever this icon is visible or not
  gboolean visible;

  //An overlay for showing number of items
  AwnOverlayText *overlay_text;

  // Config client
  DesktopAgnosticConfigClient *client;
  
  GdkPixbuf *icon;
  AwnApplet *applet;
  GtkWidget *dialog;

  /*context menu*/
  GtkWidget * menu;

  gboolean draggable;
  gboolean gets_dragged;

  guint    drag_tag;
  gboolean drag_motion;
  guint    drag_time;
  gint     drag_and_drop_hover_delay;

  gint old_width;
  gint old_height;
  gint old_x;
  gint old_y;
  
  /*Bound to config keys*/
  guint  max_indicators;
  guint  txt_indicator_threshold;

  gint update_geometry_id;

  /*Keep track if TaskLauncher was added through desktop file lookup
   FIXME _should_ be able to dump this by setting the task launcher visibility to false for ephemeral launchers.
   target for 0.6. */
  guint ephemeral_count;

  gboolean  inhibit_focus_loss;
  
  /*prop*/
  gboolean  enable_long_press;
  gint      icon_change_behavior;
  gint      desktop_copy;
  
  gboolean  long_press;     /*set to TRUE when there has been a long press so the clicked event can be ignored*/
  gchar * custom_name;
};

enum
{
  PROP_0,

  PROP_APPLET,
  PROP_DRAGGABLE,
  PROP_DRAG_AND_DROP_HOVER_DELAY,
  PROP_MAX_INDICATORS,
  PROP_TXT_INDICATOR_THRESHOLD,
  PROP_USE_LONG_PRESS,
  PROP_ICON_CHANGE_BEHAVIOR,
  PROP_DESKTOP_COPY
};

enum
{
  VISIBLE_CHANGED,

  SOURCE_DRAG_FAIL,
  SOURCE_DRAG_BEGIN,
  SOURCE_DRAG_END,

  DEST_DRAG_MOVE,
  DEST_DRAG_LEAVE,

  LAST_SIGNAL
};
static guint32 _icon_signals[LAST_SIGNAL] = { 0 };

static const GtkTargetEntry drop_types[] = 
{
  { (gchar*)"STRING", 0, 0 },
  { (gchar*)"text/plain", 0,  },
  { (gchar*)"text/uri-list", 0, 0 },
  { (gchar*)"awn/task-icon", 0, 0 }
};
static const gint n_drop_types = G_N_ELEMENTS (drop_types);

enum {
        TARGET_TASK_ICON
};

static const GtkTargetEntry task_icon_type[] = 
{
  { (gchar*)"awn/task-icon", 0, TARGET_TASK_ICON }
};
static const gint n_task_icon_type = G_N_ELEMENTS (task_icon_type);

/* Forwards */

static gboolean  task_icon_configure_event      (GtkWidget          *widget, 
                                                 GdkEventConfigure  *event);
static gboolean task_icon_scroll_event          (GtkWidget *widget, 
                                                 GdkEventScroll *event, 
                                                 TaskIcon *icon);
static void task_icon_long_press (TaskIcon * icon,gpointer null);
static void task_icon_clicked (TaskIcon * icon,GdkEventButton *event);
static gboolean  task_icon_button_release_event (GtkWidget      *widget,
                                                 GdkEventButton *event);
static gboolean  task_icon_button_press_event   (GtkWidget      *widget,
                                                 GdkEventButton *event);
static gboolean  task_icon_dialog_unfocus       (GtkWidget      *widget,
                                                GdkEventFocus   *event,
                                                TaskIcon        *icon);
/* Dnd forwards */
static void      task_icon_drag_data_get        (GtkWidget *widget, 
                                                 GdkDragContext *context, 
                                                 GtkSelectionData *selection_data,
                                                 guint target_type, 
                                                 guint time);

/* DnD 'source' forwards */

static gboolean  task_icon_source_drag_fail     (GtkWidget      *widget,
                                                 GdkDragContext *drag_context,
                                                 GtkDragResult   result);
static void      task_icon_source_drag_begin    (GtkWidget      *widget,
                                                 GdkDragContext *context);
static void      task_icon_source_drag_end      (GtkWidget      *widget,
                                                 GdkDragContext *context);

/* DnD 'destination' forwards */
static gboolean  task_icon_dest_drag_motion         (GtkWidget      *widget,
                                                     GdkDragContext *context,
                                                     gint            x,
                                                     gint            y,
                                                     guint           t);
static void      task_icon_dest_drag_leave          (GtkWidget      *widget,
                                                     GdkDragContext *context,
                                                     guint           time);
static void      task_icon_dest_drag_data_received  (GtkWidget      *widget,
                                                     GdkDragContext *context,
                                                     gint            x,
                                                     gint            y,
                                                     GtkSelectionData *data,
                                                     guint           info,
                                                     guint           time);
static void     task_icon_size_allocate             (TaskIcon *icon, 
                                                     GtkAllocation *alloc,
                                                     gpointer user_data);
static gboolean task_icon_refresh_geometry (TaskIcon *icon);
static void     task_icon_refresh_visible  (TaskIcon *icon);
static void     task_icon_search_main_item (TaskIcon *icon, TaskItem *main_item);
static void     task_icon_active_window_changed (WnckScreen *screen,
                                 WnckWindow *previously_active_window,
                                 TaskIcon *icon);

static void     size_changed_cb(AwnApplet *app, guint size, TaskIcon *icon);
static gint     task_icon_count_require_attention (TaskIcon *icon);
static void     task_icon_set_icon_pixbuf (TaskIcon * icon,TaskItem *item);
static void     task_icon_set_draggable_state (TaskIcon *icon, gboolean draggable);

static void     theme_changed_cb (GtkIconTheme *icon_theme,TaskIcon * icon);
static void     window_closed_cb (WnckScreen *screen,WnckWindow *window,
                                                        TaskIcon * icon);

static void	_destroyed_task_item (TaskIcon *icon, TaskItem *old_item);

static void on_main_item_name_changed (TaskItem    *item, const gchar *name, 
                                          TaskIcon    *icon);
static void on_main_item_icon_changed (TaskItem   *item, GdkPixbuf  *pixbuf, 
                                          TaskIcon   *icon);

static void on_main_item_visible_changed (TaskItem  *item,gboolean visible,
                                          TaskIcon  *icon);


/* GObject stuff */
static void
task_icon_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  TaskIcon        *icon = TASK_ICON (object);
  TaskIconPrivate *priv = icon->priv;

  switch (prop_id)
  {
    case PROP_DRAGGABLE:
      g_value_set_boolean (value, priv->draggable); 
      break;
    case PROP_MAX_INDICATORS:
      g_value_set_int (value,priv->max_indicators);
      break;
    case PROP_TXT_INDICATOR_THRESHOLD:
      g_value_set_int (value,priv->txt_indicator_threshold);
      break;
    case PROP_USE_LONG_PRESS:
      g_value_set_boolean (value, priv->enable_long_press);
      break;
    case PROP_ICON_CHANGE_BEHAVIOR:
      g_value_set_int (value, priv->icon_change_behavior);
      break;
    case PROP_DRAG_AND_DROP_HOVER_DELAY:
      g_value_set_int (value, priv->drag_and_drop_hover_delay);
      break;
    case PROP_DESKTOP_COPY:
      g_value_set_int (value, priv->desktop_copy);
      break;      
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
task_icon_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  TaskIcon *icon = TASK_ICON (object);

  switch (prop_id)
  {
    case PROP_APPLET:
      TASK_ICON (icon)->priv->applet = g_value_get_object (value);
      break;
    case PROP_DRAGGABLE:
      task_icon_set_draggable (icon, g_value_get_boolean (value));
      break;
    case PROP_MAX_INDICATORS:
      icon->priv->max_indicators = g_value_get_int (value);
      task_icon_refresh_visible (TASK_ICON(object));
      break;
    case PROP_TXT_INDICATOR_THRESHOLD:
      icon->priv->txt_indicator_threshold = g_value_get_int (value);
      task_icon_refresh_visible (TASK_ICON(object));
      break;
    case PROP_USE_LONG_PRESS:
      /*TODO Move into a fn */
      if (icon->priv->enable_long_press)
      {
        g_signal_handlers_disconnect_by_func(object, 
                                       G_CALLBACK (task_icon_long_press), 
                                       object);
      }
      icon->priv->enable_long_press = g_value_get_boolean (value);
      if (icon->priv->enable_long_press)
      {
        g_signal_connect (object,"long-press",
                    G_CALLBACK(task_icon_long_press),
                    object);
      }      
      break;
    case PROP_ICON_CHANGE_BEHAVIOR:
      icon->priv->icon_change_behavior = g_value_get_int (value);
      task_icon_set_icon_pixbuf (TASK_ICON(object),icon->priv->main_item);
      break;
    case PROP_DRAG_AND_DROP_HOVER_DELAY:
      icon->priv->drag_and_drop_hover_delay = g_value_get_int (value);
      break;
    case PROP_DESKTOP_COPY:
      icon->priv->desktop_copy = g_value_get_int (value);
      break;      
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

/**
 * Finalize the object and remove the list of windows, 
 * the list of launchers and the timer of update_geometry.
 */
static void
task_icon_dispose (GObject *object)
{
  TaskIconPrivate *priv = TASK_ICON_GET_PRIVATE (object);

  desktop_agnostic_config_client_unbind_all_for_object (priv->client, object,
                                                        NULL);
  
  /*this needs to be done in dispose, not finalize, due to idiosyncracies of 
   AwnDialog*/
  if (priv->dialog)
  {
    gtk_widget_destroy (priv->dialog);
    priv->dialog = NULL;
  }
  if (priv->overlay_text)
  {
    awn_overlayable_remove_overlay (AWN_OVERLAYABLE(object), AWN_OVERLAY(priv->overlay_text));
    priv->overlay_text = NULL;
  }
  G_OBJECT_CLASS (task_icon_parent_class)->dispose (object);  
}

static void
task_icon_finalize (GObject *object)
{
  TaskIconPrivate *priv = TASK_ICON_GET_PRIVATE (object);

  /*
   This needs to be an empty list.

   TODO post 0.4.  TaskItems should take a reference on TaskIcon.
   */
  g_assert (!priv->items);
  
  if (priv->update_geometry_id)
  {
    g_source_remove (priv->update_geometry_id);
  }

  g_signal_handlers_disconnect_by_func (wnck_screen_get_default (),
                          G_CALLBACK (task_icon_active_window_changed), object);
  g_signal_handlers_disconnect_by_func (awn_themed_icon_get_awn_theme (AWN_THEMED_ICON(object)),
                          G_CALLBACK (theme_changed_cb),object);
  g_signal_handlers_disconnect_by_func (wnck_screen_get_default(),
                          G_CALLBACK(window_closed_cb),object);  
  g_signal_handlers_disconnect_by_func (priv->applet,
                                        G_CALLBACK(size_changed_cb), object);
 
  G_OBJECT_CLASS (task_icon_parent_class)->finalize (object);
}

static gboolean
do_bind_property (DesktopAgnosticConfigClient *client, const gchar *key,
                  GObject *object, const gchar *property)
{
  GError *error = NULL;
  
  desktop_agnostic_config_client_bind (client,
                                       DESKTOP_AGNOSTIC_CONFIG_GROUP_DEFAULT,
                                       key, object, property, TRUE,
                                       DESKTOP_AGNOSTIC_CONFIG_BIND_METHOD_FALLBACK,
                                       &error);
  if (error)
  {
    g_warning ("Could not bind property '%s' to key '%s': %s", property, key,
               error->message);
    g_error_free (error);
    return FALSE;
  }

  return TRUE;
}

static void
task_icon_constructed (GObject *object)
{
  TaskIconPrivate *priv = TASK_ICON (object)->priv;
  GtkWidget *widget = GTK_WIDGET(object);  
  GError *error = NULL;

  if ( G_OBJECT_CLASS (task_icon_parent_class)->constructed)
  {
    G_OBJECT_CLASS (task_icon_parent_class)->constructed (object);
  }
  
  priv->dialog = awn_dialog_new_for_widget_with_applet (GTK_WIDGET (object),
                                                        priv->applet);
  g_signal_connect (G_OBJECT (priv->dialog),"focus-out-event",
                    G_CALLBACK (task_icon_dialog_unfocus), object);
  g_signal_connect  (wnck_screen_get_default (), 
                     "active-window-changed",
                     G_CALLBACK (task_icon_active_window_changed), 
                     object);

  g_signal_connect (G_OBJECT (widget), "size-allocate",
                    G_CALLBACK (task_icon_size_allocate), NULL);
  
  g_signal_connect(G_OBJECT(priv->applet), "size-changed", 
                   G_CALLBACK(size_changed_cb), object);

  g_signal_connect(G_OBJECT(object), "scroll-event",
                   G_CALLBACK(task_icon_scroll_event), object);  
  
  g_signal_connect(G_OBJECT(awn_themed_icon_get_awn_theme(AWN_THEMED_ICON(object))),
                   "changed",
                   G_CALLBACK(theme_changed_cb),object);
  g_signal_connect (wnck_screen_get_default(),"window-closed",
                    G_CALLBACK(window_closed_cb),object);
  
  priv->client = awn_config_get_default_for_applet (priv->applet, &error);

  if (error)
  {
    g_warning ("Could not get the applet's configuration object: %s",
               error->message);
    g_error_free (error);
    return;
  }
  
  if (!do_bind_property (priv->client, "max_indicators", object,
                         "max_indicators"))
  {
    return;
  }

  if (!do_bind_property (priv->client, "txt_indicator_threshold", object,
                         "txt_indicator_threshold"))
  {
    return;
  }

  if (!do_bind_property (priv->client, "enable_long_press", object,
                         "enable_long_press"))
  {
    return;
  }
  
  if (!do_bind_property (priv->client, "icon_change_behavior", object,
                         "icon_change_behavior"))
  {
    return;
  }

  if (!do_bind_property (priv->client, "drag_and_drop_hover_delay", object,
                         "drag_and_drop_hover_delay"))
  {
    return;
  }

  if (!do_bind_property (priv->client, "desktop_copy", object,
                         "desktop_copy"))
  {
    return;
  }

  if (!do_bind_property (priv->client, "drag_and_drop", object,
                         "draggable"))
  {
    return;
  }

}

static void
task_icon_size_allocate (TaskIcon *icon, GtkAllocation *alloc,
                         gpointer user_data)
{
  task_icon_schedule_geometry_refresh (icon);
}

/**
 * Set the icon geometry of the windows in a task-icon.
 * This equals to the minimize position of the window.
 */
static gboolean
task_icon_refresh_geometry (TaskIcon *icon)
{
  TaskIconPrivate *priv;
  GtkWidget *widget;
  GtkAllocation alloc;
  GtkPositionType pos_type;
  GdkWindow *win;
  GSList    *w;
  gint      base_x, base_y, x, y, size, offset, panel_size;
  gint      stripe_size, width, height;
  gint      len = 0;

  g_return_val_if_fail (TASK_IS_ICON (icon), FALSE);

  priv = icon->priv;
  widget = GTK_WIDGET (icon);

  priv->update_geometry_id = 0;

  if (!GTK_WIDGET_DRAWABLE (widget)) return FALSE;

  win = gtk_widget_get_window (widget);
  g_return_val_if_fail (win != NULL, FALSE);

  // get the position of the widget
  gdk_window_get_origin (win, &base_x, &base_y);

  gtk_widget_get_allocation (GTK_WIDGET (icon), &alloc);

  offset = awn_icon_get_offset (AWN_ICON (icon));
  pos_type = awn_icon_get_pos_type (AWN_ICON (icon));

  switch (pos_type)
  {
    case GTK_POS_LEFT:
    case GTK_POS_RIGHT:
      size = alloc.height;
      g_object_get (icon, "icon-height", &panel_size, NULL);

      x = base_x;
      y = base_y;
      // the icon has extra space above it, this compensates it
      if (pos_type == GTK_POS_RIGHT) x += alloc.width - panel_size - offset;
      break;
    case GTK_POS_TOP:
    default:
      size = alloc.width;
      g_object_get (icon, "icon-width", &panel_size, NULL);

      x = base_x;
      y = base_y;
      // the icon has extra space above it, this compensates it
      if (pos_type == GTK_POS_BOTTOM) y += alloc.height - panel_size - offset;
      break;
  }

  // get number of real windows this icon manages
  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;

    if (!TASK_IS_WINDOW (item)) continue;
    //if (!task_item_is_visible (item)) continue;

    len++;
  }
  if (len)
  {
    // divide the icon in multiple sections (or stripes)
    stripe_size = size / len;
    switch (pos_type)
    {
      case GTK_POS_LEFT:
      case GTK_POS_RIGHT:
        width = panel_size + offset;
        height = stripe_size;
        break;
      case GTK_POS_TOP:
      default:
        width = stripe_size;
        height = panel_size + offset;
        break;
    }
    // each window will get part of the icon - own stripe
    for (w = priv->items; w; w = w->next)
    {
      if (!TASK_IS_WINDOW (w->data)) continue;

      TaskWindow *window = TASK_WINDOW (w->data);

      task_window_set_icon_geometry (window, x, y, width, height);

      // shift the stripe
      if (pos_type == GTK_POS_LEFT || pos_type == GTK_POS_RIGHT)
        y += stripe_size;
      else
        x += stripe_size;
    }
  }
  return FALSE;
}

void
task_icon_schedule_geometry_refresh (TaskIcon *icon)
{
  g_return_if_fail (TASK_IS_ICON (icon));

  TaskIconPrivate *priv = icon->priv;

  if (priv->update_geometry_id == 0)
  {
    GSourceFunc func = (GSourceFunc)task_icon_refresh_geometry;
    priv->update_geometry_id = g_idle_add (func, icon);
  }
}

static void
task_icon_class_init (TaskIconClass *klass)
{
  GParamSpec     *pspec;
  GObjectClass   *obj_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wid_class = GTK_WIDGET_CLASS (klass);

  obj_class->constructed  = task_icon_constructed;
  obj_class->set_property = task_icon_set_property;
  obj_class->get_property = task_icon_get_property;
  obj_class->dispose      = task_icon_dispose;
  obj_class->finalize     = task_icon_finalize;

  wid_class->configure_event      = task_icon_configure_event;
  wid_class->button_release_event = task_icon_button_release_event;
  wid_class->button_press_event   = task_icon_button_press_event;
  wid_class->drag_begin           = task_icon_source_drag_begin;
  wid_class->drag_end             = task_icon_source_drag_end;
  wid_class->drag_data_get        = task_icon_drag_data_get;
  wid_class->drag_motion          = task_icon_dest_drag_motion;
  wid_class->drag_leave           = task_icon_dest_drag_leave;
  wid_class->drag_data_received   = task_icon_dest_drag_data_received;

  /* Install properties first */
  pspec = g_param_spec_object ("applet",
                               "Applet",
                               "AwnApplet this icon belongs to",
                               AWN_TYPE_APPLET,
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);
  g_object_class_install_property (obj_class, PROP_APPLET, pspec);

  pspec = g_param_spec_boolean ("draggable",
                                "Draggable",
                                "TaskIcon is draggable?",
                                TRUE,
                                G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_DRAGGABLE, pspec);

  pspec = g_param_spec_int ("max_indicators",
                            "max_indicators",
                            "Maxinum nmber of indicators (arrows) under icon",
                            0,
                            3,
                            3,
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_MAX_INDICATORS, pspec);
  
  pspec = g_param_spec_int ("txt_indicator_threshold",
                            "txt_indicator_threshold",
                            "Threshold at which the text count appears for tasks",
                            0,
                            9999,
                            3,
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_TXT_INDICATOR_THRESHOLD, pspec);

  pspec = g_param_spec_boolean ("enable_long_press",
                                "Use Long Press",
                                "Enable Long Preess",
                                TRUE,
                                G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_USE_LONG_PRESS, pspec);
  
  pspec = g_param_spec_int ("icon_change_behavior",
                                "Disable Icon Changes",
                                "Disable Icon Changes by App",
                                0,
                                2,
                                0,
                                G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_ICON_CHANGE_BEHAVIOR, pspec);

  pspec = g_param_spec_int ("drag_and_drop_hover_delay",
                            "Drag and drop hover delay",
                            "Value in ms to wait before activating window on hover",
                            1,
                            10000,
                            500,
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_DRAG_AND_DROP_HOVER_DELAY, pspec);

  pspec = g_param_spec_int ("desktop_copy",
                            "When/if to copy desktop files",
                            "When/if to copy desktop files",
                            DESKTOP_COPY_ALL,
                            DESKTOP_COPY_NONE,
                            DESKTOP_COPY_OWNER,
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_DESKTOP_COPY, pspec);
  
  /* Install signals */
  _icon_signals[VISIBLE_CHANGED] =
		g_signal_new ("visible_changed",
			      G_OBJECT_CLASS_TYPE (obj_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TaskIconClass, visible_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID, 
			      G_TYPE_NONE, 0);
  _icon_signals[SOURCE_DRAG_BEGIN] =
		g_signal_new ("source-drag-begin",
			      G_OBJECT_CLASS_TYPE (obj_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TaskIconClass, source_drag_begin),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID, 
			      G_TYPE_NONE, 0);
  _icon_signals[SOURCE_DRAG_FAIL] =
		g_signal_new ("source-drag-fail",
			      G_OBJECT_CLASS_TYPE (obj_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TaskIconClass, source_drag_fail),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID, 
			      G_TYPE_NONE, 0);
  _icon_signals[SOURCE_DRAG_END] =
		g_signal_new ("source-drag-end",
			      G_OBJECT_CLASS_TYPE (obj_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TaskIconClass, source_drag_end),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID, 
			      G_TYPE_NONE, 0);
  _icon_signals[DEST_DRAG_MOVE] =
		g_signal_new ("dest-drag-motion",
			      G_OBJECT_CLASS_TYPE (obj_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TaskIconClass, dest_drag_motion),
			      NULL, NULL,
			      taskmanager_marshal_VOID__INT_INT, 
			      G_TYPE_NONE, 2,
            G_TYPE_INT, G_TYPE_INT);
  _icon_signals[DEST_DRAG_LEAVE] =
		g_signal_new ("dest-drag-leave",
			      G_OBJECT_CLASS_TYPE (obj_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TaskIconClass, dest_drag_leave),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID, 
			      G_TYPE_NONE, 0);

  g_type_class_add_private (obj_class, sizeof (TaskIconPrivate));
}

static void
task_icon_init (TaskIcon *icon)
{
  TaskIconPrivate *priv;
  	
  priv = icon->priv = TASK_ICON_GET_PRIVATE (icon);

  priv->icon = NULL;
  priv->items = NULL;
  priv->drag_tag = 0;
  priv->drag_motion = FALSE;
  priv->gets_dragged = FALSE;
  priv->update_geometry_id = 0;
  priv->shown_items = 0;
  priv->needs_attention = 0;
  priv->is_active = 0;
  priv->main_item = NULL;
  priv->visible = FALSE;
  priv->overlay_text = NULL;
  priv->ephemeral_count = 0;
  
  awn_icon_set_pos_type (AWN_ICON (icon), GTK_POS_BOTTOM);

  /* D&D accept dragged objs */
  gtk_widget_add_events (GTK_WIDGET (icon), GDK_ALL_EVENTS_MASK);
  gtk_drag_dest_set (GTK_WIDGET (icon), 
                     GTK_DEST_DEFAULT_DROP,
                     drop_types, n_drop_types,
                     GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (G_OBJECT (icon), "drag-failed",
                    G_CALLBACK (task_icon_source_drag_fail), NULL);

}

/**
 * Creates a new TaskIcon, hides it and returns it.
 * (Hiding is because there are no visible TaskItems yet in the TaskIcon)  
 */
GtkWidget *
task_icon_new (AwnApplet *applet)
{
  GtkWidget *icon = g_object_new (TASK_TYPE_ICON, 
                                  "applet", applet, 
                                  "drag_and_drop",FALSE,
                                  NULL);
  gtk_widget_hide (icon);

  //BUG: AwnApplet calls upon start gtk_widget_show_all. So even when gtk_widget_hide
  //     gets called, it will get shown. So I'm forcing it to not listen to 
  //     'gtk_widget_show_all' with this function. FIXME: improve AwnApplet
  gtk_widget_set_no_show_all (icon, TRUE);
  
  return icon;
}

/**
 * The name of the main TaskItem in this TaskIcon changed.
 * So update the tooltip text.
 */
static void
on_main_item_name_changed (TaskItem    *item, 
                           const gchar *name, 
                           TaskIcon    *icon)
{
  g_return_if_fail (TASK_IS_ICON (icon));
  if (TASK_IS_WINDOW(item))
  {
#ifdef DEBUG
    g_debug ("%s: window name is %s",__func__,wnck_window_get_name (task_window_get_window(TASK_WINDOW(item))));
#endif
    awn_icon_set_tooltip_text (AWN_ICON (icon), name);
  }
}

/**
 * The icon of the main TaskItem in this TaskIcon changed.
 * So update the icon of the TaskIcon (AwnIcon).
 */
static void
on_main_item_icon_changed (TaskItem   *item, 
                           GdkPixbuf  *pixbuf, 
                           TaskIcon   *icon)
{
  TaskIconPrivate *priv;

  g_return_if_fail (TASK_IS_ICON (icon));
  g_return_if_fail (GDK_IS_PIXBUF (pixbuf));

  priv = icon->priv;
#ifdef DEBUG
  g_debug ("%s, icon width = %d, height = %d",__func__,gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
#endif
  if ( (priv->icon_change_behavior==0) || 
      (priv->icon_change_behavior==1 && TASK_IS_WINDOW(item) && task_window_use_win_icon(TASK_WINDOW(item))))
  {
    g_assert (icon->priv->main_item == item);
    task_icon_set_icon_pixbuf (TASK_ICON(icon),priv->main_item);    
  }
}

static void
on_desktop_icon_changed (TaskItem   *item,
                           GdkPixbuf  *pixbuf,
                           TaskIcon   *icon)
{
  TaskIconPrivate *priv;

  g_return_if_fail (TASK_IS_ICON (icon));
  g_return_if_fail (GDK_IS_PIXBUF (pixbuf));

  priv = icon->priv;
  if ( (priv->icon_change_behavior==2) || TASK_IS_LAUNCHER (priv->main_item) ||
      (priv->icon_change_behavior==1 && TASK_IS_WINDOW(priv->main_item) && !task_window_use_win_icon(TASK_WINDOW(priv->main_item))))
  {
    g_object_unref (priv->icon);
    priv->icon = pixbuf;
    g_debug ("%s: %s, %p",__func__,task_item_get_name(item),priv->icon);    
    g_object_ref (priv->icon);
    awn_icon_set_from_pixbuf (AWN_ICON (icon), priv->icon);
  }
}
/**
 * The visibility of the main TaskItem in this TaskIcon changed.
 * Because normally the main TaskItem should always be visible,
 * it searches after a new main TaskItem.
 */
static void
on_main_item_visible_changed (TaskItem  *item,
                              gboolean   visible,
                              TaskIcon  *icon)
{
  TaskIconPrivate *priv;
  g_return_if_fail (TASK_IS_ICON (icon));

  priv = icon->priv;
  /* the main TaskItem should have been visible, so if
     the main TaskItem becomes visible only now,
     it indicates a bug. 
     FIXME: this is possible atm in TaskWindow */
  if (visible) return;
  awn_icon_set_tooltip_text (AWN_ICON (icon),task_item_get_name (priv->main_item));  

//  task_icon_search_main_item (icon,item);
}

static void
task_icon_active_window_changed (WnckScreen *screen,
                                 WnckWindow *previously_active_window,
                                 TaskIcon *icon)
{
  WnckWindow * active;
  GSList     *i;
  TaskIconPrivate *priv = icon->priv;

  active = wnck_screen_get_active_window (screen);

  if (active )
  {
    /*this block basically detects when a window has been moved to a different
     workspace when show_all_window is false.  In this case main_item needs to
     either get set to another of the app's windows on the current workspace or
     to the launcher*/
    if (previously_active_window && WNCK_IS_WINDOW(previously_active_window))
    {
      if (wnck_window_get_application (active) != wnck_window_get_application(previously_active_window))
      {
        for (i = priv->items; i; i = i->next)
        {
          TaskItem *item = i->data;

          if (!TASK_IS_WINDOW(item)) continue;
          if ( previously_active_window == task_window_get_window (TASK_WINDOW(item)) )
          {
            if (!task_item_is_visible (item))
            {
              task_icon_search_main_item (icon,NULL);
              break; 
            }
          }
        }
      }
    }    
    for (i = priv->items; i; i = i->next)
    {
      TaskItem *item = i->data;

      if (!task_item_is_visible (item)) continue;
      if (!TASK_IS_WINDOW(item)) continue;
      if ( active == task_window_get_window (TASK_WINDOW(item)) )
      {
        task_icon_search_main_item (icon,item);
        break; 
      }
    }
    
  }
  /* 
   if found is false then ther are no "visible" Windows
   */
/*  if (!found)
  {
    task_icon_search_main_item (icon,item);
  }*/
}

/*
 * Notify that the icon that a window is closed. The window gets
 * removed from the list and when this icon doesn't has any 
 * launchers and no windows it will get destroyed.
 *
 * TODO: refactor.
 */
static void
_destroyed_task_item (TaskIcon *icon, TaskItem *old_item)
{
  TaskIconPrivate *priv;
  AwnEffects * effects;
  g_return_if_fail (TASK_IS_ICON (icon));
  g_return_if_fail (TASK_IS_ITEM (old_item));

  effects = awn_overlayable_get_effects (AWN_OVERLAYABLE (icon));
  g_return_if_fail (effects);
  priv = icon->priv;

  priv->items = g_slist_remove (priv->items, old_item);

  if (old_item == priv->main_item && priv->items)
  {
    task_icon_search_main_item (icon,NULL);
  }

  if ( (g_slist_length (priv->items) == 1 ) && task_icon_contains_launcher(icon))
  {
    awn_effects_stop (effects,AWN_EFFECT_ATTENTION); 
  }
  else if ( g_slist_length (priv->items) <= priv->ephemeral_count)
  {    
    awn_effects_stop (effects, AWN_EFFECT_ATTENTION);
  }
  else if ( !task_icon_count_require_attention (icon) )
  {
    awn_effects_stop (effects, AWN_EFFECT_ATTENTION);
  }
  else
  {
//    task_icon_refresh_visible (icon);    
  }
  task_icon_refresh_visible (icon);  
}

/**
 * Searches for a new main item unless one is provided.
 * A main item is used for displaying its icon and also the text (if there is only one item)
 * Attention: this function doesn't check if it is needed to switch to a new main item.
 */
static void
task_icon_search_main_item (TaskIcon *icon, TaskItem *main_item)
{
  TaskIconPrivate *priv;
  GSList * i;

  g_return_if_fail (TASK_IS_ICON (icon));

  priv = icon->priv;

  /*
   If we don't have a main_item at this point then go for the one that
   requires attention
   */
  if (!main_item)
  {
    for (i = priv->items; i; i = i->next)
    {
      TaskItem *item = i->data;

      if (!task_item_is_visible (item)) continue;
      if (!TASK_IS_WINDOW (item)) continue;

      if (wnck_window_needs_attention (task_window_get_window (TASK_WINDOW(item))) )
      {
#ifdef DEBUG
        g_debug ("%s: Match #1",__func__);
#endif        
        main_item = item;
        break;
      }
    }
  }

  /*
   If we don't have a main_item at this point then go for the one wnck says 
   was the most recently activated window.
   */  
  if (!main_item)
  {
    for (i = priv->items; i; i = i->next)
    {
      TaskItem *item = i->data;

      if (!task_item_is_visible (item)) continue;
      if (!TASK_IS_WINDOW (item)) continue;
      
      if (wnck_window_is_most_recently_activated (task_window_get_window (TASK_WINDOW(item))))
      {
#ifdef DEBUG
        g_debug ("%s: Match #2",__func__);
#endif        
        main_item = item;
        break;
      }
    }
  }

    /*
   If we don't have a main_item at this point then go for the one just look
   for the first TaskWindow we can find.
   */  
  if (!main_item)
  {
    for (i = priv->items; i; i = i->next)
    {
      TaskItem *item = i->data;

      if (!task_item_is_visible (item)) continue;
      if (!TASK_IS_WINDOW (item)) continue;

#ifdef DEBUG
        g_debug ("%s: Match #3",__func__);
#endif              
      main_item = item;
      break;
    }
  }

  /*
   If we don't have a main_item at this point then find the first visible item
   (TaskLauncher in all likelihood... otherwise there might be something rather
    strange going on)
   */  
  
  if (!main_item)
  {
    for (i = priv->items; i; i = i->next)
    {
      TaskItem *item = i->data;

      if (!task_item_is_visible (item)) continue;
#ifdef DEBUG
        g_debug ("%s: Match #4",__func__);
#endif                    
      main_item = item;
      break;
    }
  }
  //remove signals of old main_item
  if (priv->main_item && ( main_item != priv->main_item) )
  {
    g_signal_handlers_disconnect_by_func(priv->main_item, 
                                         G_CALLBACK (on_main_item_name_changed), icon);
    g_signal_handlers_disconnect_by_func(priv->main_item, 
                                         G_CALLBACK (on_main_item_icon_changed), icon);
    g_signal_handlers_disconnect_by_func(priv->main_item, 
                                         G_CALLBACK (on_main_item_visible_changed), icon);
    priv->main_item = NULL;
  }

  /*
   Assuming we have a main_item
   */
  if (main_item && ( main_item != priv->main_item) )
  {
    /*
     Set the TaskIcon to the Icon associated with the main_item.
     */
    priv->main_item = main_item;
#ifdef DEBUG
    g_debug ("%s, icon width g_sig= %d, height = %d",__func__,gdk_pixbuf_get_width(priv->icon), gdk_pixbuf_get_height(priv->icon));
#endif
    task_icon_set_icon_pixbuf (icon,main_item);
    awn_icon_set_tooltip_text (AWN_ICON (icon),
                               task_item_get_name (priv->main_item));
    /*
     we have callbacks for when the main_item window do things
     */
    g_signal_connect (priv->main_item, "name-changed",
                      G_CALLBACK (on_main_item_name_changed), icon);
    g_signal_connect (priv->main_item, "icon-changed",
                      G_CALLBACK (on_main_item_icon_changed), icon);
    g_signal_connect (priv->main_item, "visible-changed",
                      G_CALLBACK (on_main_item_visible_changed), icon);
  }
}

/**
 * 
 */
static void
task_icon_refresh_visible (TaskIcon *icon)
{
  TaskIconPrivate *priv;
  GSList *w;
  guint count = 0;
  guint count_windows = 0;

  g_return_if_fail (TASK_IS_ICON (icon));

  priv = icon->priv;

  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;

    if (!task_item_is_visible (item)) continue;
    count++;

    if (!TASK_IS_WINDOW (item)) continue;
    count_windows++;
  }

  task_icon_schedule_geometry_refresh (icon);

  /*Conditional operator*/
  awn_icon_set_indicator_count (AWN_ICON (icon), 
                                count_windows > priv->max_indicators?
                                priv->max_indicators:count_windows);

  if (count_windows >= priv->txt_indicator_threshold )
  {
    if (!priv->overlay_text)
    {
      priv->overlay_text = awn_overlay_text_new ();
      awn_overlayable_add_overlay (AWN_OVERLAYABLE (icon), 
                                   AWN_OVERLAY (priv->overlay_text));
      g_object_set (G_OBJECT (priv->overlay_text),
                    "gravity", GDK_GRAVITY_SOUTH_EAST, 
                    "font-sizing", AWN_FONT_SIZE_LARGE,
                    NULL);
    }

    gchar* count_str = g_strdup_printf ("%i",count_windows);
    g_object_set (G_OBJECT (priv->overlay_text),
                  "text", count_str,
                  "active",TRUE,
                  NULL);
    g_free (count_str);
  }
  else
  {
    if (priv->overlay_text)
    {
      g_object_set (priv->overlay_text,
                    "active",FALSE,
                    NULL);
    }
  }

  if (count != priv->shown_items)
  {  
    if (count <= task_icon_count_ephemeral_items(icon))
    {
      priv->visible = FALSE;
    }
    else
    {
      if (!priv->main_item)
        task_icon_search_main_item (icon,NULL);
      
      priv->visible = TRUE;
    }

    g_signal_emit (icon, _icon_signals[VISIBLE_CHANGED], 0);
  }

  priv->shown_items = count;
}

/**
 * The 'active' state of a TaskWindow changed.
 * If this is the only TaskWindow that's active,
 * the TaskIcon will get an active state.
 * If it the last TaskWindow that isn't active anymore
 * the TaskIcon will get an inactive state too.
 * STATE: adjusted
 * PROBLEM: It shouldn't get called when the state didn't change.
            Else the count of windows that need have the active state will be off.
 */
static void
on_window_active_changed (TaskWindow *window, 
                          gboolean    is_active, 
                          TaskIcon   *icon)
{
  TaskIconPrivate *priv;
  GSList *w;
  guint count = 0;

  g_return_if_fail (TASK_IS_ICON (icon));

  priv = icon->priv;

  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;

    if (!TASK_IS_WINDOW (item)) continue;
    if (!task_item_is_visible (item)) continue;
    if (!task_window_is_active (TASK_WINDOW (item))) continue;

    count++;
  }

  if (priv->is_active == 0 && count == 1)
  {
      awn_icon_set_is_active (AWN_ICON (icon), TRUE);
  }
  else if (priv->is_active == 1 && count == 0)
  {
      awn_icon_set_is_active (AWN_ICON (icon), FALSE);
  }
  
  priv->is_active = count;
}

static gint 
task_icon_count_require_attention (TaskIcon *icon)
{
  TaskIconPrivate *priv;
  GSList *w;
  guint count = 0;

  priv = icon->priv;
  
  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;

    if (!TASK_IS_WINDOW (item)) continue;
    if (!task_item_is_visible (item)) continue;
    if (!task_window_needs_attention (TASK_WINDOW (item))) continue;

    count++;
  }
  return count;
}

/**
 * The 'needs attention' state of a window changed.
 * If a window needs attention and there isn't one yet, it will
 * start the animation. When every window don't need attention anymore
 * it will stop the animation.
 * STATE: adjusted
 * TODO: h4writer - check if it is possible to interupt animation mid-air,
 * and let it start again, if there is a 2nd/3rd window that needs attention.
 * BUG: when icon becomes visible again it needs to be checked if it needs attention again.
 */
static void
on_window_needs_attention_changed (TaskWindow *window,
                                   gboolean    needs_attention,
                                   TaskIcon   *icon)
{
  TaskIconPrivate *priv;
  guint count = 0;

  g_return_if_fail (TASK_IS_ICON (icon));

  priv = icon->priv;
  
  task_icon_search_main_item (icon,TASK_ITEM(window));

  count = task_icon_count_require_attention (icon);

  if ( count)
  {
    awn_icon_set_effect (AWN_ICON (icon),AWN_EFFECT_ATTENTION);
  }
  else
  {
    awn_effects_stop (awn_overlayable_get_effects (AWN_OVERLAYABLE (icon)), 
                      AWN_EFFECT_ATTENTION);
  }
  
  priv->needs_attention = count;
}

/**
 * When the progress of a TaskWindow has changed,
 * it will recalculate the process of all the
 * TaskWindows this TaskIcon contains.
 * STATE: adjusted
 */
static void
on_window_progress_changed (TaskWindow   *window,
                            gfloat      adjusted_progress,
                            TaskIcon   *icon)
{
  TaskIconPrivate *priv;
  GSList *w;
  gfloat progress = 0;
  guint len = 0;

  g_return_if_fail (TASK_IS_ICON (icon));

  priv = icon->priv;

  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;

    if (!TASK_IS_WINDOW (item)) continue;
    if (!task_item_is_visible (item)) continue;

    if (progress != -1)
    {
      progress += task_window_get_progress (TASK_WINDOW (item));
      len++;
    }
  }

  // FIXME: noone emits progress changed so far, do something when it
  //  starts to be used
}

/**
 * When a TaskWindow becomes visible or invisible,
 * update the number of shown windows.
 * If because of that the icon has no shown TaskWindows
 * anymore it will get hidden. If there was no shown
 * TaskWindow and now the first one gets visible,
 * then show TaskIcon.
 * STATE: adjusted
 */
static void
on_item_visible_changed (TaskItem   *item,
                         gboolean   visible,
                         TaskIcon   *icon)
{
  g_return_if_fail (TASK_IS_ICON (icon));
  g_return_if_fail (TASK_IS_ITEM (item));
  
  task_icon_refresh_visible (icon);
}

/**
 * Public Functions
 */

/**
 * Returns whetever this icon should be visible.
 * (That means it should contain atleast 1 visible item)
 */
gboolean
task_icon_is_visible (TaskIcon      *icon)
{
  g_return_val_if_fail (TASK_IS_ICON (icon), FALSE);
  
  return icon->priv->visible;
}

/**
 * Returns whetever this icon contains atleast one (visible?) launcher.
 * TODO: adapt TaskIcon so it has a guint with the numbers of TaskLaunchers/TaskWindows
 */
gboolean
task_icon_contains_launcher (TaskIcon      *icon)
{
  TaskIconPrivate *priv;
  GSList *w;

  g_return_val_if_fail (TASK_IS_ICON (icon), FALSE);

  priv = icon->priv;

  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;

    if (!task_item_is_visible (item)) continue;

    if (TASK_IS_LAUNCHER (item))
      return TRUE;
  }
  return FALSE;
}

TaskItem *
task_icon_get_launcher (TaskIcon      *icon)
{
  TaskIconPrivate *priv;
  GSList *w;

  g_return_val_if_fail (TASK_IS_ICON (icon), NULL);

  priv = icon->priv;

  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;

    if (TASK_IS_LAUNCHER (item))
      return item;
  }
  return NULL;
}



guint
task_icon_count_ephemeral_items (TaskIcon * icon)
{
  TaskIconPrivate *priv;

  g_return_val_if_fail (TASK_IS_ICON (icon), FALSE);
  priv = icon->priv;
  
  return priv->ephemeral_count; 
}

void
task_icon_increment_ephemeral_count (TaskIcon *icon)
{
  TaskIconPrivate *priv;

  g_return_if_fail (TASK_IS_ICON (icon));
  priv = icon->priv;

  priv->ephemeral_count++;
/* TODO
   Leaving this here for a little bit.  Going to review some code am assure
   myself this is no longer required.*/
 if (priv->ephemeral_count >= g_slist_length (priv->items) )
 {
   gtk_widget_destroy (GTK_WIDGET(icon));
 }
  
}
/**
 * Returns the number of visible and unvisible items this TaskIcon contains.
 */
guint
task_icon_count_items (TaskIcon *icon)
{
  TaskIconPrivate *priv;

  g_return_val_if_fail (TASK_IS_ICON (icon), FALSE);
  priv = icon->priv;
  
  return g_slist_length (priv->items);
}

guint
task_icon_match_item (TaskIcon      *icon,
                      TaskItem      *item_to_match)
{
  TaskIconPrivate *priv;
  GSList *w;
  guint max_score = 0;

  g_return_val_if_fail (TASK_IS_ICON (icon), 0);
  g_return_val_if_fail (TASK_IS_ITEM (item_to_match), 0);

  priv = icon->priv;

  for (w = priv->items; w; w = w->next)
  {
    TaskItem *item = w->data;
    guint score;

    if (!task_item_is_visible (item)) continue;
    
    score = task_item_match (item, item_to_match);
    if (score > max_score)
      max_score = score;
  }
  
  return max_score;
}


static void
task_icon_set_icon_pixbuf (TaskIcon * icon,TaskItem *item)
{
  TaskIconPrivate *priv;
  
  g_return_if_fail (TASK_IS_ICON (icon) );
  priv = icon->priv;  

  if ( !item ||  ( priv->icon_change_behavior==2) ||
      (priv->icon_change_behavior==1 && TASK_IS_WINDOW(item) && !task_window_use_win_icon(TASK_WINDOW(item))))
  {
    TaskItem * launcher = task_icon_get_launcher (icon);
    if (launcher)
    {
      item = launcher;
    }
  }

  if (!item && priv->items)
  {
    item = priv->items->data;
  }
  
  if (item && TASK_IS_WINDOW(item))
  {
    if (priv->icon)
    {
      g_object_unref (priv->icon);
    } 
    priv->icon = task_item_get_icon (item);
    g_object_ref (priv->icon);
  }
  else if (priv->custom_name)
  {
    gint size;
    const gchar * state;
    g_object_get (priv->applet,
                  "size",&size,
                  NULL);    
    if (priv->icon)
    {
      g_object_unref (priv->icon);

    }          
    if (gtk_icon_theme_has_icon(awn_themed_icon_get_awn_theme (AWN_THEMED_ICON(icon)),
                                priv->custom_name) )
    {
      state = "::no_drop::customized";
    }
    else
    {
      state = "::no_drop::desktop";
    }    
    priv->icon = awn_themed_icon_get_icon_at_size (AWN_THEMED_ICON(icon),
                                                  size,
                                                  state);
  }
  if (priv->icon)
  {
    awn_icon_set_from_pixbuf (AWN_ICON (icon),priv->icon);
  }
}

static gboolean
_refresh_icon (TaskIcon * icon)
{
  TaskIconPrivate *priv;
  
  priv = icon->priv;
  awn_icon_set_from_pixbuf (AWN_ICON (icon), priv->icon);
  return FALSE;
}

/**
 * Adds a TaskWindow to this task-icon
 */
void
task_icon_append_item (TaskIcon      *icon,
                       TaskItem      *item)
{
  TaskIconPrivate *priv;

  g_assert (item);
  g_assert (icon);
  g_return_if_fail (TASK_IS_ICON (icon));
  g_return_if_fail (TASK_IS_ITEM (item));

  priv = icon->priv;
  /* 
   if we don't have an icon (this is the first item being appended) or
   this item is a launcher then we'll use this item's icon 
   */
  if (!priv->icon || TASK_IS_LAUNCHER(item))
  {
    if (TASK_IS_LAUNCHER(item))
    {
      gchar * uid,*name;
      gint  size;
      const gchar * states[3] = {"::no_drop::desktop","::no_drop::customized",NULL};
      const gchar * names[3]  = {NULL,NULL,NULL};

      g_object_get (priv->applet,
                    "uid",&uid,
                    "canonical-name",&name,
                    "size",&size,
                    NULL);
      names[0] = task_launcher_get_icon_name(item);
      priv->custom_name = g_strdup_printf ("%s-%s",name,task_launcher_get_icon_name(item));
      names[1] = priv->custom_name;
      awn_themed_icon_set_info (AWN_THEMED_ICON(icon),
                                       name,
                                       uid,
                                       (gchar**)states,
                                       (gchar**)names);
      if (gtk_icon_theme_has_icon(awn_themed_icon_get_awn_theme (AWN_THEMED_ICON(icon)),
                                  priv->custom_name) )
      {
        awn_themed_icon_set_state (AWN_THEMED_ICON(icon),"::no_drop::customized");
      }
      else
      {
        awn_themed_icon_set_state (AWN_THEMED_ICON(icon),"::no_drop::desktop");
      }
      
      awn_themed_icon_set_size (AWN_THEMED_ICON(icon),size);
      g_signal_connect (item, "icon-changed",G_CALLBACK (on_desktop_icon_changed), icon);
      
      g_free (name);
      g_free (uid);
    }    
  }
  
  priv->items = g_slist_append (priv->items, item);
  gtk_widget_show_all (GTK_WIDGET (item));
  gtk_container_add (GTK_CONTAINER (priv->dialog), GTK_WIDGET (item));
  
  g_object_weak_ref (G_OBJECT (item), (GWeakNotify)_destroyed_task_item, icon);

  task_item_set_task_icon (item, icon);
  task_icon_refresh_visible (icon);

  /* Connect item signals */
  g_signal_connect (item, "visible-changed",
                    G_CALLBACK (on_item_visible_changed), icon);

  /*we have a new matching window, it's possible the launch effect is playing.
   stop it.*/
  awn_effects_stop (awn_overlayable_get_effects (AWN_OVERLAYABLE (icon)), 
                  AWN_EFFECT_LAUNCHING);

  /*check to see if attention effect needs to be started back up*/
  if (priv->needs_attention != 0 && task_icon_count_require_attention (icon)>0)
  {
    awn_icon_set_effect (AWN_ICON (icon),AWN_EFFECT_ATTENTION);
  }
  
  /* Connect window signals */
  if (TASK_IS_WINDOW (item))
  {
    TaskWindow *window = TASK_WINDOW (item);
    g_signal_connect (window, "active-changed",
                      G_CALLBACK (on_window_active_changed), icon);
    g_signal_connect (window, "needs-attention",
                      G_CALLBACK (on_window_needs_attention_changed), icon);
    g_signal_connect (window, "progress-changed",
                      G_CALLBACK (on_window_progress_changed), icon);
    g_signal_connect (window, "progress-changed",
                      G_CALLBACK (on_window_progress_changed), icon);
    task_icon_schedule_geometry_refresh (icon);
  }
  task_icon_search_main_item (icon,item);
  /*
   when the task manager is first started up, we don't get the window icons we
   want from wnck.*/
  if (priv->icon_change_behavior <2)
  {
    g_idle_add ((GSourceFunc) _refresh_icon,icon);
  }
}

void
task_icon_append_ephemeral_item (TaskIcon      *icon,
                       TaskItem      *item)
{
  TaskIconPrivate *priv;
  
  g_assert (item);
  g_assert (icon);
  g_return_if_fail (TASK_IS_ICON (icon));
  g_return_if_fail (TASK_IS_LAUNCHER (item));

  priv = icon->priv;

  priv->ephemeral_count++;
  task_icon_append_item (icon,item); 
}


GSList *  
task_icon_get_items (TaskIcon     *icon)
{
  TaskIconPrivate *priv;
  
  g_assert (icon);
  g_return_val_if_fail (TASK_IS_ICON (icon),NULL);

  priv = icon->priv;
  return priv->items;
}

/**
 * 
 * TODO: h4writer - adjust 2nd round
 */
void
task_icon_refresh_icon (TaskIcon *icon, guint size)
{
  TaskIconPrivate *priv;

  g_return_if_fail (TASK_IS_ICON (icon));
  priv = icon->priv;

  awn_themed_icon_set_size (AWN_THEMED_ICON (icon),size);
#ifdef DEBUG
    g_debug ("%s, icon width g_sig= %d, height = %d",__func__,gdk_pixbuf_get_width(priv->icon), gdk_pixbuf_get_height(priv->icon));
#endif
  task_icon_set_icon_pixbuf (icon,priv->main_item);
}

/*
 * Widget callbacks
 */
static gboolean
task_icon_configure_event (GtkWidget          *widget,
                           GdkEventConfigure  *event)
{
  TaskIconPrivate *priv;

  g_return_val_if_fail (TASK_IS_ICON (widget), FALSE);
  priv = TASK_ICON (widget)->priv;

  if (priv->old_width == event->width && priv->old_height == event->height)
    return FALSE;

  priv->old_width = event->width;
  priv->old_height = event->height;

  g_idle_add ((GSourceFunc)task_icon_refresh_geometry, TASK_ICON (widget));

  return TRUE;
}

static void
task_icon_long_press (TaskIcon * icon,gpointer null)
{
  TaskIconPrivate *priv;
  priv = icon->priv;

#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  
  gtk_widget_show (priv->dialog);
  task_icon_set_draggable_state (icon, FALSE);
  gtk_widget_grab_focus (priv->dialog);
  priv->long_press = TRUE;
}


static void
task_icon_minimize_group(TaskIcon * icon,TaskWindow * window)
{
  g_return_if_fail (TASK_IS_WINDOW(window));
  g_return_if_fail (TASK_IS_ICON(icon));
  
  gulong group_leader = wnck_window_get_group_leader (task_window_get_window(window));
  WnckApplication* application=wnck_application_get (group_leader);
  if (application)
  {
    GList* app_windows = wnck_application_get_windows (application);
    GList * iter;
    for (iter = app_windows; iter; iter = iter->next)
    {
      GSList * i;
      for (i = icon->priv->items; i; i=i->next)
      {
        if (!TASK_IS_WINDOW (i->data) ) continue;
        if ( iter->data == task_window_get_window(i->data))
        {
          wnck_window_minimize (iter->data);
          break;
        }
      }
    }      
  }
  else
  {
    wnck_window_minimize (task_window_get_window(window));
  }
}

static void
task_icon_restore_group(TaskIcon * icon,TaskWindow * window, guint32 timestamp)
{
  g_return_if_fail (TASK_IS_WINDOW(window));
  g_return_if_fail (TASK_IS_ICON(icon));
  
  gulong group_leader = wnck_window_get_group_leader (task_window_get_window(window));
  WnckApplication* application=wnck_application_get (group_leader);
  if (application)
  {
    GList* app_windows = wnck_application_get_windows (application);
    GList * iter;
    for (iter = app_windows; iter; iter = iter->next)
    {
      GSList * i;
      for (i = icon->priv->items; i; i=i->next)
      {
        if (!TASK_IS_WINDOW (i->data) ) continue;
        if (i->data == window) continue;
        
        if ( iter->data == task_window_get_window(i->data))
        {
          wnck_window_unminimize  (iter->data,timestamp);
          break;
        }
      }
    }      
  }
  task_window_activate (window,timestamp);
}

void            
task_icon_set_inhibit_focus_loss (TaskIcon *icon, gboolean val)
{
  TaskIconPrivate *priv;
  g_return_if_fail (TASK_IS_ICON (icon));
  
  priv = icon->priv;
  priv->inhibit_focus_loss = val;
}

static gboolean
task_icon_scroll_event (GtkWidget *widget, GdkEventScroll *event, TaskIcon *icon)
{
  TaskIconPrivate *priv;

  g_assert (TASK_IS_ICON(icon));

  priv = icon->priv;
  if (event->type == GDK_SCROLL)
  {
    if (priv->main_item && TASK_IS_WINDOW (priv->main_item))
    {
      GSList *cur_item = NULL;
      guint count = 0;
      gint pos;
      
      if (!task_window_is_active (TASK_WINDOW(priv->main_item)) )
      {
        task_window_activate (TASK_WINDOW(priv->main_item),event->time);
        return TRUE;
      }
      cur_item = g_slist_find (priv->items, priv->main_item);
      do
      {
        switch (event->direction)
        {
          case GDK_SCROLL_UP:
          case GDK_SCROLL_LEFT:
            /*This is why it's almost always best to use a GList instead of a
             GSList*/
            pos = g_slist_position (priv->items, cur_item);
            pos--;
            if (pos<0)
            {
              pos = g_slist_length (priv->items) -1;
            }
            cur_item = g_slist_nth (priv->items, pos);
            break;
          case GDK_SCROLL_DOWN:
          case GDK_SCROLL_RIGHT:
            cur_item = g_slist_next (cur_item);
            if (!cur_item)
            {
              cur_item = priv->items;
            }
            break;
        }
        count ++;
      } while ( TASK_IS_LAUNCHER (cur_item->data) && (count <= g_slist_length(priv->items)) );
      priv->main_item = cur_item->data;
      task_window_activate (TASK_WINDOW(priv->main_item),event->time);
      return TRUE;
    }
  }
  return FALSE;
}

static void
task_icon_clicked (TaskIcon * icon,GdkEventButton *event)
{
  TaskIconPrivate *priv;
  TaskItem        *main_item;
  priv = icon->priv;

  /*If long press is enabled then we try to be smart about what we do on short clicks*/
  if (priv->enable_long_press)
  {
    main_item = priv->main_item;
  }
  else
  {
    main_item = NULL;  /* if main_item is NULL then dialog will always open when >1 item in it*/
  }

  /*hackish way to determine that we already had a long press for this signal.
   clicked signal still fires even if there was a long press*/
  if (priv->long_press)
  {
    priv->long_press = FALSE;
    return;
  }
  
  if(priv->gets_dragged) return;

  if (priv->shown_items == 0)
  {
    g_critical ("TaskIcon: The icons shouldn't contain a visible (and clickable) icon");
    return;
  }
  else if (gtk_widget_get_visible (priv->dialog) )
  {
  /*is the dialog open?  if so then it should be closed on icon click*/  
    
    gtk_widget_hide (priv->dialog);
    task_icon_set_draggable_state (icon, priv->draggable);    
  }  
  else if (priv->shown_items == 1)
  {
    GSList *w;

    if (main_item) 
    {
      /*if we have a main item then pass the click on to that */
      if (!TASK_IS_WINDOW(main_item))
      {
        /*it's a launcher*/
        task_item_left_click (main_item,event);
        awn_effects_start_ex (awn_overlayable_get_effects (AWN_OVERLAYABLE (icon)), 
                          AWN_EFFECT_LAUNCHING, 10, FALSE, FALSE);
        
      }
      else
      {
        if (task_window_is_active (TASK_WINDOW(main_item)))
        {
          task_icon_minimize_group (icon,TASK_WINDOW(main_item));
        }
        else
        {
          task_icon_restore_group (icon,TASK_WINDOW(main_item),event->time);
        }
      }
    }
    else
    {
      /* Otherwise Find the window/launcher that is shown and pass the click on*/
      for (w = priv->items; w; w = w->next)
      {
        TaskItem *item = w->data;

        if (!task_item_is_visible (item)) continue;
        
        if (!TASK_IS_WINDOW(item))
        {
          /*it's a launcher*/
          awn_effects_start_ex (awn_overlayable_get_effects (AWN_OVERLAYABLE (icon)), 
                            AWN_EFFECT_HOVER, 10, FALSE, FALSE);
          task_item_left_click (item,event);
        }
        else
        {
          if (task_window_is_active (TASK_WINDOW(item)))
          {
            task_icon_minimize_group (icon,TASK_WINDOW(item));            
          }
          else
          {
            task_icon_restore_group (icon,TASK_WINDOW(item),event->time);
          }
        }
        break;
      }
      return;
    }
  }
  else if (main_item)
  {
    /* 
     Reach here if we have main_item set and
     1) Launcher + 1 or more TaskWindow or
     2) No Launcher + 2 or more TaskWindow.
     In either case main_item Should be TaskWindow and we want to act on that.
     */  
    if (!TASK_IS_WINDOW(main_item))
    {
      /*it's a launcher*/
      awn_effects_start_ex (awn_overlayable_get_effects (AWN_OVERLAYABLE (icon)), 
                        AWN_EFFECT_LAUNCHING, 10, FALSE, FALSE);      
      task_item_left_click (main_item,event);
    }
    else if (task_window_is_active (TASK_WINDOW(main_item)))
    {
      task_icon_minimize_group (icon,TASK_WINDOW(main_item));
    }
    else
    {
      task_icon_restore_group (icon,TASK_WINDOW(main_item),event->time);
    }
  }  
  else if (priv->shown_items == (1 + ( task_icon_contains_launcher (icon)?1:0)))
  {
    /*
     main item not set (for whatever reason).
     This is either a case of 1 Launcher + 1 Window or
     1 Window.
     
     So, find the first TaskWindow and act on it.
     */
    GSList *w;
    for (w = priv->items; w; w = w->next)
    {
      TaskItem *item = w->data;

      if (!TASK_IS_WINDOW (item) ) continue;
      
      if (task_window_is_active (TASK_WINDOW(item)))
      {
        task_icon_minimize_group (icon,TASK_WINDOW(item));        
      }
      else
      {
        task_icon_restore_group (icon,TASK_WINDOW(item),event->time);
      }
#ifdef DEBUG
      g_debug ("clicked on: %s", task_item_get_name (item));
#endif
    }            
  }
  else
  {
    /*
     There are multiple TaskWindows.  And main_item is not set..
     therefore we show the dialog
     */
    //TODO: move to hover?
    if (gtk_widget_get_visible (priv->dialog) )
    {
      gtk_widget_hide (priv->dialog);
      task_icon_set_draggable_state (icon, priv->draggable);
    }
    else
    {
      gtk_widget_show (priv->dialog); 
      task_icon_set_draggable_state (icon, FALSE);      
      gtk_widget_grab_focus (priv->dialog);      
    }
  }
}

/**
 * Whenever there is a release event on the TaskIcon it will do the proper actions.
 * left click: - start launcher = has no (visible) windows
 *             - activate window = when there is only one (visible) window
 *             - show dialog = when there are multiple (visible) windows
 * middle click: - start launcher
 * Returns: TRUE to stop other handlers from being invoked for the event. 
 *          FALSE to propagate the event further.
 * TODO: h4writer - adjust
 */
static gboolean
task_icon_button_release_event (GtkWidget      *widget,
                                GdkEventButton *event)
{
  TaskIconPrivate *priv;
  TaskIcon *icon;
  g_return_val_if_fail (TASK_IS_ICON (widget), FALSE);

  icon = TASK_ICON (widget);
  priv = icon->priv;

  switch (event->button)
  {
    case 1:
      task_icon_clicked (TASK_ICON(widget),event);
      return TRUE;      
      break;
    case 2: // middle click: start launcher

      //TODO: start launcher
      /* Find the launcher that is shown */
      for (GSList *w = priv->items; w; w = w->next)
      {
        TaskItem *item = w->data;

        if (!task_item_is_visible (item)) continue;
        if (!TASK_IS_LAUNCHER (item) ) continue;
        task_item_left_click (item, event);

        break;
      }
      return TRUE;
      break;

    default:
      break;
  }
  return FALSE;
}

static void
window_closed_cb (WnckScreen *screen,WnckWindow *window,TaskIcon * icon)
{
  TaskWindow * taskwin = NULL;
  GSList * iter;
  TaskIconPrivate *priv;
  
  g_return_if_fail (TASK_IS_ICON (icon));
  priv = icon->priv;  
  for (iter = priv->items; iter; iter=iter->next)
  {
    if (!TASK_IS_WINDOW(iter->data))
    {
      continue;
    }
    if (task_window_get_window (iter->data) == window)
    {
      taskwin = iter->data;
      break;
    }
  }
  if (taskwin)
  {
    on_window_needs_attention_changed (taskwin,FALSE,icon);
  }
}


static void 
size_changed_cb(AwnApplet *app, guint size, TaskIcon *icon)
{
  g_return_if_fail (AWN_IS_APPLET (app) );
  g_return_if_fail (TASK_IS_ICON (icon));

  task_icon_refresh_icon (icon,size);  
}

static void
theme_changed_cb (GtkIconTheme *icon_theme,TaskIcon * icon)
{
  TaskIconPrivate *priv;
  
  g_return_if_fail (TASK_IS_ICON (icon));
  priv = icon->priv;
  if (priv->icon)
  {
    task_icon_refresh_icon (icon,gdk_pixbuf_get_height(priv->icon));
  }
}

static void
add_to_launcher_list_cb (GtkMenuItem * menu_item, TaskIcon * icon)
{
  TaskIconPrivate *priv;
  TaskLauncher    *launcher = NULL;
  
  g_return_if_fail (TASK_IS_ICON (icon));  
  priv = icon->priv;
  
  launcher = TASK_LAUNCHER(task_icon_get_launcher (icon));
  if (launcher)
  {
    TaskManager * applet = TASK_MANAGER(priv->applet);
    task_manager_remove_task_icon (TASK_MANAGER(applet), GTK_WIDGET(icon));               
    task_manager_append_launcher (TASK_MANAGER(applet),
                                  task_launcher_get_desktop_path(launcher));
  }
}
/**
 * Whenever there is a press event on the TaskIcon it will do the proper actions.
 * right click: - show the context menu 
 * Returns: TRUE to stop other handlers from being invoked for the event. 
 *          FALSE to propagate the event further. 
 */
static gboolean  
task_icon_button_press_event (GtkWidget      *widget,
                              GdkEventButton *event)
{
  TaskIconPrivate *priv;
  TaskIcon *icon;
  GtkWidget   *item;
  GSList *iter;
  TaskItem * launcher = NULL;
  static GdkPixbuf * launcher_pbuf=NULL;
  gint width,height;
  
  g_return_val_if_fail (TASK_IS_ICON (widget), FALSE);

  icon = TASK_ICON (widget);
  priv = icon->priv;

  if (launcher_pbuf)
  {
    g_object_unref (launcher_pbuf);
  }
  gtk_icon_size_lookup (GTK_ICON_SIZE_MENU,&width,&height);
  launcher_pbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default(),
                                              "system-run",
                                              height,
                                              GTK_ICON_LOOKUP_FORCE_SIZE,
                                              NULL);
  
  launcher = task_icon_get_launcher (icon);  
          
  if (event->button != 3) return FALSE;

  if (priv->shown_items == 0)
  {
    g_critical ("TaskIcon: The icons shouldn't contain a visible (and clickable) icon");
  }
  else if (priv->main_item)
  {
    /*
     Could create some reusable code in TaskWindow/TaskLauncher.  
     For now just deal with it here...  once the exact layout/behaviour is
     finalized then look into it.  TODO
     */
    if (priv->menu)
    {
      gtk_widget_destroy (priv->menu);
      priv->menu = NULL;
    }
    if (priv->main_item)
    {
      if (TASK_IS_WINDOW (priv->main_item) && task_item_is_visible (priv->main_item))
      {
        priv->menu = wnck_action_menu_new (task_window_get_window (TASK_WINDOW(priv->main_item)));
        
        item = gtk_separator_menu_item_new();
        gtk_widget_show(item);
        gtk_menu_shell_prepend(GTK_MENU_SHELL(priv->menu), item);
        if (launcher)
        {
          item = gtk_image_menu_item_new_with_label (_("Launch"));
          if (launcher_pbuf)
          {
            gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                        gtk_image_new_from_pixbuf (launcher_pbuf));
          }
          gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), item);
          gtk_widget_show (item);
          g_signal_connect_swapped (item,"activate",
                        G_CALLBACK(task_item_middle_click),
                        launcher);
        }

        if (launcher)
        {
          /* Do not show this item... AwnThemedIcon handles that*/
          item = awn_themed_icon_create_remove_custom_icon_item (AWN_THEMED_ICON(icon),
                                                          priv->custom_name);
          gtk_menu_shell_prepend (GTK_MENU_SHELL(priv->menu), item);
          
          item = awn_themed_icon_create_custom_icon_item (AWN_THEMED_ICON(icon),
                                                          priv->custom_name);
          gtk_widget_show(item);
          gtk_menu_shell_prepend (GTK_MENU_SHELL(priv->menu), item);
          item = gtk_separator_menu_item_new();
          gtk_widget_show(item);
          gtk_menu_shell_prepend(GTK_MENU_SHELL(priv->menu), item);          
        }
        item = awn_applet_create_pref_item();
        gtk_menu_shell_prepend(GTK_MENU_SHELL(priv->menu), item);

        item = gtk_separator_menu_item_new();
        gtk_widget_show(item);
        gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu), item);
        for (iter = priv->items; iter; iter=iter->next)
        {
          GtkWidget   *sub_menu;
          if ( TASK_IS_LAUNCHER (iter->data) )
          {
            continue;
          }
          if ( !task_item_is_visible (iter->data))
          {
            continue;
          }
          if ( iter->data == priv->main_item)
            continue;
          item = gtk_menu_item_new_with_label (task_window_get_name (TASK_WINDOW(iter->data)));
          sub_menu = wnck_action_menu_new (task_window_get_window (TASK_WINDOW(iter->data)));          
          gtk_menu_item_set_submenu (GTK_MENU_ITEM(item),sub_menu);
          gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu), item);
          gtk_widget_show (item);
          gtk_widget_show (sub_menu);
        }
        if (priv->ephemeral_count == 1)
        {
          item = gtk_menu_item_new_with_label (_("Add to Launcher List"));
          gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu), item);
          gtk_widget_show (item);
          g_signal_connect (item,"activate",
                            G_CALLBACK(add_to_launcher_list_cb),
                            icon);
          item = gtk_separator_menu_item_new();
          gtk_widget_show(item);
          gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu), item);
          
        }
      }
    }
    
    if (!priv->menu)
    {
      priv->menu = gtk_menu_new ();

      item = gtk_separator_menu_item_new();
      gtk_widget_show(item);
      gtk_menu_shell_prepend(GTK_MENU_SHELL(priv->menu), item);

      item = awn_applet_create_pref_item();
      gtk_widget_show(item);
      gtk_menu_shell_prepend(GTK_MENU_SHELL(priv->menu), item);

      item = awn_themed_icon_create_custom_icon_item (AWN_THEMED_ICON(icon),
                                          priv->custom_name);
      gtk_widget_show(item);
      gtk_menu_shell_append (GTK_MENU_SHELL(priv->menu), item);

      /* Do not show this item... AwnThemedIcon handles that*/      
      item = awn_themed_icon_create_remove_custom_icon_item (AWN_THEMED_ICON(icon),
                                          priv->custom_name);
      gtk_menu_shell_append (GTK_MENU_SHELL(priv->menu), item);

      item = gtk_separator_menu_item_new();
      gtk_widget_show(item);
      gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu), item);
      
      if (launcher)
      {
        item = gtk_image_menu_item_new_with_label (_("Launch"));        
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      gtk_image_new_from_stock (GTK_STOCK_EXECUTE,GTK_ICON_SIZE_MENU));
        if (launcher_pbuf)
        {
          gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
                                      gtk_image_new_from_pixbuf (launcher_pbuf));
        }        
        gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), item);
        gtk_widget_show (item);
        g_signal_connect_swapped (item,"activate",
                      G_CALLBACK(task_item_middle_click),
                      launcher);
        item = gtk_separator_menu_item_new();
        gtk_widget_show(item);
        gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu), item);
      }      
    }
    item = awn_applet_create_about_item (priv->applet,                
           "Copyright 2008,2009 Neil Jagdish Patel <njpatel@gmail.com>\n"
           "          2009 Hannes Verschore <hv1989@gmail.com>\n"
           "          2009 Rodney Cryderman <rcryderman@gmail.com>",
           AWN_APPLET_LICENSE_GPLV2,
           NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(priv->menu), item);

#if GTK_CHECK_VERSION (2,16,0)	
    /* null op if GTK+ < 2.16.0*/
    awn_utils_show_menu_images (GTK_MENU(priv->menu));
#endif

    if (priv->menu)
    {
      gtk_menu_popup (GTK_MENU (priv->menu), NULL, NULL, 
                      NULL, NULL, event->button, event->time);
      
      g_signal_connect_swapped (priv->menu,"deactivate", 
                                G_CALLBACK(gtk_widget_hide),priv->dialog);      
    }
    return TRUE;
  }
  return FALSE;  
}

static gboolean  
task_icon_dialog_unfocus (GtkWidget      *widget,
                         GdkEventFocus  *event,
                         TaskIcon *icon)
{
  TaskIconPrivate *priv;

  g_return_val_if_fail (AWN_IS_DIALOG (widget), FALSE);  

  priv = icon->priv;
  
  if (!priv->inhibit_focus_loss)
  {
    gtk_widget_hide (priv->dialog);
    task_icon_set_draggable_state (icon, priv->draggable);    
  }
  return FALSE;
}

GtkWidget *     
task_icon_get_dialog (TaskIcon *icon)
{
  TaskIconPrivate *priv;

  g_return_val_if_fail (TASK_IS_ICON (icon), FALSE);  

  priv = icon->priv;
  
  return priv->dialog;
}

static void
task_icon_set_draggable_state (TaskIcon *icon, gboolean draggable)
{
  TaskIconPrivate *priv;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_if_fail (TASK_IS_ICON (icon));
  priv = icon->priv;

  if(draggable)
  {
    gtk_drag_source_set (GTK_WIDGET (icon),
                         GDK_BUTTON1_MASK,
                         task_icon_type, n_task_icon_type,
                         GDK_ACTION_MOVE);
  }
  else
  {
    gtk_drag_source_unset(GTK_WIDGET (icon));
  }
  //g_debug("draggable:%d", draggable);
}

/*
 * Drag and Drop code
 * - code to drop things on icons
 * - code to reorder icons through dragging
 */

void
task_icon_set_draggable (TaskIcon *icon, gboolean draggable)
{
  TaskIconPrivate *priv;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_if_fail (TASK_IS_ICON (icon));
  priv = icon->priv;

  priv->draggable = draggable;

  task_icon_set_draggable_state (icon,draggable);
}


/**
 * TODO: h4writer - second stage
 */
static gboolean
drag_timeout (TaskIcon *icon)
{
  TaskIconPrivate *priv;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_val_if_fail (TASK_IS_ICON (icon), FALSE);
  priv = icon->priv;

  if (priv->drag_motion == FALSE)
  {
    return FALSE;
  }
  else if (priv->main_item && TASK_IS_WINDOW(priv->main_item) )
  { 
    if (!task_window_is_active(TASK_WINDOW(priv->main_item)))
    {
      task_window_activate (TASK_WINDOW(priv->main_item), priv->drag_time);
    }
  }
  return FALSE;
}

static void
task_icon_drag_data_get (GtkWidget *widget, 
                         GdkDragContext *context, 
                         GtkSelectionData *selection_data,
                         guint target_type, 
                         guint time_)
{
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif  
  switch(target_type)
  {
    case TARGET_TASK_ICON:
      //FIXME: give the data, so another taskmanager can accept this drop
      gtk_selection_data_set (selection_data, GDK_TARGET_STRING, 8, NULL, 0);
      break;
    default:
      /* Default to some a safe target instead of fail. */
      g_assert_not_reached ();
  }
}

/* DnD 'source' forwards */

static void
task_icon_source_drag_begin (GtkWidget      *widget,
                             GdkDragContext *context)
{
  TaskIconPrivate *priv;
  TaskSettings *settings;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_if_fail (TASK_IS_ICON (widget));
  priv = TASK_ICON (widget)->priv;

  if(!priv->draggable) return;

  priv->gets_dragged = TRUE;
  
  if (GTK_WIDGET_VISIBLE(priv->dialog))
  {
    gtk_widget_hide (priv->dialog);
    task_icon_set_draggable_state (TASK_ICON(widget), priv->draggable);
  }

  settings = task_settings_get_default (NULL);

  gtk_drag_set_icon_pixbuf (context, priv->icon, settings->panel_size/2, settings->panel_size/2);

  g_signal_emit (TASK_ICON (widget), _icon_signals[SOURCE_DRAG_BEGIN], 0);
}

static void
task_icon_source_drag_end (GtkWidget      *widget,
                           GdkDragContext *drag_context)
{
  TaskIconPrivate *priv;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_if_fail (TASK_IS_ICON (widget));
  priv = TASK_ICON (widget)->priv;

  if(!priv->gets_dragged) return;

  priv->gets_dragged = FALSE;
  g_signal_emit (TASK_ICON (widget), _icon_signals[SOURCE_DRAG_END], 0);
}

static gboolean
task_icon_source_drag_fail (GtkWidget      *widget,
                          GdkDragContext *drag_context,
                          GtkDragResult   result)
{
  TaskIconPrivate *priv;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_val_if_fail (TASK_IS_ICON (widget), FALSE);
  priv = TASK_ICON (widget)->priv;

  if(!priv->draggable) return TRUE;
  if(!priv->gets_dragged) return TRUE;

  priv->gets_dragged = FALSE;

  g_signal_emit (TASK_ICON (widget), _icon_signals[SOURCE_DRAG_FAIL], 0);

  return TRUE;
}

/* DnD 'destination' forwards */

static gboolean
task_icon_dest_drag_motion (GtkWidget      *widget,
                            GdkDragContext *context,
                            gint            x,
                            gint            y,
                            guint           t)
{
  TaskIconPrivate *priv;
  GdkAtom target;
  gchar *target_name;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_val_if_fail (TASK_IS_ICON (widget), FALSE);
  priv = TASK_ICON (widget)->priv;

  target = gtk_drag_dest_find_target (widget, context, NULL);
  target_name = gdk_atom_name (target);
  awn_effects_start_ex (awn_overlayable_get_effects (AWN_OVERLAYABLE (widget)), 
                  AWN_EFFECT_LAUNCHING, 1, FALSE, FALSE); 
  if (g_strcmp0("awn/task-icon", target_name) == 0)
  {
    if(!priv->draggable) return FALSE;

    gdk_drag_status (context, GDK_ACTION_MOVE, t);

    g_signal_emit (TASK_ICON (widget), _icon_signals[DEST_DRAG_MOVE], 0, x, y);
    return TRUE;
  }
  else
  {
    if (priv->drag_tag)
    {
      /* If it is a launcher it should show that it accepts the drag.
         Else only the the timeout should get set to activate the window. */
      
      //TODO: h4writer - 2nd round
      //if (!priv->items || !TASK_IS_LAUNCHER (priv->items->data))
      //  gdk_drag_status (context, GDK_ACTION_DEFAULT, t);
      //else
      gdk_drag_status (context, GDK_ACTION_COPY, t);
      return TRUE;
    }
    if (priv->main_item && TASK_IS_WINDOW (priv->main_item) )
    {
      if (!task_window_is_active(TASK_WINDOW(priv->main_item)) )
      {
        if (priv->drag_tag)
        {
          g_source_remove (priv->drag_tag);
        }
        priv->drag_motion = TRUE;
        priv->drag_tag = g_timeout_add (priv->drag_and_drop_hover_delay, 
                                        (GSourceFunc)drag_timeout, widget);
        priv->drag_time = t;
      }
    }
    else
    {
      gdk_drag_status (context, GDK_ACTION_COPY, t);
      return TRUE;
    }
    return FALSE;
  }
}

static void   
task_icon_dest_drag_leave (GtkWidget      *widget,
                           GdkDragContext *context,
                           guint           time_)
{
  TaskIconPrivate *priv;
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_if_fail (TASK_IS_ICON (widget));
  priv = TASK_ICON (widget)->priv;

  if(priv->drag_motion)
  {
    priv->drag_motion = FALSE;
    g_source_remove (priv->drag_tag);
    priv->drag_tag = 0;
  }
  g_signal_emit (TASK_ICON (widget), _icon_signals[DEST_DRAG_LEAVE], 0);
}


static void 
copy_over (const gchar *src, const gchar *dest)
{
  DesktopAgnosticVFSFile *from = NULL;
  DesktopAgnosticVFSFile *to = NULL;
  GError                 *error = NULL;

  from = desktop_agnostic_vfs_file_new_for_path (src, &error);
  if (error)
  {
    goto copy_over_error;
  }
  to = desktop_agnostic_vfs_file_new_for_path (dest, &error);
  if (error)
  {
    goto copy_over_error;
  }

  desktop_agnostic_vfs_file_copy (from, to, TRUE, &error);

copy_over_error:

  if (error)
  {
    g_warning ("Unable to copy %s to %s: %s", src, dest, error->message);
    g_error_free (error);
  }

  if (to)
  {
    g_object_unref (to);
  }
  if (from)
  {
    g_object_unref (from);
  }
}


static void
task_icon_dest_drag_data_received (GtkWidget      *widget,
                                   GdkDragContext *context,
                                   gint            x,
                                   gint            y,
                                   GtkSelectionData *sdata,
                                   guint           info,
                                   guint           time_)
{
  TaskIcon        *icon = TASK_ICON(widget);
  TaskIconPrivate *priv;
  GSList          *list;
  GError          *error;
  GdkAtom         target;
  gchar           *target_name;
  gchar           *sdata_data;
  TaskLauncher    *launcher = NULL;
  GStrv           tokens = NULL;
  gchar           ** i;  
  
#ifdef DEBUG
  g_debug ("%s",__func__);
#endif
  g_return_if_fail (TASK_IS_ICON (widget));
  priv = icon->priv;

  target = gtk_drag_dest_find_target (widget, context, NULL);
  target_name = gdk_atom_name (target);

  /* If it is dragging of the task icon, there is actually no data */
  if (g_strcmp0("awn/task-icon", target_name) == 0)
  {
    gtk_drag_finish (context, TRUE, TRUE, time_);
    return;
  }
  sdata_data = (gchar*)gtk_selection_data_get_data (sdata);

  /* If we are dealing with a desktop file, then we want to do something else
   * FIXME: This is a crude way of checking
   */
  if (strstr (sdata_data, ".desktop"))
  {
    /*TODO move into a separate function */
    tokens = g_strsplit  (sdata_data, "\n",-1);    
    for (i=tokens; *i;i++)
    {
      gchar * filename = g_filename_from_uri ((gchar*) *i,NULL,NULL);
      if (!filename && ((gchar *)*i))
      {
        filename = g_strdup ((gchar*)*i);
      }
      if (filename)
      {
        g_strstrip(filename);
      }
      if (filename && strlen(filename) && strstr (filename,".desktop"))
      {
        if (filename)
        {
          gboolean make_copy = FALSE;
          struct stat stat_buf;
          switch (priv->desktop_copy)
          {
            case DESKTOP_COPY_ALL:
              make_copy = TRUE;
              break;
            case DESKTOP_COPY_OWNER:
              g_stat (filename,&stat_buf);
              if ( stat_buf.st_uid == getuid())
              {
                make_copy = TRUE;
              }
              break;
            case DESKTOP_COPY_NONE:
            default:
              break;
          }
          if (make_copy)
          {
            gchar * launcher_dir = NULL;
            gchar * file_basename;
            gchar * dest;
            launcher_dir = g_strdup_printf ("%s/.config/awn/launchers", 
                                              g_get_home_dir ());
            g_mkdir_with_parents (launcher_dir,0755); 
            file_basename = g_path_get_basename (filename);
            dest = g_strdup_printf("%s/%lu-%s",launcher_dir,(gulong)time(NULL),file_basename);
            copy_over (filename,dest);
            g_free (file_basename);
            g_free (filename);
            g_free (launcher_dir);
            filename = dest;
          }
          task_manager_append_launcher (TASK_MANAGER(priv->applet),filename);
        }
      }
      if (filename)
      {
        g_free (filename);
      }
    }
    g_strfreev (tokens);
    gtk_drag_finish (context, TRUE, FALSE, time_);
    return;
  }

  error = NULL;

  //Don't use for now until desktop_agnostic_fdo_desktop_entry_launch() gets modified.
/*  list = desktop_agnostic_vfs_files_from_uri_list (sdata_data, &error);
  if (error)
  {
    g_warning ("Unable to handle drop: %s", error->message);
    g_error_free (error);
    gtk_drag_finish (context, FALSE, FALSE, time_);
    return;
  }
*/
  //temp replacement for the code above.
  list = NULL;
  tokens = g_strsplit  (sdata_data, "\n",-1);
  for (i=tokens; *i;i++)
  {
    gchar * str = g_filename_from_uri ((gchar*) *i,NULL,NULL);
    if (str)
    {
      g_strstrip(str);
    }
    if (str && strlen(str) )
    {
      list = g_slist_append (list,str);
    }
  }
  g_strfreev (tokens);
    
  launcher = TASK_LAUNCHER(task_icon_get_launcher (icon));
  
  if (launcher && list && g_slist_length(list) )
  {
    task_launcher_launch_with_data (launcher, list);
    gtk_drag_finish (context, TRUE,TRUE, time_);
    return;
  }

  if (list)
  {
    g_slist_foreach (list, (GFunc)g_free, NULL);
    g_slist_free (list);
  }

  gtk_drag_finish (context, TRUE, FALSE, time_);
}
