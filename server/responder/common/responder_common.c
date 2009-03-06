/*
   SSSD

   Common Responder methods

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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include "popt.h"
#include "util/util.h"
#include "db/sysdb.h"
#include "confdb/confdb.h"
#include "dbus/dbus.h"
#include "sbus/sssd_dbus.h"
#include "util/btreemap.h"
#include "responder/common/responder_common.h"
#include "responder/common/responder_packet.h"
#include "responder/common/responder_cmd.h"
#include "responder/common/responder_dp.h"
#include "providers/data_provider.h"
#include "monitor/monitor_sbus.h"
#include "monitor/monitor_interfaces.h"
#include "sbus/sbus_client.h"

static void set_nonblocking(int fd)
{
    unsigned v;
    v = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, v | O_NONBLOCK);
}

static void set_close_on_exec(int fd)
{
    unsigned v;
    v = fcntl(fd, F_GETFD, 0);
    fcntl(fd, F_SETFD, v | FD_CLOEXEC);
}

static int client_destructor(struct cli_ctx *ctx)
{
    if (ctx->cfd > 0) close(ctx->cfd);
    return 0;
}

static void client_send(struct tevent_context *ev, struct cli_ctx *cctx)
{
    int ret;

    ret = sss_packet_send(cctx->creq->out, cctx->cfd);
    if (ret == EAGAIN) {
        /* not all data was sent, loop again */
        return;
    }
    if (ret != EOK) {
        DEBUG(0, ("Failed to read request, aborting client!\n"));
        talloc_free(cctx);
        return;
    }

    /* ok all sent */
    TEVENT_FD_NOT_WRITEABLE(cctx->cfde);
    TEVENT_FD_READABLE(cctx->cfde);
    talloc_free(cctx->creq);
    cctx->creq = NULL;
    return;
}

static void client_recv(struct tevent_context *ev, struct cli_ctx *cctx)
{
    int ret;

    if (!cctx->creq) {
        cctx->creq = talloc_zero(cctx, struct cli_request);
        if (!cctx->creq) {
            DEBUG(0, ("Failed to alloc request, aborting client!\n"));
            talloc_free(cctx);
            return;
        }
    }

    if (!cctx->creq->in) {
        ret = sss_packet_new(cctx->creq, SSS_PACKET_MAX_RECV_SIZE,
                             0, &cctx->creq->in);
        if (ret != EOK) {
            DEBUG(0, ("Failed to alloc request, aborting client!\n"));
            talloc_free(cctx);
            return;
        }
    }

    ret = sss_packet_recv(cctx->creq->in, cctx->cfd);
    switch (ret) {
    case EOK:
        /* do not read anymore */
        TEVENT_FD_NOT_READABLE(cctx->cfde);
        /* execute command */
        ret = sss_cmd_execute(cctx, cctx->nctx->sss_cmds);
        if (ret != EOK) {
            DEBUG(0, ("Failed to execute request, aborting client!\n"));
            talloc_free(cctx);
        }
        /* past this point cctx can be freed at any time by callbacks
         * in case of error, do not use it */
        return;

    case EAGAIN:
        /* need to read still some data, loop again */
        break;

    case EINVAL:
        DEBUG(6, ("Invalid data from client, closing connection!\n"));
        talloc_free(cctx);
        break;

    case ENODATA:
        DEBUG(5, ("Client disconnected!\n"));
        talloc_free(cctx);
        break;

    default:
        DEBUG(6, ("Failed to read request, aborting client!\n"));
        talloc_free(cctx);
    }

    return;
}

static void client_fd_handler(struct tevent_context *ev,
                              struct tevent_fd *fde,
                              uint16_t flags, void *ptr)
{
    struct cli_ctx *cctx = talloc_get_type(ptr, struct cli_ctx);

    if (flags & TEVENT_FD_READ) {
        client_recv(ev, cctx);
        return;
    }
    if (flags & TEVENT_FD_WRITE) {
        client_send(ev, cctx);
        return;
    }
}

/* TODO: this is a copy of accept_fd_handler, maybe both can be put into on
 * handler.  */
