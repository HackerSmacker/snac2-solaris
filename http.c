/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_openssl.h"
#include "xs_curl.h"
#include "xs_time.h"
#include "xs_json.h"

#include "snac.h"

xs_dict *http_signed_request_raw(const char *keyid, const char *seckey,
                            const char *method, const char *url,
                            const xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout)
/* does a signed HTTP request */
{
    xs *l1 = NULL;
    xs *date = NULL;
    xs *digest = NULL;
    xs *s64 = NULL;
    xs *signature = NULL;
    xs *hdrs = NULL;
    const char *host;
    const char *target;
    const char *k, *v;
    xs_dict *response;

    date = xs_str_utctime(0, "%a, %d %b %Y %H:%M:%S GMT");

    {
        xs *s1 = xs_replace_n(url, "http:/" "/", "", 1);
        xs *s = xs_replace_n(s1, "https:/" "/", "", 1);
        l1 = xs_split_n(s, "/", 1);
    }

    /* strip the url to get host and target */
    host = xs_list_get(l1, 0);

    if (xs_list_len(l1) == 2)
        target = xs_list_get(l1, 1);
    else
        target = "";

    /* digest */
    {
        xs *s;

        if (body != NULL)
            s = xs_sha256_base64(body, b_size);
        else
            s = xs_sha256_base64("", 0);

        digest = xs_fmt("SHA-256=%s", s);
    }

    {
        /* build the string to be signed */
        xs *s = xs_fmt("(request-target): %s /%s\n"
                       "host: %s\n"
                       "digest: %s\n"
                       "date: %s",
                    strcmp(method, "POST") == 0 ? "post" : "get",
                    target, host, digest, date);

        s64 = xs_evp_sign(seckey, s, strlen(s));
    }

    /* build now the signature header */
    signature = xs_fmt("keyId=\"%s#main-key\","
                       "algorithm=\"rsa-sha256\","
                       "headers=\"(request-target) host digest date\","
                       "signature=\"%s\"",
                        keyid, s64);

    /* transfer the original headers */
    hdrs = xs_dict_new();
    int c = 0;
    while (xs_dict_next(headers, &k, &v, &c))
        hdrs = xs_dict_append(hdrs, k, v);

    /* add the new headers */
    if (strcmp(method, "POST") == 0)
        hdrs = xs_dict_append(hdrs, "content-type", "application/activity+json");
    else
        hdrs = xs_dict_append(hdrs, "accept",       "application/activity+json");

    xs *user_agent = xs_fmt("%s; +%s/", USER_AGENT, srv_baseurl);

    hdrs = xs_dict_append(hdrs, "date",         date);
    hdrs = xs_dict_append(hdrs, "signature",    signature);
    hdrs = xs_dict_append(hdrs, "digest",       digest);
    hdrs = xs_dict_append(hdrs, "host",         host);
    hdrs = xs_dict_append(hdrs, "user-agent",   user_agent);

    response = xs_http_request(method, url, hdrs,
                           body, b_size, status, payload, p_size, timeout);

    srv_archive("SEND", url, hdrs, body, b_size, *status, response, *payload, *p_size);

    return response;
}


xs_dict *http_signed_request(snac *snac, const char *method, const char *url,
                            const xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout)
/* does a signed HTTP request */
{
    const char *seckey = xs_dict_get(snac->key, "secret");
    xs_dict *response;

    response = http_signed_request_raw(snac->actor, seckey, method, url,
                headers, body, b_size, status, payload, p_size, timeout);

    return response;
}


int check_signature(const xs_dict *req, xs_str **err)
/* check the signature */
{
    const char *sig_hdr = xs_dict_get(req, "signature");
    xs *keyId = NULL;
    xs *headers = NULL;
    xs *signature = NULL;
    xs *created = NULL;
    xs *expires = NULL;
    char *p;
    const char *pubkey;
    const char *k;

    if (xs_is_null(sig_hdr)) {
        *err = xs_fmt("missing 'signature' header");
        return 0;
    }

    {
        /* extract the values */
        xs *l = xs_split(sig_hdr, ",");
        int c = 0;
        const xs_val *v;

        while (xs_list_next(l, &v, &c)) {
            xs *kv = xs_split_n(v, "=", 1);

            if (xs_list_len(kv) != 2)
                continue;

            xs *k1 = xs_strip_i(xs_dup(xs_list_get(kv, 0)));
            xs *v1 = xs_strip_chars_i(xs_dup(xs_list_get(kv, 1)), " \"");

            if (!strcmp(k1, "keyId"))
                keyId = xs_dup(v1);
            else
            if (!strcmp(k1, "headers"))
                headers = xs_dup(v1);
            else
            if (!strcmp(k1, "signature"))
                signature = xs_dup(v1);
            else
            if (!strcmp(k1, "created"))
                created = xs_dup(v1);
            else
            if (!strcmp(k1, "expires"))
                expires = xs_dup(v1);
        }
    }

    if (keyId == NULL || headers == NULL || signature == NULL) {
        *err = xs_fmt("bad signature header");
        return 0;
    }

    /* strip the # from the keyId */
    if ((p = strchr(keyId, '#')) != NULL)
        *p = '\0';

    /* also strip cgi variables */
    if ((p = strchr(keyId, '?')) != NULL)
        *p = '\0';

    xs *actor = NULL;
    int status;

    if (!valid_status((status = actor_request(NULL, keyId, &actor)))) {
        *err = xs_fmt("actor request error %s %d", keyId, status);
        return 0;
    }

    if ((k = xs_dict_get(actor, "publicKey")) == NULL ||
        ((pubkey = xs_dict_get(k, "publicKeyPem")) == NULL)) {
        *err = xs_fmt("cannot get pubkey from %s", keyId);
        return 0;
    }

    /* now build the string to be signed */
    xs *sig_str = xs_str_new(NULL);

    {
        xs *l = xs_split(headers, " ");
        xs_list *p;
        const xs_val *v;

        p = l;
        while (xs_list_iter(&p, &v)) {
            const char *hc;
            xs *ss = NULL;

            if (*sig_str != '\0')
                sig_str = xs_str_cat(sig_str, "\n");

            if (strcmp(v, "(request-target)") == 0) {
                ss = xs_fmt("%s: post %s", v, xs_dict_get(req, "path"));
            }
            else
            if (strcmp(v, "(created)") == 0) {
                ss = xs_fmt("%s: %s", v, created);
            }
            else
            if (strcmp(v, "(expires)") == 0) {
                ss = xs_fmt("%s: %s", v, expires);
            }
            else
            if (strcmp(v, "host") == 0) {
                hc = xs_dict_get(req, "host");

                /* if there is no host header or some garbage like
                   address:host has arrived here due to misconfiguration,
                   signature verify will totally fail, so let's Leroy Jenkins
                   with the global server hostname instead */
                if (hc == NULL || xs_str_in(hc, ":") != -1)
                    hc = xs_dict_get(srv_config, "host");

                ss = xs_fmt("host: %s", hc);
            }
            else {
                /* add the header */
                if ((hc = xs_dict_get(req, v)) == NULL) {
                    *err = xs_fmt("cannot find header '%s'", v);
                    return 0;
                }

                ss = xs_fmt("%s: %s", v, hc);
            }

            sig_str = xs_str_cat(sig_str, ss);
        }
    }

    if (xs_evp_verify(pubkey, sig_str, strlen(sig_str), signature) != 1) {
        *err = xs_fmt("RSA verify error %s", keyId);
        return 0;
    }

    return 1;
}
