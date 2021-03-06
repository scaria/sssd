/*
    SSSD

    Async LDAP Helper routines - initgroups operation

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

/* ==Save-fake-group-list=====================================*/
static errno_t sdap_add_incomplete_groups(struct sysdb_ctx *sysdb,
                                          struct sdap_options *opts,
                                          struct sss_domain_info *dom,
                                          char **groupnames,
                                          struct sysdb_attrs **ldap_groups,
                                          int ldap_groups_count)
{
    TALLOC_CTX *tmp_ctx;
    struct ldb_message *msg;
    int i, mi, ai;
    const char *name;
    const char *original_dn;
    char **missing;
    gid_t gid;
    int ret;
    bool in_transaction = false;
    bool posix;

    /* There are no groups in LDAP but we should add user to groups ?? */
    if (ldap_groups_count == 0) return EOK;

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) return ENOMEM;

    missing = talloc_array(tmp_ctx, char *, ldap_groups_count+1);
    if (!missing) {
        ret = ENOMEM;
        goto fail;
    }
    mi = 0;

    ret = sysdb_transaction_start(sysdb);
    if (ret != EOK) {
        DEBUG(1, ("Cannot start sysdb transaction [%d]: %s\n",
                   ret, strerror(ret)));
        goto fail;
    }
    in_transaction = true;

    for (i=0; groupnames[i]; i++) {
        ret = sysdb_search_group_by_name(tmp_ctx, sysdb, groupnames[i], NULL, &msg);
        if (ret == EOK) {
            continue;
        } else if (ret == ENOENT) {
            DEBUG(7, ("Group #%d [%s] is not cached, need to add a fake entry\n",
                       i, groupnames[i]));
            missing[mi] = groupnames[i];
            mi++;
            continue;
        } else if (ret != ENOENT) {
            DEBUG(1, ("search for group failed [%d]: %s\n",
                      ret, strerror(ret)));
            goto fail;
        }
    }
    missing[mi] = NULL;

    /* All groups are cached, nothing to do */
    if (mi == 0) {
        talloc_zfree(tmp_ctx);
        goto done;
    }

    for (i=0; missing[i]; i++) {
        /* The group is not in sysdb, need to add a fake entry */
        for (ai=0; ai < ldap_groups_count; ai++) {
            ret = sysdb_attrs_primary_name(sysdb, ldap_groups[ai],
                                           opts->group_map[SDAP_AT_GROUP_NAME].name,
                                           &name);
            if (ret != EOK) {
                DEBUG(1, ("The group has no name attribute\n"));
                goto fail;
            }

            if (strcmp(name, missing[i]) == 0) {
                posix = true;
                ret = sysdb_attrs_get_uint32_t(ldap_groups[ai],
                                               SYSDB_GIDNUM,
                                               &gid);
                if (ret == ENOENT || (ret == EOK && gid == 0)) {
                    DEBUG(9, ("The group %s gid was %s\n",
                              name, ret == ENOENT ? "missing" : "zero"));
                    DEBUG(8, ("Marking group %s as non-posix and setting GID=0!\n", name));
                    gid = 0;
                    posix = false;
                } else if (ret) {
                    DEBUG(1, ("The GID attribute is malformed\n"));
                    goto fail;
                }

                ret = sysdb_attrs_get_string(ldap_groups[ai],
                                             SYSDB_ORIG_DN,
                                             &original_dn);
                if (ret) {
                    DEBUG(5, ("The group has no name original DN\n"));
                    original_dn = NULL;
                }

                DEBUG(8, ("Adding fake group %s to sysdb\n", name));
                ret = sysdb_add_incomplete_group(sysdb, name, gid, original_dn,
                                                 posix);
                if (ret != EOK) {
                    goto fail;
                }
                break;
            }
        }

        if (ai == ldap_groups_count) {
            DEBUG(2, ("Group %s not present in LDAP\n", missing[i]));
            ret = EINVAL;
            goto fail;
        }
    }

done:
    ret = sysdb_transaction_commit(sysdb);
    if (ret != EOK) {
        DEBUG(1, ("sysdb_transaction_commit failed.\n"));
        goto fail;
    }
    in_transaction = false;
    ret = EOK;
fail:
    if (in_transaction) {
        sysdb_transaction_cancel(sysdb);
    }
    return ret;
}

static int sdap_initgr_common_store(struct sysdb_ctx *sysdb,
                                    struct sdap_options *opts,
                                    struct sss_domain_info *dom,
                                    const char *name,
                                    enum sysdb_member_type type,
                                    char **sysdb_grouplist,
                                    struct sysdb_attrs **ldap_groups,
                                    int ldap_groups_count,
                                    bool add_fake)
{
    TALLOC_CTX *tmp_ctx;
    char **ldap_grouplist = NULL;
    char **add_groups;
    char **del_groups;
    int ret;

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) return ENOMEM;

    if (ldap_groups_count == 0) {
        /* No groups for this user in LDAP.
         * We need to ensure that there are no groups
         * in the sysdb either.
         */
        ldap_grouplist = NULL;
    } else {
        ret = sysdb_attrs_primary_name_list(
                sysdb, tmp_ctx,
                ldap_groups, ldap_groups_count,
                opts->group_map[SDAP_AT_GROUP_NAME].name,
                &ldap_grouplist);
        if (ret != EOK) {
            DEBUG(1, ("sysdb_attrs_primary_name_list failed [%d]: %s\n",
                      ret, strerror(ret)));
            goto done;
        }
    }

    /* Find the differences between the sysdb and LDAP lists
     * Groups in the sysdb only must be removed.
     */
    ret = diff_string_lists(tmp_ctx, ldap_grouplist, sysdb_grouplist,
                            &add_groups, &del_groups, NULL);
    if (ret != EOK) goto done;

    /* Add fake entries for any groups the user should be added as
     * member of but that are not cached in sysdb
     */
    if (add_fake && add_groups && add_groups[0]) {
        ret = sdap_add_incomplete_groups(sysdb, opts, dom,
                                         add_groups, ldap_groups,
                                         ldap_groups_count);
        if (ret != EOK) {
            DEBUG(1, ("Adding incomplete users failed\n"));
            goto done;
        }
    }

    DEBUG(8, ("Updating memberships for %s\n", name));
    ret = sysdb_update_members(sysdb, name, type,
                               (const char *const *) add_groups,
                               (const char *const *) del_groups);
    if (ret != EOK) {
        DEBUG(1, ("Membership update failed [%d]: %s\n",
                  ret, strerror(ret)));
        goto done;
    }

    ret = EOK;
done:
    talloc_zfree(tmp_ctx);
    return ret;
}

/* ==Initgr-call-(groups-a-user-is-member-of)-RFC2307===================== */

struct sdap_initgr_rfc2307_state {
    struct tevent_context *ev;
    struct sysdb_ctx *sysdb;
    struct sdap_options *opts;
    struct sss_domain_info *dom;
    struct sdap_handle *sh;
    const char *name;

    struct sdap_op *op;

    struct sysdb_attrs **ldap_groups;
    size_t ldap_groups_count;
};