static void accept_priv_fd_handler(struct tevent_context *ev,
                              struct tevent_fd *fde,
                              uint16_t flags, void *ptr)
{
    /* accept and attach new event handler */
    struct nss_ctx *nctx = talloc_get_type(ptr, struct nss_ctx);
    struct cli_ctx *cctx;
    socklen_t len;
    struct stat stat_buf;
    int ret;

    ret = stat(nctx->priv_sock_name, &stat_buf);
    if (ret == -1) {
        DEBUG(1, ("stat on privileged pipe failed: [%d][%s].\n", errno,
                  strerror(errno)));
        return;
    }

    if ( ! (stat_buf.st_uid == 0 && stat_buf.st_gid == 0 &&
           (stat_buf.st_mode&(S_IFSOCK|S_IRUSR|S_IWUSR)) == stat_buf.st_mode)) {
        DEBUG(1, ("privileged pipe has an illegal status.\n"));
/* TODO: what is the best response to this condition? Terminate? */
        return;
    }


    cctx = talloc_zero(nctx, struct cli_ctx);
    if (!cctx) {
        struct sockaddr_un addr;
        int fd;
        DEBUG(0, ("Out of memory trying to setup client context on privileged pipe!\n"));
        /* accept and close to signal the client we have a problem */
        memset(&addr, 0, sizeof(addr));
        len = sizeof(addr);
        fd = accept(nctx->priv_lfd, (struct sockaddr *)&addr, &len);
        if (fd == -1) {
            return;
        }
        close(fd);
        return;
    }

    len = sizeof(cctx->addr);
    cctx->cfd = accept(nctx->priv_lfd, (struct sockaddr *)&cctx->addr, &len);
    if (cctx->cfd == -1) {
        DEBUG(1, ("Accept failed [%s]", strerror(errno)));
        talloc_free(cctx);
        return;
    }

    cctx->priv = 1;

    cctx->cfde = tevent_add_fd(ev, cctx, cctx->cfd,
                               TEVENT_FD_READ, client_fd_handler, cctx);
    if (!cctx->cfde) {
        close(cctx->cfd);
        talloc_free(cctx);
        DEBUG(2, ("Failed to queue client handler on privileged pipe\n"));
    }

    cctx->ev = ev;
    cctx->nctx = nctx;

    talloc_set_destructor(cctx, client_destructor);

    DEBUG(4, ("Client connected to privileged pipe!\n"));

    return;
}

static void accept_fd_handler(struct tevent_context *ev,
                              struct tevent_fd *fde,
                              uint16_t flags, void *ptr)
{
    /* accept and attach new event handler */
    struct nss_ctx *nctx = talloc_get_type(ptr, struct nss_ctx);
    struct cli_ctx *cctx;
    socklen_t len;

    cctx = talloc_zero(nctx, struct cli_ctx);
    if (!cctx) {
        struct sockaddr_un addr;
        int fd;
        DEBUG(0, ("Out of memory trying to setup client context!\n"));
        /* accept and close to signal the client we have a problem */
        memset(&addr, 0, sizeof(addr));
        len = sizeof(addr);
        fd = accept(nctx->lfd, (struct sockaddr *)&addr, &len);
        if (fd == -1) {
            return;
        }
        close(fd);
        return;
    }

    len = sizeof(cctx->addr);
    cctx->cfd = accept(nctx->lfd, (struct sockaddr *)&cctx->addr, &len);
    if (cctx->cfd == -1) {
        DEBUG(1, ("Accept failed [%s]", strerror(errno)));
        talloc_free(cctx);
        return;
    }

    cctx->cfde = tevent_add_fd(ev, cctx, cctx->cfd,
                               TEVENT_FD_READ, client_fd_handler, cctx);
    if (!cctx->cfde) {
        close(cctx->cfd);
        talloc_free(cctx);
        DEBUG(2, ("Failed to queue client handler\n"));
    }

    cctx->ev = ev;
    cctx->nctx = nctx;

    talloc_set_destructor(cctx, client_destructor);

    DEBUG(4, ("Client connected!\n"));

    return;
}

