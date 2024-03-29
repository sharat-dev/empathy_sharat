/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2008 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#include <config.h>

#include <string.h>

#include <glib.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-presence-chooser.h>
#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-images.h>
#include <libempathy-gtk/empathy-new-message-dialog.h>
#include <libempathy-gtk/empathy-new-call-dialog.h>

#include "empathy-accounts-dialog.h"
#include "empathy-status-icon.h"
#include "empathy-preferences.h"
#include "empathy-event-manager.h"

#define DEBUG_FLAG EMPATHY_DEBUG_DISPATCHER
#include <libempathy/empathy-debug.h>

/* Number of ms to wait when blinking */
#define BLINK_TIMEOUT 500

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyStatusIcon)
typedef struct {
	GtkStatusIcon       *icon;
	TpAccountManager    *account_manager;
	gboolean             showing_event_icon;
	guint                blink_timeout;
	EmpathyEventManager *event_manager;
	EmpathyEvent        *event;
	GSettings           *gsettings_ui;

	GtkWidget           *window;
	GtkUIManager        *ui_manager;
	GtkWidget           *popup_menu;
	GtkAction           *show_window_item;
	GtkAction           *new_message_item;
	GtkAction           *status_item;
} EmpathyStatusIconPriv;

G_DEFINE_TYPE (EmpathyStatusIcon, empathy_status_icon, G_TYPE_OBJECT);

static void
status_icon_update_tooltip (EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	if (priv->event) {
		gchar *tooltip = NULL;

		if (priv->event->message != NULL)
				tooltip = g_markup_printf_escaped ("<i>%s</i>\n%s",
								   priv->event->header,
								   priv->event->message);
		else
				tooltip = g_markup_printf_escaped ("<i>%s</i>",
								   priv->event->header);
		gtk_status_icon_set_tooltip_markup (priv->icon, tooltip);
		g_free (tooltip);
	} else {
		TpConnectionPresenceType type;
		gchar *msg;

		type = tp_account_manager_get_most_available_presence (
			priv->account_manager, NULL, &msg);

		if (!EMP_STR_EMPTY (msg)) {
			gtk_status_icon_set_tooltip_text (priv->icon, msg);
		}
		else {
			gtk_status_icon_set_tooltip_text (priv->icon,
						empathy_presence_get_default_message (type));
		}

		g_free (msg);
	}
}

static void
status_icon_update_icon (EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);
	const gchar           *icon_name;

	if (priv->event && priv->showing_event_icon) {
		icon_name = priv->event->icon_name;
	} else {
		TpConnectionPresenceType state;

		state = tp_account_manager_get_most_available_presence (
			priv->account_manager, NULL, NULL);

		/* An unset presence type here doesn't make sense. Force it
		 * to be offline. */
		if (state == TP_CONNECTION_PRESENCE_TYPE_UNSET) {
			state = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
		}

		icon_name = empathy_icon_name_for_presence (state);
	}

	if (icon_name != NULL)
		gtk_status_icon_set_from_icon_name (priv->icon, icon_name);
}

static gboolean
status_icon_blink_timeout_cb (EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	priv->showing_event_icon = !priv->showing_event_icon;
	status_icon_update_icon (icon);

	return TRUE;
}
static void
status_icon_event_added_cb (EmpathyEventManager *manager,
			    EmpathyEvent        *event,
			    EmpathyStatusIcon   *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	if (priv->event) {
		return;
	}

	DEBUG ("New event %p", event);

	priv->event = event;
	if (event->must_ack || event->type == EMPATHY_EVENT_TYPE_AUTH) {
		priv->showing_event_icon = TRUE;
		status_icon_update_icon (icon);
		status_icon_update_tooltip (icon);
	}

	if (!priv->blink_timeout && priv->showing_event_icon) {
		priv->blink_timeout = g_timeout_add (BLINK_TIMEOUT,
						     (GSourceFunc) status_icon_blink_timeout_cb,
						     icon);
	}
}

static void
status_icon_event_removed_cb (EmpathyEventManager *manager,
			      EmpathyEvent        *event,
			      EmpathyStatusIcon   *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	if (event != priv->event) {
		return;
	}

	priv->event = empathy_event_manager_get_top_event (priv->event_manager);

	status_icon_update_tooltip (icon);
	status_icon_update_icon (icon);

	if (!priv->event && priv->blink_timeout) {
		g_source_remove (priv->blink_timeout);
		priv->blink_timeout = 0;
	}
}

static void
status_icon_event_updated_cb (EmpathyEventManager *manager,
			      EmpathyEvent        *event,
			      EmpathyStatusIcon   *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	if (event != priv->event) {
		return;
	}

	status_icon_update_tooltip (icon);
}