static void sdap_initgr_rfc2307_process(struct tevent_req *subreq);
struct tevent_req *sdap_initgr_rfc2307_send(TALLOC_CTX *memctx,
                                            struct tevent_context *ev,
                                            struct sdap_options *opts,
                                            struct sysdb_ctx *sysdb,
                                            struct sss_domain_info *dom,
                                            struct sdap_handle *sh,
                                            const char *base_dn,
                                            const char *name)
{
    struct tevent_req *req, *subreq;
    struct sdap_initgr_rfc2307_state *state;
    const char *filter;
    const char **attrs;
    char *clean_name;
    errno_t ret;

    req = tevent_req_create(memctx, &state, struct sdap_initgr_rfc2307_state);
    if (!req) return NULL;

    state->ev = ev;
    state->opts = opts;
    state->sysdb = sysdb;
    state->dom = dom;
    state->sh = sh;
    state->op = NULL;
    state->name = talloc_strdup(state, name);
    if (!state->name) {
        talloc_zfree(req);
        return NULL;
    }

    ret = build_attrs_from_map(state, opts->group_map,
                               SDAP_OPTS_GROUP, &attrs);
    if (ret != EOK) {
        talloc_free(req);
        return NULL;
    }

    ret = sss_filter_sanitize(state, name, &clean_name);
    if (ret != EOK) {
        talloc_free(req);
        return NULL;
    }

    filter = talloc_asprintf(state,
                             "(&(%s=%s)(objectclass=%s)(%s=*)(&(%s=*)(!(%s=0))))",
                             opts->group_map[SDAP_AT_GROUP_MEMBER].name,
                             clean_name,
                             opts->group_map[SDAP_OC_GROUP].name,
                             opts->group_map[SDAP_AT_GROUP_NAME].name,
                             opts->group_map[SDAP_AT_GROUP_GID].name,
                             opts->group_map[SDAP_AT_GROUP_GID].name);
    if (!filter) {
        talloc_zfree(req);
        return NULL;
    }
    talloc_zfree(clean_name);

    subreq = sdap_get_generic_send(state, state->ev, state->opts,
                                   state->sh, base_dn, LDAP_SCOPE_SUBTREE,
                                   filter, attrs,
                                   state->opts->group_map, SDAP_OPTS_GROUP,
                                   dp_opt_get_int(state->opts->basic,
                                                  SDAP_SEARCH_TIMEOUT));
    if (!subreq) {
        talloc_zfree(req);
        return NULL;
    }
    tevent_req_set_callback(subreq, sdap_initgr_rfc2307_process, req);

    return req;
}

static void sdap_initgr_rfc2307_process(struct tevent_req *subreq)
{
    struct tevent_req *req;
    struct sdap_initgr_rfc2307_state *state;
    struct sysdb_attrs **ldap_groups;
    char **sysdb_grouplist = NULL;
    struct ldb_message *msg;
    struct ldb_message_element *groups;
    size_t count;
    const char *attrs[2];
    int ret;
    int i;

    req = tevent_req_callback_data(subreq, struct tevent_req);
    state = tevent_req_data(req, struct sdap_initgr_rfc2307_state);

    ret = sdap_get_generic_recv(subreq, state, &count, &ldap_groups);
    talloc_zfree(subreq);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    /* Search for all groups for which this user is a member */
    attrs[0] = SYSDB_MEMBEROF;
    attrs[1] = NULL;
    ret = sysdb_search_user_by_name(state, state->sysdb, state->name, attrs,
                                    &msg);
    if (ret != EOK) {
        tevent_req_error(req, ret);
        return;
    }

    groups = ldb_msg_find_element(msg, SYSDB_MEMBEROF);
    if (!groups || groups->num_values == 0) {
        /* No groups for this user in sysdb currently */
        sysdb_grouplist = NULL;
    } else {
        sysdb_grouplist = talloc_array(state, char *, groups->num_values+1);
        if (!sysdb_grouplist) {
            tevent_req_error(req, ENOMEM);
            return;
        }

        /* Get a list of the groups by groupname only */
        for (i=0; i < groups->num_values; i++) {
            ret = sysdb_group_dn_name(state->sysdb,
                                      sysdb_grouplist,
                                      (const char *)groups->values[i].data,
                                      &sysdb_grouplist[i]);
            if (ret != EOK) {
                tevent_req_error(req, ret);
                return;
            }
        }
        sysdb_grouplist[groups->num_values] = NULL;
    }

    /* There are no nested groups here so we can just update the
     * memberships */
    ret = sdap_initgr_common_store(state->sysdb, state->opts,
                                   state->dom, state->name,
                                   SYSDB_MEMBER_USER, sysdb_grouplist,
                                   ldap_groups, count, true);
    if (ret != EOK) {
        tevent_req_error(req, ret);
        return;
    }

    tevent_req_done(req);
}

static int sdap_initgr_rfc2307_recv(struct tevent_req *req)
{
    TEVENT_REQ_RETURN_ON_ERROR(req);

    return EOK;
}


/* ==Initgr-call-(groups-a-user-is-member-of)-nested-groups=============== */

struct sdap_initgr_nested_state {
    struct tevent_context *ev;
    struct sysdb_ctx *sysdb;
    struct sdap_options *opts;
    struct sss_domain_info *dom;
    struct sdap_handle *sh;

    struct sysdb_attrs *user;
    const char *username;

    const char **grp_attrs;

    char *filter;
    char **group_dns;
    int count;
    int cur;

    struct sdap_op *op;

    struct sysdb_attrs **groups;
    int groups_cur;
};

static void sdap_initgr_nested_search(struct tevent_req *subreq);
static void sdap_initgr_nested_store(struct tevent_req *req);
static struct tevent_req *sdap_initgr_nested_send(TALLOC_CTX *memctx,
                                                  struct tevent_context *ev,
                                                  struct sdap_options *opts,
                                                  struct sysdb_ctx *sysdb,
                                                  struct sss_domain_info *dom,
                                                  struct sdap_handle *sh,
                                                  struct sysdb_attrs *user,
                                                  const char **grp_attrs)
{
    struct tevent_req *req, *subreq;
    struct sdap_initgr_nested_state *state;
    struct ldb_message_element *el;
    int i;
    errno_t ret;

    req = tevent_req_create(memctx, &state, struct sdap_initgr_nested_state);
    if (!req) return NULL;

    state->ev = ev;
    state->opts = opts;
    state->sysdb = sysdb;
    state->dom = dom;
    state->sh = sh;
    state->grp_attrs = grp_attrs;
    state->user = user;
    state->op = NULL;

    ret = sysdb_attrs_primary_name(sysdb, user,
                                   opts->user_map[SDAP_AT_USER_NAME].name,
                                   &state->username);
    if (ret != EOK) {
        DEBUG(1, ("User entry had no username\n"));
        talloc_free(req);
        return NULL;
    }

    state->filter = talloc_asprintf(state, "(&(objectclass=%s)(%s=*))",
                                    opts->group_map[SDAP_OC_GROUP].name,
                                    opts->group_map[SDAP_AT_GROUP_NAME].name);
    if (!state->filter) {
        talloc_zfree(req);
        return NULL;
    }

    /* TODO: test rootDSE for deref support and use it if available */
    /* TODO: or test rootDSE for ASQ support and use it if available */

    ret = sysdb_attrs_get_el(user, SYSDB_MEMBEROF, &el);
    if (ret || !el || el->num_values == 0) {
        DEBUG(4, ("User entry lacks original memberof ?\n"));
        /* We can't find any groups for this user, so we'll
         * have to assume there aren't any. Just return
         * success here.
         */
        tevent_req_done(req);
        tevent_req_post(req, ev);
        return req;
    }
    state->count = el->num_values;

    state->groups = talloc_zero_array(state, struct sysdb_attrs *,
                                      state->count + 1);;
    if (!state->groups) {
        talloc_zfree(req);
        return NULL;
    }
    state->groups_cur = 0;

    state->group_dns = talloc_array(state, char *, state->count + 1);
    if (!state->group_dns) {
        talloc_zfree(req);
        return NULL;
    }
    for (i = 0; i < state->count; i++) {
        state->group_dns[i] = talloc_strdup(state->group_dns,
                                            (char *)el->values[i].data);
        if (!state->group_dns[i]) {
            talloc_zfree(req);
            return NULL;
        }
    }
    state->group_dns[i] = NULL; /* terminate */
    state->cur = 0;

    subreq = sdap_get_generic_send(state, state->ev, state->opts, state->sh,
                                   state->group_dns[state->cur],
                                   LDAP_SCOPE_BASE,
                                   state->filter, state->grp_attrs,
                                   state->opts->group_map, SDAP_OPTS_GROUP,
                                   dp_opt_get_int(state->opts->basic,
                                                  SDAP_SEARCH_TIMEOUT));
    if (!subreq) {
        talloc_zfree(req);
        return NULL;
    }
    tevent_req_set_callback(subreq, sdap_initgr_nested_search, req);

    return req;
}

