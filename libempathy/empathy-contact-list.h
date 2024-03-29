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

#ifndef __EMPATHY_CONTACT_LIST_H__
#define __EMPATHY_CONTACT_LIST_H__

#include <glib-object.h>

#include "empathy-types.h"
#include "empathy-contact.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_CONTACT_LIST         (empathy_contact_list_get_type ())
#define EMPATHY_CONTACT_LIST(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_CONTACT_LIST, EmpathyContactList))
#define EMPATHY_IS_CONTACT_LIST(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_CONTACT_LIST))
#define EMPATHY_CONTACT_LIST_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), EMPATHY_TYPE_CONTACT_LIST, EmpathyContactListIface))

typedef enum {
	EMPATHY_CONTACT_LIST_CAN_ADD		= 1 << 0,
	EMPATHY_CONTACT_LIST_CAN_REMOVE		= 1 << 1,
	EMPATHY_CONTACT_LIST_CAN_ALIAS		= 1 << 2,
	EMPATHY_CONTACT_LIST_CAN_GROUP		= 1 << 3,
	EMPATHY_CONTACT_LIST_CAN_BLOCK		= 1 << 4,
	EMPATHY_CONTACT_LIST_CAN_REPORT_ABUSIVE = 1 << 5,
	EMPATHY_CONTACT_LIST_MESSAGE_ADD	= 1 << 6,
} EmpathyContactListFlags;

typedef struct _EmpathyContactListIface EmpathyContactListIface;

struct _EmpathyContactListIface {
	GTypeInterface   base_iface;

	/* VTabled */
	void             (*add)               (EmpathyContactList *list,
					       EmpathyContact     *contact,
					       const gchar        *message);
	void             (*remove)            (EmpathyContactList *list,
					       EmpathyContact     *contact,
					       const gchar        *message);
	GList *          (*get_members)       (EmpathyContactList *list);
	GList *          (*get_pendings)      (EmpathyContactList *list);
	GList *          (*get_all_groups)    (EmpathyContactList *list);
	GList *          (*get_groups)        (EmpathyContactList *list,
					       EmpathyContact     *contact);
	void             (*add_to_group)      (EmpathyContactList *list,
					       EmpathyContact     *contact,
					       const gchar        *group);
	void             (*remove_from_group) (EmpathyContactList *list,
					       EmpathyContact     *contact,
					       const gchar        *group);
	void             (*rename_group)      (EmpathyContactList *list,
					       const gchar        *old_group,
					       const gchar        *new_group);
	void		 (*remove_group)      (EmpathyContactList *list,
					       const gchar	  *group);
	EmpathyContactListFlags
			 (*get_flags)         (EmpathyContactList *list);
	gboolean         (*is_favourite)      (EmpathyContactList *list,
					       EmpathyContact     *contact);
	void             (*add_favourite)     (EmpathyContactList *list,
					       EmpathyContact     *contact);
	void             (*remove_favourite)  (EmpathyContactList *list,
					       EmpathyContact     *contact);
	void             (*set_blocked)       (EmpathyContactList *list,
			                       EmpathyContact     *contact,
					       gboolean            blocked,
					       gboolean            abusive);
	gboolean         (*get_blocked)       (EmpathyContactList *list,
			                       EmpathyContact     *contact);
};

GType    empathy_contact_list_get_type          (void) G_GNUC_CONST;
void     empathy_contact_list_add               (EmpathyContactList *list,
						 EmpathyContact     *contact,
						 const gchar        *message);
void     empathy_contact_list_remove            (EmpathyContactList *list,
						 EmpathyContact     *contact,
						 const gchar        *message);
GList *  empathy_contact_list_get_members       (EmpathyContactList *list);
GList *  empathy_contact_list_get_pendings      (EmpathyContactList *list);
GList *  empathy_contact_list_get_all_groups    (EmpathyContactList *list);
GList *  empathy_contact_list_get_groups        (EmpathyContactList *list,
						 EmpathyContact     *contact);
void     empathy_contact_list_add_to_group      (EmpathyContactList *list,
						 EmpathyContact     *contact,
						 const gchar        *group);
void     empathy_contact_list_remove_from_group (EmpathyContactList *list,
						 EmpathyContact     *contact,
						 const gchar        *group);
void     empathy_contact_list_rename_group      (EmpathyContactList *list,
						 const gchar        *old_group,
						 const gchar        *new_group);
void	 empathy_contact_list_remove_group	(EmpathyContactList *list,
						 const gchar	    *group);

EmpathyContactListFlags
         empathy_contact_list_get_flags		(EmpathyContactList *list);

gboolean empathy_contact_list_is_favourite      (EmpathyContactList *list,
						 EmpathyContact     *contact);

void     empathy_contact_list_add_to_favourites (EmpathyContactList *list,
						 EmpathyContact     *contact);

void     empathy_contact_list_remove_from_favourites
                                                (EmpathyContactList *list,
						 EmpathyContact     *contact);

void     empathy_contact_list_set_blocked       (EmpathyContactList *list,
		                                 EmpathyContact     *contact,
						 gboolean            blocked,
						 gboolean            abusive);
gboolean empathy_contact_list_get_blocked       (EmpathyContactList *list,
						 EmpathyContact     *contact);


G_END_DECLS

#endif /* __EMPATHY_CONTACT_LIST_H__ */