static void
status_icon_set_visibility (EmpathyStatusIcon *icon,
			    gboolean           visible,
			    gboolean           store)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	if (store) {
		g_settings_set_boolean (priv->gsettings_ui,
					EMPATHY_PREFS_UI_MAIN_WINDOW_HIDDEN,
					!visible);
	}

	if (!visible) {
		gtk_widget_hide (priv->window);
	} else {
		empathy_window_present (GTK_WINDOW (priv->window));
	}
}

static void
status_icon_notify_visibility_cb (GSettings   *gsettings,
				  const gchar *key,
				  gpointer     user_data)
{
	EmpathyStatusIcon *icon = user_data;
	gboolean           hidden = FALSE;

	hidden = g_settings_get_boolean (gsettings, key);
	status_icon_set_visibility (icon, !hidden, FALSE);
}

static void
status_icon_toggle_visibility (EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);
	gboolean               visible;

	visible = gtk_window_is_active (GTK_WINDOW (priv->window));
	status_icon_set_visibility (icon, !visible, TRUE);
}

static void
status_icon_presence_changed_cb (EmpathyStatusIcon *icon)
{
	status_icon_update_icon (icon);
	status_icon_update_tooltip (icon);
}

static gboolean
status_icon_delete_event_cb (GtkWidget         *widget,
			     GdkEvent          *event,
			     EmpathyStatusIcon *icon)
{
	status_icon_set_visibility (icon, FALSE, TRUE);
	return TRUE;
}

static gboolean
status_icon_key_press_event_cb  (GtkWidget *window,
				 GdkEventKey *event,
				 EmpathyStatusIcon *icon)
{
	if (event->keyval == GDK_KEY_Escape) {
		status_icon_set_visibility (icon, FALSE, TRUE);
	}
	return FALSE;
}

static void
status_icon_activate_cb (GtkStatusIcon     *status_icon,
			 EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	DEBUG ("%s", priv->event ? "event" : "toggle");

	if (priv->event) {
		empathy_event_activate (priv->event);
	} else {
		status_icon_toggle_visibility (icon);
	}
}

static void
status_icon_show_hide_window_cb (GtkToggleAction   *action,
				 EmpathyStatusIcon *icon)
{
	gboolean visible;

	visible = gtk_toggle_action_get_active (action);
	status_icon_set_visibility (icon, visible, TRUE);
}

static void
status_icon_new_message_cb (GtkAction         *action,
			    EmpathyStatusIcon *icon)
{
	empathy_new_message_dialog_show (NULL);
}

static void
status_icon_new_call_cb (GtkAction         *action,
			    EmpathyStatusIcon *icon)
{
	empathy_new_call_dialog_show (NULL);
}

static void
status_icon_quit_cb (GtkAction         *action,
		     EmpathyStatusIcon *icon)
{
	gtk_main_quit ();
}

static void
status_icon_popup_menu_cb (GtkStatusIcon     *status_icon,
			   guint              button,
			   guint              activate_time,
			   EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);
	GtkWidget             *menu_item;
	GtkWidget             *submenu;
	gboolean               show;

	show = gtk_widget_get_visible (priv->window);

	g_signal_handlers_block_by_func (priv->show_window_item,
					 status_icon_show_hide_window_cb,
					 icon);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (priv->show_window_item),
				      show);
	g_signal_handlers_unblock_by_func (priv->show_window_item,
					   status_icon_show_hide_window_cb,
					   icon);

	menu_item = gtk_ui_manager_get_widget (priv->ui_manager, "/menu/status");
	submenu = empathy_presence_chooser_create_menu ();
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), submenu);

	gtk_menu_popup (GTK_MENU (priv->popup_menu),
			NULL, NULL,
			gtk_status_icon_position_menu,
			priv->icon,
			button,
			activate_time);
}

static void
status_icon_create_menu (EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);
	GtkBuilder            *gui;
	gchar                 *filename;

	filename = empathy_file_lookup ("empathy-status-icon.ui", "src");
	gui = empathy_builder_get_file (filename,
					"ui_manager", &priv->ui_manager,
					"menu", &priv->popup_menu,
					"show_list", &priv->show_window_item,
					"new_message", &priv->new_message_item,
					"status", &priv->status_item,
				       NULL);
	g_free (filename);

	empathy_builder_connect (gui, icon,
			      "show_list", "toggled", status_icon_show_hide_window_cb,
			      "new_message", "activate", status_icon_new_message_cb,
			      "new_call", "activate", status_icon_new_call_cb,
			      "quit", "activate", status_icon_quit_cb,
			      NULL);

	g_object_ref (priv->ui_manager);
	g_object_unref (gui);
}