static void sdap_initgr_nested_search(struct tevent_req *subreq)
{
    struct tevent_req *req;
    struct sdap_initgr_nested_state *state;
    struct sysdb_attrs **groups;
    size_t count;
    int ret;

    req = tevent_req_callback_data(subreq, struct tevent_req);
    state = tevent_req_data(req, struct sdap_initgr_nested_state);

    ret = sdap_get_generic_recv(subreq, state, &count, &groups);
    talloc_zfree(subreq);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    if (count == 1) {
        state->groups[state->groups_cur] = groups[0];
        state->groups_cur++;
    } else {
        DEBUG(2, ("Search for group %s, returned %d results. Skipping\n",
                  state->group_dns[state->cur], count));
    }

    state->cur++;
    if (state->cur < state->count) {
        subreq = sdap_get_generic_send(state, state->ev,
                                       state->opts, state->sh,
                                       state->group_dns[state->cur],
                                       LDAP_SCOPE_BASE,
                                       state->filter, state->grp_attrs,
                                       state->opts->group_map,
                                       SDAP_OPTS_GROUP,
                                       dp_opt_get_int(state->opts->basic,
                                                      SDAP_SEARCH_TIMEOUT));
        if (!subreq) {
            tevent_req_error(req, ENOMEM);
            return;
        }
        tevent_req_set_callback(subreq, sdap_initgr_nested_search, req);
    } else {
        sdap_initgr_nested_store(req);
    }
}

static int sdap_initgr_nested_store_group(struct sysdb_ctx *sysdb,
                                          struct sdap_options *opts,
                                          struct sss_domain_info *dom,
                                          struct sysdb_attrs *group,
                                          struct sysdb_attrs **groups,
                                          int ngroups);

static void sdap_initgr_nested_store(struct tevent_req *req)
{
    struct sdap_initgr_nested_state *state;

    struct ldb_message_element *el;
    errno_t ret;
    int i, mi;
    struct ldb_message **direct_sysdb_groups = NULL;
    size_t direct_sysdb_count = 0;

    const char *orig_dn;
    const char *user_dn;
    struct ldb_dn *basedn;
    static const char *group_attrs[] = { SYSDB_NAME, NULL };
    const char *member_filter;
    char **sysdb_grouplist = NULL;
    char **ldap_grouplist = NULL;
    const char *tmp_str;

    int ndirect;
    struct sysdb_attrs **direct_groups;

    state = tevent_req_data(req, struct sdap_initgr_nested_state);

    /* Get direct LDAP parents */
    ret = sysdb_attrs_get_string(state->user, SYSDB_ORIG_DN, &orig_dn);
    if (ret != EOK) {
        DEBUG(2, ("The user has no original DN\n"));
        goto done;
    }

    direct_groups = talloc_zero_array(state, struct sysdb_attrs *,
                                      state->count + 1);
    if (!direct_groups) {
        ret = ENOMEM;
        goto done;
    }
    ndirect = 0;

    for (i=0; i < state->groups_cur ; i++) {
        ret = sysdb_attrs_get_el(state->groups[i], SYSDB_MEMBER, &el);
        if (ret) {
            DEBUG(3, ("A group with no members during initgroups?\n"));
            goto done;
        }

        for (mi = 0; mi < el->num_values; mi++) {
            if (strcasecmp((const char *) el->values[mi].data, orig_dn) != 0) {
                continue;
            }

            direct_groups[ndirect] = state->groups[i];
            ndirect++;
        }
    }

    DEBUG(7, ("The user %s is a direct member of %d LDAP groups\n",
              state->username, ndirect));

    /* Get direct sysdb parents */
    user_dn = sysdb_user_strdn(state, state->dom->name, state->username);
    if (!user_dn) {
        ret = ENOMEM;
        goto done;
    }

    member_filter = talloc_asprintf(state, "(&(%s=%s)(%s=%s))",
                                    SYSDB_OBJECTCLASS, SYSDB_GROUP_CLASS,
                                    SYSDB_MEMBER, user_dn);
    if (!member_filter) {
        ret = ENOMEM;
        goto done;
    }

    basedn = ldb_dn_new_fmt(state, sysdb_ctx_get_ldb(state->sysdb),
                            SYSDB_TMPL_GROUP_BASE,
                            state->dom->name);
    if (!basedn) {
        ret = ENOMEM;
        goto done;
    }

    DEBUG(8, ("searching sysdb with filter [%s]\n", member_filter));

    ret = sysdb_search_entry(state, state->sysdb, basedn,
                             LDB_SCOPE_SUBTREE, member_filter, group_attrs,
                             &direct_sysdb_count, &direct_sysdb_groups);
    if (ret == EOK) {
        /* Get the list of sysdb groups by name */
        sysdb_grouplist = talloc_array(state, char *, direct_sysdb_count+1);
        if (!sysdb_grouplist) {
            ret = ENOMEM;
            goto done;
        }

        for(i = 0; i < direct_sysdb_count; i++) {
            tmp_str = ldb_msg_find_attr_as_string(direct_sysdb_groups[i],
                                                SYSDB_NAME, NULL);
            if (!tmp_str) {
                /* This should never happen, but if it does, just continue */
                continue;
            }

            sysdb_grouplist[i] = talloc_strdup(sysdb_grouplist, tmp_str);
            if (!sysdb_grouplist[i]) {
                DEBUG(1, ("A group with no name?\n"));
                ret = EIO;
                goto done;
            }
        }
        sysdb_grouplist[i] = NULL;
    } else if (ret == ENOENT) {
        direct_sysdb_groups = NULL;
        direct_sysdb_count = 0;
    } else {
        DEBUG(2, ("sysdb_search_entry failed: [%d]: %s\n", ret, strerror(ret)));
        goto done;
    }
    DEBUG(7, ("The user %s is a member of %d sysdb groups\n",
              state->username, direct_sysdb_count));

    /* Store the direct parents with full member/memberof pairs */
    ret = sdap_initgr_common_store(state->sysdb, state->opts,
                                   state->dom,
                                   state->username,
                                   SYSDB_MEMBER_USER,
                                   sysdb_grouplist,
                                   direct_groups,
                                   ndirect, true);
    if (ret != EOK) {
        DEBUG(1, ("sdap_initgr_common_store failed [%d]: %s\n",
                  ret, strerror(ret)));
        goto done;
    }

    /* Not all indirect groups may be cached.
     * Add fake entries for those that are not */
    ret = sysdb_attrs_primary_name_list(
            state->sysdb, state,
            state->groups, state->groups_cur,
            state->opts->group_map[SDAP_AT_GROUP_NAME].name,
            &ldap_grouplist);
    if (ret != EOK) {
        DEBUG(1, ("sysdb_attrs_primary_name_list failed [%d]: %s\n",
                    ret, strerror(ret)));
        goto done;
    }

    ret = sdap_add_incomplete_groups(state->sysdb, state->opts,
                                     state->dom, ldap_grouplist,
                                     state->groups, state->groups_cur);
    if (ret != EOK) {
        DEBUG(1, ("adding incomplete groups failed [%d]: %s\n",
                    ret, strerror(ret)));
        goto done;
    }

    /* Set the indirect memberships */
    for (i=0; i < state->groups_cur ; i++) {
        ret = sdap_initgr_nested_store_group(state->sysdb, state->opts,
                                             state->dom, state->groups[i],
                                             state->groups, state->groups_cur);
        if (ret != EOK) {
            DEBUG(2, ("Cannot fix nested group membership\n"));
            goto done;
        }
    }

done:
    if (ret != EOK) {
        tevent_req_error(req, ret);
        return;
    }

    tevent_req_done(req);
}

static int sdap_initgr_nested_get_direct_parents(TALLOC_CTX *mem_ctx,
                                                 struct sysdb_attrs *attrs,
                                                 struct sysdb_attrs **groups,
                                                 int ngroups,
                                                 struct sysdb_attrs ***_direct_parents,
                                                 int *_ndirect);

