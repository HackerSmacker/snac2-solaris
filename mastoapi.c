/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef NO_MASTODON_API

#include "xs.h"
#include "xs_hex.h"
#include "xs_openssl.h"
#include "xs_json.h"
#include "xs_io.h"
#include "xs_time.h"
#include "xs_glob.h"
#include "xs_set.h"
#include "xs_random.h"
#include "xs_url.h"
#include "xs_mime.h"
#include "xs_match.h"

#include "snac.h"

#include <sys/time.h>

static xs_str *random_str(void)
/* just what is says in the tin */
{
    unsigned int data[4] = {0};

    xs_rnd_buf(data, sizeof(data));
    return xs_hex_enc((char *)data, sizeof(data));
}


int app_add(const char *id, const xs_dict *app)
/* stores an app */
{
    if (!xs_is_hex(id))
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;

    int status = HTTP_STATUS_CREATED;
    xs *fn     = xs_fmt("%s/app/", srv_basedir);
    FILE *f;

    mkdirx(fn);
    fn = xs_str_cat(fn, id);
    fn = xs_str_cat(fn, ".json");

    if ((f = fopen(fn, "w")) != NULL) {
        xs_json_dump(app, 4, f);
        fclose(f);
    }
    else
        status = HTTP_STATUS_INTERNAL_SERVER_ERROR;

    return status;
}


xs_str *_app_fn(const char *id)
{
    return xs_fmt("%s/app/%s.json", srv_basedir, id);
}


xs_dict *app_get(const char *id)
/* gets an app */
{
    if (!xs_is_hex(id))
        return NULL;

    xs *fn       = _app_fn(id);
    xs_dict *app = NULL;
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        app = xs_json_load(f);
        fclose(f);

    }

    return app;
}


int app_del(const char *id)
/* deletes an app */
{
    if (!xs_is_hex(id))
        return -1;

    xs *fn = _app_fn(id);

    return unlink(fn);
}


int token_add(const char *id, const xs_dict *token)
/* stores a token */
{
    if (!xs_is_hex(id))
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;

    int status = HTTP_STATUS_CREATED;
    xs *fn     = xs_fmt("%s/token/", srv_basedir);
    FILE *f;

    mkdirx(fn);
    fn = xs_str_cat(fn, id);
    fn = xs_str_cat(fn, ".json");

    if ((f = fopen(fn, "w")) != NULL) {
        xs_json_dump(token, 4, f);
        fclose(f);
    }
    else
        status = HTTP_STATUS_INTERNAL_SERVER_ERROR;

    return status;
}


xs_dict *token_get(const char *id)
/* gets a token */
{
    if (!xs_is_hex(id))
        return NULL;

    xs *fn         = xs_fmt("%s/token/%s.json", srv_basedir, id);
    xs_dict *token = NULL;
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        token = xs_json_load(f);
        fclose(f);

        /* 'touch' the file */
        utimes(fn, NULL);

        /* also 'touch' the app */
        const char *app_id = xs_dict_get(token, "client_id");

        if (app_id) {
            xs *afn = xs_fmt("%s/app/%s.json", srv_basedir, app_id);
            utimes(afn, NULL);
        }
    }

    return token;
}


int token_del(const char *id)
/* deletes a token */
{
    if (!xs_is_hex(id))
        return -1;

    xs *fn = xs_fmt("%s/token/%s.json", srv_basedir, id);

    return unlink(fn);
}


const char *login_page = ""
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"<title>%s OAuth - Snac2</title>\n"
"<meta content=\"width=device-width, initial-scale=1, minimum-scale=1, user-scalable=no\" name=\"viewport\">"
"<style>:root {color-scheme: light dark}</style>\n"
"</head>\n"
"<body><h1>%s OAuth identify</h1>\n"
"<div style=\"background-color: red; color: white\">%s</div>\n"
"<form method=\"post\" action=\"%s:/" "/%s/%s\">\n"
"<p>Login: <input type=\"text\" name=\"login\" autocapitalize=\"off\"></p>\n"
"<p>Password: <input type=\"password\" name=\"passwd\"></p>\n"
"<input type=\"hidden\" name=\"redir\" value=\"%s\">\n"
"<input type=\"hidden\" name=\"cid\" value=\"%s\">\n"
"<input type=\"hidden\" name=\"state\" value=\"%s\">\n"
"<input type=\"submit\" value=\"OK\">\n"
"</form><p>%s</p></body></html>\n"
"";

int oauth_get_handler(const xs_dict *req, const char *q_path,
                      char **body, int *b_size, char **ctype)
{
    (void)b_size;

    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

    int status   = HTTP_STATUS_NOT_FOUND;
    const xs_dict *msg = xs_dict_get(req, "q_vars");
    xs *cmd      = xs_replace_n(q_path, "/oauth", "", 1);

    srv_debug(1, xs_fmt("oauth_get_handler %s", q_path));

    if (strcmp(cmd, "/authorize") == 0) { /** **/
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");
        const char *rtype = xs_dict_get(msg, "response_type");
        const char *state = xs_dict_get(msg, "state");

        status = HTTP_STATUS_BAD_REQUEST;

        if (cid && ruri && rtype && strcmp(rtype, "code") == 0) {
            xs *app = app_get(cid);

            if (app != NULL) {
                const char *host = xs_dict_get(srv_config, "host");
                const char *proto = xs_dict_get_def(srv_config, "protocol", "https");

                if (xs_is_null(state))
                    state = "";

                *body  = xs_fmt(login_page, host, host, "", proto, host, "oauth/x-snac-login",
                                ruri, cid, state, USER_AGENT);
                *ctype = "text/html";
                status = HTTP_STATUS_OK;

                srv_debug(1, xs_fmt("oauth authorize: generating login page"));
            }
            else
                srv_debug(1, xs_fmt("oauth authorize: bad client_id %s", cid));
        }
        else
            srv_debug(1, xs_fmt("oauth authorize: invalid or unset arguments"));
    }
    else
    if (strcmp(cmd, "/x-snac-get-token") == 0) { /** **/
        const char *host = xs_dict_get(srv_config, "host");
        const char *proto = xs_dict_get_def(srv_config, "protocol", "https");

        *body  = xs_fmt(login_page, host, host, "", proto, host, "oauth/x-snac-get-token",
                        "", "", "", USER_AGENT);
        *ctype = "text/html";
        status = HTTP_STATUS_OK;

    }

    return status;
}


int oauth_post_handler(const xs_dict *req, const char *q_path,
                       const char *payload, int p_size,
                       char **body, int *b_size, char **ctype)
{
    (void)p_size;
    (void)b_size;

    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

    int status   = HTTP_STATUS_NOT_FOUND;

    const char *i_ctype = xs_dict_get(req, "content-type");
    xs *args      = NULL;

    if (i_ctype && xs_startswith(i_ctype, "application/json")) {
        if (!xs_is_null(payload))
            args = xs_json_loads(payload);
    }
    else
    if (i_ctype && xs_startswith(i_ctype, "application/x-www-form-urlencoded") && payload) {
        args    = xs_url_vars(payload);
    }
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    if (args == NULL)
        return HTTP_STATUS_BAD_REQUEST;

    xs *cmd = xs_replace_n(q_path, "/oauth", "", 1);

    srv_debug(1, xs_fmt("oauth_post_handler %s", q_path));

    if (strcmp(cmd, "/x-snac-login") == 0) { /** **/
        const char *login  = xs_dict_get(args, "login");
        const char *passwd = xs_dict_get(args, "passwd");
        const char *redir  = xs_dict_get(args, "redir");
        const char *cid    = xs_dict_get(args, "cid");
        const char *state  = xs_dict_get(args, "state");
        const char *host   = xs_dict_get(srv_config, "host");
        const char *proto = xs_dict_get_def(srv_config, "protocol", "https");

        /* by default, generate another login form with an error */
        *body  = xs_fmt(login_page, host, host, "LOGIN INCORRECT", proto, host, "oauth/x-snac-login",
                        redir, cid, state, USER_AGENT);
        *ctype = "text/html";
        status = HTTP_STATUS_OK;

        if (login && passwd && redir && cid) {
            snac snac;

            if (user_open(&snac, login)) {
                /* check the login + password */
                if (check_password(login, passwd, xs_dict_get(snac.config, "passwd"))) {
                    /* success! redirect to the desired uri */
                    xs *code = random_str();

                    xs_free(*body);

                    if (strcmp(redir, "urn:ietf:wg:oauth:2.0:oob") == 0) {
                        *body = xs_dup(code);
                    }
                    else {
                        if (xs_str_in(redir, "?") != -1)
                            *body = xs_fmt("%s&code=%s", redir, code);
                        else
                            *body = xs_fmt("%s?code=%s", redir, code);

                        status = HTTP_STATUS_SEE_OTHER;
                    }

                    /* if there is a state, add it */
                    if (!xs_is_null(state) && *state) {
                        *body = xs_str_cat(*body, "&state=");
                        *body = xs_str_cat(*body, state);
                    }

                    srv_log(xs_fmt("oauth x-snac-login: '%s' success, redirect to %s",
                                   login, *body));

                    /* assign the login to the app */
                    xs *app = app_get(cid);

                    if (app != NULL) {
                        app = xs_dict_set(app, "uid",  login);
                        app = xs_dict_set(app, "code", code);
                        app_add(cid, app);
                    }
                    else
                        srv_log(xs_fmt("oauth x-snac-login: error getting app %s", cid));
                }
                else
                    srv_debug(1, xs_fmt("oauth x-snac-login: login '%s' incorrect", login));

                user_free(&snac);
            }
            else
                srv_debug(1, xs_fmt("oauth x-snac-login: bad user '%s'", login));
        }
        else
            srv_debug(1, xs_fmt("oauth x-snac-login: invalid or unset arguments"));
    }
    else
    if (strcmp(cmd, "/token") == 0) { /** **/
        xs *wrk = NULL;
        const char *gtype = xs_dict_get(args, "grant_type");
        const char *code  = xs_dict_get(args, "code");
        const char *cid   = xs_dict_get(args, "client_id");
        const char *csec  = xs_dict_get(args, "client_secret");
        const char *ruri  = xs_dict_get(args, "redirect_uri");
        const char *scope = xs_dict_get(args, "scope");

        /* no client_secret? check if it's inside an authorization header
           (AndStatus does it this way) */
        if (xs_is_null(csec)) {
            const char *auhdr = xs_dict_get(req, "authorization");

            if (!xs_is_null(auhdr) && xs_startswith(auhdr, "Basic ")) {
                xs *s1 = xs_replace_n(auhdr, "Basic ", "", 1);
                int size;
                xs *s2 = xs_base64_dec(s1, &size);

                if (!xs_is_null(s2)) {
                    xs *l1 = xs_split(s2, ":");

                    if (xs_list_len(l1) == 2) {
                        wrk = xs_dup(xs_list_get(l1, 1));
                        csec = wrk;
                    }
                }
            }
        }

        /* no code? 
           I'm not sure of the impacts of this right now, but Subway Tooter does not
           provide a code so one must be generated */
        if (xs_is_null(code)){
            code = random_str();
        }
        if (gtype && code && cid && csec && ruri) {
            xs *app = app_get(cid);

            if (app == NULL) {
                status = HTTP_STATUS_UNAUTHORIZED;
                srv_log(xs_fmt("oauth token: invalid app %s", cid));
            }
            else
            if (strcmp(csec, xs_dict_get(app, "client_secret")) != 0) {
                status = HTTP_STATUS_UNAUTHORIZED;
                srv_log(xs_fmt("oauth token: invalid client_secret for app %s", cid));
            }
            else {
                xs *rsp   = xs_dict_new();
                xs *cat   = xs_number_new(time(NULL));
                xs *tokid = random_str();

                rsp = xs_dict_append(rsp, "access_token", tokid);
                rsp = xs_dict_append(rsp, "token_type",   "Bearer");
                rsp = xs_dict_append(rsp, "created_at",   cat);

                if (!xs_is_null(scope))
                    rsp = xs_dict_append(rsp, "scope", scope);

                *body  = xs_json_dumps(rsp, 4);
                *ctype = "application/json";
                status = HTTP_STATUS_OK;

                const char *uid = xs_dict_get(app, "uid");

                srv_debug(1, xs_fmt("oauth token: "
                                "successful login for %s, new token %s", uid, tokid));

                xs *token = xs_dict_new();
                token = xs_dict_append(token, "token",         tokid);
                token = xs_dict_append(token, "client_id",     cid);
                token = xs_dict_append(token, "client_secret", csec);
                token = xs_dict_append(token, "uid",           uid);
                token = xs_dict_append(token, "code",          code);

                token_add(tokid, token);
            }
        }
        else {
            srv_debug(1, xs_fmt("oauth token: invalid or unset arguments"));
            status = HTTP_STATUS_BAD_REQUEST;
        }
    }
    else
    if (strcmp(cmd, "/revoke") == 0) { /** **/
        const char *cid   = xs_dict_get(args, "client_id");
        const char *csec  = xs_dict_get(args, "client_secret");
        const char *tokid = xs_dict_get(args, "token");

        if (cid && csec && tokid) {
            xs *token = token_get(tokid);

            *body  = xs_str_new("{}");
            *ctype = "application/json";

            if (token == NULL || strcmp(csec, xs_dict_get(token, "client_secret")) != 0) {
                srv_debug(1, xs_fmt("oauth revoke: bad secret for token %s", tokid));
                status = HTTP_STATUS_FORBIDDEN;
            }
            else {
                token_del(tokid);
                srv_debug(1, xs_fmt("oauth revoke: revoked token %s", tokid));
                status = HTTP_STATUS_OK;

                /* also delete the app, as it serves no purpose from now on */
                app_del(cid);
            }
        }
        else {
            srv_debug(1, xs_fmt("oauth revoke: invalid or unset arguments"));
            status = HTTP_STATUS_FORBIDDEN;
        }
    }
    if (strcmp(cmd, "/x-snac-get-token") == 0) { /** **/
        const char *login  = xs_dict_get(args, "login");
        const char *passwd = xs_dict_get(args, "passwd");
        const char *host   = xs_dict_get(srv_config, "host");
        const char *proto  = xs_dict_get_def(srv_config, "protocol", "https");

        /* by default, generate another login form with an error */
        *body  = xs_fmt(login_page, host, host, "LOGIN INCORRECT", proto, host, "oauth/x-snac-get-token",
                        "", "", "", USER_AGENT);
        *ctype = "text/html";
        status = HTTP_STATUS_OK;

        if (login && passwd) {
            snac user;

            if (user_open(&user, login)) {
                /* check the login + password */
                if (check_password(login, passwd, xs_dict_get(user.config, "passwd"))) {
                    /* success! create a new token */
                    xs *tokid = random_str();

                    srv_debug(1, xs_fmt("x-snac-new-token: "
                                    "successful login for %s, new token %s", login, tokid));

                    xs *token = xs_dict_new();
                    token = xs_dict_append(token, "token",         tokid);
                    token = xs_dict_append(token, "client_id",     "snac-client");
                    token = xs_dict_append(token, "client_secret", "");
                    token = xs_dict_append(token, "uid",           login);
                    token = xs_dict_append(token, "code",          "");

                    token_add(tokid, token);

                    *ctype = "text/plain";
                    xs_free(*body);
                    *body = xs_dup(tokid);
                }

                user_free(&user);
            }
        }
    }

    return status;
}


