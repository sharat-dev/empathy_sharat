/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 * 
 * Authors: Martyn Russell <martyn@imendio.com>
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <libtelepathy/tp-conn.h>
#include <libmissioncontrol/mc-account-monitor.h>
#include <libmissioncontrol/mission-control.h>

#include <libempathy/gossip-utils.h>

#include "gossip-ui-utils.h"
#include "gossip-account-chooser.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_ACCOUNT_CHOOSER, GossipAccountChooserPriv))

typedef struct {
	MissionControl   *mc;
	McAccountMonitor *monitor;

	gboolean          set_active_item;
	gboolean          can_select_all;
	gboolean          has_all_option;
} GossipAccountChooserPriv;

typedef struct {
	GossipAccountChooser *chooser;
	McAccount            *account;
	gboolean              set;
} SetAccountData;

enum {
	COL_ACCOUNT_IMAGE,
	COL_ACCOUNT_TEXT,
	COL_ACCOUNT_ENABLED, /* Usually tied to connected state */
	COL_ACCOUNT_POINTER,
	COL_ACCOUNT_COUNT
};

static void     account_chooser_finalize               (GObject                         *object);
static void     account_chooser_get_property           (GObject                         *object,
							guint                            param_id,
							GValue                          *value,
							GParamSpec                      *pspec);
static void     account_chooser_set_property           (GObject                         *object,
							guint                            param_id,
							const GValue                    *value,
							GParamSpec                      *pspec);
static void     account_chooser_setup                  (GossipAccountChooser            *chooser);
static void     account_chooser_account_created_cb     (McAccountMonitor                *monitor,
							const gchar                     *unique_name,
							GossipAccountChooser            *chooser);
static void     account_chooser_account_add_foreach    (McAccount                       *account,
							GossipAccountChooser            *chooser);
static void     account_chooser_account_deleted_cb     (McAccountMonitor                *monitor,
							const gchar                     *unique_name,
							GossipAccountChooser            *chooser);
static void     account_chooser_account_remove_foreach (McAccount                       *account,
							GossipAccountChooser            *chooser);
static void     account_chooser_update_iter            (GossipAccountChooser            *chooser,
							GtkTreeIter                     *iter,
							McAccount                       *account);
static void     account_chooser_status_changed_cb      (MissionControl                  *mc,
							TelepathyConnectionStatus        status,
							McPresence                       presence,
							TelepathyConnectionStatusReason  reason,
							const gchar                     *unique_name,
							GossipAccountChooser            *chooser);
static gboolean account_chooser_separator_func         (GtkTreeModel                    *model,
							GtkTreeIter                     *iter,
							GossipAccountChooser            *chooser);
static gboolean account_chooser_set_account_foreach    (GtkTreeModel                    *model,
							GtkTreePath                     *path,
							GtkTreeIter                     *iter,
							SetAccountData                  *data);
static gboolean account_chooser_set_enabled_foreach    (GtkTreeModel                    *model,
							GtkTreePath                     *path,
							GtkTreeIter                     *iter,
							GossipAccountChooser            *chooser);

enum {
	PROP_0,
	PROP_CAN_SELECT_ALL,
	PROP_HAS_ALL_OPTION,
};

G_DEFINE_TYPE (GossipAccountChooser, gossip_account_chooser, GTK_TYPE_COMBO_BOX);