static void
status_icon_status_changed_cb (TpAccount *account,
			       TpConnectionStatus current,
			       TpConnectionStatus previous,
			       TpConnectionStatusReason reason,
			       gchar *dbus_error_name,
			       GHashTable *details,
			       EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (icon);

	gtk_action_set_sensitive (priv->new_message_item,
				  empathy_account_manager_get_accounts_connected (NULL));
}

static void
status_icon_finalize (GObject *object)
{
	EmpathyStatusIconPriv *priv = GET_PRIV (object);

	if (priv->blink_timeout) {
		g_source_remove (priv->blink_timeout);
	}

	g_object_unref (priv->icon);
	g_object_unref (priv->account_manager);
	g_object_unref (priv->event_manager);
	g_object_unref (priv->ui_manager);
	g_object_unref (priv->gsettings_ui);
	g_object_unref (priv->window);
}

static void
empathy_status_icon_class_init (EmpathyStatusIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = status_icon_finalize;

	g_type_class_add_private (object_class, sizeof (EmpathyStatusIconPriv));
}

static void
account_manager_prepared_cb (GObject *source_object,
			     GAsyncResult *result,
			     gpointer user_data)
{
	GList *list, *l;
	TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
	EmpathyStatusIcon *icon = user_data;
	GError *error = NULL;

	if (!tp_proxy_prepare_finish (account_manager, result, &error)) {
		DEBUG ("Failed to prepare account manager: %s", error->message);
		g_error_free (error);
		return;
	}

	list = tp_account_manager_get_valid_accounts (account_manager);
	for (l = list; l != NULL; l = l->next) {
		tp_g_signal_connect_object (l->data, "status-changed",
					     G_CALLBACK (status_icon_status_changed_cb),
					     icon, 0);
	}
	g_list_free (list);

	status_icon_presence_changed_cb (icon);
}

static void
empathy_status_icon_init (EmpathyStatusIcon *icon)
{
	EmpathyStatusIconPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (icon,
		EMPATHY_TYPE_STATUS_ICON, EmpathyStatusIconPriv);

	icon->priv = priv;
	priv->icon = gtk_status_icon_new ();
	priv->account_manager = tp_account_manager_dup ();
	priv->event_manager = empathy_event_manager_dup_singleton ();

	tp_proxy_prepare_async (priv->account_manager, NULL,
	    account_manager_prepared_cb, icon);

	/* make icon listen and respond to MAIN_WINDOW_HIDDEN changes */
	priv->gsettings_ui = g_settings_new (EMPATHY_PREFS_UI_SCHEMA);
	g_signal_connect (priv->gsettings_ui,
			  "changed::" EMPATHY_PREFS_UI_MAIN_WINDOW_HIDDEN,
			  G_CALLBACK (status_icon_notify_visibility_cb),
			  icon);

	status_icon_create_menu (icon);

	g_signal_connect_swapped (priv->account_manager,
				  "most-available-presence-changed",
				  G_CALLBACK (status_icon_presence_changed_cb),
				  icon);
	g_signal_connect (priv->event_manager, "event-added",
			  G_CALLBACK (status_icon_event_added_cb),
			  icon);
	g_signal_connect (priv->event_manager, "event-removed",
			  G_CALLBACK (status_icon_event_removed_cb),
			  icon);
	g_signal_connect (priv->event_manager, "event-updated",
			  G_CALLBACK (status_icon_event_updated_cb),
			  icon);
	g_signal_connect (priv->icon, "activate",
			  G_CALLBACK (status_icon_activate_cb),
			  icon);
	g_signal_connect (priv->icon, "popup-menu",
			  G_CALLBACK (status_icon_popup_menu_cb),
			  icon);
}

EmpathyStatusIcon *
empathy_status_icon_new (GtkWindow *window, gboolean hide_contact_list)
{
	EmpathyStatusIconPriv *priv;
	EmpathyStatusIcon     *icon;
	gboolean               should_hide;

	g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

	icon = g_object_new (EMPATHY_TYPE_STATUS_ICON, NULL);
	priv = GET_PRIV (icon);

	priv->window = g_object_ref (window);

	g_signal_connect_after (priv->window, "key-press-event",
			  G_CALLBACK (status_icon_key_press_event_cb),
			  icon);

	g_signal_connect (priv->window, "delete-event",
			  G_CALLBACK (status_icon_delete_event_cb),
			  icon);

	if (!hide_contact_list) {
		should_hide = g_settings_get_boolean (priv->gsettings_ui,
			EMPATHY_PREFS_UI_MAIN_WINDOW_HIDDEN);
	} else {
		should_hide = TRUE;
	}

	status_icon_set_visibility (icon, !should_hide, FALSE);

	return icon;
}

