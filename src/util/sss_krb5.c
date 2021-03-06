/*
    Authors:
        Sumit Bose <sbose@redhat.com>

    Copyright (C) 2009-2010 Red Hat

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
#include <errno.h>
#include <talloc.h>

#include "config.h"

#include "util/util.h"
#include "util/sss_krb5.h"

errno_t select_principal_from_keytab(TALLOC_CTX *mem_ctx,
                                     const char *hostname,
                                     const char *desired_realm,
                                     const char *keytab_name,
                                     char **_principal,
                                     char **_primary,
                                     char **_realm)
{
    krb5_error_code kerr = 0;
    krb5_context krb_ctx = NULL;
    krb5_keytab keytab = NULL;
    krb5_principal client_princ = NULL;
    TALLOC_CTX *tmp_ctx;
    char *primary = NULL;
    char *realm = NULL;
    int i = 0;
    errno_t ret;
    char *principal_string;

    /**
     * Priority of lookup:
     * - foobar$@REALM (AD domain)
     * - host/our.hostname@REALM
     * - host/foobar@REALM
     * - host/foo@BAR
     * - pick the first principal in the keytab
     */
    const char *primary_patterns[] = {"%s$", "*$", "host/%s", "host/*", "host/*", NULL};
    const char *realm_patterns[] = {"%s", "%s", "%s", "%s", NULL, NULL};

    DEBUG(5, ("trying to select the most appropriate principal from keytab\n"));
    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) {
        DEBUG(1, ("talloc_new failed\n"));
        return ENOMEM;
    }

    kerr = krb5_init_context(&krb_ctx);
    if (kerr) {
        DEBUG(2, ("Failed to init kerberos context\n"));
        ret = EFAULT;
        goto done;
    }

    if (keytab_name != NULL) {
        kerr = krb5_kt_resolve(krb_ctx, keytab_name, &keytab);
    } else {
        kerr = krb5_kt_default(krb_ctx, &keytab);
    }
    if (kerr) {
        DEBUG(0, ("Failed to read keytab file: %s\n",
                  sss_krb5_get_error_message(krb_ctx, kerr)));
        ret = EFAULT;
        goto done;
    }

    if (!desired_realm) {
        desired_realm = "*";
    }
    if (!hostname) {
        hostname = "*";
    }

    do {
        if (primary_patterns[i]) {
            primary = talloc_asprintf(tmp_ctx, primary_patterns[i], hostname);
            if (primary == NULL) {
                ret = ENOMEM;
                goto done;
            }
        } else {
            primary = NULL;
        }
        if (realm_patterns[i]) {
            realm = talloc_asprintf(tmp_ctx, realm_patterns[i], desired_realm);
            if (realm == NULL) {
                ret = ENOMEM;
                goto done;
            }
        } else {
            realm = NULL;
        }

        kerr = find_principal_in_keytab(krb_ctx, keytab, primary, realm,
                                        &client_princ);
        talloc_zfree(primary);
        talloc_zfree(realm);
        if (kerr == 0) {
            break;
        }
        if (client_princ != NULL) {
            krb5_free_principal(krb_ctx, client_princ);
            client_princ = NULL;
        }
        i++;
    } while(primary_patterns[i-1] != NULL || realm_patterns[i-1] != NULL);

    if (kerr == 0) {
        if (_principal) {
            kerr = krb5_unparse_name(krb_ctx, client_princ, &principal_string);
            if (kerr) {
                DEBUG(1, ("krb5_unparse_name failed"));
                ret = EFAULT;
                goto done;
            }

            *_principal = talloc_strdup(mem_ctx, principal_string);
            free(principal_string);
            if (!*_principal) {
                DEBUG(1, ("talloc_strdup failed"));
                ret = ENOMEM;
                goto done;
            }
            DEBUG(5, ("Selected principal: %s\n", *_principal));
        }

        if (_primary) {
            kerr = sss_krb5_unparse_name_flags(krb_ctx, client_princ,
                                               KRB5_PRINCIPAL_UNPARSE_NO_REALM,
                                               &principal_string);
            if (kerr) {
                DEBUG(1, ("krb5_unparse_name failed"));
                ret = EFAULT;
                goto done;
            }

            *_primary = talloc_strdup(mem_ctx, principal_string);
            free(principal_string);
            if (!*_primary) {
                DEBUG(1, ("talloc_strdup failed"));
                if (_principal) talloc_zfree(*_principal);
                ret = ENOMEM;
                goto done;
            }
            DEBUG(5, ("Selected primary: %s\n", *_primary));
        }

        if (_realm) {
            *_realm = talloc_asprintf(mem_ctx, "%.*s",
                                      krb5_princ_realm(ctx, client_princ)->length,
                                      krb5_princ_realm(ctx, client_princ)->data);
            if (!*_realm) {
                DEBUG(1, ("talloc_asprintf failed"));
                if (_principal) talloc_zfree(*_principal);
                if (_primary) talloc_zfree(*_primary);
                ret = ENOMEM;
                goto done;
            }
            DEBUG(5, ("Selected realm: %s\n", *_realm));
        }

        ret = EOK;
    } else {
        DEBUG(3, ("No suitable principal found in keytab\n"));
        ret = ENOENT;
    }