static int sdap_initgr_nested_store_group(struct sysdb_ctx *sysdb,
                                          struct sdap_options *opts,
                                          struct sss_domain_info *dom,
                                          struct sysdb_attrs *group,
                                          struct sysdb_attrs **groups,
                                          int ngroups)
{
    TALLOC_CTX *tmp_ctx;
    const char *member_filter;
    const char *group_orig_dn;
    const char *group_name;
    const char *group_dn;
    int ret;
    int i;
    struct ldb_message **direct_sysdb_groups = NULL;
    size_t direct_sysdb_count = 0;
    static const char *group_attrs[] = { SYSDB_NAME, NULL };
    struct ldb_dn *basedn;
    int ndirect;
    struct sysdb_attrs **direct_groups;
    char **sysdb_grouplist = NULL;
    const char *tmp_str;

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) return ENOMEM;

    basedn = ldb_dn_new_fmt(tmp_ctx, sysdb_ctx_get_ldb(sysdb),
                            SYSDB_TMPL_GROUP_BASE,
                            dom->name);
    if (!basedn) {
        ret = ENOMEM;
        goto done;
    }

    ret = sysdb_attrs_get_string(group, SYSDB_ORIG_DN, &group_orig_dn);
    if (ret != EOK) {
        goto done;
    }

    ret = sysdb_attrs_primary_name(sysdb, group,
                                   opts->group_map[SDAP_AT_GROUP_NAME].name,
                                   &group_name);
    if (ret != EOK) {
        goto done;
    }

    /* Get direct sysdb parents */
    group_dn = sysdb_group_strdn(tmp_ctx, dom->name, group_name);
    if (!group_dn) {
        ret = ENOMEM;
        goto done;
    }

    member_filter = talloc_asprintf(tmp_ctx, "(&(%s=%s)(%s=%s))",
                                    SYSDB_OBJECTCLASS, SYSDB_GROUP_CLASS,
                                    SYSDB_MEMBER, group_dn);
    if (!member_filter) {
        ret = ENOMEM;
        goto done;
    }

    DEBUG(8, ("searching sysdb with filter %s\n", member_filter));

    ret = sysdb_search_entry(tmp_ctx, sysdb, basedn,
                             LDB_SCOPE_SUBTREE, member_filter, group_attrs,
                             &direct_sysdb_count, &direct_sysdb_groups);
    if (ret == EOK) {
        /* Get the list of sysdb groups by name */
        sysdb_grouplist = talloc_array(tmp_ctx, char *, direct_sysdb_count+1);
        if (!sysdb_grouplist) {
            ret = ENOMEM;
            goto done;
        }

        for(i = 0; i < direct_sysdb_count; i++) {
            tmp_str = ldb_msg_find_attr_as_string(direct_sysdb_groups[i],
                                                SYSDB_NAME, NULL);
            if (!tmp_str) {
                /* This should never happen, but if it does, just continue */
                continue;
            }

            sysdb_grouplist[i] = talloc_strdup(sysdb_grouplist, tmp_str);
            if (!sysdb_grouplist[i]) {
                DEBUG(1, ("A group with no name?\n"));
                ret = EIO;
                goto done;
            }
        }
        sysdb_grouplist[i] = NULL;
    } else if (ret == ENOENT) {
        sysdb_grouplist = NULL;
        direct_sysdb_count = 0;
    } else {
        DEBUG(2, ("sysdb_search_entry failed: [%d]: %s\n", ret, strerror(ret)));
        goto done;
    }
    DEBUG(7, ("The group %s is a member of %d sysdb groups\n",
              group_name, direct_sysdb_count));

    /* Filter only parents from full set */
    ret = sdap_initgr_nested_get_direct_parents(tmp_ctx, group, groups,
                                                ngroups, &direct_groups,
                                                &ndirect);
    if (ret != EOK) {
        DEBUG(1, ("Cannot get parent groups [%d]: %s\n",
                  ret, strerror(ret)));
        goto done;
    }
    DEBUG(7, ("The group %s is a direct member of %d LDAP groups\n",
              group_name, ndirect));

    /* Store the direct parents with full member/memberof pairs */
    ret = sdap_initgr_common_store(sysdb, opts, dom, group_name,
                                   SYSDB_MEMBER_GROUP, sysdb_grouplist,
                                   direct_groups, ndirect, false);
    if (ret != EOK) {
        DEBUG(1, ("sdap_initgr_common_store failed [%d]: %s\n",
                  ret, strerror(ret)));
        goto done;
    }

    ret = EOK;
done:
    talloc_zfree(tmp_ctx);
    return ret;
}

static int sdap_initgr_nested_get_direct_parents(TALLOC_CTX *mem_ctx,
                                                 struct sysdb_attrs *attrs,
                                                 struct sysdb_attrs **groups,
                                                 int ngroups,
                                                 struct sysdb_attrs ***_direct_parents,
                                                 int *_ndirect)
{
    TALLOC_CTX *tmp_ctx;
    struct ldb_message_element *member;
    int i, mi;
    int ret;
    const char *orig_dn;

    int ndirect;
    struct sysdb_attrs **direct_groups;

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) return ENOMEM;


    direct_groups = talloc_zero_array(tmp_ctx, struct sysdb_attrs *,
                                      ngroups + 1);
    if (!direct_groups) {
        ret = ENOMEM;
        goto done;
    }
    ndirect = 0;

    ret = sysdb_attrs_get_string(attrs, SYSDB_ORIG_DN, &orig_dn);
    if (ret != EOK) {
        DEBUG(3, ("Missing originalDN\n"));
        goto done;
    }
    DEBUG(9, ("Looking up direct parents for group [%s]\n", orig_dn));

    /* FIXME - Filter only parents from full set to avoid searching
     * through all members of huge groups. That requires asking for memberOf
     * with the group LDAP search
     */

    /* Filter only direct parents from the list of all groups */
    for (i=0; i < ngroups; i++) {
        ret = sysdb_attrs_get_el(groups[i], SYSDB_MEMBER, &member);
        if (ret) {
            DEBUG(7, ("A group with no members during initgroups?\n"));
            continue;
        }

        for (mi = 0; mi < member->num_values; mi++) {
            if (strcasecmp((const char *) member->values[mi].data, orig_dn) != 0) {
                continue;
            }

            direct_groups[ndirect] = groups[i];
            ndirect++;
        }
    }
    direct_groups[ndirect] = NULL;

    DEBUG(9, ("The group [%s] has %d direct parents\n", orig_dn, ndirect));

    *_direct_parents = direct_groups;
    *_ndirect = ndirect;
    ret = EOK;
done:
    talloc_zfree(tmp_ctx);
    return ret;
}

static int sdap_initgr_nested_recv(struct tevent_req *req)
{
    TEVENT_REQ_RETURN_ON_ERROR(req);

    return EOK;
}

/* ==Initgr-call-(groups-a-user-is-member-of)-RFC2307-BIS================= */

static void sdap_initgr_rfc2307bis_process(struct tevent_req *subreq);
static void sdap_initgr_rfc2307bis_done(struct tevent_req *subreq);
errno_t save_rfc2307bis_user_memberships(
        struct sdap_initgr_rfc2307_state *state);
struct tevent_req *rfc2307bis_nested_groups_send(
        TALLOC_CTX *mem_ctx, struct tevent_context *ev,
        struct sdap_options *opts, struct sysdb_ctx *sysdb,
        struct sss_domain_info *dom, struct sdap_handle *sh,
        struct sysdb_attrs **groups, size_t num_groups,
        size_t nesting);
static errno_t rfc2307bis_nested_groups_recv(struct tevent_req *req);

