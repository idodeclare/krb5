/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/krb/get_creds.c
 *
 * Copyright 1990, 2008 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 * krb5_get_credentials()
 */



/*
  Attempts to use the credentials cache or TGS exchange to get an additional
  ticket for the
  client identified by in_creds->client, the server identified by
  in_creds->server, with options options, expiration date specified in
  in_creds->times.endtime (0 means as long as possible), session key type
  specified in in_creds->keyblock.enctype (if non-zero)

  Any returned ticket and intermediate ticket-granting tickets are
  stored in ccache.

  returns errors from encryption routines, system errors
*/

#include "k5-int.h"
#include "int-proto.h"

/* Using the krb5_tkt_creds interface, get credentials matching in_creds from
 * the KDC using the credentials in ccache. */
static krb5_error_code
get_tkt_creds(krb5_context context, krb5_ccache ccache, krb5_creds *in_creds,
              int kdcopt, krb5_creds *creds)
{
    krb5_error_code retval;
    krb5_tkt_creds_context ctx = NULL;

    retval = krb5_tkt_creds_init(context, ccache, in_creds, kdcopt, &ctx);
    if (retval != 0)
        goto cleanup;
    retval = krb5_tkt_creds_get(context, ctx);
    if (retval != 0)
        goto cleanup;
    retval = krb5_tkt_creds_get_creds(context, ctx, creds);

cleanup:
    krb5_tkt_creds_free(context, ctx);
    return retval;
}

/*
 * Set *mcreds and *fields to a matching credential and field set for
 * use with krb5_cc_retrieve_cred, based on a set of input credentials
 * and options.  The fields of *mcreds will be aliased to the fields
 * of in_creds, so the contents of *mcreds should not be freed.
 */
krb5_error_code
krb5int_construct_matching_creds(krb5_context context, krb5_flags options,
                                 krb5_creds *in_creds, krb5_creds *mcreds,
                                 krb5_flags *fields)
{
    if (!in_creds || !in_creds->server || !in_creds->client)
        return EINVAL;

    memset(mcreds, 0, sizeof(krb5_creds));
    mcreds->magic = KV5M_CREDS;
    if (in_creds->times.endtime != 0) {
        mcreds->times.endtime = in_creds->times.endtime;
    } else {
        krb5_error_code retval;
        retval = krb5_timeofday(context, &mcreds->times.endtime);
        if (retval != 0) return retval;
    }
    mcreds->keyblock = in_creds->keyblock;
    mcreds->authdata = in_creds->authdata;
    mcreds->server = in_creds->server;
    mcreds->client = in_creds->client;

    *fields = KRB5_TC_MATCH_TIMES /*XXX |KRB5_TC_MATCH_SKEY_TYPE */
        | KRB5_TC_MATCH_AUTHDATA
        | KRB5_TC_SUPPORTED_KTYPES;
    if (mcreds->keyblock.enctype) {
        krb5_enctype *ktypes;
        krb5_error_code ret;
        int i;

        *fields |= KRB5_TC_MATCH_KTYPE;
        ret = krb5_get_tgs_ktypes(context, mcreds->server, &ktypes);
        for (i = 0; ktypes[i]; i++)
            if (ktypes[i] == mcreds->keyblock.enctype)
                break;
        if (ktypes[i] == 0)
            ret = KRB5_CC_NOT_KTYPE;
        free (ktypes);
        if (ret)
            return ret;
    }
    if (options & (KRB5_GC_USER_USER | KRB5_GC_CONSTRAINED_DELEGATION)) {
        /* also match on identical 2nd tkt and tkt encrypted in a
           session key */
        *fields |= KRB5_TC_MATCH_2ND_TKT;
        if (options & KRB5_GC_USER_USER) {
            *fields |= KRB5_TC_MATCH_IS_SKEY;
            mcreds->is_skey = TRUE;
        }
        mcreds->second_ticket = in_creds->second_ticket;
        if (!in_creds->second_ticket.length)
            return KRB5_NO_2ND_TKT;
    }

    return 0;
}