done:
    if (keytab) krb5_kt_close(krb_ctx, keytab);
    if (krb_ctx) krb5_free_context(krb_ctx);
    if (client_princ != NULL) {
        krb5_free_principal(krb_ctx, client_princ);
        client_princ = NULL;
    }
    talloc_free(tmp_ctx);
    return ret;
}


int sss_krb5_verify_keytab(const char *principal,
                           const char *realm_str,
                           const char *keytab_name)
{
    krb5_context context = NULL;
    krb5_keytab keytab = NULL;
    krb5_error_code krberr;
    int ret;
    char *full_princ = NULL;
    char *realm_name = NULL;
    char *default_realm = NULL;
    TALLOC_CTX *tmp_ctx;

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) {
        return ENOMEM;
    }

    krberr = krb5_init_context(&context);
    if (krberr) {
        DEBUG(2, ("Failed to init kerberos context\n"));
        ret = EFAULT;
        goto done;
    }

    if (keytab_name) {
        krberr = krb5_kt_resolve(context, keytab_name, &keytab);
    } else {
        krberr = krb5_kt_default(context, &keytab);
    }

    if (krberr) {
        DEBUG(0, ("Failed to read keytab file: %s\n",
                  sss_krb5_get_error_message(context, krberr)));
        ret = EFAULT;
        goto done;
    }

    if (!realm_str) {
        krberr = krb5_get_default_realm(context, &default_realm);
        if (krberr) {
            DEBUG(2, ("Failed to get default realm name: %s\n",
                      sss_krb5_get_error_message(context, krberr)));
            ret = EFAULT;
            goto done;
        }

        realm_name = talloc_strdup(tmp_ctx, default_realm);
        krb5_free_default_realm(context, default_realm);
        if (!realm_name) {
            ret = ENOMEM;
            goto done;
        }
    } else {
        realm_name = talloc_strdup(tmp_ctx, realm_str);
        if (!realm_name) {
            ret = ENOMEM;
            goto done;
        }
    }

    if (principal) {
        if (!strchr(principal, '@')) {
            full_princ = talloc_asprintf(tmp_ctx, "%s@%s",
                                         principal, realm_name);
        } else {
            full_princ = talloc_strdup(tmp_ctx, principal);
        }
    } else {
        char hostname[512];

        ret = gethostname(hostname, 511);
        if (ret == -1) {
            ret = errno;
            goto done;
        }
        hostname[511] = '\0';

        ret = select_principal_from_keytab(tmp_ctx, hostname, realm_name,
                                           keytab_name, &full_princ, NULL, NULL);
        if (ret) goto done;
    }
    if (!full_princ) {
        ret = ENOMEM;
        goto done;
    }
    DEBUG(4, ("Principal name is: [%s]\n", full_princ));

    ret = sss_krb5_verify_keytab_ex(full_princ, keytab_name, context, keytab);
    if (ret) goto done;

    ret = EOK;
