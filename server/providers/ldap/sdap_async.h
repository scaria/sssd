/*
    SSSD

    Async LDAP Helper routines

    Copyright (C) Simo Sorce <ssorce@redhat.com>

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

#include <talloc.h>
#include <tevent.h>
#include "providers/dp_backend.h"
#include "providers/ldap/sdap.h"

struct tevent_req *sdap_connect_send(TALLOC_CTX *memctx,
                                     struct tevent_context *ev,
                                     struct sdap_options *opts,
                                     bool use_start_tls);
int sdap_connect_recv(struct tevent_req *req,
                      TALLOC_CTX *memctx,
                      struct sdap_handle **sh);

struct tevent_req *sdap_get_users_send(TALLOC_CTX *memctx,
                                       struct tevent_context *ev,
                                       struct sss_domain_info *dom,
                                       struct sysdb_ctx *sysdb,
                                       struct sdap_options *opts,
                                       struct sdap_handle *sh,
                                       const char **attrs,
                                       const char *wildcard);
int sdap_get_users_recv(struct tevent_req *req,
                        TALLOC_CTX *mem_ctx, char **timestamp);

struct tevent_req *sdap_get_groups_send(TALLOC_CTX *memctx,
                                       struct tevent_context *ev,
                                       struct sss_domain_info *dom,
                                       struct sysdb_ctx *sysdb,
                                       struct sdap_options *opts,
                                       struct sdap_handle *sh,
                                       const char **attrs,
                                       const char *wildcard);
int sdap_get_groups_recv(struct tevent_req *req,
                         TALLOC_CTX *mem_ctx, char **timestamp);

struct tevent_req *sdap_kinit_send(TALLOC_CTX *memctx,
                                   struct tevent_context *ev,
                                   struct sdap_handle *sh,
                                   const char *keytab,
                                   const char *principal,
                                   const char *realm);
int sdap_kinit_recv(struct tevent_req *req, enum sdap_result *result);

struct tevent_req *sdap_auth_send(TALLOC_CTX *memctx,
                                  struct tevent_context *ev,
                                  struct sdap_handle *sh,
                                  const char *sasl_mech,
                                  const char *sasl_user,
                                  const char *user_dn,
                                  const char *authtok_type,
                                  struct dp_opt_blob authtok);
int sdap_auth_recv(struct tevent_req *req, enum sdap_result *result);

struct tevent_req *sdap_get_initgr_send(TALLOC_CTX *memctx,
                                        struct tevent_context *ev,
                                        struct sss_domain_info *dom,
                                        struct sysdb_ctx *sysdb,
                                        struct sdap_options *opts,
                                        struct sdap_handle *sh,
                                        const char *name,
                                        const char **grp_attrs);
int sdap_get_initgr_recv(struct tevent_req *req);

struct tevent_req *sdap_exop_modify_passwd_send(TALLOC_CTX *memctx,
                                                struct tevent_context *ev,
                                                struct sdap_handle *sh,
                                                char *user_dn,
                                                char *password,
                                                char *new_password);
int sdap_exop_modify_passwd_recv(struct tevent_req *req, enum sdap_result *result);

struct tevent_req *sdap_cli_connect_send(TALLOC_CTX *memctx,
                                         struct tevent_context *ev,
                                         struct sdap_options *opts,
                                         struct sysdb_attrs **rootdse);
int sdap_cli_connect_recv(struct tevent_req *req,
                          TALLOC_CTX *memctx,
                          struct sdap_handle **gsh,
                          struct sysdb_attrs **rootdse);

struct tevent_req *sdap_get_generic_send(TALLOC_CTX *memctx,
                                         struct tevent_context *ev,
                                         struct sdap_options *opts,
                                         struct sdap_handle *sh,
                                         const char *search_base,
                                         int scope,
                                         const char *filter,
                                         const char **attrs);
int sdap_get_generic_recv(struct tevent_req *req,
                         TALLOC_CTX *mem_ctx, size_t *reply_count,
                         struct sysdb_attrs ***reply_list);