xs_str *mastoapi_id(const xs_dict *msg)
/* returns a somewhat Mastodon-compatible status id */
{
    const char *id = xs_dict_get(msg, "id");
    xs *md5        = xs_md5_hex(id, strlen(id));

    return xs_fmt("%10.0f%s", object_ctime_by_md5(md5), md5);
}

#define MID_TO_MD5(id) (id + 10)


xs_dict *mastoapi_account(snac *logged, const xs_dict *actor)
/* converts an ActivityPub actor to a Mastodon account */
{
    const char *id  = xs_dict_get(actor, "id");
    const char *pub = xs_dict_get(actor, "published");
    const char *proxy = NULL;

    if (xs_type(id) != XSTYPE_STRING)
        return NULL;

    if (logged && xs_is_true(xs_dict_get(srv_config, "proxy_media")))
        proxy = logged->actor;

    const char *prefu = xs_dict_get(actor, "preferredUsername");

    const char *display_name = xs_dict_get(actor, "name");
    if (xs_is_null(display_name) || *display_name == '\0')
        display_name = prefu;

    xs_dict *acct = xs_dict_new();
    xs *acct_md5  = xs_md5_hex(id, strlen(id));
    acct = xs_dict_append(acct, "id",           acct_md5);
    acct = xs_dict_append(acct, "username",     prefu);
    acct = xs_dict_append(acct, "display_name", display_name);
    acct = xs_dict_append(acct, "discoverable", xs_stock(XSTYPE_TRUE));
    acct = xs_dict_append(acct, "group",        xs_stock(XSTYPE_FALSE));
    acct = xs_dict_append(acct, "hide_collections", xs_stock(XSTYPE_FALSE));
    acct = xs_dict_append(acct, "indexable",    xs_stock(XSTYPE_TRUE));
    acct = xs_dict_append(acct, "noindex",      xs_stock(XSTYPE_FALSE));
    acct = xs_dict_append(acct, "roles",        xs_stock(XSTYPE_LIST));

    {
        /* create the acct field as user@host */
        xs *l     = xs_split(id, "/");
        xs *fquid = xs_fmt("%s@%s", prefu, xs_list_get(l, 2));
        acct      = xs_dict_append(acct, "acct", fquid);
    }

    if (pub)
        acct = xs_dict_append(acct, "created_at", pub);
    else {
        /* unset created_at crashes Tusky, so lie like a mf */
        xs *date = xs_str_utctime(0, ISO_DATE_SPEC);
        acct = xs_dict_append(acct, "created_at", date);
    }

    xs *last_status_at = xs_str_utctime(0, "%Y-%m-%d");
    acct = xs_dict_append(acct, "last_status_at", last_status_at);

    const char *note = xs_dict_get(actor, "summary");
    if (xs_is_null(note))
        note = "";

    if (strcmp(xs_dict_get(actor, "type"), "Service") == 0)
        acct = xs_dict_append(acct, "bot", xs_stock(XSTYPE_TRUE));
    else
        acct = xs_dict_append(acct, "bot", xs_stock(XSTYPE_FALSE));

    acct = xs_dict_append(acct, "note", note);

    acct = xs_dict_append(acct, "url", id);
    acct = xs_dict_append(acct, "uri", id);

    xs *avatar  = NULL;
    const xs_dict *av = xs_dict_get(actor, "icon");

    if (xs_type(av) == XSTYPE_DICT) {
        const char *url = xs_dict_get(av, "url");

        if (url != NULL)
            avatar = make_url(url, proxy, 1);
    }

    if (avatar == NULL)
        avatar = xs_fmt("%s/susie.png", srv_baseurl);

    acct = xs_dict_append(acct, "avatar", avatar);
    acct = xs_dict_append(acct, "avatar_static", avatar);

    xs *header  = NULL;
    const xs_dict *hd = xs_dict_get(actor, "image");

    if (xs_type(hd) == XSTYPE_DICT)
        header = make_url(xs_dict_get(hd, "url"), proxy, 1);

    if (xs_is_null(header))
        header = xs_str_new(NULL);

    acct = xs_dict_append(acct, "header", header);
    acct = xs_dict_append(acct, "header_static", header);

    /* emojis */
    const xs_list *p;
    if (!xs_is_null(p = xs_dict_get(actor, "tag"))) {
        xs *eml = xs_list_new();
        const xs_dict *v;
        int c = 0;

        while (xs_list_next(p, &v, &c)) {
            const char *type = xs_dict_get(v, "type");

            if (!xs_is_null(type) && strcmp(type, "Emoji") == 0) {
                const char *name    = xs_dict_get(v, "name");
                const xs_dict *icon = xs_dict_get(v, "icon");

                if (!xs_is_null(name) && !xs_is_null(icon)) {
                    const char *o_url = xs_dict_get(icon, "url");

                    if (!xs_is_null(o_url)) {
                        xs *url = make_url(o_url, proxy, 1);
                        xs *nm = xs_strip_chars_i(xs_dup(name), ":");
                        xs *d1 = xs_dict_new();

                        d1 = xs_dict_append(d1, "shortcode",         nm);
                        d1 = xs_dict_append(d1, "url",               url);
                        d1 = xs_dict_append(d1, "static_url",        url);
                        d1 = xs_dict_append(d1, "visible_in_picker", xs_stock(XSTYPE_TRUE));

                        eml = xs_list_append(eml, d1);
                    }
                }
            }
        }

        acct = xs_dict_append(acct, "emojis", eml);
    }

    acct = xs_dict_append(acct, "locked", xs_stock(XSTYPE_FALSE));
    acct = xs_dict_append(acct, "followers_count", xs_stock(0));
    acct = xs_dict_append(acct, "following_count", xs_stock(0));
    acct = xs_dict_append(acct, "statuses_count", xs_stock(0));

    xs *fields = xs_list_new();
    p = xs_dict_get(actor, "attachment");
    const xs_dict *v;

    /* dict of validated links */
    xs_dict *val_links = NULL;
    const xs_dict *metadata  = xs_stock(XSTYPE_DICT);
    snac user = {0};

    if (xs_startswith(id, srv_baseurl)) {
        /* if it's a local user, open it and pick its validated links */
        if (user_open(&user, prefu)) {
            val_links = user.links;
            metadata  = xs_dict_get_def(user.config, "metadata", xs_stock(XSTYPE_DICT));
        }
    }

    if (xs_is_null(val_links))
        val_links = xs_stock(XSTYPE_DICT);

    int c = 0;
    while (xs_list_next(p, &v, &c)) {
        const char *type  = xs_dict_get(v, "type");
        const char *name  = xs_dict_get(v, "name");
        const char *value = xs_dict_get(v, "value");

        if (!xs_is_null(type) && !xs_is_null(name) &&
            !xs_is_null(value) && strcmp(type, "PropertyValue") == 0) {
            xs *val_date = NULL;

            const char *url = xs_dict_get(metadata, name);

            if (!xs_is_null(url) && xs_startswith(url, "https:/" "/")) {
                const xs_number *verified_time = xs_dict_get(val_links, url);
                if (xs_type(verified_time) == XSTYPE_NUMBER) {
                    time_t t = xs_number_get(verified_time);

                    if (t > 0)
                        val_date = xs_str_utctime(t, ISO_DATE_SPEC);
                }
            }

            xs *d = xs_dict_new();

            d = xs_dict_append(d, "name", name);
            d = xs_dict_append(d, "value", value);
            d = xs_dict_append(d, "verified_at",
                xs_type(val_date) == XSTYPE_STRING && *val_date ?
                    val_date : xs_stock(XSTYPE_NULL));

            fields = xs_list_append(fields, d);
        }
    }

    user_free(&user);

    acct = xs_dict_append(acct, "fields", fields);

    return acct;
}


xs_str *mastoapi_date(const char *date)
/* converts an ISO 8601 date to whatever format Mastodon uses */
{
    xs_str *s = xs_crop_i(xs_dup(date), 0, 19);
    s = xs_str_cat(s, ".000Z");

    return s;
}