done:
    if (keytab) krb5_kt_close(context, keytab);
    if (context) krb5_free_context(context);
    talloc_free(tmp_ctx);
    return ret;
}

int sss_krb5_verify_keytab_ex(const char *principal, const char *keytab_name,
                              krb5_context context, krb5_keytab keytab)
{
    bool found;
    char *kt_principal;
    krb5_error_code krberr;
    krb5_kt_cursor cursor;
    krb5_keytab_entry entry;

    krberr = krb5_kt_start_seq_get(context, keytab, &cursor);
    if (krberr) {
        DEBUG(0, ("Cannot read keytab [%s].\n", keytab_name));

        sss_log(SSS_LOG_ERR, "Error reading keytab file [%s]: [%d][%s]. "
                             "Unable to create GSSAPI-encrypted LDAP connection.",
                             keytab_name, krberr,
                             sss_krb5_get_error_message(context, krberr));

        return EIO;
    }

    found = false;
    while((krb5_kt_next_entry(context, keytab, &entry, &cursor)) == 0){
        krb5_unparse_name(context, entry.principal, &kt_principal);
        if (strcmp(principal, kt_principal) == 0) {
            found = true;
        }
        free(kt_principal);
        krberr = krb5_free_keytab_entry_contents(context, &entry);
        if (krberr) {
            /* This should never happen. The API docs for this function
             * specify only success for this function
             */
            DEBUG(1,("Could not free keytab entry contents\n"));
            /* This is non-fatal, so we'll continue here */
        }

        if (found) {
            break;
        }
    }

    krberr = krb5_kt_end_seq_get(context, keytab, &cursor);
    if (krberr) {
        DEBUG(0, ("Could not close keytab.\n"));
        sss_log(SSS_LOG_ERR, "Could not close keytab file [%s].",
                             keytab_name);
        return EIO;
    }

    if (!found) {
        DEBUG(0, ("Principal [%s] not found in keytab [%s]\n",
                  principal, keytab_name ? keytab_name : "default"));
        sss_log(SSS_LOG_ERR, "Error processing keytab file [%s]: "
                             "Principal [%s] was not found. "
                             "Unable to create GSSAPI-encrypted LDAP connection.",
                             keytab_name, principal);

        return EFAULT;
    }

    return EOK;
}


enum matching_mode {MODE_NORMAL, MODE_PREFIX, MODE_POSTFIX};
/**
 * We only have primary and instances stored separately, we need to
 * join them to one string and compare that string.
 *
 * @param ctx kerberos context
 * @param principal principal we want to match
 * @param pattern_primary primary part of the principal we want to
 *        perform matching against. It is possible to use * wildcard
 *        at the beginning or at the end of the string. If NULL, it
 *        will act as "*"
 * @param pattern_realm realm part of the principal we want to perform
 *        the matching against. If NULL, it will act as "*"
 */
static bool match_principal(krb5_context ctx,
                     krb5_principal principal,
                     const char *pattern_primary,
                     const char *pattern_realm)
{
    krb5_data *realm_data;
    char *primary = NULL;
    char *primary_str = NULL;
    int primary_str_len = 0;
    int tmp_len;
    int len_diff;

    int mode = MODE_NORMAL;
    TALLOC_CTX *tmp_ctx;
    bool ret = false;

    realm_data = krb5_princ_realm(ctx, principal);

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) {
        DEBUG(1, ("talloc_new failed\n"));
        return false;
    }

    if (pattern_primary) {
        tmp_len = strlen(pattern_primary);
        if (pattern_primary[tmp_len] == '*') {
            mode = MODE_PREFIX;
            primary_str = talloc_strdup(tmp_ctx, pattern_primary);
            primary_str[tmp_len] = '\0';
            primary_str_len = tmp_len-1;
        } else if (pattern_primary[0] == '*') {
            mode = MODE_POSTFIX;
            primary_str = talloc_strdup(tmp_ctx, pattern_primary+1);
            primary_str_len = tmp_len-1;
        }

        sss_krb5_unparse_name_flags(ctx, principal, KRB5_PRINCIPAL_UNPARSE_NO_REALM,
                                    &primary);

        len_diff = strlen(primary)-primary_str_len;

        if ((mode == MODE_NORMAL &&
                strcmp(primary, pattern_primary) != 0) ||
            (mode == MODE_PREFIX &&
                strncmp(primary, primary_str, primary_str_len) != 0) ||
            (mode == MODE_POSTFIX &&
                strcmp(primary+len_diff, primary_str) != 0)) {
            goto done;
        }
    }

    if (!pattern_realm || (realm_data->length == strlen(pattern_realm) &&
        strncmp(realm_data->data, pattern_realm, realm_data->length) == 0)) {
        DEBUG(7, ("Principal matched to the sample (%s@%s).\n", pattern_primary,
                                                                pattern_realm));
        ret = true;
    }

