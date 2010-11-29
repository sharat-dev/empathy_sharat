/* * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include <config.h>
#include <string.h>

#include <glib/gi18n.h>
#include <libnotify/notification.h>
#include <libnotify/notify.h>
#include <telepathy-glib/telepathy-glib.h>

#include <libempathy/empathy-contact-manager.h>

#include <libempathy-gtk/empathy-notify-manager.h>

#include "empathy-event-manager.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include "empathy-notifications-approver.h"

struct _EmpathyNotificationsApproverPrivate
{
  EmpathyEventManager *event_mgr;
  EmpathyNotifyManager *notify_mgr;

  NotifyNotification *notification;
  EmpathyEvent *event;
};

G_DEFINE_TYPE (EmpathyNotificationsApprover, empathy_notifications_approver,
    G_TYPE_OBJECT);

static EmpathyNotificationsApprover *notifications_approver = NULL;

static GObject *
notifications_approver_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (notifications_approver != NULL)
    return g_object_ref (notifications_approver);

  retval = G_OBJECT_CLASS (empathy_notifications_approver_parent_class)->
      constructor (type, n_construct_params, construct_params);

  notifications_approver = EMPATHY_NOTIFICATIONS_APPROVER (retval);
  g_object_add_weak_pointer (retval, (gpointer) &notifications_approver);

  return retval;
}

static void
notifications_approver_dispose (GObject *object)
{
  EmpathyNotificationsApprover *self = (EmpathyNotificationsApprover *) object;

  tp_clear_object (&self->priv->event_mgr);
  tp_clear_object (&self->priv->notify_mgr);

  if (self->priv->notification != NULL)
    {
      notify_notification_close (self->priv->notification, NULL);
      tp_clear_object (&self->priv->notification);
    }

  G_OBJECT_CLASS (empathy_notifications_approver_parent_class)->dispose (
      object);
}

static void
empathy_notifications_approver_class_init (
    EmpathyNotificationsApproverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = notifications_approver_dispose;
  object_class->constructor = notifications_approver_constructor;

  g_type_class_add_private (object_class,
      sizeof (EmpathyNotificationsApproverPrivate));
}

static void
notification_closed_cb (NotifyNotification *notification,
    EmpathyNotificationsApprover *self)
{
  if (self->priv->notification == notification)
    tp_clear_object (&self->priv->notification);
}

static void
notification_close_helper (EmpathyNotificationsApprover *self)
{
  if (self->priv->notification != NULL)
    {
      notify_notification_close (self->priv->notification, NULL);
      tp_clear_object (&self->priv->notification);
    }
}

static void
notification_approve_cb (NotifyNotification *notification,
    gchar *action,
    EmpathyNotificationsApprover *self)
{
  if (self->priv->event != NULL)
    empathy_event_approve (self->priv->event);
}

static void
notification_decline_cb (NotifyNotification *notification,
    gchar *action,
    EmpathyNotificationsApprover  *self)
{
  if (self->priv->event != NULL)
    empathy_event_decline (self->priv->event);
}

static void
notification_decline_subscription_cb (NotifyNotification *notification,
    gchar *action,
    EmpathyNotificationsApprover *self)
{
  EmpathyContactManager *manager;

  if (self->priv->event == NULL)
    return;

  manager = empathy_contact_manager_dup_singleton ();
  empathy_contact_list_remove (EMPATHY_CONTACT_LIST (manager),
      self->priv->event->contact, "");

  empathy_event_remove (self->priv->event);

  g_object_unref (manager);
}

static void
notification_accept_subscription_cb (NotifyNotification *notification,
    gchar *action,
    EmpathyNotificationsApprover *self)
{
  EmpathyContactManager *manager;

  if (self->priv->event == NULL)
    return;

  manager = empathy_contact_manager_dup_singleton ();
  empathy_contact_list_add (EMPATHY_CONTACT_LIST (manager),
      self->priv->event->contact, "");

  empathy_event_remove (self->priv->event);

  g_object_unref (manager);
}

static void
add_notification_actions (EmpathyNotificationsApprover *self,
    NotifyNotification *notification)
{
  switch (self->priv->event->type) {
    case EMPATHY_EVENT_TYPE_CHAT:
      notify_notification_add_action (notification,
        "respond", _("Respond"), (NotifyActionCallback) notification_approve_cb,
          self, NULL);
      break;

    case EMPATHY_EVENT_TYPE_VOIP:
      notify_notification_add_action (notification,
        "reject", _("Reject"), (NotifyActionCallback) notification_decline_cb,
          self, NULL);

      notify_notification_add_action (notification,
        "answer", _("Answer"), (NotifyActionCallback) notification_approve_cb,
          self, NULL);
      break;

    case EMPATHY_EVENT_TYPE_TRANSFER:
    case EMPATHY_EVENT_TYPE_INVITATION:
      notify_notification_add_action (notification,
        "decline", _("Decline"), (NotifyActionCallback) notification_decline_cb,
          self, NULL);

      notify_notification_add_action (notification,
        "accept", _("Accept"), (NotifyActionCallback) notification_approve_cb,
          self, NULL);
      break;

    case EMPATHY_EVENT_TYPE_SUBSCRIPTION:
      notify_notification_add_action (notification,
        "decline", _("Decline"),
          (NotifyActionCallback) notification_decline_subscription_cb,
          self, NULL);

      notify_notification_add_action (notification,
        "accept", _("Accept"),
          (NotifyActionCallback) notification_accept_subscription_cb,
          self, NULL);

    default:
      break;
  }
}

static void
update_notification (EmpathyNotificationsApprover *self)
{
  GdkPixbuf *pixbuf = NULL;
  gchar *message_esc = NULL;
  gboolean has_x_canonical_append;
  NotifyNotification *notification;

  if (!empathy_notify_manager_notification_is_enabled (self->priv->notify_mgr))
    {
      /* always close the notification if this happens */
      notification_close_helper (self);
      return;
    }

  if (self->priv->event == NULL)
    {
      notification_close_helper (self);
      return;
     }

  if (self->priv->event->message != NULL)
    message_esc = g_markup_escape_text (self->priv->event->message, -1);

  has_x_canonical_append = empathy_notify_manager_has_capability (
      self->priv->notify_mgr, EMPATHY_NOTIFY_MANAGER_CAP_X_CANONICAL_APPEND);

  if (self->priv->notification != NULL && ! has_x_canonical_append)
    {
      /* if the notification server does NOT supports x-canonical-append, it is
       * better to not use notify_notification_update to avoid
       * overwriting the current notification message */
      notification = g_object_ref (self->priv->notification);

      notify_notification_update (notification,
          self->priv->event->header, message_esc, NULL);
    }
  else
    {
      /* if the notification server supports x-canonical-append,
       * the hint will be added, so that the message from the
       * just created notification will be automatically appended
       * to an existing notification with the same title.
       * In this way the previous message will not be lost: the new
       * message will appear below it, in the same notification */
      notification = notify_notification_new (self->priv->event->header,
           message_esc, NULL);

      if (self->priv->notification == NULL)
        {
          self->priv->notification = g_object_ref (notification);

          g_signal_connect (notification, "closed",
              G_CALLBACK (notification_closed_cb), self);
        }

      notify_notification_set_timeout (notification,
          NOTIFY_EXPIRES_DEFAULT);

      if (has_x_canonical_append)
        notify_notification_set_hint_string (notification,
            EMPATHY_NOTIFY_MANAGER_CAP_X_CANONICAL_APPEND, "");

      if (empathy_notify_manager_has_capability (self->priv->notify_mgr,
            EMPATHY_NOTIFY_MANAGER_CAP_ACTIONS))
        add_notification_actions (self, notification);
    }

  pixbuf = empathy_notify_manager_get_pixbuf_for_notification (
      self->priv->notify_mgr, self->priv->event->contact,
      self->priv->event->icon_name);

  if (pixbuf != NULL)
    {
      notify_notification_set_icon_from_pixbuf (notification, pixbuf);
      g_object_unref (pixbuf);
    }

  notify_notification_show (notification, NULL);

  g_free (message_esc);
  g_object_unref (notification);
}

