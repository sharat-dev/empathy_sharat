/*
*  Copyright (C) 2009 Collabora Ltd.
*
*  This library is free software; you can redistribute it and/or
*  modify it under the terms of the GNU Lesser General Public
*  License as published by the Free Software Foundation; either
*  version 2.1 of the License, or (at your option) any later version.
*
*  This library is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*  Lesser General Public License for more details.
*
*  You should have received a copy of the GNU Lesser General Public
*  License along with this library; if not, write to the Free Software
*  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
*  Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
*/

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-account-chooser.h>

#include <telepathy-glib/dbus.h>

#include "extensions/extensions.h"

#include "empathy-debug-dialog.h"

G_DEFINE_TYPE (EmpathyDebugDialog, empathy_debug_dialog,
    GTK_TYPE_DIALOG)

enum
{
  PROP_0,
  PROP_PARENT
};

enum
{
  COL_TIMESTAMP = 0,
  COL_DOMAIN,
  COL_CATEGORY,
  COL_LEVEL,
  COL_MESSAGE,
  NUM_COLS
};

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyDebugDialog)
typedef struct
{
  GtkWidget *filter;
  GtkWindow *parent;
  GtkWidget *view;
  GtkWidget *account_chooser;
  GtkListStore *store;
  gboolean dispose_run;
} EmpathyDebugDialogPriv;