done:
    free(primary);
    talloc_free(tmp_ctx);
    return ret;
}

krb5_error_code find_principal_in_keytab(krb5_context ctx,
                                         krb5_keytab keytab,
                                         const char *pattern_primary,
                                         const char *pattern_realm,
                                         krb5_principal *princ)
{
    krb5_error_code kerr;
    krb5_error_code kt_err;
    krb5_error_code kerr_d;
    krb5_kt_cursor cursor;
    krb5_keytab_entry entry;
    bool principal_found = false;

    memset(&cursor, 0, sizeof(cursor));
    kerr = krb5_kt_start_seq_get(ctx, keytab, &cursor);
    if (kerr != 0) {
        DEBUG(1, ("krb5_kt_start_seq_get failed.\n"));
        return kerr;
    }

    DEBUG(9, ("Trying to find principal %s@%s in keytab.\n", pattern_primary, pattern_realm));
    memset(&entry, 0, sizeof(entry));
    while ((kt_err = krb5_kt_next_entry(ctx, keytab, &entry, &cursor)) == 0) {
        principal_found = match_principal(ctx, entry.principal, pattern_primary, pattern_realm);
        if (principal_found) {
            break;
        }

        kerr = krb5_free_keytab_entry_contents(ctx, &entry);
        if (kerr != 0) {
            DEBUG(1, ("Failed to free keytab entry.\n"));
        }
        memset(&entry, 0, sizeof(entry));
    }

    /* Close the keytab here.  Even though we're using cursors, the file
     * handle is stored in the krb5_keytab structure, and it gets
     * overwritten by other keytab calls, creating a leak. */
    kerr = krb5_kt_end_seq_get(ctx, keytab, &cursor);
    if (kerr != 0) {
        DEBUG(1, ("krb5_kt_end_seq_get failed.\n"));
        goto done;
    }

    if (!principal_found) {
        kerr = KRB5_KT_NOTFOUND;
        DEBUG(1, ("No principal matching %s@%s found in keytab.\n",
                  pattern_primary, pattern_realm));
        goto done;
    }

    /* check if we got any errors from krb5_kt_next_entry */
    if (kt_err != 0 && kt_err != KRB5_KT_END) {
        DEBUG(1, ("Error while reading keytab.\n"));
        goto done;
    }

    kerr = krb5_copy_principal(ctx, entry.principal, princ);
    if (kerr != 0) {
        DEBUG(1, ("krb5_copy_principal failed.\n"));
        goto done;
    }

    kerr = 0;

done:
    kerr_d = krb5_free_keytab_entry_contents(ctx, &entry);
    if (kerr_d != 0) {
        DEBUG(1, ("Failed to free keytab entry.\n"));
    }

    return kerr;
}

const char *KRB5_CALLCONV sss_krb5_get_error_message(krb5_context ctx,
                                               krb5_error_code ec)
{
#ifdef HAVE_KRB5_GET_ERROR_MESSAGE
    return krb5_get_error_message(ctx, ec);
#else
    int ret;
    char *s = NULL;
    int size = sizeof("Kerberos error [XXXXXXXXXXXX]");

    s = malloc(sizeof(char) * (size));
    if (s == NULL) {
        return NULL;
    }

    ret = snprintf(s, size, "Kerberos error [%12d]", ec);

    if (ret < 0 || ret >= size) {
        return NULL;
    }

    return s;
#endif
}

