/*
    SSSD

    Async LDAP Helper routines - retrieving users

    Copyright (C) Simo Sorce <ssorce@redhat.com> - 2009
    Copyright (C) 2010, Ralf Haferkamp <rhafer@suse.de>, Novell Inc.
    Copyright (C) Jan Zeleny <jzeleny@redhat.com> - 2011

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

#include "util/util.h"
#include "db/sysdb.h"
#include "providers/ldap/sdap_async_private.h"
#include "providers/ldap/ldap_common.h"

/* ==Save-User-Entry====================================================== */

/* FIXME: support storing additional attributes */

int sdap_save_user(TALLOC_CTX *memctx,
                   struct sysdb_ctx *ctx,
                   struct sdap_options *opts,
                   struct sss_domain_info *dom,
                   struct sysdb_attrs *attrs,
                   const char **ldap_attrs,
                   bool is_initgr,
                   char **_usn_value)
{
    struct ldb_message_element *el;
    int ret;
    const char *name = NULL;
    const char *pwd;
    const char *gecos;
    const char *homedir;
    const char *shell;
    uid_t uid;
    gid_t gid;
    struct sysdb_attrs *user_attrs;
    char *upn = NULL;
    size_t i;
    char *val = NULL;
    int cache_timeout;
    char *usn_value = NULL;
    size_t c;
    char **missing = NULL;
    TALLOC_CTX *tmpctx = NULL;

    DEBUG(9, ("Save user\n"));

    tmpctx = talloc_new(memctx);
    if (!tmpctx) {
        ret = ENOMEM;
        goto fail;
    }

    user_attrs = sysdb_new_attrs(tmpctx);
    if (user_attrs == NULL) {
        ret = ENOMEM;
        goto fail;
    }

    ret = sysdb_attrs_primary_name(ctx, attrs,
                                   opts->user_map[SDAP_AT_USER_NAME].name,
                                   &name);
    if (ret != EOK) {
        DEBUG(1, ("Failed to save the user - entry has no name attribute\n"));
        goto fail;
    }

    ret = sysdb_attrs_get_el(attrs,
                             opts->user_map[SDAP_AT_USER_PWD].sys_name, &el);
    if (ret) goto fail;
    if (el->num_values == 0) pwd = NULL;
    else pwd = (const char *)el->values[0].data;

    ret = sysdb_attrs_get_el(attrs,
                             opts->user_map[SDAP_AT_USER_GECOS].sys_name, &el);
    if (ret) goto fail;
    if (el->num_values == 0) gecos = NULL;
    else gecos = (const char *)el->values[0].data;

    if (!gecos) {
        /* Fall back to the user's full name */
        ret = sysdb_attrs_get_el(
                attrs,
                opts->user_map[SDAP_AT_USER_FULLNAME].sys_name, &el);
        if (ret) goto fail;
        if (el->num_values > 0) gecos = (const char *)el->values[0].data;
    }

    ret = sysdb_attrs_get_el(attrs,
                             opts->user_map[SDAP_AT_USER_HOME].sys_name, &el);
    if (ret) goto fail;
    if (el->num_values == 0) homedir = NULL;
    else homedir = (const char *)el->values[0].data;

    ret = sysdb_attrs_get_el(attrs,
                             opts->user_map[SDAP_AT_USER_SHELL].sys_name, &el);
    if (ret) goto fail;
    if (el->num_values == 0) shell = NULL;
    else shell = (const char *)el->values[0].data;

    ret = sysdb_attrs_get_uint32_t(attrs,
                                   opts->user_map[SDAP_AT_USER_UID].sys_name,
                                   &uid);
    if (ret != EOK) {
        DEBUG(1, ("no uid provided for [%s] in domain [%s].\n",
                  name, dom->name));
        ret = EINVAL;
        goto fail;
    }

    /* check that the uid is valid for this domain */
    if (OUT_OF_ID_RANGE(uid, dom->id_min, dom->id_max)) {
            DEBUG(2, ("User [%s] filtered out! (id out of range)\n",
                      name));
        ret = EINVAL;
        goto fail;
    }

    ret = sysdb_attrs_get_uint32_t(attrs,
                                   opts->user_map[SDAP_AT_USER_GID].sys_name,
                                   &gid);
    if (ret != EOK) {
        DEBUG(1, ("no gid provided for [%s] in domain [%s].\n",
                  name, dom->name));
        ret = EINVAL;
        goto fail;
    }

    /* check that the gid is valid for this domain */
    if (OUT_OF_ID_RANGE(gid, dom->id_min, dom->id_max)) {
            DEBUG(2, ("User [%s] filtered out! (id out of range)\n",
                      name));
        ret = EINVAL;
        goto fail;
    }

    ret = sysdb_attrs_get_el(attrs, SYSDB_ORIG_DN, &el);
    if (ret) {
        goto fail;
    }
    if (el->num_values == 0) {
        DEBUG(7, ("Original DN is not available for [%s].\n", name));
    } else {
        DEBUG(7, ("Adding original DN [%s] to attributes of [%s].\n",
                  el->values[0].data, name));
        ret = sysdb_attrs_add_string(user_attrs, SYSDB_ORIG_DN,
                                     (const char *) el->values[0].data);
        if (ret) {
            goto fail;
        }
    }

    ret = sysdb_attrs_get_el(attrs, SYSDB_MEMBEROF, &el);
    if (ret) {
        goto fail;
    }
    if (el->num_values == 0) {
        DEBUG(7, ("Original memberOf is not available for [%s].\n",
                  name));
    } else {
        DEBUG(7, ("Adding original memberOf attributes to [%s].\n",
                  name));
        for (i = 0; i < el->num_values; i++) {
            ret = sysdb_attrs_add_string(user_attrs, SYSDB_ORIG_MEMBEROF,
                                         (const char *) el->values[i].data);
            if (ret) {
                goto fail;
            }
        }
    }

    ret = sysdb_attrs_get_el(attrs,
                      opts->user_map[SDAP_AT_USER_MODSTAMP].sys_name, &el);
    if (ret) {
        goto fail;
    }
    if (el->num_values == 0) {
        DEBUG(7, ("Original mod-Timestamp is not available for [%s].\n",
                  name));
    } else {
        ret = sysdb_attrs_add_string(user_attrs,
                          opts->user_map[SDAP_AT_USER_MODSTAMP].sys_name,
                          (const char*)el->values[0].data);
        if (ret) {
            goto fail;
        }
    }

    ret = sysdb_attrs_get_el(attrs,
                      opts->user_map[SDAP_AT_USER_USN].sys_name, &el);
    if (ret) {
        goto fail;
    }
    if (el->num_values == 0) {
        DEBUG(7, ("Original USN value is not available for [%s].\n",
                  name));
    } else {
        ret = sysdb_attrs_add_string(user_attrs,
                          opts->user_map[SDAP_AT_USER_USN].sys_name,
                          (const char*)el->values[0].data);
        if (ret) {
            goto fail;
        }
        usn_value = talloc_strdup(memctx, (const char*)el->values[0].data);
        if (!usn_value) {
            ret = ENOMEM;
            goto fail;
        }
    }

    ret = sysdb_attrs_get_el(attrs,
                             opts->user_map[SDAP_AT_USER_PRINC].sys_name, &el);
    if (ret) {
        goto fail;
    }
    if (el->num_values == 0) {
        DEBUG(7, ("User principal is not available for [%s].\n", name));
    } else {
        upn = talloc_strdup(user_attrs, (const char*) el->values[0].data);
        if (!upn) {
            ret = ENOMEM;
            goto fail;
        }
        if (dp_opt_get_bool(opts->basic, SDAP_FORCE_UPPER_CASE_REALM)) {
            make_realm_upper_case(upn);
        }
        DEBUG(7, ("Adding user principal [%s] to attributes of [%s].\n",
                  upn, name));
        ret = sysdb_attrs_add_string(user_attrs, SYSDB_UPN, upn);
        if (ret) {
            goto fail;
        }
    }

    for (i = SDAP_FIRST_EXTRA_USER_AT; i < SDAP_OPTS_USER; i++) {
        ret = sysdb_attrs_get_el(attrs, opts->user_map[i].sys_name, &el);
        if (ret) {
            goto fail;
        }
        if (el->num_values > 0) {
            for (c = 0; c < el->num_values; c++) {
                DEBUG(9, ("Adding [%s]=[%s] to user attributes.\n",
                          opts->user_map[i].sys_name,
                          (const char*) el->values[c].data));
                val = talloc_strdup(user_attrs, (const char*) el->values[c].data);
                if (val == NULL) {
                    ret = ENOMEM;
                    goto fail;
                }
                ret = sysdb_attrs_add_string(user_attrs,
                                             opts->user_map[i].sys_name, val);
                if (ret) {
                    goto fail;
                }
            }
        }
    }

    cache_timeout = dp_opt_get_int(opts->basic, SDAP_ENTRY_CACHE_TIMEOUT);

    if (is_initgr) {
        ret = sysdb_attrs_add_time_t(user_attrs, SYSDB_INITGR_EXPIRE,
                                     (cache_timeout ?
                                      (time(NULL) + cache_timeout) : 0));
        if (ret) {
            goto fail;
        }
    }

    /* Make sure that any attributes we requested from LDAP that we
     * did not receive are also removed from the sysdb
     */
    ret = list_missing_attrs(user_attrs, opts->user_map, SDAP_OPTS_USER,
                             ldap_attrs, attrs, &missing);
    if (ret != EOK) {
        goto fail;
    }

    /* Remove missing attributes */
    if (missing && !missing[0]) {
        /* Nothing to remove */
        talloc_zfree(missing);
    }

    DEBUG(6, ("Storing info for user %s\n", name));

    ret = sysdb_store_user(ctx, name, pwd, uid, gid, gecos, homedir, shell,
                           user_attrs, missing, cache_timeout);
    if (ret) goto fail;

    if (_usn_value) {
        *_usn_value = usn_value;
    }

    talloc_steal(memctx, user_attrs);
    talloc_free(tmpctx);
    return EOK;

fail:
    DEBUG(2, ("Failed to save user [%s]\n",
              name ? name : "Unknown"));
    talloc_free(tmpctx);
    return ret;
}