krb5_error_code KRB5_CALLCONV
krb5_get_credentials(krb5_context context, krb5_flags options,
                     krb5_ccache ccache, krb5_creds *in_creds,
                     krb5_creds **out_creds)
{
    krb5_error_code retval;
    krb5_creds mcreds, *ncreds = NULL;
    krb5_flags fields;
    krb5_boolean not_ktype = FALSE;
    int kdcopt = 0;

    *out_creds = NULL;

    ncreds = k5alloc(sizeof(*ncreds), &retval);
    if (ncreds == NULL)
        goto cleanup;

    /*
     * See if we already have the ticket cached. To do this usefully
     * for constrained delegation, we would need to look inside
     * second_ticket, which we can't do.
     */
    if ((options & KRB5_GC_CONSTRAINED_DELEGATION) == 0) {
        retval = krb5int_construct_matching_creds(context, options, in_creds,
                                                  &mcreds, &fields);

        if (retval)
            goto cleanup;

        retval = krb5_cc_retrieve_cred(context, ccache, fields, &mcreds,
                                       ncreds);
        if (retval == 0) {
            *out_creds = ncreds;
            return 0;
        }
        if ((retval != KRB5_CC_NOTFOUND && retval != KRB5_CC_NOT_KTYPE)
            || options & KRB5_GC_CACHED)
            goto cleanup;
        not_ktype = (retval == KRB5_CC_NOT_KTYPE);
    } else if (options & KRB5_GC_CACHED) {
        retval = KRB5_CC_NOTFOUND;
        goto cleanup;
    }

    if (options & KRB5_GC_CANONICALIZE)
        kdcopt |= KDC_OPT_CANONICALIZE;
    if (options & KRB5_GC_FORWARDABLE)
        kdcopt |= KDC_OPT_FORWARDABLE;
    if (options & KRB5_GC_NO_TRANSIT_CHECK)
        kdcopt |= KDC_OPT_DISABLE_TRANSITED_CHECK;
    if (options & KRB5_GC_CONSTRAINED_DELEGATION) {
        if (options & KRB5_GC_USER_USER) {
            retval = EINVAL;
            goto cleanup;

        }
        kdcopt |= KDC_OPT_FORWARDABLE | KDC_OPT_CNAME_IN_ADDL_TKT;
    }

    /* Get the credential from the KDC. */
    retval = get_tkt_creds(context, ccache, in_creds, kdcopt, ncreds);
    if (retval != 0)
        goto cleanup;

    /* Attempt to cache the returned ticket. */
    if (!(options & KRB5_GC_NO_STORE))
        (void) krb5_cc_store_cred(context, ccache, ncreds);

    if ((options & KRB5_GC_CONSTRAINED_DELEGATION)
        && (ncreds->ticket_flags & TKT_FLG_FORWARDABLE) == 0) {
        /* This ticket won't work for constrained delegation. */
        retval = KRB5_TKT_NOT_FORWARDABLE;
        goto cleanup;
    }

    /*
     * Translate KRB5_CC_NOTFOUND if we previously got KRB5_CC_NOT_KTYPE from
     * krb5_cc_retrieve_cred(), in order to handle the case where there is no
     * TGT in the ccache and the input enctype didn't match.  This handling is
     * necessary because some callers, such as GSSAPI, iterate through enctypes
     * and KRB5_CC_NOTFOUND passed through from get_tkt_creds() is semantically
     * incorrect, since the actual failure was the non-existence of a ticket of
     * the correct enctype rather than the missing TGT.
     */
    if (retval == KRB5_CC_NOTFOUND && not_ktype)
        retval = KRB5_CC_NOT_KTYPE;

    *out_creds = ncreds;
    ncreds = NULL;

cleanup:
    krb5_free_creds(context, ncreds);
    return retval;
}