static int sss_sbus_init(struct nss_ctx *nctx)
{
    int ret;
    char *sbus_address;
    struct service_sbus_ctx *ss_ctx;
    struct sbus_method_ctx *sm_ctx;

    /* Set up SBUS connection to the monitor */
    ret = monitor_get_sbus_address(nctx, nctx->cdb, &sbus_address);
    if (ret != EOK) {
        DEBUG(0, ("Could not locate monitor address.\n"));
        return ret;
    }

    ret = monitor_init_sbus_methods(nctx, nctx->sss_sbus_methods, &sm_ctx);
    if (ret != EOK) {
        DEBUG(0, ("Could not initialize SBUS methods.\n"));
        return ret;
    }

    ret = sbus_client_init(nctx, nctx->ev,
                           sbus_address, sm_ctx,
                           NULL /* Private Data */,
                           NULL /* Destructor */,
                           &ss_ctx);
    if (ret != EOK) {
        DEBUG(0, ("Failed to connect to monitor services.\n"));
        return ret;
    }

    /* Set up NSS-specific listeners */
    /* None currently used */

    nctx->ss_ctx = ss_ctx;

    return EOK;
}

/* create a unix socket and listen to it */
static int set_unix_socket(struct nss_ctx *nctx)
{
    struct sockaddr_un addr;
    char *default_pipe;
    int ret;

    default_pipe = talloc_asprintf(nctx, "%s/%s", PIPE_PATH,
                                   nctx->sss_pipe_name);
    if (!default_pipe) {
        return ENOMEM;
    }

    ret = confdb_get_string(nctx->cdb, nctx,
                            nctx->confdb_socket_path, "unixSocket",
                            default_pipe, &nctx->sock_name);
    if (ret != EOK) {
        talloc_free(default_pipe);
        return ret;
    }
    talloc_free(default_pipe);

    default_pipe = talloc_asprintf(nctx, "%s/private/%s.priv", PIPE_PATH,
                                   nctx->sss_pipe_name);
    if (!default_pipe) {
        return ENOMEM;
    }

    ret = confdb_get_string(nctx->cdb, nctx,
                            nctx->confdb_socket_path, "privUnixSocket",
                            default_pipe, &nctx->priv_sock_name);
    if (ret != EOK) {
        talloc_free(default_pipe);
        return ret;
    }
    talloc_free(default_pipe);

    nctx->lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (nctx->lfd == -1) {
        return EIO;
    }

    nctx->priv_lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (nctx->priv_lfd == -1) {
        close(nctx->lfd);
        return EIO;
    }

    /* Set the umask so that permissions are set right on the socket.
     * It must be readable and writable by anybody on the system. */
    umask(0111);

    set_nonblocking(nctx->lfd);
    set_close_on_exec(nctx->lfd);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, nctx->sock_name, sizeof(addr.sun_path));

    /* make sure we have no old sockets around */
    unlink(nctx->sock_name);

    if (bind(nctx->lfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        DEBUG(0,("Unable to bind on socket '%s'\n", nctx->sock_name));
        goto failed;
    }
    if (listen(nctx->lfd, 10) != 0) {
        DEBUG(0,("Unable to listen on socket '%s'\n", nctx->sock_name));
        goto failed;
    }

    /* create privileged pipe */
    umask(0177);

    set_nonblocking(nctx->priv_lfd);
    set_close_on_exec(nctx->priv_lfd);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, nctx->priv_sock_name, sizeof(addr.sun_path));

    unlink(nctx->priv_sock_name);

    if (bind(nctx->priv_lfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        DEBUG(0,("Unable to bind on socket '%s'\n", nctx->priv_sock_name));
        goto failed;
    }
    if (listen(nctx->priv_lfd, 10) != 0) {
        DEBUG(0,("Unable to listen on socket '%s'\n", nctx->priv_sock_name));
        goto failed;
    }

    nctx->lfde = tevent_add_fd(nctx->ev, nctx, nctx->lfd,
                               TEVENT_FD_READ, accept_fd_handler, nctx);
    if (!nctx->lfde) {
        DEBUG(0, ("Failed to queue handler on pipe\n"));
        goto failed;
    }

    nctx->priv_lfde = tevent_add_fd(nctx->ev, nctx, nctx->priv_lfd,
                               TEVENT_FD_READ, accept_priv_fd_handler, nctx);
    if (!nctx->priv_lfde) {
        DEBUG(0, ("Failed to queue handler on privileged pipe\n"));
        goto failed;
    }

    /* we want default permissions on created files to be very strict,
       so set our umask to 0177 */
    umask(0177);
    return EOK;

failed:
    /* we want default permissions on created files to be very strict,
       so set our umask to 0177 */
    umask(0177);
    close(nctx->lfd);
    close(nctx->priv_lfd);
    return EIO;
}