void KRB5_CALLCONV sss_krb5_free_error_message(krb5_context ctx, const char *s)
{
#ifdef HAVE_KRB5_GET_ERROR_MESSAGE
    krb5_free_error_message(ctx, s);
#else
    free(s);
#endif

    return;
}

krb5_error_code KRB5_CALLCONV sss_krb5_get_init_creds_opt_alloc(
                                                  krb5_context context,
                                                  krb5_get_init_creds_opt **opt)
{
#ifdef HAVE_KRB5_GET_INIT_CREDS_OPT_ALLOC
    return krb5_get_init_creds_opt_alloc(context, opt);
#else
    *opt = calloc(1, sizeof(krb5_get_init_creds_opt));
    if (*opt == NULL) {
        return ENOMEM;
    }
    krb5_get_init_creds_opt_init(*opt);

    return 0;
#endif
}

void KRB5_CALLCONV sss_krb5_get_init_creds_opt_free (krb5_context context,
                                                   krb5_get_init_creds_opt *opt)
{
#ifdef HAVE_KRB5_GET_INIT_CREDS_OPT_ALLOC
    krb5_get_init_creds_opt_free(context, opt);
#else
    free(opt);
#endif

    return;
}

void KRB5_CALLCONV sss_krb5_free_unparsed_name(krb5_context context, char *name)
{
#ifdef HAVE_KRB5_FREE_UNPARSED_NAME
    krb5_free_unparsed_name(context, name);
#else
    if (name != NULL) {
        memset(name, 0, strlen(name));
        free(name);
    }
#endif
}


krb5_error_code check_for_valid_tgt(const char *ccname, const char *realm,
                                    const char *client_princ_str, bool *result)
{
    krb5_context context = NULL;
    krb5_ccache ccache = NULL;
    krb5_error_code krberr;
    TALLOC_CTX *tmp_ctx = NULL;
    krb5_creds mcred;
    krb5_creds cred;
    char *server_name = NULL;
    krb5_principal client_principal = NULL;
    krb5_principal server_principal = NULL;

    *result = false;

    tmp_ctx = talloc_new(NULL);
    if (tmp_ctx == NULL) {
        DEBUG(1, ("talloc_new failed.\n"));
        return ENOMEM;
    }

    krberr = krb5_init_context(&context);
    if (krberr) {
        DEBUG(1, ("Failed to init kerberos context\n"));
        goto done;
    }

    krberr = krb5_cc_resolve(context, ccname, &ccache);
    if (krberr != 0) {
        DEBUG(1, ("krb5_cc_resolve failed.\n"));
        goto done;
    }

    server_name = talloc_asprintf(tmp_ctx, "krbtgt/%s@%s", realm, realm);
    if (server_name == NULL) {
        DEBUG(1, ("talloc_asprintf failed.\n"));
        krberr = ENOMEM;
        goto done;
    }

    krberr = krb5_parse_name(context, server_name, &server_principal);
    if (krberr != 0) {
        DEBUG(1, ("krb5_parse_name failed.\n"));
        goto done;
    }

    krberr = krb5_parse_name(context, client_princ_str, &client_principal);
    if (krberr != 0) {
        DEBUG(1, ("krb5_parse_name failed.\n"));
        goto done;
    }

    memset(&mcred, 0, sizeof(mcred));
    memset(&cred, 0, sizeof(mcred));
    mcred.client = client_principal;
    mcred.server = server_principal;

    krberr = krb5_cc_retrieve_cred(context, ccache, 0, &mcred, &cred);
    if (krberr != 0) {
        DEBUG(1, ("krb5_cc_retrieve_cred failed.\n"));
        krberr = 0;
        goto done;
    }

    DEBUG(7, ("TGT end time [%d].\n", cred.times.endtime));

    if (cred.times.endtime > time(NULL)) {
        DEBUG(3, ("TGT is valid.\n"));
        *result = true;
    }
    krb5_free_cred_contents(context, &cred);

    krberr = 0;

done:
    if (client_principal != NULL) {
        krb5_free_principal(context, client_principal);
    }
    if (server_principal != NULL) {
        krb5_free_principal(context, server_principal);
    }
    if (ccache != NULL) {
        krb5_cc_close(context, ccache);
    }
    if (context != NULL) krb5_free_context(context);
    talloc_free(tmp_ctx);
    return krberr;
}