static void
gossip_account_chooser_class_init (GossipAccountChooserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = account_chooser_finalize;
	object_class->get_property = account_chooser_get_property;
	object_class->set_property = account_chooser_set_property;

	g_object_class_install_property (object_class,
					 PROP_CAN_SELECT_ALL,
					 g_param_spec_boolean ("can-select-all",
							       "Can Select All",
							       "Should the user be able to select offline accounts",
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_HAS_ALL_OPTION,
					 g_param_spec_boolean ("has-all-option",
							       "Has All Option",
							       "Have a separate option in the list to mean ALL accounts",
							       FALSE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (GossipAccountChooserPriv));
}

static void
gossip_account_chooser_init (GossipAccountChooser *chooser)
{
}

static void
account_chooser_finalize (GObject *object)
{
	GossipAccountChooser     *chooser;
	GossipAccountChooserPriv *priv;

	chooser = GOSSIP_ACCOUNT_CHOOSER (object);
	priv = GET_PRIV (object);

	g_signal_handlers_disconnect_by_func (priv->monitor,
					      account_chooser_account_created_cb,
					      chooser);
	g_signal_handlers_disconnect_by_func (priv->monitor,
					      account_chooser_account_deleted_cb,
					      chooser);
	dbus_g_proxy_disconnect_signal (DBUS_G_PROXY (priv->mc),
					"AccountStatusChanged",
					G_CALLBACK (account_chooser_status_changed_cb),
					chooser);
	g_object_unref (priv->mc);
	g_object_unref (priv->monitor);

	G_OBJECT_CLASS (gossip_account_chooser_parent_class)->finalize (object);
}

static void
account_chooser_get_property (GObject    *object,
			      guint       param_id,
			      GValue     *value,
			      GParamSpec *pspec)
{
	GossipAccountChooserPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_CAN_SELECT_ALL:
		g_value_set_boolean (value, priv->can_select_all);
		break;
	case PROP_HAS_ALL_OPTION:
		g_value_set_boolean (value, priv->has_all_option);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
account_chooser_set_property (GObject      *object,
			      guint         param_id,
			      const GValue *value,
			      GParamSpec   *pspec)
{
	GossipAccountChooserPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_CAN_SELECT_ALL:
		gossip_account_chooser_set_can_select_all (GOSSIP_ACCOUNT_CHOOSER (object),
							   g_value_get_boolean (value));
		break;
	case PROP_HAS_ALL_OPTION:
		gossip_account_chooser_set_has_all_option (GOSSIP_ACCOUNT_CHOOSER (object),
							   g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

GtkWidget *
gossip_account_chooser_new (void)
{
	GossipAccountChooserPriv *priv;
	McAccountMonitor         *monitor;
	GtkWidget                *chooser;

	monitor = mc_account_monitor_new ();
	chooser = g_object_new (GOSSIP_TYPE_ACCOUNT_CHOOSER, NULL);

	priv = GET_PRIV (chooser);

	priv->mc = gossip_mission_control_new ();
	priv->monitor = mc_account_monitor_new ();

	g_signal_connect (priv->monitor, "account-created",
			  G_CALLBACK (account_chooser_account_created_cb),
			  chooser);
	g_signal_connect (priv->monitor, "account-deleted",
			  G_CALLBACK (account_chooser_account_deleted_cb),
			  chooser);
	dbus_g_proxy_connect_signal (DBUS_G_PROXY (priv->mc), "AccountStatusChanged",
				     G_CALLBACK (account_chooser_status_changed_cb),
				     chooser, NULL);

	account_chooser_setup (GOSSIP_ACCOUNT_CHOOSER (chooser));

	return chooser;
}

McAccount *
gossip_account_chooser_get_account (GossipAccountChooser *chooser)
{
	GossipAccountChooserPriv *priv;
	McAccount                *account;
	GtkTreeModel             *model;
	GtkTreeIter               iter;

	g_return_val_if_fail (GOSSIP_IS_ACCOUNT_CHOOSER (chooser), NULL);

	priv = GET_PRIV (chooser);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (chooser));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (chooser), &iter);

	gtk_tree_model_get (model, &iter, COL_ACCOUNT_POINTER, &account, -1);

	return account;
}

gboolean
gossip_account_chooser_set_account (GossipAccountChooser *chooser,
				    McAccount            *account)
{
	GtkComboBox    *combobox;
	GtkTreeModel   *model;
	GtkTreeIter     iter;
	SetAccountData  data;

	g_return_val_if_fail (GOSSIP_IS_ACCOUNT_CHOOSER (chooser), FALSE);

	combobox = GTK_COMBO_BOX (chooser);
	model = gtk_combo_box_get_model (combobox);
	gtk_combo_box_get_active_iter (combobox, &iter);

	data.chooser = chooser;
	data.account = account;

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) account_chooser_set_account_foreach,
				&data);

	return data.set;
}

gboolean
gossip_account_chooser_get_can_select_all (GossipAccountChooser *chooser)
{
	GossipAccountChooserPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_ACCOUNT_CHOOSER (chooser), FALSE);

	priv = GET_PRIV (chooser);
	
	return priv->can_select_all;
}

void
gossip_account_chooser_set_can_select_all (GossipAccountChooser *chooser,
					   gboolean              can_select_all)
{
	GossipAccountChooserPriv *priv;
	GtkComboBox              *combobox;
	GtkTreeModel             *model;

	g_return_if_fail (GOSSIP_IS_ACCOUNT_CHOOSER (chooser));

	priv = GET_PRIV (chooser);

	if (priv->can_select_all == can_select_all) {
		return;
	}

	combobox = GTK_COMBO_BOX (chooser);
	model = gtk_combo_box_get_model (combobox);

	priv->can_select_all = can_select_all;

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) account_chooser_set_enabled_foreach,
				chooser);

	g_object_notify (G_OBJECT (chooser), "can-select-all");
}