/* ==Generic-Function-to-save-multiple-users============================= */

int sdap_save_users(TALLOC_CTX *memctx,
                    struct sysdb_ctx *sysdb,
                    const char **attrs,
                    struct sss_domain_info *dom,
                    struct sdap_options *opts,
                    struct sysdb_attrs **users,
                    int num_users,
                    char **_usn_value)
{
    TALLOC_CTX *tmpctx;
    char *higher_usn = NULL;
    char *usn_value;
    int ret;
    int i;

    if (num_users == 0) {
        /* Nothing to do if there are no users */
        return EOK;
    }

    tmpctx = talloc_new(memctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    ret = sysdb_transaction_start(sysdb);
    if (ret) {
        goto done;
    }

    for (i = 0; i < num_users; i++) {
        usn_value = NULL;

        ret = sdap_save_user(tmpctx, sysdb, opts, dom,
                             users[i], attrs, false,
                             &usn_value);

        /* Do not fail completely on errors.
         * Just report the failure to save and go on */
        if (ret) {
            DEBUG(2, ("Failed to store user %d. Ignoring.\n", i));
        } else {
            DEBUG(9, ("User %d processed!\n", i));
        }

        if (usn_value) {
            if (higher_usn) {
                if ((strlen(usn_value) > strlen(higher_usn)) ||
                    (strcmp(usn_value, higher_usn) > 0)) {
                    talloc_zfree(higher_usn);
                    higher_usn = usn_value;
                } else {
                    talloc_zfree(usn_value);
                }
            } else {
                higher_usn = usn_value;
            }
        }
    }

    ret = sysdb_transaction_commit(sysdb);
    if (ret) {
        DEBUG(1, ("Failed to commit transaction!\n"));
        goto done;
    }

    if (_usn_value) {
        *_usn_value = talloc_steal(memctx, higher_usn);
    }

done:
    talloc_zfree(tmpctx);
    return ret;
}


/* ==Search-Users-with-filter============================================= */

struct sdap_get_users_state {
    struct tevent_context *ev;
    struct sdap_options *opts;
    struct sdap_handle *sh;
    struct sss_domain_info *dom;
    struct sysdb_ctx *sysdb;
    const char **attrs;
    const char *filter;

    char *higher_usn;
    struct sysdb_attrs **users;
    size_t count;
};

static void sdap_get_users_process(struct tevent_req *subreq);

struct tevent_req *sdap_get_users_send(TALLOC_CTX *memctx,
                                       struct tevent_context *ev,
                                       struct sss_domain_info *dom,
                                       struct sysdb_ctx *sysdb,
                                       struct sdap_options *opts,
                                       struct sdap_handle *sh,
                                       const char **attrs,
                                       const char *filter,
                                       int timeout)
{
    struct tevent_req *req, *subreq;
    struct sdap_get_users_state *state;

    req = tevent_req_create(memctx, &state, struct sdap_get_users_state);
    if (!req) return NULL;

    state->ev = ev;
    state->opts = opts;
    state->dom = dom;
    state->sh = sh;
    state->sysdb = sysdb;
    state->filter = filter;
    state->attrs = attrs;
    state->higher_usn = NULL;
    state->users =  NULL;
    state->count = 0;

    subreq = sdap_get_generic_send(state, state->ev, state->opts, state->sh,
                                   dp_opt_get_string(state->opts->basic,
                                                     SDAP_USER_SEARCH_BASE),
                                   LDAP_SCOPE_SUBTREE,
                                   state->filter, state->attrs,
                                   state->opts->user_map, SDAP_OPTS_USER,
                                   timeout);
    if (!subreq) {
        talloc_zfree(req);
        return NULL;
    }
    tevent_req_set_callback(subreq, sdap_get_users_process, req);

    return req;
}

static void sdap_get_users_process(struct tevent_req *subreq)
{
    struct tevent_req *req = tevent_req_callback_data(subreq,
                                                      struct tevent_req);
    struct sdap_get_users_state *state = tevent_req_data(req,
                                            struct sdap_get_users_state);
    int ret;

    ret = sdap_get_generic_recv(subreq, state,
                                &state->count, &state->users);
    talloc_zfree(subreq);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    DEBUG(6, ("Search for users, returned %d results.\n", state->count));

    if (state->count == 0) {
        tevent_req_error(req, ENOENT);
        return;
    }

    ret = sdap_save_users(state, state->sysdb,
                          state->attrs,
                          state->dom, state->opts,
                          state->users, state->count,
                          &state->higher_usn);
    if (ret) {
        DEBUG(2, ("Failed to store users.\n"));
        tevent_req_error(req, ret);
        return;
    }

    DEBUG(9, ("Saving %d Users - Done\n", state->count));

    tevent_req_done(req);
}

int sdap_get_users_recv(struct tevent_req *req,
                        TALLOC_CTX *mem_ctx, char **usn_value)
{
    struct sdap_get_users_state *state = tevent_req_data(req,
                                            struct sdap_get_users_state);

    TEVENT_REQ_RETURN_ON_ERROR(req);

    if (usn_value) {
        *usn_value = talloc_steal(mem_ctx, state->higher_usn);
    }

    return EOK;
}