static void
event_added_cb (EmpathyEventManager *manager,
    EmpathyEvent *event,
    EmpathyNotificationsApprover *self)
{
  if (self->priv->event != NULL)
    return;

  self->priv->event = event;
  update_notification (self);
}

static void
event_removed_cb (EmpathyEventManager *manager,
    EmpathyEvent *event,
    EmpathyNotificationsApprover *self)
{
  if (event != self->priv->event)
    return;

  self->priv->event = empathy_event_manager_get_top_event (
      self->priv->event_mgr);

  update_notification (self);
}

static void
event_updated_cb (EmpathyEventManager *manager,
    EmpathyEvent *event,
    EmpathyNotificationsApprover *self)
{
  if (event != self->priv->event)
    return;

  if (empathy_notify_manager_notification_is_enabled (self->priv->notify_mgr))
    update_notification (self);
}

static void
empathy_notifications_approver_init (EmpathyNotificationsApprover *self)
{
  EmpathyNotificationsApproverPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_NOTIFICATIONS_APPROVER, EmpathyNotificationsApproverPrivate);

  self->priv = priv;

  self->priv->event_mgr = empathy_event_manager_dup_singleton ();
  self->priv->notify_mgr = empathy_notify_manager_dup_singleton ();

  tp_g_signal_connect_object (self->priv->event_mgr, "event-added",
      G_CALLBACK (event_added_cb), self, 0);
  tp_g_signal_connect_object (priv->event_mgr, "event-removed",
      G_CALLBACK (event_removed_cb), self, 0);
  tp_g_signal_connect_object (priv->event_mgr, "event-updated",
      G_CALLBACK (event_updated_cb), self, 0);
}

EmpathyNotificationsApprover *
empathy_notifications_approver_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_NOTIFICATIONS_APPROVER, NULL);
}