gboolean
gossip_account_chooser_get_has_all_option (GossipAccountChooser *chooser)
{
	GossipAccountChooserPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_ACCOUNT_CHOOSER (chooser), FALSE);

	priv = GET_PRIV (chooser);
	
	return priv->has_all_option;
}

void
gossip_account_chooser_set_has_all_option (GossipAccountChooser *chooser,
					   gboolean              has_all_option)
{
	GossipAccountChooserPriv *priv;
	GtkComboBox              *combobox;
	GtkListStore             *store;
	GtkTreeModel             *model;
	GtkTreeIter               iter;

	g_return_if_fail (GOSSIP_IS_ACCOUNT_CHOOSER (chooser));

	priv = GET_PRIV (chooser);

	if (priv->has_all_option == has_all_option) {
		return;
	}

	combobox = GTK_COMBO_BOX (chooser);
	model = gtk_combo_box_get_model (combobox);
	store = GTK_LIST_STORE (model);

	priv->has_all_option = has_all_option;

	/*
	 * The first 2 options are the ALL and separator
	 */

	if (has_all_option) {
		gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (chooser), 
						      (GtkTreeViewRowSeparatorFunc)
						      account_chooser_separator_func,
						      chooser, 
						      NULL);

		gtk_list_store_prepend (store, &iter);
		gtk_list_store_set (store, &iter, 
				    COL_ACCOUNT_TEXT, NULL,
				    COL_ACCOUNT_ENABLED, TRUE,
				    COL_ACCOUNT_POINTER, NULL,
				    -1);

		gtk_list_store_prepend (store, &iter);
		gtk_list_store_set (store, &iter, 
				    COL_ACCOUNT_TEXT, _("All"), 
				    COL_ACCOUNT_ENABLED, TRUE,
				    COL_ACCOUNT_POINTER, NULL,
				    -1);
	} else {
		if (gtk_tree_model_get_iter_first (model, &iter)) {
			if (gtk_list_store_remove (GTK_LIST_STORE (model), &iter)) {
				gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
			}
		}

		gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (chooser), 
						      (GtkTreeViewRowSeparatorFunc)
						      NULL,
						      NULL, 
						      NULL);
	}

	g_object_notify (G_OBJECT (chooser), "has-all-option");
}

static void
account_chooser_setup (GossipAccountChooser *chooser)
{
	GossipAccountChooserPriv *priv;
	GList                    *accounts;
	GtkListStore             *store;
	GtkCellRenderer          *renderer;
	GtkComboBox              *combobox;

	priv = GET_PRIV (chooser);

	/* Set up combo box with new store */
	combobox = GTK_COMBO_BOX (chooser);

	gtk_cell_layout_clear (GTK_CELL_LAYOUT (combobox));

	store = gtk_list_store_new (COL_ACCOUNT_COUNT,
				    G_TYPE_STRING,
				    G_TYPE_STRING,    /* Name */
				    G_TYPE_BOOLEAN,   /* Enabled */
				    MC_TYPE_ACCOUNT);

	gtk_combo_box_set_model (combobox, GTK_TREE_MODEL (store));

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, FALSE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
					"icon-name", COL_ACCOUNT_IMAGE,
					"sensitive", COL_ACCOUNT_ENABLED,
					NULL);
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
					"text", COL_ACCOUNT_TEXT,
					"sensitive", COL_ACCOUNT_ENABLED,
					NULL);

	/* Populate accounts */
	accounts = mc_accounts_list ();
	g_list_foreach (accounts,
			(GFunc) account_chooser_account_add_foreach,
			chooser);

	mc_accounts_list_free (accounts);
	g_object_unref (store);
}

static void
account_chooser_account_created_cb (McAccountMonitor     *monitor,
				    const gchar          *unique_name,
				    GossipAccountChooser *chooser)
{
	McAccount *account;

	account = mc_account_lookup (unique_name);
	account_chooser_account_add_foreach (account, chooser);
	g_object_unref (account);
}