static const gchar *
log_level_to_string (guint level)
{
  switch (level)
    {
    case EMP_DEBUG_LEVEL_ERROR:
      return _("Error");
      break;
    case EMP_DEBUG_LEVEL_CRITICAL:
      return _("Critical");
      break;
    case EMP_DEBUG_LEVEL_WARNING:
      return _("Warning");
      break;
    case EMP_DEBUG_LEVEL_MESSAGE:
      return _("Message");
      break;
    case EMP_DEBUG_LEVEL_INFO:
      return _("Info");
      break;
    case EMP_DEBUG_LEVEL_DEBUG:
      return _("Debug");
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

static void
debug_dialog_get_messages_cb (TpProxy *proxy,
			      const GPtrArray *messages,
			      const GError *error,
			      gpointer user_data,
			      GObject *weak_object)
{
  EmpathyDebugDialog *debug_dialog = (EmpathyDebugDialog *) user_data;
  EmpathyDebugDialogPriv *priv = GET_PRIV (debug_dialog);
  gint i;
  GtkTreeIter iter;

  if (error != NULL)
    {
      DEBUG ("GetMessages failed: %s", error->message);
      return;
    }

  for (i = 0; i < messages->len; i++)
    {
      GValueArray *values;
      gdouble timestamp;
      const gchar *domain_category;
      guint level;
      const gchar *message;

      gchar *domain;
      gchar *category;

      values = g_ptr_array_index (messages, i);

      timestamp = g_value_get_double (g_value_array_get_nth (values, 0));
      domain_category = g_value_get_string (g_value_array_get_nth (values, 1));
      level = g_value_get_uint (g_value_array_get_nth (values, 2));
      message = g_value_get_string (g_value_array_get_nth (values, 3));

      if (g_strrstr (domain_category, "/"))
	{
	  gchar **parts = g_strsplit (domain_category, "/", 2);
	  domain = g_strdup (parts[0]);
	  category = g_strdup (parts[1]);
	  g_strfreev (parts);
	}
      else
	{
	  domain = g_strdup (domain_category);
	  category = "";
	}

      gtk_list_store_append (priv->store, &iter);
      gtk_list_store_set (priv->store, &iter,
          COL_TIMESTAMP, timestamp,
          COL_DOMAIN, domain,
          COL_CATEGORY, category,
	  COL_LEVEL, log_level_to_string (level),
          COL_MESSAGE, message,
          -1);

      g_free (domain);
      g_free (category);
    }
}

static void
debug_dialog_account_chooser_changed_cb (GtkComboBox *account_chooser,
					 EmpathyDebugDialog *debug_dialog)
{
  McAccount *account;
  TpConnection *connection;
  MissionControl *mc;
  TpDBusDaemon *dbus;
  GError *error = NULL;

  mc = empathy_mission_control_dup_singleton ();
  account = empathy_account_chooser_get_account (EMPATHY_ACCOUNT_CHOOSER (account_chooser));
  connection = mission_control_get_tpconnection (mc, account, &error);

  if (error != NULL)
    {
      DEBUG ("Getting the account's TpConnection failed: %s", error->message);
      g_error_free (error);
      return;
    }

  dbus = g_object_ref (tp_proxy_get_dbus_daemon (connection));

  /* TODO: Fix this. */
  connection = tp_connection_new (dbus,
				  "org.freedesktop.Telepathy.ConnectionManager.gabble",
				  "/org/freedesktop/Telepathy/debug",
				  &error);

  if (error != NULL)
    {
      DEBUG ("Getting a new TpConnection failed: %s", error->message);
      g_error_free (error);
      return;
    }

  emp_cli_debug_call_get_messages (TP_PROXY (connection), -1,
      debug_dialog_get_messages_cb, debug_dialog, NULL, NULL);

  g_object_unref (connection);
  g_object_unref (account);
  g_object_unref (dbus);
}

static GObject *
debug_dialog_constructor (GType type,
                          guint n_construct_params,
                          GObjectConstructParam *construct_params)
{
  GObject *object;
  EmpathyDebugDialogPriv *priv;
  GtkWidget *vbox;
  GtkWidget *toolbar;
  GtkWidget *image;
  GtkToolItem *item;
  GtkCellRenderer *renderer;
  GtkWidget *scrolled_win;

  object = G_OBJECT_CLASS (empathy_debug_dialog_parent_class)->constructor
    (type, n_construct_params, construct_params);
  priv = GET_PRIV (object);

  gtk_window_set_title (GTK_WINDOW (object), _("Debug Window"));
  gtk_window_set_default_size (GTK_WINDOW (object), 800, 400);
  gtk_window_set_transient_for (GTK_WINDOW (object), priv->parent);

  vbox = GTK_DIALOG (object)->vbox;

  toolbar = gtk_toolbar_new ();
  gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);
  gtk_toolbar_set_show_arrow (GTK_TOOLBAR (toolbar), TRUE);
  gtk_toolbar_set_icon_size (GTK_TOOLBAR (toolbar), GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_widget_show (toolbar);

  gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, FALSE, 0);

  /* Account */
  item = gtk_tool_item_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_container_add (GTK_CONTAINER (item), gtk_label_new (_("Account ")));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  priv->account_chooser = empathy_account_chooser_new ();
  empathy_account_chooser_set_filter (EMPATHY_ACCOUNT_CHOOSER (priv->account_chooser),
      (EmpathyAccountChooserFilterFunc) mc_account_is_enabled, NULL);
  g_signal_connect (priv->account_chooser, "changed",
      G_CALLBACK (debug_dialog_account_chooser_changed_cb), object);
  gtk_widget_show (priv->account_chooser);

  item = gtk_tool_item_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_container_add (GTK_CONTAINER (item), priv->account_chooser);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  item = gtk_separator_tool_item_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  /* Save */
  item = gtk_tool_button_new_from_stock (GTK_STOCK_SAVE);
  gtk_widget_show (GTK_WIDGET (item));
  gtk_tool_item_set_is_important (GTK_TOOL_ITEM (item), TRUE);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  /* Clear */
  item = gtk_tool_button_new_from_stock (GTK_STOCK_CLEAR);
  gtk_widget_show (GTK_WIDGET (item));
  gtk_tool_item_set_is_important (GTK_TOOL_ITEM (item), TRUE);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  item = gtk_separator_tool_item_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  /* Pause */
  image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PAUSE, GTK_ICON_SIZE_MENU);
  gtk_widget_show (image);
  item = gtk_toggle_tool_button_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_tool_item_set_is_important (GTK_TOOL_ITEM (item), TRUE);
  gtk_tool_button_set_label (GTK_TOOL_BUTTON (item), _("Pause"));
  gtk_tool_button_set_icon_widget (GTK_TOOL_BUTTON (item), image);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  item = gtk_separator_tool_item_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  /* Level */
  item = gtk_tool_item_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_container_add (GTK_CONTAINER (item), gtk_label_new (_("Level ")));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  priv->filter = gtk_combo_box_new_text ();
  gtk_widget_show (priv->filter);

  item = gtk_tool_item_new ();
  gtk_widget_show (GTK_WIDGET (item));
  gtk_container_add (GTK_CONTAINER (item), priv->filter);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  gtk_combo_box_append_text (GTK_COMBO_BOX (priv->filter), _("All"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (priv->filter), _("Debug"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (priv->filter), _("Info"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (priv->filter), _("Message"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (priv->filter), _("Warning"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (priv->filter), _("Critical"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (priv->filter), _("Error"));

  gtk_combo_box_set_active (GTK_COMBO_BOX (priv->filter), 0);
  gtk_widget_show (GTK_WIDGET (priv->filter));

  /* Debug treeview */
  priv->view = gtk_tree_view_new ();

  renderer = gtk_cell_renderer_text_new ();

  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (priv->view),
      -1, _("Time"), renderer, "text", COL_TIMESTAMP, NULL);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (priv->view),
      -1, _("Domain"), renderer, "text", COL_DOMAIN, NULL);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (priv->view),
      -1, _("Category"), renderer, "text", COL_CATEGORY, NULL);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (priv->view),
      -1, _("Level"), renderer, "text", COL_LEVEL, NULL);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (priv->view),
      -1, _("Message"), renderer, "text", COL_MESSAGE, NULL);

  priv->store = gtk_list_store_new (NUM_COLS, G_TYPE_DOUBLE,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  /* Fill treeview */
  debug_dialog_account_chooser_changed_cb (GTK_COMBO_BOX (priv->account_chooser),
      EMPATHY_DEBUG_DIALOG (object));

  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->view),
      GTK_TREE_MODEL (priv->store));

  /* Scrolled window */
  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  gtk_widget_show (priv->view);
  gtk_container_add (GTK_CONTAINER (scrolled_win), priv->view);

  gtk_widget_show (scrolled_win);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled_win, TRUE, TRUE, 0);

  gtk_widget_show (GTK_WIDGET (object));

  return object;
}