krb5_error_code KRB5_CALLCONV sss_krb5_get_init_creds_opt_set_expire_callback(
                                                   krb5_context context,
                                                   krb5_get_init_creds_opt *opt,
                                                   krb5_expire_callback_func cb,
                                                   void *data)
{
#ifdef HAVE_KRB5_GET_INIT_CREDS_OPT_SET_EXPIRE_CALLBACK
    return krb5_get_init_creds_opt_set_expire_callback(context, opt, cb, data);
#else
    DEBUG(5, ("krb5_get_init_creds_opt_set_expire_callback not available.\n"));
    return 0;
#endif
}

errno_t check_fast(const char *str, bool *use_fast)
{
#if HAVE_KRB5_GET_INIT_CREDS_OPT_SET_FAST_FLAGS
    if (strcasecmp(str, "never") == 0 ) {
        *use_fast = false;
    } else if (strcasecmp(str, "try") == 0 || strcasecmp(str, "demand") == 0) {
        *use_fast = true;
    } else {
        sss_log(SSS_LOG_ALERT, "Unsupported value [%s] for option krb5_use_fast,"
                               "please use never, try, or demand.\n");
        return EINVAL;
    }

    return EOK;
#else
    sss_log(SSS_LOG_ALERT, "This build of sssd done not support FAST. "
                           "Please remove option krb5_use_fast.\n");
    return EINVAL;
#endif
}

krb5_error_code KRB5_CALLCONV sss_krb5_get_init_creds_opt_set_fast_ccache_name(
                                                   krb5_context context,
                                                   krb5_get_init_creds_opt *opt,
                                                   const char *fast_ccache_name)
{
#ifdef HAVE_KRB5_GET_INIT_CREDS_OPT_SET_FAST_CCACHE_NAME
    return krb5_get_init_creds_opt_set_fast_ccache_name(context, opt,
                                                        fast_ccache_name);
#else
    DEBUG(5, ("krb5_get_init_creds_opt_set_fast_ccache_name not available.\n"));
    return 0;
#endif
}

krb5_error_code KRB5_CALLCONV sss_krb5_get_init_creds_opt_set_fast_flags(
                                                   krb5_context context,
                                                   krb5_get_init_creds_opt *opt,
                                                   krb5_flags flags)
{
#ifdef HAVE_KRB5_GET_INIT_CREDS_OPT_SET_FAST_FLAGS
    return krb5_get_init_creds_opt_set_fast_flags(context, opt, flags);
#else
    DEBUG(5, ("krb5_get_init_creds_opt_set_fast_flags not available.\n"));
    return 0;
#endif
}


#ifndef HAVE_KRB5_UNPARSE_NAME_FLAGS
#ifndef REALM_SEP
#define REALM_SEP       '@'
#endif
#ifndef COMPONENT_SEP
#define COMPONENT_SEP   '/'
#endif

static int
sss_krb5_copy_component_quoting(char *dest, const krb5_data *src, int flags)
{
    int j;
    const char *cp = src->data;
    char *q = dest;
    int length = src->length;

    if (flags & KRB5_PRINCIPAL_UNPARSE_DISPLAY) {
        memcpy(dest, src->data, src->length);
        return src->length;
    }

    for (j=0; j < length; j++,cp++) {
        int no_realm = (flags & KRB5_PRINCIPAL_UNPARSE_NO_REALM) &&
            !(flags & KRB5_PRINCIPAL_UNPARSE_SHORT);

        switch (*cp) {
        case REALM_SEP:
            if (no_realm) {
                *q++ = *cp;
                break;
            }
        case COMPONENT_SEP:
        case '\\':
            *q++ = '\\';
            *q++ = *cp;
            break;
        case '\t':
            *q++ = '\\';
            *q++ = 't';
            break;
        case '\n':
            *q++ = '\\';
            *q++ = 'n';
            break;
        case '\b':
            *q++ = '\\';
            *q++ = 'b';
            break;
        case '\0':
            *q++ = '\\';
            *q++ = '0';
            break;
        default:
            *q++ = *cp;
        }
    }
    return q - dest;
}