static struct tevent_req *sdap_initgr_rfc2307bis_send(
        TALLOC_CTX *memctx,
        struct tevent_context *ev,
        struct sdap_options *opts,
        struct sysdb_ctx *sysdb,
        struct sss_domain_info *dom,
        struct sdap_handle *sh,
        const char *base_dn,
        const char *name,
        const char *orig_dn)
{
    errno_t ret;
    struct tevent_req *req;
    struct tevent_req *subreq;
    struct sdap_initgr_rfc2307_state *state;
    const char *filter;
    const char **attrs;
    char *clean_orig_dn;

    req = tevent_req_create(memctx, &state, struct sdap_initgr_rfc2307_state);
    if (!req) return NULL;

    state->ev = ev;
    state->opts = opts;
    state->sysdb = sysdb;
    state->dom = dom;
    state->sh = sh;
    state->op = NULL;
    state->name = name;

    ret = build_attrs_from_map(state, opts->group_map,
                               SDAP_OPTS_GROUP, &attrs);
    if (ret != EOK) {
        talloc_free(req);
        return NULL;
    }

    ret = sss_filter_sanitize(state, orig_dn, &clean_orig_dn);
    if (ret != EOK) {
        talloc_free(req);
        return NULL;
    }

    filter = talloc_asprintf(state, "(&(%s=%s)(objectclass=%s)(%s=*))",
                             opts->group_map[SDAP_AT_GROUP_MEMBER].name,
                             clean_orig_dn,
                             opts->group_map[SDAP_OC_GROUP].name,
                             opts->group_map[SDAP_AT_GROUP_NAME].name);
    if (!filter) {
        talloc_zfree(req);
        return NULL;
    }
    talloc_zfree(clean_orig_dn);

    DEBUG(6, ("Looking up parent groups for user [%s]\n", orig_dn));
    subreq = sdap_get_generic_send(state, state->ev, state->opts,
                                   state->sh, base_dn, LDAP_SCOPE_SUBTREE,
                                   filter, attrs,
                                   state->opts->group_map, SDAP_OPTS_GROUP,
                                   dp_opt_get_int(state->opts->basic,
                                                  SDAP_SEARCH_TIMEOUT));
    if (!subreq) {
        talloc_zfree(req);
        return NULL;
    }
    tevent_req_set_callback(subreq, sdap_initgr_rfc2307bis_process, req);

    return req;

}

static void sdap_initgr_rfc2307bis_process(struct tevent_req *subreq)
{
    struct tevent_req *req;
    struct sdap_initgr_rfc2307_state *state;
    int ret;

    req = tevent_req_callback_data(subreq, struct tevent_req);
    state = tevent_req_data(req, struct sdap_initgr_rfc2307_state);

    ret = sdap_get_generic_recv(subreq, state,
                                &state->ldap_groups_count,
                                &state->ldap_groups);
    talloc_zfree(subreq);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    if (state->ldap_groups_count == 0) {
        /* Start a transaction to look up the groups in the sysdb
         * and update them with LDAP data
         */
        ret = save_rfc2307bis_user_memberships(state);
        if (ret != EOK) {
            tevent_req_error(req, ret);
        } else {
            tevent_req_done(req);
        }
        return;
    }

    subreq = rfc2307bis_nested_groups_send(state, state->ev, state->opts,
                                           state->sysdb, state->dom,
                                           state->sh, state->ldap_groups,
                                           state->ldap_groups_count, 0);
    if (!subreq) {
        tevent_req_error(req, EIO);
        return;
    }
    tevent_req_set_callback(subreq, sdap_initgr_rfc2307bis_done, req);
}

static void sdap_initgr_rfc2307bis_done(struct tevent_req *subreq)
{
    errno_t ret;
    struct tevent_req *req =
            tevent_req_callback_data(subreq, struct tevent_req);
    struct sdap_initgr_rfc2307_state *state =
            tevent_req_data(req, struct sdap_initgr_rfc2307_state);

    ret = rfc2307bis_nested_groups_recv(subreq);
    talloc_zfree(subreq);
    if (ret != EOK) {
        tevent_req_error(req, ret);
        return;
    }

    /* save the user memberships */
    ret = save_rfc2307bis_user_memberships(state);
    if (ret != EOK) {
        tevent_req_error(req, ret);
    } else {
        tevent_req_done(req);
    }
    return;
}

static int sdap_initgr_rfc2307bis_recv(struct tevent_req *req)
{
    TEVENT_REQ_RETURN_ON_ERROR(req);
    return EOK;
}

errno_t save_rfc2307bis_user_memberships(
        struct sdap_initgr_rfc2307_state *state)
{
    errno_t ret, tret;
    char *member_dn;
    char *sanitized_dn;
    char *filter;
    const char **attrs;
    size_t reply_count, i;
    struct ldb_message **replies;
    char **ldap_grouplist;
    char **sysdb_grouplist;
    char **add_groups;
    char **del_groups;
    const char *tmp_str;
    bool in_transaction = false;

    TALLOC_CTX *tmp_ctx = talloc_new(NULL);
    if(!tmp_ctx) {
        return ENOMEM;
    }

    DEBUG(7, ("Save parent groups to sysdb\n"));
    ret = sysdb_transaction_start(state->sysdb);
    if (ret != EOK) {
        goto error;
    }
    in_transaction = true;

    /* Save this user and their memberships */
    attrs = talloc_array(tmp_ctx, const char *, 2);
    if (!attrs) {
        ret = ENOMEM;
        goto error;
    }

    attrs[0] = SYSDB_NAME;
    attrs[1] = NULL;

    member_dn = sysdb_user_strdn(tmp_ctx, state->dom->name, state->name);
    if (!member_dn) {
        ret = ENOMEM;
        goto error;
    }
    ret = sss_filter_sanitize(tmp_ctx, member_dn, &sanitized_dn);
    if (ret != EOK) {
        goto error;
    }
    talloc_free(member_dn);

    filter = talloc_asprintf(tmp_ctx, "(member=%s)", sanitized_dn);
    if (!filter) {
        ret = ENOMEM;
        goto error;
    }
    talloc_free(sanitized_dn);

    ret = sysdb_search_groups(tmp_ctx, state->sysdb, filter, attrs,
                              &reply_count, &replies);
    if (ret != EOK && ret != ENOENT) {
        goto error;
    } if (ret == ENOENT) {
        reply_count = 0;
    }

    if (reply_count == 0) {
        DEBUG(6, ("User [%s] is not a direct member of any groups\n",
                  state->name));
        sysdb_grouplist = NULL;
    } else {
        sysdb_grouplist = talloc_array(tmp_ctx, char *, reply_count+1);
        if (!sysdb_grouplist) {
            ret = ENOMEM;
            goto error;
        }

        for (i = 0; i < reply_count; i++) {
            tmp_str = ldb_msg_find_attr_as_string(replies[i],
                                                  SYSDB_NAME,
                                                  NULL);
            if (!tmp_str) {
                /* This should never happen, but if it
                 * does, just skip it.
                 */
                continue;
            }

            sysdb_grouplist[i] = talloc_strdup(sysdb_grouplist, tmp_str);
            if (!sysdb_grouplist[i]) {
                ret = ENOMEM;
                goto error;
            }
        }
        sysdb_grouplist[i] = NULL;
    }

    if (state->ldap_groups_count == 0) {
        ldap_grouplist = NULL;
    }
    else {
        ret = sysdb_attrs_primary_name_list(
                state->sysdb, tmp_ctx,
                state->ldap_groups, state->ldap_groups_count,
                state->opts->group_map[SDAP_AT_GROUP_NAME].name,
                &ldap_grouplist);
        if (ret != EOK) {
            goto error;
        }
    }

    /* Find the differences between the sysdb and ldap lists
     * Groups in ldap only must be added to the sysdb;
     * groups in the sysdb only must be removed.
     */
    ret = diff_string_lists(tmp_ctx,
                            ldap_grouplist, sysdb_grouplist,
                            &add_groups, &del_groups, NULL);
    if (ret != EOK) {
        goto error;
    }

    DEBUG(8, ("Updating memberships for %s\n", state->name));
    ret = sysdb_update_members(state->sysdb, state->name, SYSDB_MEMBER_USER,
                               (const char *const *)add_groups,
                               (const char *const *)del_groups);
    if (ret != EOK) {
        goto error;
    }