static void
empathy_debug_dialog_init (EmpathyDebugDialog *empathy_debug_dialog)
{
  EmpathyDebugDialogPriv *priv =
      G_TYPE_INSTANCE_GET_PRIVATE (empathy_debug_dialog,
      EMPATHY_TYPE_DEBUG_DIALOG, EmpathyDebugDialogPriv);

  empathy_debug_dialog->priv = priv;

  priv->dispose_run = FALSE;
}

static void
debug_dialog_set_property (GObject *object,
                           guint prop_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
  EmpathyDebugDialogPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_PARENT:
	priv->parent = GTK_WINDOW (g_value_dup_object (value));
	break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
debug_dialog_get_property (GObject *object,
                           guint prop_id,
                           GValue *value,
                           GParamSpec *pspec)
{
  EmpathyDebugDialogPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_PARENT:
	g_value_set_object (value, priv->parent);
	break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
debug_dialog_dispose (GObject *object)
{
  EmpathyDebugDialog *selector = EMPATHY_DEBUG_DIALOG (object);
  EmpathyDebugDialogPriv *priv = GET_PRIV (selector);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  if (priv->parent)
    g_object_unref (priv->parent);

  if (priv->store)
    g_object_unref (priv->store);

  (G_OBJECT_CLASS (empathy_debug_dialog_parent_class)->dispose) (object);
}

static void
empathy_debug_dialog_class_init (EmpathyDebugDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->constructor = debug_dialog_constructor;
  object_class->dispose = debug_dialog_dispose;
  object_class->set_property = debug_dialog_set_property;
  object_class->get_property = debug_dialog_get_property;
  g_type_class_add_private (klass, sizeof (EmpathyDebugDialogPriv));

  g_object_class_install_property (object_class, PROP_PARENT,
      g_param_spec_object ("parent", "parent", "parent",
      GTK_TYPE_WINDOW, G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));
}

/* public methods */

GtkWidget *
empathy_debug_dialog_new (GtkWindow *parent)
{
  return GTK_WIDGET (g_object_new (EMPATHY_TYPE_DEBUG_DIALOG,
      "parent", parent, NULL));
}
