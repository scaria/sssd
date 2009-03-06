/*
   SSSD

   NSS Responder - Data Provider Interfaces

   Copyright (C) Simo Sorce <ssorce@redhat.com>	2008

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

#include <sys/time.h>
#include <time.h>

#include <talloc.h>
#include <security/pam_modules.h>

#include "util/util.h"
#include "responder/common/responder_packet.h"
#include "providers/data_provider.h"
#include "sbus/sbus_client.h"
#include "providers/dp_sbus.h"
#include "responder/pam/pamsrv.h"

struct pam_reply_ctx {
    struct cli_ctx *cctx;
    pam_dp_callback_t callback;
};

static void pam_process_dp_reply(DBusPendingCall *pending, void *ptr) {
    DBusError dbus_error;
    DBusMessage* msg;
    int ret;
    int type;
    dbus_uint32_t pam_status;
    char *domain;
    struct pam_reply_ctx *rctx;

    rctx = talloc_get_type(ptr, struct pam_reply_ctx);

    dbus_error_init(&dbus_error);

    dbus_pending_call_block(pending);
    msg = dbus_pending_call_steal_reply(pending);
    if (msg == NULL) {
        DEBUG(0, ("Severe error. A reply callback was called but no reply was received and no timeout occurred\n"));
        pam_status = PAM_SYSTEM_ERR;
        goto done;
    }


    type = dbus_message_get_type(msg);
    switch (type) {
        case DBUS_MESSAGE_TYPE_METHOD_RETURN:
            ret = dbus_message_get_args(msg, &dbus_error,
                                        DBUS_TYPE_UINT32, &pam_status,
                                        DBUS_TYPE_STRING, &domain,
                                        DBUS_TYPE_INVALID);
            if (!ret) {
                DEBUG(0, ("Failed to parse reply.\n"));
                pam_status = PAM_SYSTEM_ERR;
                domain = "";
                goto done;
            }
            DEBUG(4, ("received: [%d][%s]\n", pam_status, domain));
            break;
        case DBUS_MESSAGE_TYPE_ERROR:
            DEBUG(0, ("Reply error.\n"));
            pam_status = PAM_SYSTEM_ERR;
            break;
        default:
            DEBUG(0, ("Default... what now?.\n"));
            pam_status = PAM_SYSTEM_ERR;
    }


done:
    dbus_pending_call_unref(pending);
    dbus_message_unref(msg);
    rctx->callback(rctx->cctx, pam_status, domain);
}

int pam_dp_send_req(struct cli_ctx *cctx,
                         pam_dp_callback_t callback,
                         int timeout, struct pam_data *pd) {
    DBusMessage *msg;
    DBusPendingCall *pending_reply;
    DBusConnection *conn;
    DBusError dbus_error;
    dbus_bool_t ret;
    struct pam_reply_ctx *rctx;

    rctx = talloc(cctx, struct pam_reply_ctx);
    if (rctx == NULL) {
        DEBUG(0,("Out of memory?!\n"));
        return ENOMEM;
    }
    rctx->cctx=cctx;
    rctx->callback=callback;

    if (pd->domain==NULL ||
        pd->user==NULL ||
        pd->service==NULL ||
        pd->tty==NULL ||
        pd->ruser==NULL ||
        pd->rhost==NULL ) {
        return EINVAL;
    }

    conn = sbus_get_connection(cctx->nctx->dp_ctx->scon_ctx);
    dbus_error_init(&dbus_error);

    msg = dbus_message_new_method_call(NULL,
                                       DP_CLI_PATH,
                                       DP_CLI_INTERFACE,
                                       DP_SRV_METHOD_PAMHANDLER);
    if (msg == NULL) {
        DEBUG(0,("Out of memory?!\n"));
        return ENOMEM;
    }


    DEBUG(4, ("Sending request with the following data:\n"));
    DEBUG_PAM_DATA(4, pd);

    ret = dbus_message_append_args(msg,
                                   DBUS_TYPE_INT32, &(pd->cmd),
                                   DBUS_TYPE_STRING, &(pd->domain),
                                   DBUS_TYPE_STRING, &(pd->user),
                                   DBUS_TYPE_STRING, &(pd->service),
                                   DBUS_TYPE_STRING, &(pd->tty),
                                   DBUS_TYPE_STRING, &(pd->ruser),
                                   DBUS_TYPE_STRING, &(pd->rhost),
                                   DBUS_TYPE_INT32, &(pd->authtok_type),
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                                       &(pd->authtok), pd->authtok_size,
                                   DBUS_TYPE_INT32, &(pd->newauthtok_type),
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                                       &(pd->newauthtok), pd->newauthtok_size,
                                   DBUS_TYPE_INVALID);
    if (!ret) {
        DEBUG(1,("Failed to build message\n"));
        return EIO;
    }

    ret = dbus_connection_send_with_reply(conn, msg, &pending_reply, timeout);
    if (!ret) {
        /*
         * Critical Failure
         * We can't communicate on this connection
         * We'll drop it using the default destructor.
         */
        DEBUG(0, ("D-BUS send failed.\n"));
        dbus_message_unref(msg);
        return EIO;
    }

    dbus_pending_call_set_notify(pending_reply, pam_process_dp_reply, rctx,
                                 NULL);
    dbus_message_unref(msg);

    return EOK;
}

static int pam_dp_identity(DBusMessage *message, struct sbus_conn_ctx *sconn)
{
    dbus_uint16_t version = DATA_PROVIDER_VERSION;
    dbus_uint16_t clitype = DP_CLI_FRONTEND;
    const char *cliname = "PAM";
    const char *nullname = "";
    DBusMessage *reply;
    dbus_bool_t ret;

    DEBUG(4,("Sending ID reply: (%d,%d,%s)\n", clitype, version, cliname));

    reply = dbus_message_new_method_return(message);
    if (!reply) return ENOMEM;

    ret = dbus_message_append_args(reply,
                                   DBUS_TYPE_UINT16, &clitype,
                                   DBUS_TYPE_UINT16, &version,
                                   DBUS_TYPE_STRING, &cliname,
                                   DBUS_TYPE_STRING, &nullname,
                                   DBUS_TYPE_INVALID);
    if (!ret) {
        dbus_message_unref(reply);
        return EIO;
    }

    /* send reply back */
    sbus_conn_send_reply(sconn, reply);
    dbus_message_unref(reply);

    return EOK;
}

struct sbus_method *register_pam_dp_methods(void) {
    static struct sbus_method pam_dp_methods[] = {
            { DP_CLI_METHOD_IDENTITY, pam_dp_identity },
            { NULL, NULL }
    };

    return pam_dp_methods;
}