    ret = sysdb_transaction_commit(state->sysdb);
    if (ret != EOK) {
        goto error;
    }

    return EOK;

error:
    if (in_transaction) {
        tret = sysdb_transaction_cancel(state->sysdb);
        if (tret != EOK) {
            DEBUG(1, ("Failed to cancel transaction\n"));
        }
    }
    talloc_free(tmp_ctx);
    return ret;
}

struct sdap_rfc2307bis_nested_ctx {
    struct tevent_context *ev;
    struct sdap_options *opts;
    struct sysdb_ctx *sysdb;
    struct sss_domain_info *dom;
    struct sdap_handle *sh;
    struct sysdb_attrs **groups;
    size_t num_groups;

    size_t nesting_level;

    size_t group_iter;
    struct sysdb_attrs **ldap_groups;
    size_t ldap_groups_count;

    struct sysdb_handle *handle;
};

static errno_t rfc2307bis_nested_groups_step(struct tevent_req *req);
struct tevent_req *rfc2307bis_nested_groups_send(
        TALLOC_CTX *mem_ctx, struct tevent_context *ev,
        struct sdap_options *opts, struct sysdb_ctx *sysdb,
        struct sss_domain_info *dom, struct sdap_handle *sh,
        struct sysdb_attrs **groups, size_t num_groups,
        size_t nesting)
{
    errno_t ret;
    struct tevent_req *req;
    struct sdap_rfc2307bis_nested_ctx *state;

    req = tevent_req_create(mem_ctx, &state,
                            struct sdap_rfc2307bis_nested_ctx);
    if (!req) return NULL;

    if ((num_groups == 0) ||
        (nesting > dp_opt_get_int(opts->basic, SDAP_NESTING_LEVEL))) {
        /* No parent groups to process or too deep*/
        tevent_req_done(req);
        tevent_req_post(req, ev);
        return req;
    }

    state->ev = ev;
    state->opts = opts;
    state->sysdb = sysdb;
    state->dom = dom;
    state->sh = sh;
    state->groups = groups;
    state->num_groups = num_groups;
    state->group_iter = 0;
    state->nesting_level = nesting;

    ret = rfc2307bis_nested_groups_step(req);
    if (ret != EOK) {
        tevent_req_error(req, ret);
        tevent_req_post(req, ev);
    }
    return req;
}

static void rfc2307bis_nested_groups_process(struct tevent_req *subreq);
static errno_t rfc2307bis_nested_groups_step(struct tevent_req *req)
{
    errno_t ret, tret;
    struct tevent_req *subreq;
    const char *name;
    struct sysdb_attrs **grouplist;
    char **groupnamelist;
    bool in_transaction = false;
    TALLOC_CTX *tmp_ctx = NULL;
    char *filter;
    const char *orig_dn;
    const char **attrs;
    char *clean_orig_dn;
    struct sdap_rfc2307bis_nested_ctx *state =
            tevent_req_data(req, struct sdap_rfc2307bis_nested_ctx);

    tmp_ctx = talloc_new(state);
    if (!tmp_ctx) {
        ret = ENOMEM;
        goto error;
    }

    ret = sysdb_attrs_primary_name(
            state->sysdb,
            state->groups[state->group_iter],
            state->opts->group_map[SDAP_AT_GROUP_NAME].name,
            &name);
    if (ret != EOK) {
        goto error;
    }

    DEBUG(6, ("Processing group [%s]\n", name));

    ret = sysdb_transaction_start(state->sysdb);
    if (ret != EOK) {
        goto error;
    }
    in_transaction = true;

    /* First, save the group we're processing to the sysdb
     * sdap_add_incomplete_groups_send will add them if needed
     */

    /* sdap_add_incomplete_groups_send expects a list of groups */
    grouplist = talloc_array(tmp_ctx, struct sysdb_attrs *, 1);
    if (!grouplist) {
        ret = ENOMEM;
        goto error;
    }
    grouplist[0] = state->groups[state->group_iter];

    groupnamelist = talloc_array(tmp_ctx, char *, 2);
    if (!groupnamelist) {
        ret = ENOMEM;
        goto error;
    }
    groupnamelist[0] = talloc_strdup(groupnamelist, name);
    if (!groupnamelist[0]) {
        ret = ENOMEM;
        goto error;
    }
    groupnamelist[1] = NULL;

    DEBUG(6, ("Saving incomplete group [%s] to the sysdb\n",
              groupnamelist[0]));
    ret = sdap_add_incomplete_groups(state->sysdb, state->opts,
                                     state->dom, groupnamelist,
                                     grouplist, 1);
    if (ret != EOK) {
        goto error;
    }

    ret = sysdb_transaction_commit(state->sysdb);
    if (ret != EOK) {
        goto error;
    }

    /* Get any parent groups for this group */
    ret = sysdb_attrs_get_string(state->groups[state->group_iter],
                                 SYSDB_ORIG_DN,
                                 &orig_dn);
    if (ret != EOK) {
        goto error;
    }

    ret = build_attrs_from_map(tmp_ctx, state->opts->group_map,
                               SDAP_OPTS_GROUP, &attrs);
    if (ret != EOK) {
        goto error;
    }

    ret = sss_filter_sanitize(state, orig_dn, &clean_orig_dn);
    if (ret != EOK) {
        goto error;
    }

    filter = talloc_asprintf(
            tmp_ctx, "(&(%s=%s)(objectclass=%s)(%s=*))",
            state->opts->group_map[SDAP_AT_GROUP_MEMBER].name,
            clean_orig_dn,
            state->opts->group_map[SDAP_OC_GROUP].name,
            state->opts->group_map[SDAP_AT_GROUP_NAME].name);
    if (!filter) {
        ret = ENOMEM;
        goto error;
    }
    talloc_zfree(clean_orig_dn);

    DEBUG(6, ("Looking up parent groups for group [%s]\n", orig_dn));
    subreq = sdap_get_generic_send(state, state->ev, state->opts,
                                   state->sh,
                                   dp_opt_get_string(state->opts->basic,
                                                     SDAP_GROUP_SEARCH_BASE),
                                   LDAP_SCOPE_SUBTREE,
                                   filter, attrs,
                                   state->opts->group_map, SDAP_OPTS_GROUP,
                                   dp_opt_get_int(state->opts->basic,
                                                  SDAP_SEARCH_TIMEOUT));
    if (!subreq) {
        ret = EIO;
        goto error;
    }
    talloc_steal(subreq, tmp_ctx);
    tevent_req_set_callback(subreq,
                            rfc2307bis_nested_groups_process,
                            req);

    return EOK;

error:
    if (in_transaction) {
        tret = sysdb_transaction_cancel(state->sysdb);
        if (tret != EOK) {
            DEBUG(1, ("Failed to cancel transaction\n"));
        }
    }

    talloc_free(tmp_ctx);
    return ret;
}

static errno_t rfc2307bis_nested_groups_update_sysdb(
        struct sdap_rfc2307bis_nested_ctx *state);
static void rfc2307bis_nested_groups_done(struct tevent_req *subreq);
static void rfc2307bis_nested_groups_process(struct tevent_req *subreq)
{
    errno_t ret;
    struct tevent_req *req =
            tevent_req_callback_data(subreq, struct tevent_req);
    struct sdap_rfc2307bis_nested_ctx *state =
            tevent_req_data(req, struct sdap_rfc2307bis_nested_ctx);

    ret = sdap_get_generic_recv(subreq, state,
                                &state->ldap_groups_count,
                                &state->ldap_groups);
    talloc_zfree(subreq);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    if (state->ldap_groups_count == 0) {
        /* No parent groups for this group in LDAP
         * We need to ensure that there are no groups
         * in the sysdb either.
         */

        ret = rfc2307bis_nested_groups_update_sysdb(state);
        if (ret != EOK) {
            tevent_req_error(req, ret);
            return;
        }

        state->group_iter++;
        if (state->group_iter < state->num_groups) {
            ret = rfc2307bis_nested_groups_step(req);
            if (ret != EOK) {
                tevent_req_error(req, ret);
                return;
            }
        } else {
            tevent_req_done(req);
        }
        return;
    }

    /* Otherwise, recurse into the groups */
    subreq = rfc2307bis_nested_groups_send(
            state, state->ev, state->opts, state->sysdb,
            state->dom, state->sh,
            state->ldap_groups,
            state->ldap_groups_count,
            state->nesting_level+1);
    if (!subreq) {
        tevent_req_error(req, EIO);
        return;
    }
    tevent_req_set_callback(subreq, rfc2307bis_nested_groups_done, req);
}