static krb5_error_code
get_credentials_val_renew_core(krb5_context context, krb5_flags options,
                               krb5_ccache ccache, krb5_creds *in_creds,
                               krb5_creds **out_creds, int kdcopt)
{
    krb5_error_code retval;
    krb5_principal tmp;
    krb5_creds *ncreds = NULL;

    *out_creds = NULL;

    /* Get the validated or renewed credential from the KDC. */
    ncreds = k5alloc(sizeof(*ncreds), &retval);
    if (ncreds == NULL)
        goto cleanup;
    retval = get_tkt_creds(context, ccache, in_creds, kdcopt, ncreds);
    if (retval != 0)
        goto cleanup;

    /* Reinitialize the cache with the new credential. */
    retval = krb5_cc_get_principal(context, ccache, &tmp);
    if (retval != 0)
        goto cleanup;
    retval = krb5_cc_initialize(context, ccache, tmp);
    if (retval != 0)
        goto cleanup;
    retval = krb5_cc_store_cred(context, ccache, ncreds);
    if (retval != 0)
        goto cleanup;

    *out_creds = ncreds;
    ncreds = NULL;

cleanup:
    krb5_free_creds(context, ncreds);
    return retval;
}

krb5_error_code KRB5_CALLCONV
krb5_get_credentials_validate(krb5_context context, krb5_flags options,
                              krb5_ccache ccache, krb5_creds *in_creds,
                              krb5_creds **out_creds)
{
    return(get_credentials_val_renew_core(context, options, ccache,
                                          in_creds, out_creds,
                                          KDC_OPT_VALIDATE));
}

krb5_error_code KRB5_CALLCONV
krb5_get_credentials_renew(krb5_context context, krb5_flags options,
                           krb5_ccache ccache, krb5_creds *in_creds,
                           krb5_creds **out_creds)
{

    return(get_credentials_val_renew_core(context, options, ccache,
                                          in_creds, out_creds,
                                          KDC_OPT_RENEW));
}

static krb5_error_code
validate_or_renew_creds(krb5_context context, krb5_creds *creds,
                        krb5_principal client, krb5_ccache ccache,
                        char *in_tkt_service, int kdcopt)
{
    krb5_error_code ret;
    krb5_creds in_creds; /* only client and server need to be filled in */

    memset(&in_creds, 0, sizeof(krb5_creds));
    in_creds.server = NULL;
    in_creds.client = client;

    if (in_tkt_service) {
        /* this is ugly, because so are the data structures involved.  I'm
           in the library, so I'm going to manipulate the data structures
           directly, otherwise, it will be worse. */

        if ((ret = krb5_parse_name(context, in_tkt_service, &in_creds.server)))
            goto cleanup;

        /* stuff the client realm into the server principal.
           realloc if necessary */
        if (in_creds.server->realm.length < in_creds.client->realm.length)
            if ((in_creds.server->realm.data =
                 (char *) realloc(in_creds.server->realm.data,
                                  in_creds.client->realm.length)) == NULL) {
                ret = ENOMEM;
                goto cleanup;
            }

        in_creds.server->realm.length = in_creds.client->realm.length;
        memcpy(in_creds.server->realm.data, in_creds.client->realm.data,
               in_creds.client->realm.length);
    } else {
        if ((ret = krb5_build_principal_ext(context, &in_creds.server,
                                            in_creds.client->realm.length,
                                            in_creds.client->realm.data,
                                            KRB5_TGS_NAME_SIZE,
                                            KRB5_TGS_NAME,
                                            in_creds.client->realm.length,
                                            in_creds.client->realm.data,
                                            0)))
            goto cleanup;
    }

    /* Get the validated or renewed credential from the KDC. */
    ret = get_tkt_creds(context, ccache, &in_creds, kdcopt, creds);

cleanup:
    krb5_free_principal(context, in_creds.server);
    return ret;
}

krb5_error_code KRB5_CALLCONV
krb5_get_validated_creds(krb5_context context, krb5_creds *creds, krb5_principal client, krb5_ccache ccache, char *in_tkt_service)
{
    return validate_or_renew_creds(context, creds, client, ccache,
                                   in_tkt_service, KDC_OPT_VALIDATE);
}

krb5_error_code KRB5_CALLCONV
krb5_get_renewed_creds(krb5_context context, krb5_creds *creds, krb5_principal client, krb5_ccache ccache, char *in_tkt_service)
{
    return validate_or_renew_creds(context, creds, client, ccache,
                                   in_tkt_service, KDC_OPT_RENEW);
}