xs_dict *mastoapi_poll(snac *snac, const xs_dict *msg)
/* creates a mastoapi Poll object */
{
    xs_dict *poll = xs_dict_new();
    xs *mid       = mastoapi_id(msg);
    const xs_list *opts = NULL;
    const xs_val *v;
    int num_votes = 0;
    xs *options = xs_list_new();

    poll = xs_dict_append(poll, "id", mid);
    const char *date = xs_dict_get(msg, "endTime");
    if (date == NULL)
        date = xs_dict_get(msg, "closed");
    if (date == NULL)
        return NULL;

    xs *fd = mastoapi_date(date);
    poll = xs_dict_append(poll, "expires_at", fd);

    date = xs_dict_get(msg, "closed");
    time_t t = 0;

    if (date != NULL)
        t = xs_parse_iso_date(date, 0);

    poll = xs_dict_append(poll, "expired",
            t < time(NULL) ? xs_stock(XSTYPE_FALSE) : xs_stock(XSTYPE_TRUE));

    if ((opts = xs_dict_get(msg, "oneOf")) != NULL)
        poll = xs_dict_append(poll, "multiple", xs_stock(XSTYPE_FALSE));
    else {
        opts = xs_dict_get(msg, "anyOf");
        poll = xs_dict_append(poll, "multiple", xs_stock(XSTYPE_TRUE));
    }

    int c = 0;
    while (xs_list_next(opts, &v, &c)) {
        const char *title   = xs_dict_get(v, "name");
        const char *replies = xs_dict_get(v, "replies");

        if (title && replies) {
            const char *votes_count = xs_dict_get(replies, "totalItems");

            if (xs_type(votes_count) == XSTYPE_NUMBER) {
                xs *d = xs_dict_new();
                d = xs_dict_append(d, "title",  title);
                d = xs_dict_append(d, "votes_count", votes_count);

                options = xs_list_append(options, d);
                num_votes += xs_number_get(votes_count);
            }
        }
    }

    poll = xs_dict_append(poll, "options", options);
    xs *vc = xs_number_new(num_votes);
    poll = xs_dict_append(poll, "votes_count", vc);

    poll = xs_dict_append(poll, "emojis", xs_stock(XSTYPE_LIST));

    poll = xs_dict_append(poll, "voted",
            (snac && was_question_voted(snac, xs_dict_get(msg, "id"))) ?
                xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

    return poll;
}


xs_dict *mastoapi_status(snac *snac, const xs_dict *msg)
/* converts an ActivityPub note to a Mastodon status */
{
    xs *actor = NULL;
    actor_get_refresh(snac, get_atto(msg), &actor);
    const char *proxy = NULL;

    /* if the author is not here, discard */
    if (actor == NULL)
        return NULL;

    if (snac && xs_is_true(xs_dict_get(srv_config, "proxy_media")))
        proxy = snac->actor;

    const char *type = xs_dict_get(msg, "type");
    const char *id   = xs_dict_get(msg, "id");

    /* fail if it's not a valid actor */
    if (xs_is_null(type) || xs_is_null(id))
        return NULL;

    xs *acct = mastoapi_account(snac, actor);
    if (acct == NULL)
        return NULL;

    xs *idx = NULL;
    xs *ixc = NULL;
    const char *tmp;
    xs *mid  = mastoapi_id(msg);

    xs_dict *st = xs_dict_new();

    st = xs_dict_append(st, "id",           mid);
    st = xs_dict_append(st, "uri",          id);
    st = xs_dict_append(st, "url",          id);
    st = xs_dict_append(st, "account",      acct);

    const char *published = xs_dict_get(msg, "published");
    xs *fd = NULL;

    if (published)
        fd = mastoapi_date(published);
    else {
        xs *p = xs_str_iso_date(0);
        fd = mastoapi_date(p);
    }

    st = xs_dict_append(st, "created_at", fd);

    {
        const char *content = xs_dict_get(msg, "content");
        const char *name    = xs_dict_get(msg, "name");
        xs *s1 = NULL;

        if (name && content)
            s1 = xs_fmt("%s<br><br>%s", name, content);
        else
        if (name)
            s1 = xs_dup(name);
        else
        if (content)
            s1 = xs_dup(content);
        else
            s1 = xs_str_new(NULL);

        st = xs_dict_append(st, "content", s1);
    }

    st = xs_dict_append(st, "visibility",
        is_msg_public(msg) ? "public" : "private");

    tmp = xs_dict_get(msg, "sensitive");
    if (xs_is_null(tmp))
        tmp = xs_stock(XSTYPE_FALSE);

    st = xs_dict_append(st, "sensitive",    tmp);

    tmp = xs_dict_get(msg, "summary");
    if (xs_is_null(tmp))
        tmp = "";

    st = xs_dict_append(st, "spoiler_text", tmp);

    /* create the list of attachments */
    xs *attach = get_attachments(msg);

    {
        xs_list *p = attach;
        const xs_dict *v;

        xs *matt = xs_list_new();

        while (xs_list_iter(&p, &v)) {
            const char *type = xs_dict_get(v, "type");
            const char *o_href = xs_dict_get(v, "href");
            const char *name = xs_dict_get(v, "name");

            if (xs_match(type, "image/*|video/*|Image|Video")) { /* */
                xs *matteid = xs_fmt("%s_%d", id, xs_list_len(matt));
                xs *href = make_url(o_href, proxy, 1);

                xs *d = xs_dict_new();

                d = xs_dict_append(d, "id",          matteid);
                d = xs_dict_append(d, "url",         href);
                d = xs_dict_append(d, "preview_url", href);
                d = xs_dict_append(d, "remote_url",  href);
                d = xs_dict_append(d, "description", name);

                d = xs_dict_append(d, "type", (*type == 'v' || *type == 'V') ? "video" : "image");

                matt = xs_list_append(matt, d);
            }
        }

        st = xs_dict_append(st, "media_attachments", matt);
    }

    {
        xs *ml  = xs_list_new();
        xs *htl = xs_list_new();
        xs *eml = xs_list_new();
        const xs_list *tag = xs_dict_get(msg, "tag");
        int n = 0;

        xs *tag_list = NULL;

        if (xs_type(tag) == XSTYPE_DICT) {
            tag_list = xs_list_new();
            tag_list = xs_list_append(tag_list, tag);
        }
        else
        if (xs_type(tag) == XSTYPE_LIST)
            tag_list = xs_dup(tag);
        else
            tag_list = xs_list_new();

        tag = tag_list;
        const xs_dict *v;

        int c = 0;
        while (xs_list_next(tag, &v, &c)) {
            const char *type = xs_dict_get(v, "type");

            if (xs_is_null(type))
                continue;

            xs *d1 = xs_dict_new();

            if (strcmp(type, "Mention") == 0) {
                const char *name = xs_dict_get(v, "name");
                const char *href = xs_dict_get(v, "href");

                if (!xs_is_null(name) && !xs_is_null(href) &&
                    (snac == NULL || strcmp(href, snac->actor) != 0)) {
                    xs *nm = xs_strip_chars_i(xs_dup(name), "@");

                    xs *id = xs_fmt("%d", n++);
                    d1 = xs_dict_append(d1, "id", id);
                    d1 = xs_dict_append(d1, "username", nm);
                    d1 = xs_dict_append(d1, "acct", nm);
                    d1 = xs_dict_append(d1, "url", href);

                    ml = xs_list_append(ml, d1);
                }
            }
            else
            if (strcmp(type, "Hashtag") == 0) {
                const char *name = xs_dict_get(v, "name");
                const char *href = xs_dict_get(v, "href");

                if (!xs_is_null(name) && !xs_is_null(href)) {
                    xs *nm = xs_strip_chars_i(xs_dup(name), "#");

                    d1 = xs_dict_append(d1, "name", nm);
                    d1 = xs_dict_append(d1, "url", href);

                    htl = xs_list_append(htl, d1);
                }
            }
            else
            if (strcmp(type, "Emoji") == 0) {
                const char *name    = xs_dict_get(v, "name");
                const xs_dict *icon = xs_dict_get(v, "icon");

                if (!xs_is_null(name) && !xs_is_null(icon)) {
                    const char *o_url = xs_dict_get(icon, "url");

                    if (!xs_is_null(o_url)) {
                        xs *url = make_url(o_url, snac->actor, 1);
                        xs *nm = xs_strip_chars_i(xs_dup(name), ":");

                        d1 = xs_dict_append(d1, "shortcode", nm);
                        d1 = xs_dict_append(d1, "url", url);
                        d1 = xs_dict_append(d1, "static_url", url);
                        d1 = xs_dict_append(d1, "visible_in_picker", xs_stock(XSTYPE_TRUE));
                        d1 = xs_dict_append(d1, "category", "Emojis");

                        eml = xs_list_append(eml, d1);
                    }
                }
            }
        }

        st = xs_dict_append(st, "mentions", ml);
        st = xs_dict_append(st, "tags",     htl);
        st = xs_dict_append(st, "emojis",   eml);
    }

    xs_free(idx);
    xs_free(ixc);
    idx = object_likes(id);
    ixc = xs_number_new(xs_list_len(idx));

    st = xs_dict_append(st, "favourites_count", ixc);
    st = xs_dict_append(st, "favourited",
        (snac && xs_list_in(idx, snac->md5) != -1) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

    xs_free(idx);
    xs_free(ixc);
    idx = object_announces(id);
    ixc = xs_number_new(xs_list_len(idx));

    st = xs_dict_append(st, "reblogs_count", ixc);
    st = xs_dict_append(st, "reblogged",
        (snac && xs_list_in(idx, snac->md5) != -1) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

    /* get the last person who boosted this */
    xs *boosted_by_md5 = NULL;
    if (xs_list_len(idx))
        boosted_by_md5 = xs_dup(xs_list_get(idx, -1));

    xs_free(idx);
    xs_free(ixc);
    idx = object_children(id);
    ixc = xs_number_new(xs_list_len(idx));

    st = xs_dict_append(st, "replies_count", ixc);

    /* default in_reply_to values */
    st = xs_dict_append(st, "in_reply_to_id",         xs_stock(XSTYPE_NULL));
    st = xs_dict_append(st, "in_reply_to_account_id", xs_stock(XSTYPE_NULL));

    tmp = get_in_reply_to(msg);
    if (!xs_is_null(tmp)) {
        xs *irto = NULL;

        if (valid_status(object_get(tmp, &irto))) {
            xs *irt_mid = mastoapi_id(irto);
            st = xs_dict_set(st, "in_reply_to_id", irt_mid);

            const char *at = NULL;
            if (!xs_is_null(at = get_atto(irto))) {
                xs *at_md5 = xs_md5_hex(at, strlen(at));
                st = xs_dict_set(st, "in_reply_to_account_id", at_md5);
            }
        }
    }

    st = xs_dict_append(st, "reblog",   xs_stock(XSTYPE_NULL));
    st = xs_dict_append(st, "card",     xs_stock(XSTYPE_NULL));
    st = xs_dict_append(st, "language", "en");

    st = xs_dict_append(st, "filtered", xs_stock(XSTYPE_LIST));
    st = xs_dict_append(st, "muted",    xs_stock(XSTYPE_FALSE));

    tmp = xs_dict_get(msg, "sourceContent");
    if (xs_is_null(tmp))
        tmp = "";

    st = xs_dict_append(st, "text", tmp);

    tmp = xs_dict_get(msg, "updated");
    xs *fd2 = NULL;
    if (xs_is_null(tmp))
        tmp = xs_stock(XSTYPE_NULL);
    else {
        fd2 = mastoapi_date(tmp);
        tmp = fd2;
    }

    st = xs_dict_append(st, "edited_at", tmp);

    if (strcmp(type, "Question") == 0) {
        xs *poll = mastoapi_poll(snac, msg);
        st = xs_dict_append(st, "poll", poll);
    }
    else
        st = xs_dict_append(st, "poll", xs_stock(XSTYPE_NULL));

    st = xs_dict_append(st, "bookmarked",
        (snac && is_bookmarked(snac, id)) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

    st = xs_dict_append(st, "pinned",
        (snac && is_pinned(snac, id)) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

    /* is it a boost? */
    if (!xs_is_null(boosted_by_md5)) {
        /* create a new dummy status, using st as the 'reblog' field */
        xs_dict *bst = xs_dup(st);
        xs *b_actor = NULL;

        if (valid_status(object_get_by_md5(boosted_by_md5, &b_actor))) {
            xs *b_acct   = mastoapi_account(snac, b_actor);
            xs *fake_uri = NULL;

            if (snac)
                fake_uri = xs_fmt("%s/d/%s/Announce", snac->actor, mid);
            else
                fake_uri = xs_fmt("%s#%s", srv_baseurl, mid);

            bst = xs_dict_set(bst, "uri", fake_uri);
            bst = xs_dict_set(bst, "url", fake_uri);
            bst = xs_dict_set(bst, "account", b_acct);
            bst = xs_dict_set(bst, "content", "");
            bst = xs_dict_set(bst, "reblog", st);

            xs_free(st);
            st = bst;
        }
    }

    return st;
}


xs_dict *mastoapi_relationship(snac *snac, const char *md5)
{
    xs_dict *rel = NULL;
    xs *actor_o  = NULL;

    if (valid_status(object_get_by_md5(md5, &actor_o))) {
        rel = xs_dict_new();

        const char *actor = xs_dict_get(actor_o, "id");

        rel = xs_dict_append(rel, "id",                   md5);
        rel = xs_dict_append(rel, "following",
            following_check(snac, actor) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

        rel = xs_dict_append(rel, "showing_reblogs",      xs_stock(XSTYPE_TRUE));
        rel = xs_dict_append(rel, "notifying",            xs_stock(XSTYPE_FALSE));
        rel = xs_dict_append(rel, "followed_by",
            follower_check(snac, actor) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

        rel = xs_dict_append(rel, "blocking",
            is_muted(snac, actor) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

        rel = xs_dict_append(rel, "muting",               xs_stock(XSTYPE_FALSE));
        rel = xs_dict_append(rel, "muting_notifications", xs_stock(XSTYPE_FALSE));
        rel = xs_dict_append(rel, "requested",            xs_stock(XSTYPE_FALSE));
        rel = xs_dict_append(rel, "domain_blocking",      xs_stock(XSTYPE_FALSE));
        rel = xs_dict_append(rel, "endorsed",             xs_stock(XSTYPE_FALSE));
        rel = xs_dict_append(rel, "note",                 "");
    }

    return rel;
}


int process_auth_token(snac *snac, const xs_dict *req)
/* processes an authorization token, if there is one */
{
    int logged_in = 0;
    const char *v;

    /* if there is an authorization field, try to validate it */
    if (!xs_is_null(v = xs_dict_get(req, "authorization")) && xs_startswith(v, "Bearer ")) {
        xs *tokid = xs_replace_n(v, "Bearer ", "", 1);
        xs *token = token_get(tokid);

        if (token != NULL) {
            const char *uid = xs_dict_get(token, "uid");

            if (!xs_is_null(uid) && user_open(snac, uid)) {
                logged_in = 1;

                /* this counts as a 'login' */
                lastlog_write(snac, "mastoapi");

                srv_debug(2, xs_fmt("mastoapi auth: valid token for user '%s'", uid));
            }
            else
                srv_log(xs_fmt("mastoapi auth: corrupted token '%s'", tokid));
        }
        else
            srv_log(xs_fmt("mastoapi auth: invalid token '%s'", tokid));
    }

    return logged_in;
}

void credentials_get(char **body, char **ctype, int *status, snac snac)
{
    xs *acct = xs_dict_new();

    acct = xs_dict_append(acct, "id", snac.md5);
    acct = xs_dict_append(acct, "username", xs_dict_get(snac.config, "uid"));
    acct = xs_dict_append(acct, "acct", xs_dict_get(snac.config, "uid"));
    acct = xs_dict_append(acct, "display_name", xs_dict_get(snac.config, "name"));
    acct = xs_dict_append(acct, "created_at", xs_dict_get(snac.config, "published"));
    acct = xs_dict_append(acct, "last_status_at", xs_dict_get(snac.config, "published"));
    acct = xs_dict_append(acct, "note", xs_dict_get(snac.config, "bio"));
    acct = xs_dict_append(acct, "url", snac.actor);
    acct = xs_dict_append(acct, "locked", xs_stock(XSTYPE_FALSE));
    acct = xs_dict_append(acct, "bot", xs_dict_get(snac.config, "bot"));
    acct = xs_dict_append(acct, "emojis", xs_stock(XSTYPE_LIST));

    xs *src = xs_json_loads("{\"privacy\":\"public\", \"language\":\"en\","
        "\"follow_requests_count\": 0,"
        "\"sensitive\":false,\"fields\":[],\"note\":\"\"}");
    /* some apps take the note from the source object */
    src = xs_dict_set(src, "note", xs_dict_get(snac.config, "bio"));
    src = xs_dict_set(src, "privacy", xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE ? "private" : "public");

    const xs_str *cw = xs_dict_get(snac.config, "cw");
    src = xs_dict_set(src, "sensitive",
        strcmp(cw, "open") == 0 ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));

    src = xs_dict_set(src, "bot", xs_dict_get(snac.config, "bot"));

    xs *avatar = NULL;
    const char *av = xs_dict_get(snac.config, "avatar");

    if (xs_is_null(av) || *av == '\0')
        avatar = xs_fmt("%s/susie.png", srv_baseurl);
    else
        avatar = xs_dup(av);

    acct = xs_dict_append(acct, "avatar", avatar);
    acct = xs_dict_append(acct, "avatar_static", avatar);

    xs *header = NULL;
    const char *hd = xs_dict_get(snac.config, "header");

    if (!xs_is_null(hd))
        header = xs_dup(hd);
    else
        header = xs_str_new(NULL);

    acct = xs_dict_append(acct, "header", header);
    acct = xs_dict_append(acct, "header_static", header);

    const xs_dict *metadata = xs_dict_get(snac.config, "metadata");
    if (xs_type(metadata) == XSTYPE_DICT) {
        xs *fields = xs_list_new();
        const xs_str *k;
        const xs_str *v;

        xs_dict *val_links = snac.links;
        if (xs_is_null(val_links))
            val_links = xs_stock(XSTYPE_DICT);

        int c = 0;
        while (xs_dict_next(metadata, &k, &v, &c)) {
            xs *val_date = NULL;

            const xs_number *verified_time = xs_dict_get(val_links, v);
            if (xs_type(verified_time) == XSTYPE_NUMBER) {
                time_t t = xs_number_get(verified_time);

                if (t > 0)
                    val_date = xs_str_utctime(t, ISO_DATE_SPEC);
            }

            xs *d = xs_dict_new();

            d = xs_dict_append(d, "name", k);
            d = xs_dict_append(d, "value", v);
            d = xs_dict_append(d, "verified_at",
                               xs_type(val_date) == XSTYPE_STRING && *val_date ? val_date : xs_stock(XSTYPE_NULL));

            fields = xs_list_append(fields, d);
        }

        acct = xs_dict_set(acct, "fields", fields);
        /* some apps take the fields from the source object */
        src = xs_dict_set(src, "fields", fields);
    }

    acct = xs_dict_append(acct, "source", src);
    acct = xs_dict_append(acct, "followers_count", xs_stock(0));
    acct = xs_dict_append(acct, "following_count", xs_stock(0));
    acct = xs_dict_append(acct, "statuses_count", xs_stock(0));

    *body = xs_json_dumps(acct, 4);
    *ctype = "application/json";
    *status = HTTP_STATUS_OK;
}


xs_list *mastoapi_timeline(snac *user, const xs_dict *args, const char *index_fn)
{
    xs_list *out = xs_list_new();
    FILE *f;
    char md5[MD5_HEX_SIZE];

    if (dbglevel) {
        xs *js = xs_json_dumps(args, 0);
        srv_debug(1, xs_fmt("mastoapi_timeline args %s", js));
    }

    if ((f = fopen(index_fn, "r")) == NULL)
        return out;

    const char *max_id   = xs_dict_get(args, "max_id");
    const char *since_id = xs_dict_get(args, "since_id");
    const char *min_id   = xs_dict_get(args, "min_id");
    const char *limit_s  = xs_dict_get(args, "limit");
    int limit = 0;
    int cnt   = 0;

    if (!xs_is_null(limit_s))
        limit = atoi(limit_s);

    if (limit == 0)
        limit = 20;

    if (index_desc_first(f, md5, 0)) {
        do {
            xs *msg = NULL;

            /* only return entries older that max_id */
            if (max_id) {
                if (strcmp(md5, MID_TO_MD5(max_id)) == 0)
                    max_id = NULL;

                continue;
            }

            /* only returns entries newer than since_id */
            if (since_id) {
                if (strcmp(md5, MID_TO_MD5(since_id)) == 0)
                    break;
            }

            /* only returns entries newer than min_id */
            /* what does really "Return results immediately newer than ID" mean? */
            if (min_id) {
                if (strcmp(md5, MID_TO_MD5(min_id)) == 0)
                    break;
            }

            /* get the entry */
            if (user) {
                if (!valid_status(timeline_get_by_md5(user, md5, &msg)))
                    continue;
            }
            else {
                if (!valid_status(object_get_by_md5(md5, &msg)))
                    continue;
            }

            /* discard non-Notes */
            const char *id   = xs_dict_get(msg, "id");
            const char *type = xs_dict_get(msg, "type");
            if (!xs_match(type, POSTLIKE_OBJECT_TYPE))
                continue;

            const char *from = NULL;
            if (strcmp(type, "Page") == 0)
                from = xs_dict_get(msg, "audience");

            if (from == NULL)
                from = get_atto(msg);

            if (from == NULL)
                continue;

            if (user) {
                /* is this message from a person we don't follow? */
                if (strcmp(from, user->actor) && !following_check(user, from)) {
                    /* discard if it was not boosted */
                    xs *idx = object_announces(id);

                    if (xs_list_len(idx) == 0)
                        continue;
                }

                /* discard notes from muted morons */
                if (is_muted(user, from))
                    continue;

                /* discard hidden notes */
                if (is_hidden(user, id))
                    continue;
            }
            else {
                /* skip non-public messages */
                if (!is_msg_public(msg))
                    continue;

                /* discard messages from private users */
                if (is_msg_from_private_user(msg))
                    continue;
            }

            /* if it has a name and it's not a Page or a Video,
               it's a poll vote, so discard it */
            if (!xs_is_null(xs_dict_get(msg, "name")) && !xs_match(type, "Page|Video"))
                continue;

            /* convert the Note into a Mastodon status */
            xs *st = mastoapi_status(user, msg);

            if (st != NULL) {
                out = xs_list_append(out, st);
                cnt++;
            }

        } while (cnt < limit && index_desc_next(f, md5));
    }

    int more = index_desc_next(f, md5);

    fclose(f);

    srv_debug(1, xs_fmt("mastoapi_timeline ret %d%s", cnt, more ? " (+)" : ""));

    return out;
}


int mastoapi_get_handler(const xs_dict *req, const char *q_path,
                         char **body, int *b_size, char **ctype)
{
    (void)b_size;

    if (!xs_startswith(q_path, "/api/v1/") && !xs_startswith(q_path, "/api/v2/"))
        return 0;

    int status    = HTTP_STATUS_NOT_FOUND;
    const xs_dict *args = xs_dict_get(req, "q_vars");
    xs *cmd       = xs_replace_n(q_path, "/api", "", 1);

    snac snac1 = {0};
    int logged_in = process_auth_token(&snac1, req);

    if (strcmp(cmd, "/v1/accounts/verify_credentials") == 0) { /** **/
        if (logged_in) {
            credentials_get(body, ctype, &status, snac1);
        }
        else {
            status = HTTP_STATUS_UNPROCESSABLE_CONTENT; // (no login)
        }
    }
    else
    if (strcmp(cmd, "/v1/accounts/relationships") == 0) { /** **/
        /* find if an account is followed, blocked, etc. */
        /* the account to get relationships about is in args "id[]" */

        if (logged_in) {
            xs *res         = xs_list_new();
            const char *md5 = xs_dict_get(args, "id[]");

            if (xs_is_null(md5))
                md5 = xs_dict_get(args, "id");

            if (!xs_is_null(md5)) {
                if (xs_type(md5) == XSTYPE_LIST)
                    md5 = xs_list_get(md5, 0);

                xs *rel = mastoapi_relationship(&snac1, md5);

                if (rel != NULL)
                    res = xs_list_append(res, rel);
            }

            *body  = xs_json_dumps(res, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNPROCESSABLE_CONTENT;
    }
    else
    if (strcmp(cmd, "/v1/accounts/lookup") == 0) { /** **/
        /* lookup an account */
        const char *acct = xs_dict_get(args, "acct");

        if (!xs_is_null(acct)) {
            xs *s = xs_strip_chars_i(xs_dup(acct), "@");
            xs *l = xs_split_n(s, "@", 1);
            const char *uid = xs_list_get(l, 0);
            const char *host = xs_list_get(l, 1);

            if (uid && (!host || strcmp(host, xs_dict_get(srv_config, "host")) == 0)) {
                snac user;

                if (user_open(&user, uid)) {
                    xs *actor = msg_actor(&user);
                    xs *macct = mastoapi_account(NULL, actor);

                    *body  = xs_json_dumps(macct, 4);
                    *ctype = "application/json";
                    status = HTTP_STATUS_OK;

                    user_free(&user);
                }
            }
        }
    }
    else
    if (xs_startswith(cmd, "/v1/accounts/")) { /** **/
        /* account-related information */
        xs *l = xs_split(cmd, "/");
        const char *uid = xs_list_get(l, 3);
        const char *opt = xs_list_get(l, 4);

        if (uid != NULL) {
            snac snac2;
            xs *out   = NULL;
            xs *actor = NULL;

            if (logged_in && strcmp(uid, "search") == 0) { /** **/
                /* search for accounts starting with q */
                const char *aq = xs_dict_get(args, "q");

                if (!xs_is_null(aq)) {
                    xs *q    = xs_tolower_i(xs_dup(aq));
                    out      = xs_list_new();
                    xs *wing = following_list(&snac1);
                    xs *wers = follower_list(&snac1);
                    xs *ulst = user_list();
                    xs_list *p;
                    const xs_str *v;
                    xs_set seen;

                    xs_set_init(&seen);

                    /* user relations */
                    xs_list *lsts[] = { wing, wers, NULL };
                    int n;
                    for (n = 0; (p = lsts[n]) != NULL; n++) {

                        while (xs_list_iter(&p, &v)) {
                            /* already seen? skip */
                            if (xs_set_add(&seen, v) == 0)
                                continue;

                            xs *actor = NULL;

                            if (valid_status(object_get(v, &actor))) {
                                const char *uname = xs_dict_get(actor, "preferredUsername");

                                if (!xs_is_null(uname)) {
                                    xs *luname = xs_tolower_i(xs_dup(uname));

                                    if (xs_startswith(luname, q)) {
                                        xs *acct = mastoapi_account(&snac1, actor);

                                        out = xs_list_append(out, acct);
                                    }
                                }
                            }
                        }
                    }

                    /* local users */
                    p = ulst;
                    while (xs_list_iter(&p, &v)) {
                        snac user;

                        /* skip this same user */
                        if (strcmp(v, xs_dict_get(snac1.config, "uid")) == 0)
                            continue;

                        /* skip if the uid does not start with the query */
                        xs *v2 = xs_tolower_i(xs_dup(v));
                        if (!xs_startswith(v2, q))
                            continue;

                        if (user_open(&user, v)) {
                            /* if it's not already seen, add it */
                            if (xs_set_add(&seen, user.actor) == 1) {
                                xs *actor = msg_actor(&user);
                                xs *acct  = mastoapi_account(&snac1, actor);

                                out = xs_list_append(out, acct);
                            }

                            user_free(&user);
                        }
                    }

                    xs_set_free(&seen);
                }
            }
            else
            /* is it a local user? */
            if (user_open(&snac2, uid) || user_open_by_md5(&snac2, uid)) {
                if (opt == NULL) {
                    /* account information */
                    actor = msg_actor(&snac2);
                    out   = mastoapi_account(NULL, actor);
                }
                else
                if (strcmp(opt, "statuses") == 0) { /** **/
                    /* the public list of posts of a user */
                    xs *timeline = timeline_simple_list(&snac2, "public", 0, 256);
                    xs_list *p   = timeline;
                    const xs_str *v;

                    out = xs_list_new();

                    while (xs_list_iter(&p, &v)) {
                        xs *msg = NULL;

                        if (valid_status(timeline_get_by_md5(&snac2, v, &msg))) {
                            /* add only posts by the author */
                            if (strcmp(xs_dict_get(msg, "type"), "Note") == 0 &&
                                xs_startswith(xs_dict_get(msg, "id"), snac2.actor)) {
                                xs *st = mastoapi_status(&snac2, msg);

                                if (st)
                                    out = xs_list_append(out, st);
                            }
                        }
                    }
                }
                else
                if (strcmp(opt, "featured_tags") == 0) {
                    /* snac doesn't have features tags, yet? */
                    /* implement empty response so apps like Tokodon don't show an error */
                    out = xs_list_new();
                }
                else
                if (strcmp(opt, "following") == 0) {
                    xs *wing = following_list(&snac1);
                    out = xs_list_new();
                    int c = 0;
                    const char *v;

                    while (xs_list_next(wing, &v, &c)) {
                        xs *actor = NULL;

                        if (valid_status(object_get(v, &actor))) {
                            xs *acct = mastoapi_account(NULL, actor);
                            out = xs_list_append(out, acct);
                        }
                    }
                }
                else
                if (strcmp(opt, "followers") == 0) {
                    out = xs_list_new();
                }

                user_free(&snac2);
            }
            else {
                /* try the uid as the md5 of a possibly loaded actor */
                if (logged_in && valid_status(object_get_by_md5(uid, &actor))) {
                    if (opt == NULL) {
                        /* account information */
                        out = mastoapi_account(&snac1, actor);
                    }
                    else
                    if (strcmp(opt, "statuses") == 0) {
                        /* we don't serve statuses of others; return the empty list */ 
                        out = xs_list_new();
                    }
                    else
                    if (strcmp(opt, "featured_tags") == 0) {
                        /* snac doesn't have features tags, yet? */
                        /* implement empty response so apps like Tokodon don't show an error */
                        out = xs_list_new();
                    }
                }
            }

            if (out != NULL) {
                *body  = xs_json_dumps(out, 4);
                *ctype = "application/json";
                status = HTTP_STATUS_OK;
            }
        }
    }
    else
    if (strcmp(cmd, "/v1/timelines/home") == 0) { /** **/
        /* the private timeline */
        if (logged_in) {
            xs *ifn = user_index_fn(&snac1, "private");
            xs *out = mastoapi_timeline(&snac1, args, ifn);

            *body  = xs_json_dumps(out, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;

            srv_debug(2, xs_fmt("mastoapi timeline: returned %d entries", xs_list_len(out)));
        }
        else {
            status = HTTP_STATUS_UNAUTHORIZED;
        }
    }
    else
    if (strcmp(cmd, "/v1/timelines/public") == 0) { /** **/
        /* the instance public timeline (public timelines for all users) */
        xs *ifn = instance_index_fn();
        xs *out = mastoapi_timeline(NULL, args, ifn);

        *body  = xs_json_dumps(out, 4);
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (xs_startswith(cmd, "/v1/timelines/tag/")) { /** **/
        /* get the tag */
        xs *l = xs_split(cmd, "/");
        const char *tag = xs_list_get(l, -1);

        xs *ifn = tag_fn(tag);
        xs *out = mastoapi_timeline(NULL, args, ifn);

        *body  = xs_json_dumps(out, 4);
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (xs_startswith(cmd, "/v1/timelines/list/")) { /** **/
        /* get the list id */
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *list = xs_list_get(l, -1);

            xs *ifn = list_timeline_fn(&snac1, list);
            xs *out = mastoapi_timeline(NULL, args, ifn);

            *body  = xs_json_dumps(out, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_MISDIRECTED_REQUEST;
    }
    else
    if (strcmp(cmd, "/v1/conversations") == 0) { /** **/
        /* TBD */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/notifications") == 0) { /** **/
        if (logged_in) {
            xs *l      = notify_list(&snac1, 0, 64);
            xs *out    = xs_list_new();
            const xs_dict *v;
            const xs_list *excl = xs_dict_get(args, "exclude_types[]");
            const char *max_id = xs_dict_get(args, "max_id");

            xs_list_foreach(l, v) {
                xs *noti = notify_get(&snac1, v);

                if (noti == NULL)
                    continue;

                const char *type  = xs_dict_get(noti, "type");
                const char *utype = xs_dict_get(noti, "utype");
                const char *objid = xs_dict_get(noti, "objid");
                const char *id    = xs_dict_get(noti, "id");
                xs *fid = xs_replace(id, ".", "");
                xs *actor = NULL;
                xs *entry = NULL;

                if (!valid_status(actor_get(xs_dict_get(noti, "actor"), &actor)))
                    continue;

                if (objid != NULL && !valid_status(object_get(objid, &entry)))
                    continue;

                if (is_hidden(&snac1, objid))
                    continue;

                if (max_id) {
                    if (strcmp(fid, max_id) == 0)
                        max_id = NULL;

                    continue;
                }

                /* convert the type */
                if (strcmp(type, "Like") == 0 || strcmp(type, "EmojiReact") == 0)
                    type = "favourite";
                else
                if (strcmp(type, "Announce") == 0)
                    type = "reblog";
                else
                if (strcmp(type, "Follow") == 0)
                    type = "follow";
                else
                if (strcmp(type, "Create") == 0)
                    type = "mention";
                else
                if (strcmp(type, "Update") == 0 && strcmp(utype, "Question") == 0)
                    type = "poll";
                else
                    continue;

                /* excluded type? */
                if (!xs_is_null(excl) && xs_list_in(excl, type) != -1)
                    continue;

                xs *mn = xs_dict_new();

                mn = xs_dict_append(mn, "type", type);

                mn = xs_dict_append(mn, "id", fid);

                mn = xs_dict_append(mn, "created_at", xs_dict_get(noti, "date"));

                xs *acct = mastoapi_account(&snac1, actor);

                if (acct == NULL)
                    continue;

                mn = xs_dict_append(mn, "account", acct);

                if (strcmp(type, "follow") != 0 && !xs_is_null(objid)) {
                    xs *st = mastoapi_status(&snac1, entry);

                    if (st)
                        mn = xs_dict_append(mn, "status", st);
                }

                out = xs_list_append(out, mn);
            }

            *body  = xs_json_dumps(out, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/filters") == 0) { /** **/
        /* snac will never have filters */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v2/filters") == 0) { /** **/
        /* snac will never have filters 
         * but still, without a v2 endpoint a short delay is introduced
         * in some apps */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/favourites") == 0) { /** **/
        /* snac will never support a list of favourites */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/bookmarks") == 0) { /** **/
        if (logged_in) {
            xs *ifn = bookmark_index_fn(&snac1);
            xs *out = mastoapi_timeline(&snac1, args, ifn);

            *body  = xs_json_dumps(out, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/lists") == 0) { /** list of lists **/
        if (logged_in) {
            xs *lol = list_maint(&snac1, NULL, 0);
            xs *l   = xs_list_new();
            int c = 0;
            const xs_list *li;

            while (xs_list_next(lol, &li, &c)) {
                xs *d = xs_dict_new();

                d = xs_dict_append(d, "id", xs_list_get(li, 0));
                d = xs_dict_append(d, "title", xs_list_get(li, 1));
                d = xs_dict_append(d, "replies_policy", "list");
                d = xs_dict_append(d, "exclusive", xs_stock(XSTYPE_FALSE));

                l = xs_list_append(l, d);
            }

            *body  = xs_json_dumps(l, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (xs_startswith(cmd, "/v1/lists/")) { /** list information **/
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *p = xs_list_get(l, -1);

            if (p) {
                if (strcmp(p, "accounts") == 0) {
                    p = xs_list_get(l, -2);

                    if (p && xs_is_hex(p)) {
                        xs *actors = list_content(&snac1, p, NULL, 0);
                        xs *out = xs_list_new();
                        int c = 0;
                        const char *v;

                        while (xs_list_next(actors, &v, &c)) {
                            xs *actor = NULL;

                            if (valid_status(object_get_by_md5(v, &actor))) {
                                xs *acct = mastoapi_account(&snac1, actor);
                                out = xs_list_append(out, acct);
                            }
                        }

                        *body  = xs_json_dumps(out, 4);
                        *ctype = "application/json";
                        status = HTTP_STATUS_OK;
                    }
                }
                else
                if (xs_is_hex(p)) {
                    xs *out = xs_list_new();
                    xs *lol = list_maint(&snac1, NULL, 0);
                    int c = 0;
                    const xs_list *v;

                    while (xs_list_next(lol, &v, &c)) {
                        const char *id = xs_list_get(v, 0);

                        if (id && strcmp(id, p) == 0) {
                            xs *d = xs_dict_new();

                            d = xs_dict_append(d, "id", p);
                            d = xs_dict_append(d, "title", xs_list_get(v, 1));
                            d = xs_dict_append(d, "replies_policy", "list");
                            d = xs_dict_append(d, "exclusive", xs_stock(XSTYPE_FALSE));

                            out = xs_dup(d);
                            break;
                        }
                    }

                    *body  = xs_json_dumps(out, 4);
                    *ctype = "application/json";
                    status = HTTP_STATUS_OK;
                }
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/scheduled_statuses") == 0) { /** **/
        /* snac does not schedule notes */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/follow_requests") == 0) { /** **/
        /* snac does not support optional follow confirmations */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/announcements") == 0) { /** **/
        if (logged_in) {
            xs *resp = xs_list_new();
            double la = 0.0;
            xs *user_la = xs_dup(xs_dict_get(snac1.config, "last_announcement"));
            if (user_la != NULL)
                la = xs_number_get(user_la);
            xs *val_date = xs_str_utctime(la, ISO_DATE_SPEC);

            /* contrary to html, we always send the announcement and set the read flag instead */

            const t_announcement *annce = announcement(la);
            if (annce != NULL && annce->text != NULL) {
                xs *an = xs_dict_new();
                xs *id = xs_fmt("%d", annce->timestamp);
                xs *ct = xs_fmt("<p>%s</p>", annce->text);

                an = xs_dict_set(an, "id",           id);
                an = xs_dict_set(an, "content",      ct);
                an = xs_dict_set(an, "starts_at",    xs_stock(XSTYPE_NULL));
                an = xs_dict_set(an, "ends_at",      xs_stock(XSTYPE_NULL));
                an = xs_dict_set(an, "all_day",      xs_stock(XSTYPE_TRUE));
                an = xs_dict_set(an, "published_at", val_date);
                an = xs_dict_set(an, "updated_at",   val_date);
                an = xs_dict_set(an, "read",         (annce->timestamp >= la)
                    ? xs_stock(XSTYPE_FALSE) : xs_stock(XSTYPE_TRUE));
                an = xs_dict_set(an, "mentions",     xs_stock(XSTYPE_LIST));
                an = xs_dict_set(an, "statuses",     xs_stock(XSTYPE_LIST));
                an = xs_dict_set(an, "tags",         xs_stock(XSTYPE_LIST));
                an = xs_dict_set(an, "emojis",       xs_stock(XSTYPE_LIST));
                an = xs_dict_set(an, "reactions",    xs_stock(XSTYPE_LIST));
                resp = xs_list_append(resp, an);
            }

            *body  = xs_json_dumps(resp, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
    }
    else
    if (strcmp(cmd, "/v1/custom_emojis") == 0) { /** **/
        /* are you kidding me? */
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/instance") == 0) { /** **/
        /* returns an instance object */
        xs *ins = xs_dict_new();
        const char *host  = xs_dict_get(srv_config, "host");
        const char *title = xs_dict_get(srv_config, "title");
        const char *sdesc = xs_dict_get(srv_config, "short_description");

        ins = xs_dict_append(ins, "uri",         host);
        ins = xs_dict_append(ins, "domain",      host);
        ins = xs_dict_append(ins, "title",       title && *title ? title : host);
        ins = xs_dict_append(ins, "version",     "4.0.0 (not true; really " USER_AGENT ")");
        ins = xs_dict_append(ins, "source_url",  WHAT_IS_SNAC_URL);
        ins = xs_dict_append(ins, "description", host);

        ins = xs_dict_append(ins, "short_description", sdesc && *sdesc ? sdesc : host);

        xs *susie = xs_fmt("%s/susie.png", srv_baseurl);
        ins = xs_dict_append(ins, "thumbnail", susie);

        const char *v = xs_dict_get(srv_config, "admin_email");
        if (xs_is_null(v) || *v == '\0')
            v = "admin@localhost";

        ins = xs_dict_append(ins, "email", v);

        ins = xs_dict_append(ins, "rules", xs_stock(XSTYPE_LIST));

        xs *l1 = xs_list_append(xs_list_new(), "en");
        ins = xs_dict_append(ins, "languages", l1);

        xs *wss = xs_fmt("wss:/" "/%s", xs_dict_get(srv_config, "host"));
        xs *urls = xs_dict_new();
        urls = xs_dict_append(urls, "streaming_api", wss);

        ins = xs_dict_append(ins, "urls", urls);

        xs *d2 = xs_dict_append(xs_dict_new(), "user_count", xs_stock(0));
        d2 = xs_dict_append(d2, "status_count", xs_stock(0));
        d2 = xs_dict_append(d2, "domain_count", xs_stock(0));
        ins = xs_dict_append(ins, "stats", d2);

        ins = xs_dict_append(ins, "registrations",     xs_stock(XSTYPE_FALSE));
        ins = xs_dict_append(ins, "approval_required", xs_stock(XSTYPE_FALSE));
        ins = xs_dict_append(ins, "invites_enabled",   xs_stock(XSTYPE_FALSE));

        xs *cfg = xs_dict_new();

        {
            xs *d11 = xs_json_loads("{\"characters_reserved_per_url\":32,"
                "\"max_characters\":100000,\"max_media_attachments\":8}");
            cfg = xs_dict_append(cfg, "statuses", d11);

            xs *d12 = xs_json_loads("{\"max_featured_tags\":0}");
            cfg = xs_dict_append(cfg, "accounts", d12);

            xs *d13 = xs_json_loads("{\"image_matrix_limit\":33177600,"
                        "\"image_size_limit\":16777216,"
                        "\"video_frame_rate_limit\":120,"
                        "\"video_matrix_limit\":8294400,"
                        "\"video_size_limit\":103809024}"
            );

            {
                /* get the supported mime types from the internal list */
                const char **p = xs_mime_types;
                xs_set mtypes;
                xs_set_init(&mtypes);

                while (*p) {
                    const char *type = p[1];

                    if (xs_startswith(type, "image/") ||
                        xs_startswith(type, "video/") ||
                        xs_startswith(type, "audio/"))
                        xs_set_add(&mtypes, type);

                    p += 2;
                }

                xs *l = xs_set_result(&mtypes);
                d13 = xs_dict_append(d13, "supported_mime_types", l);
            }

            cfg = xs_dict_append(cfg, "media_attachments", d13);

            xs *d14 = xs_json_loads("{\"max_characters_per_option\":50,"
                "\"max_expiration\":2629746,"
                "\"max_options\":8,\"min_expiration\":300}");
            cfg = xs_dict_append(cfg, "polls", d14);
        }

        ins = xs_dict_append(ins, "configuration", cfg);

        const char *admin_account = xs_dict_get(srv_config, "admin_account");

        if (!xs_is_null(admin_account) && *admin_account) {
            snac admin;

            if (user_open(&admin, admin_account)) {
                xs *actor = msg_actor(&admin);
                xs *acct  = mastoapi_account(NULL, actor);

                ins = xs_dict_append(ins, "contact_account", acct);

                user_free(&admin);
            }
        }

        *body  = xs_json_dumps(ins, 4);
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (xs_startswith(cmd, "/v1/statuses/")) { /** **/
        /* information about a status */
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *id = xs_list_get(l, 3);
            const char *op = xs_list_get(l, 4);

            if (!xs_is_null(id)) {
                xs *msg = NULL;
                xs *out = NULL;

                /* skip the 'fake' part of the id */
                id = MID_TO_MD5(id);

                if (valid_status(object_get_by_md5(id, &msg))) {
                    if (op == NULL) {
                        if (!is_muted(&snac1, get_atto(msg))) {
                            /* return the status itself */
                            out = mastoapi_status(&snac1, msg);
                        }
                    }
                    else
                    if (strcmp(op, "context") == 0) { /** **/
                        /* return ancestors and children */
                        xs *anc = xs_list_new();
                        xs *des = xs_list_new();
                        xs_list *p;
                        const xs_str *v;
                        char pid[MD5_HEX_SIZE];

                        /* build the [grand]parent list, moving up */
                        strncpy(pid, id, sizeof(pid));

                        while (object_parent(pid, pid)) {
                            xs *m2 = NULL;

                            if (valid_status(timeline_get_by_md5(&snac1, pid, &m2))) {
                                xs *st = mastoapi_status(&snac1, m2);

                                if (st)
                                    anc = xs_list_insert(anc, 0, st);
                            }
                            else
                                break;
                        }

                        /* build the children list */
                        xs *children = object_children(xs_dict_get(msg, "id"));
                        p = children;

                        while (xs_list_iter(&p, &v)) {
                            xs *m2 = NULL;

                            if (valid_status(timeline_get_by_md5(&snac1, v, &m2))) {
                                if (xs_is_null(xs_dict_get(m2, "name"))) {
                                    xs *st = mastoapi_status(&snac1, m2);

                                    if (st)
                                        des = xs_list_append(des, st);
                                }
                            }
                        }

                        out = xs_dict_new();
                        out = xs_dict_append(out, "ancestors",   anc);
                        out = xs_dict_append(out, "descendants", des);
                    }
                    else
                    if (strcmp(op, "reblogged_by") == 0 || /** **/
                        strcmp(op, "favourited_by") == 0) { /** **/
                        /* return the list of people who liked or boosted this */
                        out = xs_list_new();

                        xs *l = NULL;

                        if (op[0] == 'r')
                            l = object_announces(xs_dict_get(msg, "id"));
                        else
                            l = object_likes(xs_dict_get(msg, "id"));

                        xs_list *p = l;
                        const xs_str *v;

                        while (xs_list_iter(&p, &v)) {
                            xs *actor2 = NULL;

                            if (valid_status(object_get_by_md5(v, &actor2))) {
                                xs *acct2 = mastoapi_account(&snac1, actor2);

                                out = xs_list_append(out, acct2);
                            }
                        }
                    }
                    else
                    if (strcmp(op, "source") == 0) { /** **/
                        out = xs_dict_new();

                        /* get the mastoapi status id */
                        out = xs_dict_append(out, "id", xs_list_get(l, 3));

                        out = xs_dict_append(out, "text", xs_dict_get(msg, "sourceContent"));
                        out = xs_dict_append(out, "spoiler_text", xs_dict_get(msg, "summary"));
                    }
                }
                else
                    srv_debug(1, xs_fmt("mastoapi status: bad id %s", id));

                if (out != NULL) {
                    *body  = xs_json_dumps(out, 4);
                    *ctype = "application/json";
                    status = HTTP_STATUS_OK;
                }
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/preferences") == 0) { /** **/
        *body  = xs_dup("{}");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/markers") == 0) { /** **/
        *body  = xs_dup("{}");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/followed_tags") == 0) { /** **/
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/trends/tags") == 0) { /** **/
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v1/trends/statuses") == 0) { /** **/
        *body  = xs_dup("[]");
        *ctype = "application/json";
        status = HTTP_STATUS_OK;
    }
    else
    if (strcmp(cmd, "/v2/search") == 0) { /** **/
        if (logged_in) {
            const char *q      = xs_dict_get(args, "q");
            const char *type   = xs_dict_get(args, "type");
            const char *offset = xs_dict_get(args, "offset");

            xs *acl = xs_list_new();
            xs *stl = xs_list_new();
            xs *htl = xs_list_new();
            xs *res = xs_dict_new();

            if (xs_is_null(offset) || strcmp(offset, "0") == 0) {
                /* reply something only for offset 0; otherwise,
                   apps like Tusky keep asking again and again */

                if (!xs_is_null(q)) {
                    if (xs_is_null(type) || strcmp(type, "accounts") == 0) {
                        /* do a webfinger query */
                        char *actor = NULL;
                        char *user  = NULL;

                        if (valid_status(webfinger_request(q, &actor, &user)) && actor) {
                            xs *actor_o = NULL;

                            if (valid_status(actor_request(&snac1, actor, &actor_o))) {
                                xs *acct = mastoapi_account(NULL, actor_o);

                                acl = xs_list_append(acl, acct);

                                if (!object_here(actor))
                                    object_add(actor, actor_o);
                            }
                        }
                    }

                    if (xs_is_null(type) || strcmp(type, "hashtags") == 0) {
                        /* search this tag */
                        xs *tl = tag_search((char *)q, 0, 1);

                        if (xs_list_len(tl)) {
                            xs *d = xs_dict_new();

                            d = xs_dict_append(d, "name", q);
                            xs *url = xs_fmt("%s?t=%s", srv_baseurl, q);
                            d = xs_dict_append(d, "url", url);
                            d = xs_dict_append(d, "history", xs_stock(XSTYPE_LIST));

                            htl = xs_list_append(htl, d);
                        }
                    }

                    if (xs_is_null(type) || strcmp(type, "statuses") == 0) {
                        int to = 0;
                        int cnt = 40;
                        xs *tl = content_search(&snac1, q, 1, 0, cnt, 0, &to);
                        int c = 0;
                        const char *v;

                        while (xs_list_next(tl, &v, &c) && --cnt) {
                            xs *post = NULL;

                            if (!valid_status(timeline_get_by_md5(&snac1, v, &post)))
                                continue;

                            xs *s = mastoapi_status(&snac1, post);

                            if (!xs_is_null(s))
                                stl = xs_list_append(stl, s);
                        }
                    }
                }
            }

            res = xs_dict_append(res, "accounts", acl);
            res = xs_dict_append(res, "statuses", stl);
            res = xs_dict_append(res, "hashtags", htl);

            *body  = xs_json_dumps(res, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac1);

    srv_debug(1, xs_fmt("mastoapi_get_handler %s %d", q_path, status));

    return status;
}


int mastoapi_post_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
{
    (void)p_size;
    (void)b_size;

    if (!xs_startswith(q_path, "/api/v1/") && !xs_startswith(q_path, "/api/v2/"))
        return 0;

    int status    = HTTP_STATUS_NOT_FOUND;
    xs *args      = NULL;
    const char *i_ctype = xs_dict_get(req, "content-type");

    if (i_ctype && xs_startswith(i_ctype, "application/json")) {
        if (!xs_is_null(payload))
            args = xs_json_loads(payload);
    }
    else if (i_ctype && xs_startswith(i_ctype, "application/x-www-form-urlencoded"))
    {
        // Some apps send form data instead of json so we should cater for those
        if (!xs_is_null(payload)) {
            args    = xs_url_vars(payload);
        }
    }
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    if (args == NULL)
        return HTTP_STATUS_BAD_REQUEST;

    xs *cmd = xs_replace_n(q_path, "/api", "", 1);

    snac snac = {0};
    int logged_in = process_auth_token(&snac, req);

    if (strcmp(cmd, "/v1/apps") == 0) { /** **/
        const char *name  = xs_dict_get(args, "client_name");
        const char *ruri  = xs_dict_get(args, "redirect_uris");
        const char *scope = xs_dict_get(args, "scope");

        /* Ice Cubes sends these values as query parameters, so try these */
        if (name == NULL && ruri == NULL && scope == NULL) {
            args = xs_dup(xs_dict_get(req, "q_vars"));
            name  = xs_dict_get(args, "client_name");
            ruri  = xs_dict_get(args, "redirect_uris");
            scope = xs_dict_get(args, "scope");
        }

        if (xs_type(ruri) == XSTYPE_LIST)
            ruri = xs_dict_get(ruri, 0);

        if (name && ruri) {
            xs *app  = xs_dict_new();
            xs *id   = xs_replace_i(tid(0), ".", "");
            xs *csec = random_str();
            xs *vkey = random_str();
            xs *cid  = NULL;

            /* pick a non-existent random cid */
            for (;;) {
                cid = random_str();
                xs *p_app = app_get(cid);

                if (p_app == NULL)
                    break;

                xs_free(cid);
            }

            app = xs_dict_append(app, "name",          name);
            app = xs_dict_append(app, "redirect_uri",  ruri);
            app = xs_dict_append(app, "client_id",     cid);
            app = xs_dict_append(app, "client_secret", csec);
            app = xs_dict_append(app, "vapid_key",     vkey);
            app = xs_dict_append(app, "id",            id);

            *body  = xs_json_dumps(app, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;

            app = xs_dict_append(app, "code", "");

            if (scope)
                app = xs_dict_append(app, "scope", scope);

            app_add(cid, app);

            srv_debug(1, xs_fmt("mastoapi apps: new app %s", cid));
        }
    }
    else
    if (strcmp(cmd, "/v1/statuses") == 0) { /** **/
        if (logged_in) {
            /* post a new Note */
            const char *content    = xs_dict_get(args, "status");
            const char *mid        = xs_dict_get(args, "in_reply_to_id");
            const char *visibility = xs_dict_get(args, "visibility");
            const char *summary    = xs_dict_get(args, "spoiler_text");
            const char *media_ids  = xs_dict_get(args, "media_ids");

            if (xs_is_null(media_ids))
                media_ids = xs_dict_get(args, "media_ids[]");

            if (xs_is_null(media_ids))
                media_ids = xs_dict_get(args, "media_ids");

            if (xs_is_null(visibility))
                visibility = "public";

            xs *attach_list = xs_list_new();
            xs *irt         = NULL;

            /* is it a reply? */
            if (mid != NULL) {
                xs *r_msg = NULL;
                const char *md5 = MID_TO_MD5(mid);

                if (valid_status(object_get_by_md5(md5, &r_msg)))
                    irt = xs_dup(xs_dict_get(r_msg, "id"));
            }

            /* does it have attachments? */
            if (!xs_is_null(media_ids)) {
                xs *mi = NULL;

                if (xs_type(media_ids) == XSTYPE_LIST)
                    mi = xs_dup(media_ids);
                else {
                    mi = xs_list_new();
                    mi = xs_list_append(mi, media_ids);
                }

                xs_list *p = mi;
                const xs_str *v;

                while (xs_list_iter(&p, &v)) {
                    xs *l    = xs_list_new();
                    xs *url  = xs_fmt("%s/s/%s", snac.actor, v);
                    xs *desc = static_get_meta(&snac, v);

                    l = xs_list_append(l, url);
                    l = xs_list_append(l, desc);

                    attach_list = xs_list_append(attach_list, l);
                }
            }

            /* prepare the message */
            xs *msg = msg_note(&snac, content, NULL, irt, attach_list,
                        strcmp(visibility, "public") == 0 ? 0 : 1);

            if (!xs_is_null(summary) && *summary) {
                msg = xs_dict_set(msg, "sensitive", xs_stock(XSTYPE_TRUE));
                msg = xs_dict_set(msg, "summary",   summary);
            }

            /* store */
            timeline_add(&snac, xs_dict_get(msg, "id"), msg);

            /* 'Create' message */
            xs *c_msg = msg_create(&snac, msg);
            enqueue_message(&snac, c_msg);

            timeline_touch(&snac);

            /* convert to a mastodon status as a response code */
            xs *st = mastoapi_status(&snac, msg);

            *body  = xs_json_dumps(st, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (xs_startswith(cmd, "/v1/statuses")) { /** **/
        if (logged_in) {
            /* operations on a status */
            xs *l = xs_split(cmd, "/");
            const char *mid = xs_list_get(l, 3);
            const char *op  = xs_list_get(l, 4);

            if (!xs_is_null(mid)) {
                xs *msg = NULL;
                xs *out = NULL;

                /* skip the 'fake' part of the id */
                mid = MID_TO_MD5(mid);

                if (valid_status(timeline_get_by_md5(&snac, mid, &msg))) {
                    const char *id = xs_dict_get(msg, "id");

                    if (op == NULL) {
                        /* no operation (?) */
                    }
                    else
                    if (strcmp(op, "favourite") == 0) { /** **/
                        xs *n_msg = msg_admiration(&snac, id, "Like");

                        if (n_msg != NULL) {
                            enqueue_message(&snac, n_msg);
                            timeline_admire(&snac, xs_dict_get(n_msg, "object"), snac.actor, 1);

                            out = mastoapi_status(&snac, msg);
                        }
                    }
                    else
                    if (strcmp(op, "unfavourite") == 0) { /** **/
                        xs *n_msg = msg_repulsion(&snac, id, "Like");

                        if (n_msg != NULL) {
                            enqueue_message(&snac, n_msg);

                            out = mastoapi_status(&snac, msg);
                        }
                    }
                    else
                    if (strcmp(op, "reblog") == 0) { /** **/
                        xs *n_msg = msg_admiration(&snac, id, "Announce");

                        if (n_msg != NULL) {
                            enqueue_message(&snac, n_msg);
                            timeline_admire(&snac, xs_dict_get(n_msg, "object"), snac.actor, 0);

                            out = mastoapi_status(&snac, msg);
                        }
                    }
                    else
                    if (strcmp(op, "unreblog") == 0) { /** **/
                        xs *n_msg = msg_repulsion(&snac, id, "Announce");

                        if (n_msg != NULL) {
                            enqueue_message(&snac, n_msg);

                            out = mastoapi_status(&snac, msg);
                        }
                    }
                    else
                    if (strcmp(op, "bookmark") == 0) { /** **/
                        /* bookmark this message */
                        if (bookmark(&snac, id) == 0)
                            out = mastoapi_status(&snac, msg);
                        else
                            status = HTTP_STATUS_UNPROCESSABLE_CONTENT;
                    }
                    else
                    if (strcmp(op, "unbookmark") == 0) { /** **/
                        /* unbookmark this message */
                        unbookmark(&snac, id);
                        out = mastoapi_status(&snac, msg);
                    }
                    else
                    if (strcmp(op, "pin") == 0) { /** **/
                        /* pin this message */
                        if (pin(&snac, id) == 0)
                            out = mastoapi_status(&snac, msg);
                        else
                            status = HTTP_STATUS_UNPROCESSABLE_CONTENT;
                    }
                    else
                    if (strcmp(op, "unpin") == 0) { /** **/
                        /* unpin this message */
                        unpin(&snac, id);
                        out = mastoapi_status(&snac, msg);
                    }
                    else
                    if (strcmp(op, "mute") == 0) { /** **/
                        /* Mastodon's mute is snac's hide */
                    }
                    else
                    if (strcmp(op, "unmute") == 0) { /** **/
                        /* Mastodon's unmute is snac's unhide */
                    }
                }

                if (out != NULL) {
                    *body  = xs_json_dumps(out, 4);
                    *ctype = "application/json";
                    status = HTTP_STATUS_OK;
                }
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/notifications/clear") == 0) { /** **/
        if (logged_in) {
            notify_clear(&snac);
            timeline_touch(&snac);

            *body  = xs_dup("{}");
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/push/subscription") == 0) { /** **/
        /* I don't know what I'm doing */
        if (logged_in) {
            const char *v;

            xs *wpush = xs_dict_new();

            wpush = xs_dict_append(wpush, "id", "1");

            v = xs_dict_get(args, "data");
            v = xs_dict_get(v, "alerts");
            wpush = xs_dict_append(wpush, "alerts", v);

            v = xs_dict_get(args, "subscription");
            v = xs_dict_get(v, "endpoint");
            wpush = xs_dict_append(wpush, "endpoint", v);

            xs *server_key = random_str();
            wpush = xs_dict_append(wpush, "server_key", server_key);

            *body  = xs_json_dumps(wpush, 4);
            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/media") == 0 || strcmp(cmd, "/v2/media") == 0) { /** **/
        if (logged_in) {
            const xs_list *file = xs_dict_get(args, "file");
            const char *desc    = xs_dict_get(args, "description");

            if (xs_is_null(desc))
                desc = "";

            status = HTTP_STATUS_BAD_REQUEST;

            if (xs_type(file) == XSTYPE_LIST) {
                const char *fn = xs_list_get(file, 0);

                if (*fn != '\0') {
                    char *ext = strrchr(fn, '.');
                    xs *hash  = xs_md5_hex(fn, strlen(fn));
                    xs *id    = xs_fmt("%s%s", hash, ext);
                    xs *url   = xs_fmt("%s/s/%s", snac.actor, id);
                    int fo    = xs_number_get(xs_list_get(file, 1));
                    int fs    = xs_number_get(xs_list_get(file, 2));

                    /* store */
                    static_put(&snac, id, payload + fo, fs);
                    static_put_meta(&snac, id, desc);

                    /* prepare a response */
                    xs *rsp = xs_dict_new();

                    rsp = xs_dict_append(rsp, "id",          id);
                    rsp = xs_dict_append(rsp, "type",        "image");
                    rsp = xs_dict_append(rsp, "url",         url);
                    rsp = xs_dict_append(rsp, "preview_url", url);
                    rsp = xs_dict_append(rsp, "remote_url",  url);
                    rsp = xs_dict_append(rsp, "description", desc);

                    *body  = xs_json_dumps(rsp, 4);
                    *ctype = "application/json";
                    status = HTTP_STATUS_OK;
                }
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (xs_startswith(cmd, "/v1/accounts")) { /** **/
        if (logged_in) {
            /* account-related information */
            xs *l = xs_split(cmd, "/");
            const char *md5 = xs_list_get(l, 3);
            const char *opt = xs_list_get(l, 4);
            xs *rsp = NULL;

            if (!xs_is_null(md5) && *md5) {
                xs *actor_o = NULL;

                if (xs_is_null(opt)) {
                    /* ? */
                }
                else
                if (strcmp(opt, "follow") == 0) { /** **/
                    if (valid_status(object_get_by_md5(md5, &actor_o))) {
                        const char *actor = xs_dict_get(actor_o, "id");

                        xs *msg = msg_follow(&snac, actor);

                        if (msg != NULL) {
                            /* reload the actor from the message, in may be different */
                            actor = xs_dict_get(msg, "object");

                            following_add(&snac, actor, msg);

                            enqueue_output_by_actor(&snac, msg, actor, 0);

                            rsp = mastoapi_relationship(&snac, md5);
                        }
                    }
                }
                else
                if (strcmp(opt, "unfollow") == 0) { /** **/
                    if (valid_status(object_get_by_md5(md5, &actor_o))) {
                        const char *actor = xs_dict_get(actor_o, "id");

                        /* get the following object */
                        xs *object = NULL;

                        if (valid_status(following_get(&snac, actor, &object))) {
                            xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                            following_del(&snac, actor);

                            enqueue_output_by_actor(&snac, msg, actor, 0);

                            rsp = mastoapi_relationship(&snac, md5);
                        }
                    }
                }
                else
                if (strcmp(opt, "block") == 0) { /** **/
                    if (valid_status(object_get_by_md5(md5, &actor_o))) {
                        const char *actor = xs_dict_get(actor_o, "id");

                        mute(&snac, actor);

                        rsp = mastoapi_relationship(&snac, md5);
                    }
                }
                else
                if (strcmp(opt, "unblock") == 0) { /** **/
                    if (valid_status(object_get_by_md5(md5, &actor_o))) {
                        const char *actor = xs_dict_get(actor_o, "id");

                        unmute(&snac, actor);

                        rsp = mastoapi_relationship(&snac, md5);
                    }
                }
            }

            if (rsp != NULL) {
                *body  = xs_json_dumps(rsp, 4);
                *ctype = "application/json";
                status = HTTP_STATUS_OK;
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (xs_startswith(cmd, "/v1/polls")) { /** **/
        if (logged_in) {
            /* operations on a status */
            xs *l = xs_split(cmd, "/");
            const char *mid = xs_list_get(l, 3);
            const char *op  = xs_list_get(l, 4);

            if (!xs_is_null(mid)) {
                xs *msg = NULL;
                xs *out = NULL;

                /* skip the 'fake' part of the id */
                mid = MID_TO_MD5(mid);

                if (valid_status(timeline_get_by_md5(&snac, mid, &msg))) {
                    const char *id   = xs_dict_get(msg, "id");
                    const char *atto = get_atto(msg);

                    const xs_list *opts = xs_dict_get(msg, "oneOf");
                    if (opts == NULL)
                        opts = xs_dict_get(msg, "anyOf");

                    if (op == NULL) {
                    }
                    else
                    if (strcmp(op, "votes") == 0) {
                        const xs_list *choices = xs_dict_get(args, "choices[]");

                        if (xs_is_null(choices))
                            choices = xs_dict_get(args, "choices");

                        if (xs_type(choices) == XSTYPE_LIST) {
                            const xs_str *v;

                            int c = 0;
                            while (xs_list_next(choices, &v, &c)) {
                                int io           = atoi(v);
                                const xs_dict *o = xs_list_get(opts, io);

                                if (o) {
                                    const char *name = xs_dict_get(o, "name");

                                    xs *msg = msg_note(&snac, "", atto, (char *)id, NULL, 1);
                                    msg = xs_dict_append(msg, "name", name);

                                    xs *c_msg = msg_create(&snac, msg);
                                    enqueue_message(&snac, c_msg);
                                    timeline_add(&snac, xs_dict_get(msg, "id"), msg);
                                }
                            }

                            out = mastoapi_poll(&snac, msg);
                        }
                    }
                }

                if (out != NULL) {
                    *body  = xs_json_dumps(out, 4);
                    *ctype = "application/json";
                    status = HTTP_STATUS_OK;
                }
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (strcmp(cmd, "/v1/lists") == 0) {
        if (logged_in) {
            const char *title = xs_dict_get(args, "title");

            if (xs_type(title) == XSTYPE_STRING) {
                /* add the list */
                xs *out = xs_dict_new();
                xs *lid = list_maint(&snac, title, 1);

                if (!xs_is_null(lid)) {
                    out = xs_dict_append(out, "id", lid);
                    out = xs_dict_append(out, "title", title);
                    out = xs_dict_append(out, "replies_policy", xs_dict_get_def(args, "replies_policy", "list"));
                    out = xs_dict_append(out, "exclusive", xs_stock(XSTYPE_FALSE));

                    status = HTTP_STATUS_OK;
                }
                else {
                    out = xs_dict_append(out, "error", "cannot create list");
                    status = HTTP_STATUS_UNPROCESSABLE_CONTENT;
                }

                *body  = xs_json_dumps(out, 4);
                *ctype = "application/json";
            }
            else
                status = HTTP_STATUS_UNPROCESSABLE_CONTENT;
        }
    }
    if (xs_startswith(cmd, "/v1/lists/")) { /** list maintenance **/
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *op = xs_list_get(l, -1);
            const char *id = xs_list_get(l, -2);

            if (op && id && xs_is_hex(id)) {
                if (strcmp(op, "accounts") == 0) {
                    const xs_list *accts = xs_dict_get(args, "account_ids[]");

                    if (xs_is_null(accts))
                        accts = xs_dict_get(args, "account_ids");

                    int c = 0;
                    const char *v;

                    while (xs_list_next(accts, &v, &c)) {
                        list_content(&snac, id, v, 1);
                    }

                    xs *out = xs_dict_new();
                    *body   = xs_json_dumps(out, 4);
                    *ctype  = "application/json";
                    status  = HTTP_STATUS_OK;
                }
            }
        }
        else
            status = HTTP_STATUS_UNPROCESSABLE_CONTENT;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac);

    srv_debug(1, xs_fmt("mastoapi_post_handler %s %d", q_path, status));

    return status;
}


int mastoapi_delete_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
{
    (void)p_size;
    (void)body;
    (void)b_size;
    (void)ctype;

    if (!xs_startswith(q_path, "/api/v1/") && !xs_startswith(q_path, "/api/v2/"))
        return 0;

    int status    = HTTP_STATUS_NOT_FOUND;
    xs *args      = NULL;
    const char *i_ctype = xs_dict_get(req, "content-type");

    if (i_ctype && xs_startswith(i_ctype, "application/json")) {
        if (!xs_is_null(payload))
            args = xs_json_loads(payload);
    }
    else if (i_ctype && xs_startswith(i_ctype, "application/x-www-form-urlencoded"))
    {
        // Some apps send form data instead of json so we should cater for those
        if (!xs_is_null(payload)) {
            args = xs_url_vars(payload);
        }
    }
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    if (args == NULL)
        return HTTP_STATUS_BAD_REQUEST;

    snac snac = {0};
    int logged_in = process_auth_token(&snac, req);

    xs *cmd = xs_replace_n(q_path, "/api", "", 1);

    if (xs_startswith(cmd, "/v1/push/subscription") || xs_startswith(cmd, "/v2/push/subscription")) { /** **/
        // pretend we deleted it, since it doesn't exist anyway
        status = HTTP_STATUS_OK;
    }
    else
    if (xs_startswith(cmd, "/v1/lists/")) {
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *p = xs_list_get(l, -1);

            if (p) {
                if (strcmp(p, "accounts") == 0) {
                    /* delete account from list */
                    p = xs_list_get(l, -2);
                    const xs_list *accts = xs_dict_get(args, "account_ids[]");

                    if (xs_is_null(accts))
                        accts = xs_dict_get(args, "account_ids");

                    int c = 0;
                    const char *v;

                    while (xs_list_next(accts, &v, &c)) {
                        list_content(&snac, p, v, 2);
                    }
                }
                else {
                    /* delete list */
                    if (xs_is_hex(p)) {
                        list_maint(&snac, p, 2);
                    }
                }
            }

            *ctype = "application/json";
            status = HTTP_STATUS_OK;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac);

    srv_debug(1, xs_fmt("mastoapi_delete_handler %s %d", q_path, status));

    return status;
}


int mastoapi_put_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
{
    (void)p_size;
    (void)b_size;

    if (!xs_startswith(q_path, "/api/v1/") && !xs_startswith(q_path, "/api/v2/"))
        return 0;

    int status    = HTTP_STATUS_NOT_FOUND;
    xs *args      = NULL;
    const char *i_ctype = xs_dict_get(req, "content-type");

    if (i_ctype && xs_startswith(i_ctype, "application/json")) {
        if (!xs_is_null(payload))
            args = xs_json_loads(payload);
    }
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    if (args == NULL)
        return HTTP_STATUS_BAD_REQUEST;

    xs *cmd = xs_replace_n(q_path, "/api", "", 1);

    snac snac = {0};
    int logged_in = process_auth_token(&snac, req);

    if (xs_startswith(cmd, "/v1/media") || xs_startswith(cmd, "/v2/media")) { /** **/
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *stid = xs_list_get(l, 3);

            if (!xs_is_null(stid)) {
                const char *desc = xs_dict_get(args, "description");

                /* set the image metadata */
                static_put_meta(&snac, stid, desc);

                /* prepare a response */
                xs *rsp = xs_dict_new();
                xs *url = xs_fmt("%s/s/%s", snac.actor, stid);

                rsp = xs_dict_append(rsp, "id",          stid);
                rsp = xs_dict_append(rsp, "type",        "image");
                rsp = xs_dict_append(rsp, "url",         url);
                rsp = xs_dict_append(rsp, "preview_url", url);
                rsp = xs_dict_append(rsp, "remote_url",  url);
                rsp = xs_dict_append(rsp, "description", desc);

                *body  = xs_json_dumps(rsp, 4);
                *ctype = "application/json";
                status = HTTP_STATUS_OK;
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    if (xs_startswith(cmd, "/v1/statuses")) {
        if (logged_in) {
            xs *l = xs_split(cmd, "/");
            const char *mid = xs_list_get(l, 3);

            if (!xs_is_null(mid)) {
                const char *md5 = MID_TO_MD5(mid);
                xs *rsp = NULL;
                xs *msg = NULL;

                if (valid_status(timeline_get_by_md5(&snac, md5, &msg))) {
                    const char *content = xs_dict_get(args, "status");
                    xs *atls = xs_list_new();
                    xs *f_content = not_really_markdown(content, &atls, NULL);

                    /* replace fields with new content */
                    msg = xs_dict_set(msg, "sourceContent", content);
                    msg = xs_dict_set(msg, "content", f_content);

                    xs *updated = xs_str_utctime(0, ISO_DATE_SPEC);
                    msg = xs_dict_set(msg, "updated", updated);

                    /* overwrite object, not updating the indexes */
                    object_add_ow(xs_dict_get(msg, "id"), msg);

                    /* update message */
                    xs *c_msg = msg_update(&snac, msg);

                    enqueue_message(&snac, c_msg);

                    rsp = mastoapi_status(&snac, msg);
                }

                if (rsp != NULL) {
                    *body  = xs_json_dumps(rsp, 4);
                    *ctype = "application/json";
                    status = HTTP_STATUS_OK;
                }
            }
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac);

    srv_debug(1, xs_fmt("mastoapi_put_handler %s %d", q_path, status));

    return status;
}

void persist_image(const char *key, const xs_val *data, const char *payload, snac *snac)
/* Store header or avatar */
{
    if (data != NULL) {
        if (xs_type(data) == XSTYPE_LIST) {
            const char *fn = xs_list_get(data, 0);

            if (fn && *fn) {
                const char *ext = strrchr(fn, '.');

                /* Mona iOS sends always jpg as application/octet-stream with no filename */
                if (ext == NULL || strcmp(fn, key) == 0) {
                    ext = ".jpg";
                }

                /* Make sure we have a unique file name, otherwise updated images will not be
                 * re-loaded by clients. */
                xs *rnd         = random_str();
                xs *hash        = xs_md5_hex(rnd, strlen(rnd));
                xs *id          = xs_fmt("%s%s", hash, ext);
                xs *url         = xs_fmt("%s/s/%s", snac->actor, id);
                int fo          = xs_number_get(xs_list_get(data, 1));
                int fs          = xs_number_get(xs_list_get(data, 2));

                /* store */
                static_put(snac, id, payload + fo, fs);

                snac->config = xs_dict_set(snac->config, key, url);
            }
        }
    }
}

int mastoapi_patch_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
/* Handle profile updates */
{
    (void)p_size;
    (void)b_size;

    if (!xs_startswith(q_path, "/api/v1/"))
        return 0;

    int status    = HTTP_STATUS_NOT_FOUND;
    xs *args      = NULL;
    const char *i_ctype = xs_dict_get(req, "content-type");

    if (i_ctype && xs_startswith(i_ctype, "application/json")) {
        if (!xs_is_null(payload))
            args = xs_json_loads(payload);
    }
    else if (i_ctype && xs_startswith(i_ctype, "application/x-www-form-urlencoded"))
    {
        // Some apps send form data instead of json so we should cater for those
        if (!xs_is_null(payload)) {
            args    = xs_url_vars(payload);
        }
    }
    else
        args = xs_dup(xs_dict_get(req, "p_vars"));

    if (args == NULL)
        return HTTP_STATUS_BAD_REQUEST;

    xs *cmd = xs_replace_n(q_path, "/api", "", 1);

    snac snac = {0};
    int logged_in = process_auth_token(&snac, req);

    if (xs_startswith(cmd, "/v1/accounts/update_credentials")) {
        /* Update user profile fields */
        if (logged_in) {
            int c = 0;
            const xs_str *k;
            const xs_val *v;
            const xs_str *field_name = NULL;
            xs *new_fields = xs_dict_new();
            while (xs_dict_next(args, &k, &v, &c)) {
                if (strcmp(k, "display_name") == 0) {
                    if (v != NULL)
                        snac.config = xs_dict_set(snac.config, "name", v);
                }
                else
                if (strcmp(k, "note") == 0) {
                    if (v != NULL)
                        snac.config = xs_dict_set(snac.config, "bio", v);
                }
                else
                if (strcmp(k, "bot") == 0) {
                    if (v != NULL)
                        snac.config = xs_dict_set(snac.config, "bot",
                            (strcmp(v, "true") == 0 ||
                                strcmp(v, "1") == 0) ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));
                }
                else
                if (strcmp(k, "source[sensitive]") == 0) {
                    if (v != NULL)
                        snac.config = xs_dict_set(snac.config, "cw",
                            strcmp(v, "true") == 0 ? "open" : "");
                }
                else
                if (strcmp(k, "source[privacy]") == 0) {
                    if (v != NULL)
                        snac.config = xs_dict_set(snac.config, "private",
                            strcmp(v, "private") == 0 ? xs_stock(XSTYPE_TRUE) : xs_stock(XSTYPE_FALSE));
                }
                else
                if (strcmp(k, "header") == 0) {
                    persist_image("header", v, payload, &snac);
                }
                else
                if (strcmp(k, "avatar") == 0) {
                    persist_image("avatar", v, payload, &snac);
                }
                else
                if (xs_between("fields_attributes", k, "[name]")) {
                    field_name = strcmp(v, "") != 0 ? v : NULL;
                }
                else
                if (xs_between("fields_attributes", k, "[value]")) {
                    if (field_name != NULL) {
                        new_fields = xs_dict_set(new_fields, field_name, v);
                        snac.config = xs_dict_set(snac.config, "metadata", new_fields);
                    }
                }
                /* we don't have support for the following options, yet
                   - discoverable (0/1)
                   - locked (0/1)
                 */
            }

            /* Persist profile */
            if (user_persist(&snac, 1) == 0)
                credentials_get(body, ctype, &status, snac);
            else
                status = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        else
            status = HTTP_STATUS_UNAUTHORIZED;
    }

    /* user cleanup */
    if (logged_in)
        user_free(&snac);

    srv_debug(1, xs_fmt("mastoapi_patch_handler %s %d", q_path, status));

    return status;
}


void mastoapi_purge(void)
{
    xs *spec   = xs_fmt("%s/app/" "*.json", srv_basedir);
    xs *files  = xs_glob(spec, 1, 0);
    xs_list *p = files;
    const xs_str *v;

    time_t mt = time(NULL) - 3600;

    while (xs_list_iter(&p, &v)) {
        xs *cid = xs_replace(v, ".json", "");
        xs *fn  = _app_fn(cid);

        if (mtime(fn) < mt) {
            /* get the app */
            xs *app = app_get(cid);

            if (app) {
                /* old apps with no uid are incomplete cruft */
                const char *uid = xs_dict_get(app, "uid");

                if (xs_is_null(uid) || *uid == '\0') {
                    unlink(fn);
                    srv_debug(2, xs_fmt("purged %s", fn));
                }
            }
        }
    }
}


#endif /* #ifndef NO_MASTODON_API */