static int sss_init_domains(struct nss_ctx *nctx)
{
    TALLOC_CTX *tmp_ctx;
    int ret;
    int retval;

    tmp_ctx = talloc_new(nctx);
    ret = confdb_get_domains(nctx->cdb, nctx, &nctx->domain_map);
    if (ret != EOK) {
        retval = ret;
        goto done;
    }

    if (nctx->domain_map == NULL) {
        /* No domains configured!
         * Note: this should never happen, since LOCAL should
         * always be configured */
        DEBUG(0, ("No domains configured on this client!\n"));
        retval = EINVAL;
        goto done;
    }

    ret = confdb_get_string(nctx->cdb, nctx,
                            "config/domains", "default",
                            NULL, &nctx->default_domain);
    if (ret != EOK) {
        retval = ret;
        goto done;
    }

    retval = EOK;

done:
    talloc_free(tmp_ctx);
    return retval;
}

int sss_process_init(TALLOC_CTX *mem_ctx,
                     struct tevent_context *ev,
                     struct confdb_ctx *cdb,
                     struct sbus_method sss_sbus_methods[],
                     struct sss_cmd_table sss_cmds[],
                     const char *sss_pipe_name,
                     const char *confdb_socket_path,
                     struct sbus_method dp_methods[])
{
    struct nss_ctx *nctx;
    int ret;

    nctx = talloc_zero(mem_ctx, struct nss_ctx);
    if (!nctx) {
        DEBUG(0, ("fatal error initializing nss_ctx\n"));
        return ENOMEM;
    }
    nctx->ev = ev;
    nctx->cdb = cdb;
    nctx->sss_sbus_methods = sss_sbus_methods;
    nctx->sss_cmds = sss_cmds;
    nctx->sss_pipe_name = sss_pipe_name;
    nctx->confdb_socket_path = confdb_socket_path;
    nctx->dp_methods = dp_methods;

    ret = sss_init_domains(nctx);
    if (ret != EOK) {
        DEBUG(0, ("fatal error setting up domain map\n"));
        return ret;
    }

    ret = sss_sbus_init(nctx);
    if (ret != EOK) {
        DEBUG(0, ("fatal error setting up message bus\n"));
        return ret;
    }

    ret = sss_dp_init(nctx, nctx->dp_methods);
    if (ret != EOK) {
        DEBUG(0, ("fatal error setting up backend connector\n"));
        return ret;
    }

    ret = sysdb_init(nctx, ev, cdb, NULL, &nctx->sysdb);
    if (ret != EOK) {
        DEBUG(0, ("fatal error initializing nss_ctx\n"));
        return ret;
    }

    /* after all initializations we are ready to listen on our socket */
    ret = set_unix_socket(nctx);
    if (ret != EOK) {
        DEBUG(0, ("fatal error initializing socket\n"));
        return ret;
    }

    nctx->cache_timeout = 600; /* FIXME: read from conf */

    DEBUG(1, ("NSS Initialization complete\n"));

    return EOK;
}

int sss_parse_name(TALLOC_CTX *memctx,
                          const char *fullname,
                          struct btreemap *domain_map,
                          const char **domain, const char **name) {
    char *delim;
    struct btreemap *node;
    int ret;

    if ((delim = strchr(fullname, SSS_DOMAIN_DELIM)) != NULL) {

        /* Check for registered domain */
        ret = btreemap_search_key(domain_map, (void *)(delim+1), &node);
        if (ret != BTREEMAP_FOUND) {
            /* No such domain was registered. Return EINVAL.
             * TODO: alternative approach?
             * Alternatively, we could simply fail down to
             * below, treating the entire construct as the
             * full name if the domain is unspecified.
             */
            return EINVAL;
        }

        *name = talloc_strndup(memctx, fullname, delim-fullname);
        *domain = talloc_strdup(memctx, delim+1);
    }
    else {
        *name = talloc_strdup(memctx, fullname);
        *domain = NULL;
    }

    return EOK;
}