static errno_t rfc2307bis_nested_groups_recv(struct tevent_req *req)
{
    TEVENT_REQ_RETURN_ON_ERROR(req);
    return EOK;
}

static void rfc2307bis_nested_groups_done(struct tevent_req *subreq)
{
    errno_t ret;
    struct tevent_req *req =
            tevent_req_callback_data(subreq, struct tevent_req);
    struct sdap_rfc2307bis_nested_ctx *state =
            tevent_req_data(req, struct sdap_rfc2307bis_nested_ctx);

    ret = rfc2307bis_nested_groups_recv(subreq);
    talloc_zfree(subreq);
    if (ret != EOK) {
        DEBUG(6, ("rfc2307bis_nested failed [%d][%s]\n",
                  ret, strerror(ret)));
        tevent_req_error(req, ret);
        return;
    }

    /* All of the parent groups have been added
     * Now add the memberships
     */

    ret = rfc2307bis_nested_groups_update_sysdb(state);
    if (ret != EOK) {
        tevent_req_error(req, ret);
        return;
    }

    state->group_iter++;
    if (state->group_iter < state->num_groups) {
        ret = rfc2307bis_nested_groups_step(req);
        if (ret != EOK) {
            tevent_req_error(req, ret);
        }
    } else {
        tevent_req_done(req);
    }
}

static errno_t rfc2307bis_nested_groups_update_sysdb(
        struct sdap_rfc2307bis_nested_ctx *state)
{
    errno_t ret, tret;
    const char *name;
    bool in_transaction = false;
    char *member_dn;
    char *sanitized_dn;
    char *filter;
    const char **attrs;
    size_t reply_count, i;
    struct ldb_message **replies;
    char **sysdb_grouplist;
    char **ldap_grouplist;
    char **add_groups;
    char **del_groups;
    const char *tmp_str;

    TALLOC_CTX *tmp_ctx = talloc_new(state);
    if (!tmp_ctx) {
        return ENOMEM;
    }

    /* Start a transaction to look up the groups in the sysdb
     * and update them with LDAP data
     */
    ret = sysdb_transaction_start(state->sysdb);
    if (ret != EOK) {
        goto error;
    }
    in_transaction = true;

    ret = sysdb_attrs_primary_name(
            state->sysdb,
            state->groups[state->group_iter],
            state->opts->group_map[SDAP_AT_GROUP_NAME].name,
            &name);
    if (ret != EOK) {
        goto error;
    }

    DEBUG(6, ("Processing group [%s]\n", name));

    attrs = talloc_array(tmp_ctx, const char *, 2);
    if (!attrs) {
        ret = ENOMEM;
        goto error;
    }
    attrs[0] = SYSDB_NAME;
    attrs[1] = NULL;

    member_dn = sysdb_group_strdn(tmp_ctx, state->dom->name, name);
    if (!member_dn) {
        ret = ENOMEM;
        goto error;
    }

    ret = sss_filter_sanitize(tmp_ctx, member_dn, &sanitized_dn);
    if (ret != EOK) {
        goto error;
    }
    talloc_free(member_dn);

    filter = talloc_asprintf(tmp_ctx, "(member=%s)", sanitized_dn);
    if (!filter) {
        ret = ENOMEM;
        goto error;
    }
    talloc_free(sanitized_dn);

    ret = sysdb_search_groups(tmp_ctx, state->sysdb, filter, attrs,
                              &reply_count, &replies);
    if (ret != EOK && ret != ENOENT) {
        goto error;
    } else if (ret == ENOENT) {
        reply_count = 0;
    }

    if (reply_count == 0) {
        DEBUG(6, ("Group [%s] is not a direct member of any groups\n", name));
        sysdb_grouplist = NULL;
    } else {
        sysdb_grouplist = talloc_array(tmp_ctx, char *, reply_count+1);
        if (!sysdb_grouplist) {
            ret = ENOMEM;
            goto error;
        }

        for (i = 0; i < reply_count; i++) {
            tmp_str = ldb_msg_find_attr_as_string(replies[i],
                                                  SYSDB_NAME,
                                                  NULL);
            if (!tmp_str) {
                /* This should never happen, but if it
                 * does, just skip it.
                 */
                continue;
            }

            sysdb_grouplist[i] = talloc_strdup(sysdb_grouplist, tmp_str);
            if (!sysdb_grouplist[i]) {
                ret = ENOMEM;
                goto error;
            }
        }
        sysdb_grouplist[i] = NULL;
    }

    if (state->ldap_groups_count == 0) {
        ldap_grouplist = NULL;
    }
    else {
        ret = sysdb_attrs_primary_name_list(
                state->sysdb, tmp_ctx,
                state->ldap_groups, state->ldap_groups_count,
                state->opts->group_map[SDAP_AT_GROUP_NAME].name,
                &ldap_grouplist);
        if (ret != EOK) {
            goto error;
        }
    }

    /* Find the differences between the sysdb and ldap lists
     * Groups in ldap only must be added to the sysdb;
     * groups in the sysdb only must be removed.
     */
    ret = diff_string_lists(state,
                            ldap_grouplist, sysdb_grouplist,
                            &add_groups, &del_groups, NULL);
    if (ret != EOK) {
        goto error;
    }
    talloc_free(ldap_grouplist);
    talloc_free(sysdb_grouplist);

    DEBUG(8, ("Updating memberships for %s\n", name));
    ret = sysdb_update_members(state->sysdb, name, SYSDB_MEMBER_GROUP,
                               (const char *const *)add_groups,
                               (const char *const *)del_groups);
    if (ret != EOK) {
        goto error;
    }

    ret = sysdb_transaction_commit(state->sysdb);
    if (ret != EOK) {
        goto error;
    }
    in_transaction = false;

    ret = EOK;

error:
    if (in_transaction) {
        tret = sysdb_transaction_cancel(state->sysdb);
        if (tret != EOK) {
            DEBUG(1, ("Failed to cancel transaction\n"));
        }
    }
    talloc_free(tmp_ctx);
    return ret;
}


/* ==Initgr-call-(groups-a-user-is-member-of)============================= */

struct sdap_get_initgr_state {
    struct tevent_context *ev;
    struct sysdb_ctx *sysdb;
    struct sdap_options *opts;
    struct sss_domain_info *dom;
    struct sdap_handle *sh;
    struct sdap_id_ctx *id_ctx;
    const char *name;
    const char **grp_attrs;
    const char **ldap_attrs;

    struct sysdb_attrs *orig_user;
};

static void sdap_get_initgr_user(struct tevent_req *subreq);
static void sdap_get_initgr_done(struct tevent_req *subreq);