static int
sss_krb5_component_length_quoted(const krb5_data *src, int flags)
{
    const char *cp = src->data;
    int length = src->length;
    int j;
    int size = length;

    if ((flags & KRB5_PRINCIPAL_UNPARSE_DISPLAY) == 0) {
        int no_realm = (flags & KRB5_PRINCIPAL_UNPARSE_NO_REALM) &&
            !(flags & KRB5_PRINCIPAL_UNPARSE_SHORT);

        for (j = 0; j < length; j++,cp++)
            if ((!no_realm && *cp == REALM_SEP) ||
                *cp == COMPONENT_SEP ||
                *cp == '\0' || *cp == '\\' || *cp == '\t' ||
                *cp == '\n' || *cp == '\b')
                size++;
    }

    return size;
}

#endif /* HAVE_KRB5_UNPARSE_NAME_FLAGS */


krb5_error_code
sss_krb5_unparse_name_flags(krb5_context context, krb5_const_principal principal,
                        int flags, char **name)
{
#ifdef HAVE_KRB5_UNPARSE_NAME_FLAGS
    return krb5_unparse_name_flags(context, principal, flags, name);
#else
    char *cp, *q;
    int i;
    int length;
    krb5_int32 nelem;
    unsigned int totalsize = 0;
    char *default_realm = NULL;
    krb5_error_code ret = 0;

    if (name != NULL)
        *name = NULL;

    if (!principal || !name)
        return KRB5_PARSE_MALFORMED;

    if (flags & KRB5_PRINCIPAL_UNPARSE_SHORT) {
        /* omit realm if local realm */
        krb5_principal_data p;

        ret = krb5_get_default_realm(context, &default_realm);
        if (ret != 0)
            goto cleanup;

        krb5_princ_realm(context, &p)->length = strlen(default_realm);
        krb5_princ_realm(context, &p)->data = default_realm;

        if (krb5_realm_compare(context, &p, principal))
            flags |= KRB5_PRINCIPAL_UNPARSE_NO_REALM;
    }

    if ((flags & KRB5_PRINCIPAL_UNPARSE_NO_REALM) == 0) {
        totalsize += sss_krb5_component_length_quoted(krb5_princ_realm(context,
                                                              principal),
                                             flags);
        totalsize++;
    }

    nelem = krb5_princ_size(context, principal);
    for (i = 0; i < (int) nelem; i++) {
        cp = krb5_princ_component(context, principal, i)->data;
        totalsize += sss_krb5_component_length_quoted(krb5_princ_component(context, principal, i), flags);
        totalsize++;
    }
    if (nelem == 0)
        totalsize++;

    *name = malloc(totalsize);

    if (!*name) {
        ret = ENOMEM;
        goto cleanup;
    }

    q = *name;

    for (i = 0; i < (int) nelem; i++) {
        cp = krb5_princ_component(context, principal, i)->data;
        length = krb5_princ_component(context, principal, i)->length;
        q += sss_krb5_copy_component_quoting(q,
                                    krb5_princ_component(context,
                                                         principal,
                                                         i),
                                    flags);
        *q++ = COMPONENT_SEP;
    }

    if (i > 0)
        q--;
    if ((flags & KRB5_PRINCIPAL_UNPARSE_NO_REALM) == 0) {
        *q++ = REALM_SEP;
        q += sss_krb5_copy_component_quoting(q, krb5_princ_realm(context, principal), flags);
    }
    *q++ = '\0';

cleanup:
    free(default_realm);

    return ret;
#endif /* HAVE_KRB5_UNPARSE_NAME_FLAGS */
}