static void
account_chooser_account_add_foreach (McAccount            *account,
				     GossipAccountChooser *chooser)
{
	GossipAccountChooserPriv *priv;
	GtkListStore             *store;
	GtkComboBox              *combobox;
	GtkTreeIter               iter;

	priv = GET_PRIV (chooser);

	combobox = GTK_COMBO_BOX (chooser);
	store = GTK_LIST_STORE (gtk_combo_box_get_model (combobox));

	gtk_list_store_append (store, &iter);
	account_chooser_update_iter (chooser, &iter, account);
}

static void
account_chooser_account_deleted_cb (McAccountMonitor     *monitor,
				    const gchar          *unique_name,
				    GossipAccountChooser *chooser)
{
	McAccount *account;

	account = mc_account_lookup (unique_name);
	account_chooser_account_remove_foreach (account, chooser);
	g_object_unref (account);
}

static void
account_chooser_account_remove_foreach (McAccount            *account,
					GossipAccountChooser *chooser)
{
	/* Fixme: TODO */
}

static void
account_chooser_update_iter (GossipAccountChooser *chooser,
			     GtkTreeIter          *iter,
			     McAccount            *account)
{
	GossipAccountChooserPriv *priv;
	GtkListStore             *store;
	GtkComboBox              *combobox;
	TpConn                   *tp_conn;
	const gchar              *icon_name;
	gboolean                  is_enabled;

	priv = GET_PRIV (chooser);

	combobox = GTK_COMBO_BOX (chooser);
	store = GTK_LIST_STORE (gtk_combo_box_get_model (combobox));

	icon_name = gossip_icon_name_from_account (account);
	tp_conn = mission_control_get_connection (priv->mc, account, NULL);
	is_enabled = (tp_conn != NULL || priv->can_select_all);

	if (tp_conn) {
		g_object_unref (tp_conn);
	}

	gtk_list_store_set (store, iter,
			    COL_ACCOUNT_IMAGE, icon_name,
			    COL_ACCOUNT_TEXT, mc_account_get_display_name (account),
			    COL_ACCOUNT_ENABLED, is_enabled,
			    COL_ACCOUNT_POINTER, account,
			    -1);

	/* set first connected account as active account */
	if (priv->set_active_item == FALSE && is_enabled) {
		priv->set_active_item = TRUE;
		gtk_combo_box_set_active_iter (combobox, iter);
	}
}

static void
account_chooser_status_changed_cb (MissionControl                  *mc,
				   TelepathyConnectionStatus        status,
				   McPresence                       presence,
				   TelepathyConnectionStatusReason  reason,
				   const gchar                     *unique_name,
				   GossipAccountChooser            *chooser)
{
}

static gboolean
account_chooser_separator_func (GtkTreeModel         *model,
				GtkTreeIter          *iter,
				GossipAccountChooser *chooser)
{
	GossipAccountChooserPriv *priv;
	gchar                    *text;
	gboolean                  is_separator;

	priv = GET_PRIV (chooser);
	
	if (!priv->has_all_option) {
		return FALSE;
	}
	
	gtk_tree_model_get (model, iter, COL_ACCOUNT_TEXT, &text, -1);
	is_separator = text == NULL;
	g_free (text);

	return is_separator;
}

static gboolean
account_chooser_set_account_foreach (GtkTreeModel   *model,
				     GtkTreePath    *path,
				     GtkTreeIter    *iter,
				     SetAccountData *data)
{
	McAccount *account;
	gboolean   equal;

	gtk_tree_model_get (model, iter, COL_ACCOUNT_POINTER, &account, -1);

	/* Special case so we can make it possible to select the All option */
	if (!data->account && !account) {
		equal = TRUE;
	} else {
		equal = gossip_account_equal (data->account, account);
		g_object_unref (account);
	}

	if (equal) {
		GtkComboBox *combobox;

		combobox = GTK_COMBO_BOX (data->chooser);
		gtk_combo_box_set_active_iter (combobox, iter);

		data->set = TRUE;
	}

	return equal;
}

static gboolean
account_chooser_set_enabled_foreach (GtkTreeModel         *model,
				     GtkTreePath          *path,
				     GtkTreeIter          *iter,
				     GossipAccountChooser *chooser)
{
	GossipAccountChooserPriv *priv;
	McAccount                *account;

	priv = GET_PRIV (chooser);

	gtk_tree_model_get (model, iter, COL_ACCOUNT_POINTER, &account, -1);
	if (!account) {
		return FALSE;
	}

	account_chooser_update_iter (chooser, iter, account);
	g_object_unref (account);

	return FALSE;
}