struct tevent_req *sdap_get_initgr_send(TALLOC_CTX *memctx,
                                        struct tevent_context *ev,
                                        struct sdap_handle *sh,
                                        struct sdap_id_ctx *id_ctx,
                                        const char *name,
                                        const char **grp_attrs)
{
    struct tevent_req *req, *subreq;
    struct sdap_get_initgr_state *state;
    const char *base_dn;
    char *filter;
    int ret;
    char *clean_name;

    DEBUG(9, ("Retrieving info for initgroups call\n"));

    req = tevent_req_create(memctx, &state, struct sdap_get_initgr_state);
    if (!req) return NULL;

    state->ev = ev;
    state->opts = id_ctx->opts;
    state->sysdb = id_ctx->be->sysdb;
    state->dom = id_ctx->be->domain;
    state->sh = sh;
    state->id_ctx = id_ctx;
    state->name = name;
    state->grp_attrs = grp_attrs;
    state->orig_user = NULL;

    ret = sss_filter_sanitize(state, name, &clean_name);
    if (ret != EOK) {
        return NULL;
    }

    filter = talloc_asprintf(state, "(&(%s=%s)(objectclass=%s))",
                        state->opts->user_map[SDAP_AT_USER_NAME].name,
                        clean_name,
                        state->opts->user_map[SDAP_OC_USER].name);
    if (!filter) {
        talloc_zfree(req);
        return NULL;
    }

    base_dn = dp_opt_get_string(state->opts->basic,
                                SDAP_USER_SEARCH_BASE);
    if (!base_dn) {
        talloc_zfree(req);
        return NULL;
    }

    ret = build_attrs_from_map(state, state->opts->user_map,
                               SDAP_OPTS_USER, &state->ldap_attrs);
    if (ret) {
        talloc_zfree(req);
        return NULL;
    }

    subreq = sdap_get_generic_send(state, state->ev,
                                   state->opts, state->sh,
                                   base_dn, LDAP_SCOPE_SUBTREE,
                                   filter, state->ldap_attrs,
                                   state->opts->user_map, SDAP_OPTS_USER,
                                   dp_opt_get_int(state->opts->basic,
                                                  SDAP_SEARCH_TIMEOUT));
    if (!subreq) {
        talloc_zfree(req);
        return NULL;
    }
    tevent_req_set_callback(subreq, sdap_get_initgr_user, req);

    return req;
}

static struct tevent_req *sdap_initgr_rfc2307bis_send(
        TALLOC_CTX *memctx,
        struct tevent_context *ev,
        struct sdap_options *opts,
        struct sysdb_ctx *sysdb,
        struct sss_domain_info *dom,
        struct sdap_handle *sh,
        const char *base_dn,
        const char *name,
        const char *orig_dn);
static void sdap_get_initgr_user(struct tevent_req *subreq)
{
    struct tevent_req *req = tevent_req_callback_data(subreq,
                                                      struct tevent_req);
    struct sdap_get_initgr_state *state = tevent_req_data(req,
                                               struct sdap_get_initgr_state);
    struct sysdb_attrs **usr_attrs;
    size_t count;
    int ret;
    const char *orig_dn;

    DEBUG(9, ("Receiving info for the user\n"));

    ret = sdap_get_generic_recv(subreq, state, &count, &usr_attrs);
    talloc_zfree(subreq);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    if (count != 1) {
        DEBUG(2, ("Expected one user entry and got %d\n", count));
        tevent_req_error(req, ENOENT);
        return;
    }

    state->orig_user = usr_attrs[0];

    ret = sysdb_transaction_start(state->sysdb);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    DEBUG(9, ("Storing the user\n"));

    ret = sdap_save_user(state, state->sysdb,
                         state->opts, state->dom,
                         state->orig_user, state->ldap_attrs,
                         true, NULL);
    if (ret) {
        sysdb_transaction_cancel(state->sysdb);
        tevent_req_error(req, ret);
        return;
    }

    DEBUG(9, ("Commit change\n"));

    ret = sysdb_transaction_commit(state->sysdb);
    if (ret) {
        tevent_req_error(req, ret);
        return;
    }

    DEBUG(9, ("Process user's groups\n"));

    switch (state->opts->schema_type) {
    case SDAP_SCHEMA_RFC2307:
        subreq = sdap_initgr_rfc2307_send(state, state->ev, state->opts,
                                    state->sysdb, state->dom, state->sh,
                                    dp_opt_get_string(state->opts->basic,
                                                  SDAP_GROUP_SEARCH_BASE),
                                    state->name);
        if (!subreq) {
            tevent_req_error(req, ENOMEM);
            return;
        }
        tevent_req_set_callback(subreq, sdap_get_initgr_done, req);
        break;

    case SDAP_SCHEMA_RFC2307BIS:
        ret = sysdb_attrs_get_string(state->orig_user,
                                     SYSDB_ORIG_DN,
                                     &orig_dn);
        if (ret != EOK) {
            tevent_req_error(req, ret);
            return;
        }

        subreq = sdap_initgr_rfc2307bis_send(
                state, state->ev, state->opts, state->sysdb,
                state->dom, state->sh,
                dp_opt_get_string(state->opts->basic,
                                  SDAP_GROUP_SEARCH_BASE),
                state->name, orig_dn);
        if (!subreq) {
            tevent_req_error(req, ENOMEM);
            return;
        }
        talloc_steal(subreq, orig_dn);
        tevent_req_set_callback(subreq, sdap_get_initgr_done, req);
        break;
    case SDAP_SCHEMA_IPA_V1:
    case SDAP_SCHEMA_AD:
        /* TODO: AD uses a different member/memberof schema
         *       We need an AD specific call that is able to unroll
         *       nested groups by doing extensive recursive searches */

        subreq = sdap_initgr_nested_send(state, state->ev, state->opts,
                                         state->sysdb, state->dom, state->sh,
                                         state->orig_user, state->grp_attrs);
        if (!subreq) {
            tevent_req_error(req, ENOMEM);
            return;
        }
        tevent_req_set_callback(subreq, sdap_get_initgr_done, req);
        return;

    default:
        tevent_req_error(req, EINVAL);
        return;
    }
}

static int sdap_initgr_rfc2307bis_recv(struct tevent_req *req);
static void sdap_get_initgr_pgid(struct tevent_req *req);
static void sdap_get_initgr_done(struct tevent_req *subreq)
{
    struct tevent_req *req = tevent_req_callback_data(subreq,
                                                      struct tevent_req);
    struct sdap_get_initgr_state *state = tevent_req_data(req,
                                               struct sdap_get_initgr_state);
    int ret;
    gid_t primary_gid;
    char *gid;

    DEBUG(9, ("Initgroups done\n"));

    switch (state->opts->schema_type) {
    case SDAP_SCHEMA_RFC2307:
        ret = sdap_initgr_rfc2307_recv(subreq);
        break;

    case SDAP_SCHEMA_RFC2307BIS:
        ret = sdap_initgr_rfc2307bis_recv(subreq);
        break;

    case SDAP_SCHEMA_IPA_V1:
    case SDAP_SCHEMA_AD:
        ret = sdap_initgr_nested_recv(subreq);
        break;

    default:

        ret = EINVAL;
        break;
    }

    talloc_zfree(subreq);
    if (ret) {
        DEBUG(9, ("Error in initgroups: [%d][%s]\n",
                  ret, strerror(ret)));
        tevent_req_error(req, ret);
        return;
    }

    /* We also need to update the user's primary group, since
     * the user may not be an explicit member of that group
     */
    ret = sysdb_attrs_get_uint32_t(state->orig_user, SYSDB_GIDNUM, &primary_gid);
    if (ret != EOK) {
        DEBUG(6, ("Could not find user's primary GID\n"));
        tevent_req_error(req, ret);
        return;
    }

    gid = talloc_asprintf(state, "%lu", (unsigned long)primary_gid);
    if (gid == NULL) {
        tevent_req_error(req, ENOMEM);
        return;
    }

    subreq = groups_get_send(req, state->ev, state->id_ctx, gid,
                             BE_FILTER_IDNUM, BE_ATTR_ALL);
    if (!subreq) {
        tevent_req_error(req, ENOMEM);
        return;
    }
    tevent_req_set_callback(subreq, sdap_get_initgr_pgid, req);

    tevent_req_done(req);
}

static void sdap_get_initgr_pgid(struct tevent_req *subreq)
{
    struct tevent_req *req =
            tevent_req_callback_data(subreq, struct tevent_req);
    errno_t ret;

    ret = groups_get_recv(subreq, NULL);
    talloc_zfree(subreq);
    if (ret != EOK) {
        tevent_req_error(req, ret);
        return;
    }

    tevent_req_done(req);
}

int sdap_get_initgr_recv(struct tevent_req *req)
{
    TEVENT_REQ_RETURN_ON_ERROR(req);

    return EOK;
}

