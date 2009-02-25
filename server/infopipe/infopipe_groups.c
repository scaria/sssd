/*
   SSSD

   InfoPipe

   Copyright (C) Stephen Gallagher <sgallagh@redhat.com>	2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <dbus/dbus.h>
#include "util/util.h"
#include "infopipe.h"

int infp_groups_create(DBusMessage *message, struct sbus_conn_ctx *sconn)
{
    DBusMessage *reply;

    reply = dbus_message_new_error(message, DBUS_ERROR_NOT_SUPPORTED, "Not yet implemented");

    /* send reply */
    sbus_conn_send_reply(sconn, reply);

    dbus_message_unref(reply);
    return EOK;
}

int infp_groups_delete(DBusMessage *message, struct sbus_conn_ctx *sconn)
{
    DBusMessage *reply;

    reply = dbus_message_new_error(message, DBUS_ERROR_NOT_SUPPORTED, "Not yet implemented");

    /* send reply */
    sbus_conn_send_reply(sconn, reply);

    dbus_message_unref(reply);
    return EOK;
}

int infp_groups_add_members(DBusMessage *message, struct sbus_conn_ctx *sconn)
{
    DBusMessage *reply;

    reply = dbus_message_new_error(message, DBUS_ERROR_NOT_SUPPORTED, "Not yet implemented");

    /* send reply */
    sbus_conn_send_reply(sconn, reply);

    dbus_message_unref(reply);
    return EOK;
}

int infp_groups_remove_members(DBusMessage *message, struct sbus_conn_ctx *sconn)
{
    DBusMessage *reply;

    reply = dbus_message_new_error(message, DBUS_ERROR_NOT_SUPPORTED, "Not yet implemented");

    /* send reply */
    sbus_conn_send_reply(sconn, reply);

    dbus_message_unref(reply);
    return EOK;
}

int infp_groups_set_gid(DBusMessage *message, struct sbus_conn_ctx *sconn)
{
    DBusMessage *reply;

    reply = dbus_message_new_error(message, DBUS_ERROR_NOT_SUPPORTED, "Not yet implemented");

    /* send reply */
    sbus_conn_send_reply(sconn, reply);

    dbus_message_unref(reply);
    return EOK;
}