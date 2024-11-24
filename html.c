/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_openssl.h"
#include "xs_time.h"
#include "xs_mime.h"
#include "xs_match.h"
#include "xs_html.h"
#include "xs_curl.h"

#include "snac.h"

int login(snac *snac, const xs_dict *headers)
/* tries a login */
{
    int logged_in = 0;
    const char *auth = xs_dict_get(headers, "authorization");

    if (auth && xs_startswith(auth, "Basic ")) {
        int sz;
        xs *s1 = xs_crop_i(xs_dup(auth), 6, 0);
        xs *s2 = xs_base64_dec(s1, &sz);

        xs *l1 = xs_split_n(s2, ":", 1);

        if (xs_list_len(l1) == 2) {
            logged_in = check_password(
                xs_list_get(l1, 0), xs_list_get(l1, 1),
                xs_dict_get(snac->config, "passwd"));
        }
    }

    if (logged_in)
        lastlog_write(snac, "web");

    return logged_in;
}


xs_str *replace_shortnames(xs_str *s, const xs_list *tag, int ems, const char *proxy)
/* replaces all the :shortnames: with the emojis in tag */
{
    if (!xs_is_null(tag)) {
        xs *tag_list = NULL;
        if (xs_type(tag) == XSTYPE_DICT) {
            /* not a list */
            tag_list = xs_list_new();
            tag_list = xs_list_append(tag_list, tag);
        } else {
            /* is a list */
            tag_list = xs_dup(tag);
        }

        xs *style = xs_fmt("height: %dem; width: %dem; vertical-align: middle;", ems, ems);

        const char *v;
        int c = 0;

        while (xs_list_next(tag_list, &v, &c)) {
            const char *t = xs_dict_get(v, "type");

            if (t && strcmp(t, "Emoji") == 0) {
                const char *n = xs_dict_get(v, "name");
                const char *i = xs_dict_get(v, "icon");

                if (n && i) {
                    const char *u = xs_dict_get(i, "url");
                    xs *url = make_url(u, proxy, 0);

                    xs_html *img = xs_html_sctag("img",
                        xs_html_attr("loading", "lazy"),
                        xs_html_attr("src", url),
                        xs_html_attr("style", style));

                    xs *s1 = xs_html_render(img);
                    s = xs_replace_i(s, n, s1);
                }
            }
        }
    }

    return s;
}


xs_str *actor_name(xs_dict *actor, const char *proxy)
/* gets the actor name */
{
    const char *v;

    if (xs_is_null((v = xs_dict_get(actor, "name"))) || *v == '\0') {
        if (xs_is_null(v = xs_dict_get(actor, "preferredUsername")) || *v == '\0') {
            v = "anonymous";
        }
    }

    return replace_shortnames(xs_html_encode(v), xs_dict_get(actor, "tag"), 1, proxy);
}


xs_html *html_actor_icon(snac *user, xs_dict *actor, const char *date,
                        const char *udate, const char *url, int priv, int in_people, const char *proxy)
{
    xs_html *actor_icon = xs_html_tag("p", NULL);

    xs *avatar = NULL;
    const char *v;
    int fwing = 0;
    int fwer = 0;

    xs *name = actor_name(actor, proxy);

    /* get the avatar */
    if ((v = xs_dict_get(actor, "icon")) != NULL) {
        /* if it's a list (Peertube), get the first one */
        if (xs_type(v) == XSTYPE_LIST)
            v = xs_list_get(v, 0);

        if ((v = xs_dict_get(v, "url")) != NULL)
            avatar = make_url(v, proxy, 0);
    }

    if (avatar == NULL)
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());

    const char *actor_id = xs_dict_get(actor, "id");
    xs *href = NULL;

    if (user) {
        fwer = follower_check(user, actor_id);
        fwing = following_check(user, actor_id);
    }

    if (user && !in_people) {
        /* if this actor is a follower or being followed, create an
           anchored link to the people page instead of the actor url */
        if (fwer || fwing) {
            xs *md5 = xs_md5_hex(actor_id, strlen(actor_id));
            href = xs_fmt("%s/people#%s", user->actor, md5);
        }
    }

    if (href == NULL)
        href = xs_dup(actor_id);

    xs_html_add(actor_icon,
        xs_html_sctag("img",
            xs_html_attr("loading", "lazy"),
            xs_html_attr("class",   "snac-avatar"),
            xs_html_attr("src",     avatar),
            xs_html_attr("alt",     "")),
        xs_html_tag("a",
            xs_html_attr("href",    href),
            xs_html_attr("class",   "p-author h-card snac-author"),
            xs_html_raw(name))); /* name is already html-escaped */

    if (!xs_is_null(url)) {
        xs *md5 = xs_md5_hex(url, strlen(url));

        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("a",
                xs_html_attr("href", (char *)url),
                xs_html_attr("title", md5),
                xs_html_text("»")));
    }

    if (strcmp(xs_dict_get(actor, "type"), "Service") == 0) {
        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("span",
                xs_html_attr("title", "bot"),
                xs_html_raw("&#129302;")));
    }

    if (fwing && fwer) {
        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("span",
                xs_html_attr("title", "mutual relation"),
                xs_html_raw("&#129309;")));
    }

    if (priv) {
        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("span",
                xs_html_attr("title", "private"),
                xs_html_raw("&#128274;")));
    }

    if (xs_is_null(date)) {
        xs_html_add(actor_icon,
            xs_html_raw("&nbsp;"));
    }
    else {
        xs *date_label = xs_crop_i(xs_dup(date), 0, 10);
        xs *date_title = xs_dup(date);

        if (!xs_is_null(udate)) {
            xs *sd = xs_crop_i(xs_dup(udate), 0, 10);

            date_label = xs_str_cat(date_label, " / ", sd);

            date_title = xs_str_cat(date_title, " / ", udate);
        }

        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("time",
                xs_html_attr("class", "dt-published snac-pubdate"),
                xs_html_attr("title", date_title),
                xs_html_text(date_label)));
    }

    {
        const char *username, *id;

        if (xs_is_null(username = xs_dict_get(actor, "preferredUsername")) || *username == '\0') {
            /* This should never be reached */
            username = "anonymous";
        }

        if (xs_is_null(id = xs_dict_get(actor, "id")) || *id == '\0') {
            /* This should never be reached */
            id = "https://social.example.org/anonymous";
        }

        /* "LIKE AN ANIMAL" */
        xs *domain = xs_split(id, "/");
        xs *user   = xs_fmt("@%s@%s", username, xs_list_get(domain, 2));

        xs_html_add(actor_icon,
            xs_html_sctag("br", NULL),
            xs_html_tag("a",
                xs_html_attr("href",  xs_dict_get(actor, "id")),
                xs_html_attr("class", "p-author-tag h-card snac-author-tag"),
                xs_html_text(user)));
    }

    return actor_icon;
}


xs_html *html_msg_icon(snac *user, const char *actor_id, const xs_dict *msg, const char *proxy)
{
    xs *actor = NULL;
    xs_html *actor_icon = NULL;

    if (actor_id && valid_status(actor_get_refresh(user, actor_id, &actor))) {
        const char *date  = NULL;
        const char *udate = NULL;
        const char *url   = NULL;
        int priv    = 0;
        const char *type = xs_dict_get(msg, "type");

        if (xs_match(type, POSTLIKE_OBJECT_TYPE))
            url = xs_dict_get(msg, "id");

        priv = !is_msg_public(msg);

        date  = xs_dict_get(msg, "published");
        udate = xs_dict_get(msg, "updated");

        actor_icon = html_actor_icon(user, actor, date, udate, url, priv, 0, proxy);
    }

    return actor_icon;
}


xs_html *html_note(snac *user, const char *summary,
                   const char *div_id, const char *form_id,
                   const char *ta_plh, const char *ta_content,
                   const char *edit_id, const char *actor_id,
                   const xs_val *cw_yn, const char *cw_text,
                   const xs_val *mnt_only, const char *redir,
                   const char *in_reply_to, int poll,
                   const char *att_file, const char *att_alt_text,
                   int is_draft)
/* Yes, this is a FUCKTON of arguments and I'm a bit embarrased */
{
    xs *action = xs_fmt("%s/admin/note", user->actor);

    xs_html *form;

    xs_html *note = xs_html_tag("div",
        xs_html_tag("details",
            xs_html_tag("summary",
                xs_html_text(summary)),
                xs_html_tag("p", NULL),
                xs_html_tag("div",
                    xs_html_attr("class", "snac-note"),
                    xs_html_attr("id",    div_id),
                    form = xs_html_tag("form",
                        xs_html_attr("autocomplete", "off"),
                        xs_html_attr("method",       "post"),
                        xs_html_attr("action",       action),
                        xs_html_attr("enctype",      "multipart/form-data"),
                        xs_html_attr("id",           form_id),
                        xs_html_tag("textarea",
                            xs_html_attr("class",    "snac-textarea"),
                            xs_html_attr("name",     "content"),
                            xs_html_attr("rows",     "4"),
                            xs_html_attr("wrap",     "virtual"),
                            xs_html_attr("required", "required"),
                            xs_html_attr("placeholder", ta_plh),
                            xs_html_text(ta_content)),
                        xs_html_tag("p", NULL),
                        xs_html_text(L("Sensitive content: ")),
                        xs_html_sctag("input",
                            xs_html_attr("type", "checkbox"),
                            xs_html_attr("name", "sensitive"),
                            xs_html_attr(xs_type(cw_yn) == XSTYPE_TRUE ? "checked" : "", NULL)),
                        xs_html_sctag("input",
                            xs_html_attr("type", "text"),
                            xs_html_attr("name", "summary"),
                            xs_html_attr("placeholder", L("Sensitive content description")),
                            xs_html_attr("value", xs_is_null(cw_text) ? "" : cw_text))))));

    if (actor_id)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "to"),
                xs_html_attr("value", actor_id)));
    else {
        /* no actor_id; ask for mentioned_only */
        xs_html_add(form,
            xs_html_tag("p", NULL),
            xs_html_text(L("Only for mentioned people: ")),
            xs_html_sctag("input",
                xs_html_attr("type",  "checkbox"),
                xs_html_attr("name",  "mentioned_only"),
                xs_html_attr(xs_type(mnt_only) == XSTYPE_TRUE ? "checked" : "", NULL)));
    }

    if (redir)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "redir"),
                xs_html_attr("value", redir)));

    if (in_reply_to)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "in_reply_to"),
                xs_html_attr("value", in_reply_to)));
    else
        xs_html_add(form,
            xs_html_tag("p", NULL),
            xs_html_text(L("Reply to (URL): ")),
            xs_html_sctag("input",
                xs_html_attr("type",     "url"),
                xs_html_attr("name",     "in_reply_to"),
                xs_html_attr("placeholder", "Optional URL to reply to")));

    xs_html_add(form,
        xs_html_tag("p", NULL),
        xs_html_tag("span",
            xs_html_attr("title", L("Don't send, but store as a draft")),
            xs_html_text(L("Draft:")),
            xs_html_sctag("input",
                xs_html_attr("type", "checkbox"),
                xs_html_attr("name", "is_draft"),
                xs_html_attr(is_draft ? "checked" : "", NULL))));

    if (edit_id)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "edit_id"),
                xs_html_attr("value", edit_id)));

    /* attachment controls */
    xs_html *att;

    xs_html_add(form,
        xs_html_tag("p", NULL),
        att = xs_html_tag("details",
            xs_html_tag("summary",
                xs_html_text(L("Attachment..."))),
            xs_html_tag("p", NULL)));

    if (att_file && *att_file)
        xs_html_add(att,
            xs_html_text(L("File:")),
            xs_html_sctag("input",
                xs_html_attr("type", "text"),
                xs_html_attr("name", "attach_url"),
                xs_html_attr("title", L("Clear this field to delete the attachment")),
                xs_html_attr("value", att_file)));
    else
        xs_html_add(att,
            xs_html_sctag("input",
                xs_html_attr("type",    "file"),
                xs_html_attr("name",    "attach")));

    xs_html_add(att,
        xs_html_text(" "),
        xs_html_sctag("input",
            xs_html_attr("type",    "text"),
            xs_html_attr("name",    "alt_text"),
            xs_html_attr("value",   att_alt_text),
            xs_html_attr("placeholder", L("Attachment description"))));

    /* add poll controls */
    if (poll) {
        xs_html_add(form,
            xs_html_tag("p", NULL),
            xs_html_tag("details",
                xs_html_tag("summary",
                    xs_html_text(L("Poll..."))),
                xs_html_tag("p",
                    xs_html_text(L("Poll options (one per line, up to 8):")),
                    xs_html_sctag("br", NULL),
                    xs_html_tag("textarea",
                        xs_html_attr("class",    "snac-textarea"),
                        xs_html_attr("name",     "poll_options"),
                        xs_html_attr("rows",     "4"),
                        xs_html_attr("wrap",     "virtual"),
                        xs_html_attr("placeholder", "Option 1...\nOption 2...\nOption 3...\n..."))),
                xs_html_tag("select",
                    xs_html_attr("name",    "poll_multiple"),
                    xs_html_tag("option",
                        xs_html_attr("value", "off"),
                        xs_html_text(L("One choice"))),
                    xs_html_tag("option",
                        xs_html_attr("value", "on"),
                        xs_html_text(L("Multiple choices")))),
                xs_html_text(" "),
                xs_html_tag("select",
                    xs_html_attr("name",    "poll_end_secs"),
                    xs_html_tag("option",
                        xs_html_attr("value", "300"),
                        xs_html_text(L("End in 5 minutes"))),
                    xs_html_tag("option",
                        xs_html_attr("value", "3600"),
                        xs_html_attr("selected", NULL),
                        xs_html_text(L("End in 1 hour"))),
                    xs_html_tag("option",
                        xs_html_attr("value", "86400"),
                        xs_html_text(L("End in 1 day"))))));
    }

    xs_html_add(form,
        xs_html_tag("p", NULL),
        xs_html_sctag("input",
            xs_html_attr("type",     "submit"),
            xs_html_attr("class",    "button"),
            xs_html_attr("value",    L("Post"))),
        xs_html_tag("p", NULL));

    return note;
}


static xs_html *html_base_head(void)
{
    xs_html *head = xs_html_tag("head",
        xs_html_sctag("meta",
            xs_html_attr("name", "viewport"),
            xs_html_attr("content", "width=device-width, initial-scale=1")),
        xs_html_sctag("meta",
            xs_html_attr("name", "generator"),
            xs_html_attr("content", USER_AGENT)));

    /* add server CSS and favicon */
    xs *f;
    f = xs_fmt("%s/favicon.ico", srv_baseurl);
    const xs_list *p = xs_dict_get(srv_config, "cssurls");
    const char *v;
    int c = 0;

    while (xs_list_next(p, &v, &c)) {
        xs_html_add(head,
            xs_html_sctag("link",
                xs_html_attr("rel",  "stylesheet"),
                xs_html_attr("type", "text/css"),
                xs_html_attr("href", v)));
    }

    xs_html_add(head,
        xs_html_sctag("link",
            xs_html_attr("rel",  "icon"),
            xs_html_attr("type", "image/x-icon"),
            xs_html_attr("href", f)));

    return head;
}


xs_html *html_instance_head(void)
{
    xs_html *head = html_base_head();

    {
        FILE *f;
        xs *g_css_fn = xs_fmt("%s/style.css", srv_basedir);

        if ((f = fopen(g_css_fn, "r")) != NULL) {
            xs *css = xs_readall(f);
            fclose(f);

            xs_html_add(head,
                xs_html_tag("style",
                    xs_html_raw(css)));
        }
    }

    const char *host  = xs_dict_get(srv_config, "host");
    const char *title = xs_dict_get(srv_config, "title");

    xs_html_add(head,
        xs_html_tag("title",
            xs_html_text(title && *title ? title : host)));

    return head;
}


static xs_html *html_instance_body(void)
{
    const char *host  = xs_dict_get(srv_config, "host");
    const char *sdesc = xs_dict_get(srv_config, "short_description");
    const char *email = xs_dict_get(srv_config, "admin_email");
    const char *acct  = xs_dict_get(srv_config, "admin_account");

    xs *blurb = xs_replace(snac_blurb, "%host%", host);

    xs_html *dl;

    xs_html *body = xs_html_tag("body",
        xs_html_tag("div",
            xs_html_attr("class", "snac-instance-blurb"),
            xs_html_raw(blurb), /* pure html */
            dl = xs_html_tag("dl", NULL)));

    if (sdesc && *sdesc) {
        xs_html_add(dl,
            xs_html_tag("di",
                xs_html_tag("dt",
                    xs_html_text(L("Site description"))),
                xs_html_tag("dd",
                    xs_html_text(sdesc))));
    }
    if (email && *email) {
        xs *mailto = xs_fmt("mailto:%s", email);

        xs_html_add(dl,
            xs_html_tag("di",
                xs_html_tag("dt",
                    xs_html_text(L("Admin email"))),
                xs_html_tag("dd",
                    xs_html_tag("a",
                        xs_html_attr("href", mailto),
                        xs_html_text(email)))));
    }
    if (acct && *acct) {
        xs *url    = xs_fmt("%s/%s", srv_baseurl, acct);
        xs *handle = xs_fmt("@%s@%s", acct, host);

        xs_html_add(dl,
            xs_html_tag("di",
                xs_html_tag("dt",
                    xs_html_text(L("Admin account"))),
                xs_html_tag("dd",
                    xs_html_tag("a",
                        xs_html_attr("href", url),
                        xs_html_text(handle)))));
    }

    return body;
}


xs_html *html_user_head(snac *user, const char *desc, const char *url)
{
    xs_html *head = html_base_head();

    /* add the user CSS */
    {
        xs *css = NULL;
        int size;

        /* try to open the user css */
        if (!valid_status(static_get(user, "style.css", &css, &size, NULL, NULL))) {
            /* it's not there; try to open the server-wide css */
            FILE *f;
            xs *g_css_fn = xs_fmt("%s/style.css", srv_basedir);

            if ((f = fopen(g_css_fn, "r")) != NULL) {
                css = xs_readall(f);
                fclose(f);
            }
        }

        if (css != NULL) {
            xs_html_add(head,
                xs_html_tag("style",
                    xs_html_raw(css)));
        }
    }

    /* title */
    xs *title = xs_fmt("%s (@%s@%s)", xs_dict_get(user->config, "name"),
        user->uid, xs_dict_get(srv_config, "host"));

    xs_html_add(head,
        xs_html_tag("title",
            xs_html_text(title)));

    xs *avatar = xs_dup(xs_dict_get(user->config, "avatar"));

    if (avatar == NULL || *avatar == '\0') {
        xs_free(avatar);
        avatar = xs_fmt("%s/susie.png", srv_baseurl);
    }

    /* create a description field */
    xs *s_desc = NULL;
    int n;

    if (desc == NULL)
        s_desc = xs_dup(xs_dict_get(user->config, "bio"));
    else
        s_desc = xs_dup(desc);

    /* shorten desc to a reasonable size */
    for (n = 0; s_desc[n]; n++) {
        if (n > 512 && (s_desc[n] == ' ' || s_desc[n] == '\n'))
            break;
    }

    s_desc[n] = '\0';

    /* og properties */
    xs_html_add(head,
        xs_html_sctag("meta",
            xs_html_attr("property", "og:site_name"),
            xs_html_attr("content", xs_dict_get(srv_config, "host"))),
        xs_html_sctag("meta",
            xs_html_attr("property", "og:title"),
            xs_html_attr("content", title)),
        xs_html_sctag("meta",
            xs_html_attr("property", "og:description"),
            xs_html_attr("content", s_desc)),
        xs_html_sctag("meta",
            xs_html_attr("property", "og:image"),
            xs_html_attr("content", avatar)),
        xs_html_sctag("meta",
            xs_html_attr("property", "og:width"),
            xs_html_attr("content", "300")),
        xs_html_sctag("meta",
            xs_html_attr("property", "og:height"),
            xs_html_attr("content", "300")));

    /* RSS link */
    xs *rss_url = xs_fmt("%s.rss", user->actor);
    xs_html_add(head,
        xs_html_sctag("link",
            xs_html_attr("rel", "alternate"),
            xs_html_attr("type", "application/rss+xml"),
            xs_html_attr("title", "RSS"),
            xs_html_attr("href", rss_url)));

    /* ActivityPub alternate link (actor id) */
    xs_html_add(head,
        xs_html_sctag("link",
            xs_html_attr("rel", "alternate"),
            xs_html_attr("type", "application/activity+json"),
            xs_html_attr("href", url ? url : user->actor)));

    return head;
}


static xs_html *html_user_body(snac *user, int read_only)
{
    const char *proxy = NULL;

    if (user && !read_only && xs_is_true(xs_dict_get(srv_config, "proxy_media")))
        proxy = user->actor;

    xs_html *body = xs_html_tag("body", NULL);

    /* top nav */
    xs_html *top_nav = xs_html_tag("nav",
        xs_html_attr("class", "snac-top-nav"));

    xs *avatar = xs_dup(xs_dict_get(user->config, "avatar"));

    if (avatar == NULL || *avatar == '\0') {
        xs_free(avatar);
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());
    }

    xs_html_add(top_nav,
        xs_html_sctag("img",
            xs_html_attr("src", avatar),
            xs_html_attr("class", "snac-avatar"),
            xs_html_attr("alt", "")));

    if (read_only) {
        xs *rss_url = xs_fmt("%s.rss", user->actor);
        xs *admin_url = xs_fmt("%s/admin", user->actor);

        xs_html_add(top_nav,
            xs_html_tag("a",
                xs_html_attr("href", rss_url),
                xs_html_text(L("RSS"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", admin_url),
                xs_html_attr("rel", "nofollow"),
                xs_html_text(L("private"))));
    }
    else {
        int n_len = notify_new_num(user);
        xs_html *notify_count = NULL;

        /* show the number of new notifications, if there are any */
        if (n_len) {
            xs *n_len_str = xs_fmt(" %d ", n_len);
            notify_count = xs_html_tag("sup",
                xs_html_attr("style", "background-color: red; color: white;"),
                xs_html_text(n_len_str));
        }
        else
            notify_count = xs_html_text("");

        xs *admin_url = xs_fmt("%s/admin", user->actor);
        xs *notify_url = xs_fmt("%s/notifications", user->actor);
        xs *people_url = xs_fmt("%s/people", user->actor);
        xs *instance_url = xs_fmt("%s/instance", user->actor);

        xs_html_add(top_nav,
            xs_html_tag("a",
                xs_html_attr("href", user->actor),
                xs_html_text(L("public"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", admin_url),
                xs_html_text(L("private"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", notify_url),
                xs_html_text(L("notifications"))),
            notify_count,
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", people_url),
                xs_html_text(L("people"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", instance_url),
                xs_html_text(L("instance"))),
            xs_html_text(" "),
            xs_html_tag("form",
                xs_html_attr("style", "display: inline!important"),
                xs_html_attr("class", "snac-search-box"),
                xs_html_attr("action", admin_url),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "q"),
                        xs_html_attr("title", L("Search posts by content (regular expression) or #tag")),
                        xs_html_attr("placeholder", L("Content search")))));
    }

    xs_html_add(body,
        top_nav);

    /* user info */
    xs_html *top_user = xs_html_tag("div",
        xs_html_attr("class", "h-card snac-top-user"));

    if (read_only) {
        const char *header = xs_dict_get(user->config, "header");
        if (header && *header) {
            xs_html_add(top_user,
                xs_html_tag("div",
                    xs_html_attr("class", "snac-top-user-banner"),
                    xs_html_attr("style", "clear: both"),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("img",
                        xs_html_attr("src", header))));
        }
    }

    xs *handle = xs_fmt("@%s@%s",
        xs_dict_get(user->config, "uid"),
        xs_dict_get(srv_config, "host"));

    xs_html_add(top_user,
        xs_html_tag("p",
            xs_html_attr("class", "p-name snac-top-user-name"),
            xs_html_text(xs_dict_get(user->config, "name"))),
        xs_html_tag("p",
            xs_html_attr("class", "snac-top-user-id"),
            xs_html_text(handle)));

    /** instance announcement **/

    if (!read_only) {
        double la = 0.0;
        xs *user_la = xs_dup(xs_dict_get(user->config, "last_announcement"));
        if (user_la != NULL)
            la = xs_number_get(user_la);

        const t_announcement *an = announcement(la);
        if (an != NULL && (an->text != NULL)) {
            xs *s = xs_fmt("?da=%.0f", an->timestamp);

            xs_html_add(top_user,  xs_html_tag("div",
                xs_html_attr("class", "snac-announcement"),
                    xs_html_text(an->text),
                    xs_html_text(" "),
                    xs_html_tag("a",
                            xs_html_attr("href", s),
                            xs_html_text("Dismiss"))));
        }
    }

    if (read_only) {
        xs *es1  = encode_html(xs_dict_get(user->config, "bio"));
        xs *tags = xs_list_new();
        xs *bio1 = not_really_markdown(es1, NULL, &tags);
        xs *bio2 = process_tags(user, bio1, &tags);

        bio2 = replace_shortnames(bio2, tags, 2, proxy);

        xs_html *top_user_bio = xs_html_tag("div",
            xs_html_attr("class", "p-note snac-top-user-bio"),
            xs_html_raw(bio2)); /* already sanitized */

        xs_html_add(top_user,
            top_user_bio);

        const xs_dict *metadata = xs_dict_get(user->config, "metadata");
        if (xs_type(metadata) == XSTYPE_DICT) {
            const xs_str *k;
            const xs_str *v;

            xs_dict *val_links = user->links;
            if (xs_is_null(val_links))
                val_links = xs_stock(XSTYPE_DICT);

            xs_html *snac_metadata = xs_html_tag("div",
                xs_html_attr("class", "snac-metadata"));

            int c = 0;
            while (xs_dict_next(metadata, &k, &v, &c)) {
                xs_html *value;

                if (xs_startswith(v, "https:/") || xs_startswith(v, "http:/")) {
                    /* is this link validated? */
                    xs *verified_link = NULL;
                    const xs_number *val_time = xs_dict_get(val_links, v);

                    if (xs_type(val_time) == XSTYPE_NUMBER) {
                        time_t t = xs_number_get(val_time);

                        if (t > 0) {
                            xs *s1 = xs_str_utctime(t, ISO_DATE_SPEC);
                            verified_link = xs_fmt("%s (%s)", L("verified link"), s1);
                        }
                    }

                    if (!xs_is_null(verified_link)) {
                        value = xs_html_tag("span",
                            xs_html_attr("title", verified_link),
                            xs_html_raw("&#10004; "),
                            xs_html_tag("a",
                                xs_html_attr("rel", "me"),
                                xs_html_attr("href", v),
                                xs_html_text(v)));
                    }
                    else {
                        value = xs_html_tag("a",
                            xs_html_attr("rel", "me"),
                            xs_html_attr("href", v),
                            xs_html_text(v));
                    }
                }
                else
                if (xs_startswith(v, "gemini:/")) {
                    value = xs_html_tag("a",
                        xs_html_attr("rel", "me"),
                        xs_html_attr("href", v),
                        xs_html_text(v));
                }
                else
                    value = xs_html_text(v);

                xs_html_add(snac_metadata,
                    xs_html_tag("span",
                        xs_html_attr("class", "snac-property-name"),
                        xs_html_text(k)),
                    xs_html_text(":"),
                    xs_html_raw("&nbsp;"),
                    xs_html_tag("span",
                        xs_html_attr("class", "snac-property-value"),
                        value),
                    xs_html_sctag("br", NULL));
            }

            xs_html_add(top_user,
                snac_metadata);
        }
    }

    xs_html_add(body,
        top_user);

    return body;
}


xs_html *html_top_controls(snac *snac)
/* generates the top controls */
{
    xs *ops_action = xs_fmt("%s/admin/action", snac->actor);

    xs_html *top_controls = xs_html_tag("div",
        xs_html_attr("class", "snac-top-controls"),

        /** new post **/
        html_note(snac, L("New Post..."),
            "new_post_div", "new_post_form",
            L("What's on your mind?"), "",
            NULL, NULL,
            xs_stock(XSTYPE_FALSE), "",
            xs_stock(XSTYPE_FALSE), NULL,
            NULL, 1, "", "", 0),

        /** operations **/
        xs_html_tag("details",
            xs_html_tag("summary",
                xs_html_text(L("Operations..."))),
            xs_html_tag("p", NULL),
            xs_html_tag("form",
                xs_html_attr("autocomplete", "off"),
                xs_html_attr("method",       "post"),
                xs_html_attr("action",       ops_action),
                xs_html_sctag("input",
                    xs_html_attr("type",     "text"),
                    xs_html_attr("name",     "actor"),
                    xs_html_attr("required", "required"),
                    xs_html_attr("placeholder", "bob@example.com")),
                xs_html_text(" "),
                xs_html_sctag("input",
                    xs_html_attr("type",    "submit"),
                    xs_html_attr("name",    "action"),
                    xs_html_attr("value",   L("Follow"))),
            xs_html_text(" "),
            xs_html_text(L("(by URL or user@host)"))),
            xs_html_tag("p", NULL),
            xs_html_tag("form",
                xs_html_attr("autocomplete", "off"),
                xs_html_attr("method",       "post"),
                xs_html_attr("action",       ops_action),
                xs_html_sctag("input",
                    xs_html_attr("type",     "url"),
                    xs_html_attr("name",     "id"),
                    xs_html_attr("required", "required"),
                    xs_html_attr("placeholder", "https:/" "/fedi.example.com/bob/...")),
                xs_html_text(" "),
                xs_html_sctag("input",
                    xs_html_attr("type",    "submit"),
                    xs_html_attr("name",    "action"),
                    xs_html_attr("value",   L("Boost"))),
                xs_html_text(" "),
                xs_html_text(L("(by URL)"))),
            xs_html_tag("p", NULL)));

    /** user settings **/

    const char *email = "[disabled by admin]";

    if (xs_type(xs_dict_get(srv_config, "disable_email_notifications")) != XSTYPE_TRUE) {
        email = xs_dict_get(snac->config_o, "email");
        if (xs_is_null(email)) {
            email = xs_dict_get(snac->config, "email");

            if (xs_is_null(email))
                email = "";
        }
    }

    const char *cw = xs_dict_get(snac->config, "cw");
    if (xs_is_null(cw))
        cw = "";

    const char *telegram_bot = xs_dict_get(snac->config, "telegram_bot");
    if (xs_is_null(telegram_bot))
        telegram_bot = "";

    const char *telegram_chat_id = xs_dict_get(snac->config, "telegram_chat_id");
    if (xs_is_null(telegram_chat_id))
        telegram_chat_id = "";

    const char *ntfy_server = xs_dict_get(snac->config, "ntfy_server");
    if (xs_is_null(ntfy_server))
        ntfy_server = "";

    const char *ntfy_token = xs_dict_get(snac->config, "ntfy_token");
    if (xs_is_null(ntfy_token))
        ntfy_token = "";

    const char *purge_days = xs_dict_get(snac->config, "purge_days");
    if (!xs_is_null(purge_days) && xs_type(purge_days) == XSTYPE_NUMBER)
        purge_days = (char *)xs_number_str(purge_days);
    else
        purge_days = "0";

    const xs_val *d_dm_f_u  = xs_dict_get(snac->config, "drop_dm_from_unknown");
    const xs_val *bot       = xs_dict_get(snac->config, "bot");
    const xs_val *a_private = xs_dict_get(snac->config, "private");
    const xs_val *auto_boost = xs_dict_get(snac->config, "auto_boost");
    const xs_val *coll_thrds = xs_dict_get(snac->config, "collapse_threads");

    xs *metadata = xs_str_new(NULL);
    const xs_dict *md = xs_dict_get(snac->config, "metadata");
    const xs_str *k;
    const xs_str *v;

    int c = 0;
    while (xs_dict_next(md, &k, &v, &c)) {
        xs *kp = xs_fmt("%s=%s", k, v);

        if (*metadata)
            metadata = xs_str_cat(metadata, "\n");
        metadata = xs_str_cat(metadata, kp);
    }

    xs *user_setup_action = xs_fmt("%s/admin/user-setup", snac->actor);

    xs_html_add(top_controls,
        xs_html_tag("details",
        xs_html_tag("summary",
            xs_html_text(L("User Settings..."))),
        xs_html_tag("div",
            xs_html_attr("class", "snac-user-setup"),
            xs_html_tag("form",
                xs_html_attr("autocomplete", "off"),
                xs_html_attr("method",       "post"),
                xs_html_attr("action",       user_setup_action),
                xs_html_attr("enctype",      "multipart/form-data"),
                xs_html_tag("p",
                    xs_html_text(L("Display name:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "name"),
                        xs_html_attr("value", xs_dict_get(snac->config, "name")),
                        xs_html_attr("placeholder", L("Your name")))),
                xs_html_tag("p",
                    xs_html_text(L("Avatar: ")),
                    xs_html_sctag("input",
                        xs_html_attr("type", "file"),
                        xs_html_attr("name", "avatar_file"))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "avatar_delete")),
                        xs_html_text(L("Delete current avatar"))),
                xs_html_tag("p",
                    xs_html_text(L("Header image (banner): ")),
                    xs_html_sctag("input",
                        xs_html_attr("type", "file"),
                        xs_html_attr("name", "header_file"))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "header_delete")),
                        xs_html_text(L("Delete current header image"))),
                xs_html_tag("p",
                    xs_html_text(L("Bio:")),
                    xs_html_sctag("br", NULL),
                    xs_html_tag("textarea",
                        xs_html_attr("name", "bio"),
                        xs_html_attr("cols", "40"),
                        xs_html_attr("rows", "4"),
                        xs_html_attr("placeholder", L("Write about yourself here...")),
                        xs_html_text(xs_dict_get(snac->config, "bio")))),
                xs_html_sctag("input",
                    xs_html_attr("type", "checkbox"),
                    xs_html_attr("name", "cw"),
                    xs_html_attr("id",   "cw"),
                    xs_html_attr(strcmp(cw, "open") == 0 ? "checked" : "", NULL)),
                xs_html_tag("label",
                    xs_html_attr("for", "cw"),
                    xs_html_text(L("Always show sensitive content"))),
                xs_html_tag("p",
                    xs_html_text(L("Email address for notifications:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "email"),
                        xs_html_attr("value", email),
                        xs_html_attr("placeholder", "bob@example.com"))),
                xs_html_tag("p",
                    xs_html_text(L("Telegram notifications (bot key and chat id):")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "telegram_bot"),
                        xs_html_attr("value", telegram_bot),
                        xs_html_attr("placeholder", "Bot API key")),
                    xs_html_text(" "),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "telegram_chat_id"),
                        xs_html_attr("value", telegram_chat_id),
                        xs_html_attr("placeholder", "Chat id"))),
                xs_html_tag("p",
                    xs_html_text(L("ntfy notifications (ntfy server and token):")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "ntfy_server"),
                        xs_html_attr("value", ntfy_server),
                        xs_html_attr("placeholder", "ntfy server - full URL (example: https://ntfy.sh/YourTopic)")),
                    xs_html_text(" "),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "ntfy_token"),
                        xs_html_attr("value", ntfy_token),
                        xs_html_attr("placeholder", "ntfy token - if needed"))),
                xs_html_tag("p",
                    xs_html_text(L("Maximum days to keep posts (0: server settings):")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "number"),
                        xs_html_attr("name", "purge_days"),
                        xs_html_attr("value", purge_days))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "drop_dm_from_unknown"),
                        xs_html_attr("id", "drop_dm_from_unknown"),
                        xs_html_attr(xs_type(d_dm_f_u) == XSTYPE_TRUE ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "drop_dm_from_unknown"),
                        xs_html_text(L("Drop direct messages from people you don't follow")))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "bot"),
                        xs_html_attr("id", "bot"),
                        xs_html_attr(xs_type(bot) == XSTYPE_TRUE ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "bot"),
                        xs_html_text(L("This account is a bot")))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "auto_boost"),
                        xs_html_attr("id", "auto_boost"),
                        xs_html_attr(xs_is_true(auto_boost) ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "auto_boost"),
                        xs_html_text(L("Auto-boost all mentions to this account")))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "private"),
                        xs_html_attr("id", "private"),
                        xs_html_attr(xs_type(a_private) == XSTYPE_TRUE ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "private"),
                        xs_html_text(L("This account is private "
                            "(posts are not shown through the web)")))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "collapse_threads"),
                        xs_html_attr("id",   "collapse_threads"),
                        xs_html_attr(xs_is_true(coll_thrds) ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "collapse_threads"),
                        xs_html_text(L("Collapse top threads by default")))),
                xs_html_tag("p",
                    xs_html_text(L("Profile metadata (key=value pairs in each line):")),
                    xs_html_sctag("br", NULL),
                    xs_html_tag("textarea",
                        xs_html_attr("name", "metadata"),
                        xs_html_attr("cols", "40"),
                        xs_html_attr("rows", "4"),
                        xs_html_attr("placeholder", "Blog=https:/"
                            "/example.com/my-blog\nGPG Key=1FA54\n..."),
                    xs_html_text(metadata))),

                xs_html_tag("p",
                    xs_html_text(L("New password:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "password"),
                        xs_html_attr("name", "passwd1"),
                        xs_html_attr("value", ""))),
                xs_html_tag("p",
                    xs_html_text(L("Repeat new password:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "password"),
                        xs_html_attr("name", "passwd2"),
                        xs_html_attr("value", ""))),

                xs_html_sctag("input",
                    xs_html_attr("type", "submit"),
                    xs_html_attr("class", "button"),
                    xs_html_attr("value", L("Update user info")))))));

    return top_controls;
}


static xs_html *html_button(char *clss, char *label, char *hint)
{
    xs *c = xs_fmt("snac-btn-%s", clss);

    /* use an NULL tag to separate non-css-classed buttons from one another */
    return xs_html_container(
        xs_html_sctag("input",
            xs_html_attr("type",    "submit"),
            xs_html_attr("name",    "action"),
            xs_html_attr("class",   c),
            xs_html_attr("value",   label),
            xs_html_attr("title",   hint)),
        xs_html_text("\n"));
}


xs_str *build_mentions(snac *snac, const xs_dict *msg)
/* returns a string with the mentions in msg */
{
    xs_str *s = xs_str_new(NULL);
    const char *list = xs_dict_get(msg, "tag");
    const char *v;
    int c = 0;

    while (xs_list_next(list, &v, &c)) {
        const char *type = xs_dict_get(v, "type");
        const char *href = xs_dict_get(v, "href");
        const char *name = xs_dict_get(v, "name");

        if (type && strcmp(type, "Mention") == 0 &&
            href && strcmp(href, snac->actor) != 0 && name) {
            xs *s1 = NULL;

            if (name[0] != '@') {
                s1 = xs_fmt("@%s", name);
                name = s1;
            }

            xs *l = xs_split(name, "@");

            /* is it a name without a host? */
            if (xs_list_len(l) < 3) {
                /* split the href and pick the host name LIKE AN ANIMAL */
                /* would be better to query the webfinger but *won't do that* here */
                xs *l2 = xs_split(href, "/");

                if (xs_list_len(l2) >= 3) {
                    xs *s1 = xs_fmt("%s@%s ", name, xs_list_get(l2, 2));

                    if (xs_str_in(s, s1) == -1)
                        s = xs_str_cat(s, s1);
                }
            }
            else {
                if (xs_str_in(s, name) == -1) {
                    s = xs_str_cat(s, name);
                    s = xs_str_cat(s, " ");
                }
            }
        }
    }

    if (*s) {
        xs *s1 = s;
        s = xs_fmt("\n\n\nCC: %s", s1);
    }

    return s;
}


xs_html *html_entry_controls(snac *snac, const char *actor,
                            const xs_dict *msg, const char *md5)
{
    const char *id    = xs_dict_get(msg, "id");
    const char *group = xs_dict_get(msg, "audience");

    xs *likes   = object_likes(id);
    xs *boosts  = object_announces(id);

    xs *action = xs_fmt("%s/admin/action", snac->actor);
    xs *redir  = xs_fmt("%s_entry", md5);

    xs_html *form;
    xs_html *controls = xs_html_tag("div",
        xs_html_attr("class", "snac-controls"),
        form = xs_html_tag("form",
            xs_html_attr("autocomplete", "off"),
            xs_html_attr("method",       "post"),
            xs_html_attr("action",       action),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "id"),
                xs_html_attr("value",    id)),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "actor"),
                xs_html_attr("value",    actor)),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "group"),
                xs_html_attr("value",    xs_is_null(group) ? "" : group)),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "redir"),
                xs_html_attr("value",    redir))));

    if (!xs_startswith(id, snac->actor)) {
        if (xs_list_in(likes, snac->md5) == -1) {
            /* not already liked; add button */
            xs_html_add(form,
                html_button("like", L("Like"), L("Say you like this post")));
        }
        else {
            /* not like it anymore */
            xs_html_add(form,
                html_button("unlike", L("Unlike"), L("Nah don't like it that much")));
        }
    }
    else {
        if (is_pinned(snac, id))
            xs_html_add(form,
                html_button("unpin", L("Unpin"), L("Unpin this post from your timeline")));
        else
            xs_html_add(form,
                html_button("pin", L("Pin"), L("Pin this post to the top of your timeline")));
    }

    if (is_msg_public(msg)) {
        if (xs_list_in(boosts, snac->md5) == -1) {
            /* not already boosted; add button */
            xs_html_add(form,
                html_button("boost", L("Boost"), L("Announce this post to your followers")));
        }
        else {
            /* already boosted; add button to regret */
            xs_html_add(form,
                html_button("unboost", L("Unboost"), L("I regret I boosted this")));
        }
    }

    if (is_bookmarked(snac, id))
            xs_html_add(form,
                html_button("unbookmark", L("Unbookmark"), L("Delete this post from your bookmarks")));
        else
            xs_html_add(form,
                html_button("bookmark", L("Bookmark"), L("Add this post to your bookmarks")));

    if (strcmp(actor, snac->actor) != 0) {
        /* controls for other actors than this one */
        if (following_check(snac, actor)) {
            xs_html_add(form,
                html_button("unfollow", L("Unfollow"), L("Stop following this user's activity")));
        }
        else {
            xs_html_add(form,
                html_button("follow", L("Follow"), L("Start following this user's activity")));
        }

        if (!xs_is_null(group)) {
            if (following_check(snac, group)) {
                xs_html_add(form,
                    html_button("unfollow", L("Unfollow Group"),
                        L("Stop following this group or channel")));
            }
            else {
                xs_html_add(form,
                    html_button("follow", L("Follow Group"),
                        L("Start following this group or channel")));
            }
        }

        xs_html_add(form,
            html_button("mute", L("MUTE"),
                L("Block any activity from this user forever")));
    }

    if (!xs_is_true(xs_dict_get(srv_config, "hide_delete_post_button")))
        xs_html_add(form,
            html_button("delete", L("Delete"), L("Delete this post")));

    xs_html_add(form,
        html_button("hide",   L("Hide"), L("Hide this post and its children")));

    const char *prev_src = xs_dict_get(msg, "sourceContent");

    if (!xs_is_null(prev_src) && strcmp(actor, snac->actor) == 0) { /** edit **/
        /* post can be edited */
        xs *div_id  = xs_fmt("%s_edit", md5);
        xs *form_id = xs_fmt("%s_edit_form", md5);
        xs *redir   = xs_fmt("%s_entry", md5);

        const char *att_file = "";
        const char *att_alt_text = "";
        const xs_list *att_list = xs_dict_get(msg, "attachment");

        /* does it have an attachment? */
        if (xs_type(att_list) == XSTYPE_LIST && xs_list_len(att_list)) {
            const xs_dict *d = xs_list_get(att_list, 0);

            if (xs_type(d) == XSTYPE_DICT) {
                att_file = xs_dict_get_def(d, "url", "");
                att_alt_text = xs_dict_get_def(d, "name", "");
            }
        }

        xs_html_add(controls, xs_html_tag("div",
            xs_html_tag("p", NULL),
            html_note(snac, L("Edit..."),
                div_id, form_id,
                "", prev_src,
                id, NULL,
                xs_dict_get(msg, "sensitive"), xs_dict_get(msg, "summary"),
                xs_stock(is_msg_public(msg) ? XSTYPE_FALSE : XSTYPE_TRUE), redir,
                NULL, 0, att_file, att_alt_text, is_draft(snac, id))),
            xs_html_tag("p", NULL));
    }

    { /** reply **/
        /* the post textarea */
        xs *ct      = build_mentions(snac, msg);
        xs *div_id  = xs_fmt("%s_reply", md5);
        xs *form_id = xs_fmt("%s_reply_form", md5);
        xs *redir   = xs_fmt("%s_entry", md5);

        xs_html_add(controls, xs_html_tag("div",
            xs_html_tag("p", NULL),
            html_note(snac, L("Reply..."),
                div_id, form_id,
                "", ct,
                NULL, NULL,
                xs_dict_get(msg, "sensitive"), xs_dict_get(msg, "summary"),
                xs_stock(is_msg_public(msg) ? XSTYPE_FALSE : XSTYPE_TRUE), redir,
                id, 0, "", "", 0)),
            xs_html_tag("p", NULL));
    }

    return controls;
}


xs_html *html_entry(snac *user, xs_dict *msg, int read_only,
                   int level, const char *md5, int hide_children)
{
    const char *id    = xs_dict_get(msg, "id");
    const char *type  = xs_dict_get(msg, "type");
    const char *actor;
    const char *v;
    int has_title = 0;
    int collapse_threads = 0;
    const char *proxy = NULL;

    if (user && !read_only && xs_is_true(xs_dict_get(srv_config, "proxy_media")))
        proxy = user->actor;

    /* do not show non-public messages in the public timeline */
    if ((read_only || !user) && !is_msg_public(msg))
        return NULL;

    if (user && level == 0 && xs_is_true(xs_dict_get(user->config, "collapse_threads")))
        collapse_threads = 1;

    /* hidden? do nothing more for this conversation */
    if (user && is_hidden(user, id)) {
        xs *s1 = xs_fmt("%s_entry", md5);

        /* return just an dummy anchor, to keep position after hitting 'Hide' */
        return xs_html_tag("div",
            xs_html_tag("a",
                xs_html_attr("name", s1)));
    }

    /* avoid too deep nesting, as it may be a loop */
    if (level >= MAX_CONVERSATION_LEVELS)
        return xs_html_tag("mark",
            xs_html_text(L("Truncated (too deep)")));

    if (strcmp(type, "Follow") == 0) {
        return xs_html_tag("div",
            xs_html_attr("class", "snac-post"),
            xs_html_tag("div",
                xs_html_attr("class", "snac-post-header"),
                xs_html_tag("div",
                    xs_html_attr("class", "snac-origin"),
                    xs_html_text(L("follows you"))),
                html_msg_icon(read_only ? NULL : user, xs_dict_get(msg, "actor"), msg, proxy)));
    }
    else
    if (!xs_match(type, POSTLIKE_OBJECT_TYPE)) {
        /* skip oddities */
        snac_debug(user, 1, xs_fmt("html_entry: ignoring object type '%s' %s", type, id));
        return NULL;
    }

    /* ignore notes with "name", as they are votes to Questions */
    if (strcmp(type, "Note") == 0 && !xs_is_null(xs_dict_get(msg, "name")))
        return NULL;

    /* get the attributedTo */
    if ((actor = get_atto(msg)) == NULL)
        return NULL;

    /* ignore muted morons immediately */
    if (user && is_muted(user, actor)) {
        xs *s1 = xs_fmt("%s_entry", md5);

        /* return just an dummy anchor, to keep position after hitting 'MUTE' */
        return xs_html_tag("div",
            xs_html_tag("a",
                xs_html_attr("name", s1)));
    }

    if ((user == NULL || strcmp(actor, user->actor) != 0)
        && !valid_status(actor_get(actor, NULL)))
        return NULL;

    /** html_entry top tag **/
    xs_html *entry_top = xs_html_tag("div", NULL);

    {
        xs *s1 = xs_fmt("%s_entry", md5);
        xs_html_add(entry_top,
            xs_html_tag("a",
                xs_html_attr("name", s1)));
    }

    xs_html *entry = xs_html_tag("div",
        xs_html_attr("class", level == 0 ? "snac-post" : "snac-child"));

    xs_html_add(entry_top,
        entry);

    /** post header **/

    xs_html *score;
    xs_html *post_header = xs_html_tag("div",
        xs_html_attr("class", "snac-post-header"),
        score = xs_html_tag("div",
            xs_html_attr("class", "snac-score")));

    xs_html_add(entry,
        post_header);

    if (user && is_pinned(user, id)) {
        /* add a pin emoji */
        xs_html_add(score,
            xs_html_tag("span",
                xs_html_attr("title", L("Pinned")),
                xs_html_raw(" &#128204; ")));
    }

    if (user && is_bookmarked(user, id)) {
        /* add a bookmark emoji */
        xs_html_add(score,
            xs_html_tag("span",
                xs_html_attr("title", L("Bookmarked")),
                xs_html_raw(" &#128278; ")));
    }

    if (strcmp(type, "Question") == 0) {
        /* add the ballot box emoji */
        xs_html_add(score,
            xs_html_tag("span",
                xs_html_attr("title", L("Poll")),
                xs_html_raw(" &#128499; ")));

        if (user && was_question_voted(user, id)) {
            /* add a check to show this poll was voted */
            xs_html_add(score,
                xs_html_tag("span",
                    xs_html_attr("title", L("Voted")),
                    xs_html_raw(" &#10003; ")));
        }
    }

    if (strcmp(type, "Event") == 0) {
        /* add the calendar emoji */
        xs_html_add(score,
            xs_html_tag("span",
                xs_html_attr("title", L("Event")),
                xs_html_raw(" &#128197; ")));
    }

    /* if it's a user from this same instance, add the score */
    if (xs_startswith(id, srv_baseurl)) {
        int n_likes  = object_likes_len(id);
        int n_boosts = object_announces_len(id);

        /* alternate emojis: %d &#128077; %d &#128257; */
        xs *s1 = xs_fmt("%d &#9733; %d &#8634;\n", n_likes, n_boosts);

        xs_html_add(score,
            xs_html_raw(s1));
    }

    xs *boosts = object_announces(id);

    if (xs_list_len(boosts)) {
        /* if somebody boosted this, show as origin */
        const char *p = xs_list_get(boosts, -1);
        xs *actor_r = NULL;

        if (user && xs_list_in(boosts, user->md5) != -1) {
            /* we boosted this */
            xs_html_add(post_header,
                xs_html_tag("div",
                    xs_html_attr("class", "snac-origin"),
                    xs_html_tag("a",
                        xs_html_attr("href", user->actor),
                        xs_html_text(xs_dict_get(user->config, "name"))),
                        xs_html_text(" "),
                        xs_html_text(L("boosted"))));
        }
        else
        if (valid_status(object_get_by_md5(p, &actor_r))) {
            xs *name = actor_name(actor_r, proxy);

            if (!xs_is_null(name)) {
                xs *href = NULL;
                const char *id = xs_dict_get(actor_r, "id");
                int fwers = 0;
                int fwing = 0;

                if (user != NULL) {
                    fwers = follower_check(user, id);
                    fwing = following_check(user, id);
                }

                if (!read_only && (fwers || fwing))
                    href = xs_fmt("%s/people#%s", user->actor, p);
                else
                    href = xs_dup(id);

                xs_html_add(post_header,
                    xs_html_tag("div",
                        xs_html_attr("class", "snac-origin"),
                        xs_html_tag("a",
                            xs_html_attr("href", href),
                            xs_html_raw(name)), /* already sanitized */
                            xs_html_text(" "),
                            xs_html_text(L("boosted"))));
            }
        }
    }
    else
    if (strcmp(type, "Note") == 0) {
        if (level == 0) {
            /* is the parent not here? */
            const char *parent = get_in_reply_to(msg);

            if (user && !xs_is_null(parent) && *parent && !timeline_here(user, parent)) {
                xs_html_add(post_header,
                    xs_html_tag("div",
                        xs_html_attr("class", "snac-origin"),
                        xs_html_text(L("in reply to")),
                        xs_html_text(" "),
                        xs_html_tag("a",
                            xs_html_attr("href", parent),
                            xs_html_text("»"))));
            }
        }
    }

    xs_html_add(post_header,
        html_msg_icon(read_only ? NULL : user, actor, msg, proxy));

    /** post content **/

    xs_html *snac_content_wrap = xs_html_tag("div",
        xs_html_attr("class", "e-content snac-content"));

    xs_html_add(entry,
        snac_content_wrap);

    if (!has_title && !xs_is_null(v = xs_dict_get(msg, "name"))) {
        xs_html_add(snac_content_wrap,
            xs_html_tag("h3",
                xs_html_attr("class", "snac-entry-title"),
                xs_html_text(v)));

        has_title = 1;
    }

    xs_html *snac_content = NULL;

    v = xs_dict_get(msg, "summary");

    /* is it sensitive? */
    if (xs_type(xs_dict_get(msg, "sensitive")) == XSTYPE_TRUE) {
        if (xs_is_null(v) || *v == '\0')
            v = "...";

        const char *cw = "";

        if (user) {
            /* only show it when not in the public timeline and the config setting is "open" */
            cw = xs_dict_get(user->config, "cw");
            if (xs_is_null(cw) || read_only)
                cw = "";
        }

        snac_content = xs_html_tag("details",
            xs_html_attr(cw, NULL),
            xs_html_tag("summary",
                xs_html_text(v),
                xs_html_text(L(" [SENSITIVE CONTENT]"))));
    }
    else {
        /* print the summary as a header (sites like e.g. Friendica can contain one) */
        if (!has_title && !xs_is_null(v) && *v) {
            xs_html_add(snac_content_wrap,
                xs_html_tag("h3",
                    xs_html_attr("class", "snac-entry-title"),
                    xs_html_text(v)));

            has_title = 1;
        }

        snac_content = xs_html_tag("div", NULL);
    }

    xs_html_add(snac_content_wrap,
        snac_content);

    {
        /** build the content string **/
        const char *content = xs_dict_get(msg, "content");

        if (xs_type(content) != XSTYPE_STRING) {
            if (!xs_is_null(content))
                srv_archive_error("unexpected_content_xstype",
                    "content field type", xs_stock(XSTYPE_DICT), msg);

            content = "";
        }

        /* skip ugly line breaks at the beginning */
        while (xs_startswith(content, "<br>"))
            content += 4;

        xs *c = sanitize(content);

        /* do some tweaks to the content */
        c = xs_replace_i(c, "\r", "");

        while (xs_endswith(c, "<br><br>"))
            c = xs_crop_i(c, 0, -4);

        c = xs_replace_i(c, "<br><br>", "<p>");

        c = xs_str_cat(c, "<p>");

        /* replace the :shortnames: */
        c = replace_shortnames(c, xs_dict_get(msg, "tag"), 2, proxy);

        /* Peertube videos content is in markdown */
        const char *mtype = xs_dict_get(msg, "mediaType");
        if (xs_type(mtype) == XSTYPE_STRING && strcmp(mtype, "text/markdown") == 0) {
            /* a full conversion could be better */
            c = xs_replace_i(c, "\r", "");
            c = xs_replace_i(c, "\n", "<br>");
        }

        /* c contains sanitized HTML */
        xs_html_add(snac_content,
            xs_html_raw(c));
    }

    if (strcmp(type, "Question") == 0) { /** question content **/
        const xs_list *oo = xs_dict_get(msg, "oneOf");
        const xs_list *ao = xs_dict_get(msg, "anyOf");
        const xs_list *p;
        const xs_dict *v;
        int closed = 0;
        const char *f_closed = NULL;

        xs_html *poll = xs_html_tag("div", NULL);

        if (read_only)
            closed = 1; /* non-identified page; show as closed */
        else
        if (user && xs_startswith(id, user->actor))
            closed = 1; /* we questioned; closed for us */
        else
        if (user && was_question_voted(user, id))
            closed = 1; /* we already voted; closed for us */

        if ((f_closed = xs_dict_get(msg, "closed")) != NULL) {
            /* it has a closed date... but is it in the past? */
            time_t t0 = time(NULL);
            time_t t1 = xs_parse_iso_date(f_closed, 0);

            if (t1 < t0)
                closed = 2;
        }

        /* get the appropriate list of options */
        p = oo != NULL ? oo : ao;

        if (closed || user == NULL) {
            /* closed poll */
            xs_html *poll_result = xs_html_tag("table",
                xs_html_attr("class", "snac-poll-result"));
            int c = 0;

            while (xs_list_next(p, &v, &c)) {
                const char *name       = xs_dict_get(v, "name");
                const xs_dict *replies = xs_dict_get(v, "replies");

                if (name && replies) {
                    char *ti = (char *)xs_number_str(xs_dict_get(replies, "totalItems"));

                    xs_html_add(poll_result,
                        xs_html_tag("tr",
                            xs_html_tag("td",
                                xs_html_text(name),
                                xs_html_text(":")),
                            xs_html_tag("td",
                                xs_html_text(ti))));
                }
            }

            xs_html_add(poll,
                poll_result);
        }
        else {
            /* poll still active */
            xs *vote_action = xs_fmt("%s/admin/vote", user->actor);
            xs_html *form;
            xs_html *poll_form = xs_html_tag("div",
                xs_html_attr("class", "snac-poll-form"),
                form = xs_html_tag("form",
                    xs_html_attr("autocomplete", "off"),
                    xs_html_attr("method", "post"),
                    xs_html_attr("action", vote_action),
                    xs_html_sctag("input",
                        xs_html_attr("type", "hidden"),
                        xs_html_attr("name", "actor"),
                        xs_html_attr("value", actor)),
                    xs_html_sctag("input",
                        xs_html_attr("type", "hidden"),
                        xs_html_attr("name", "irt"),
                        xs_html_attr("value", id))));

            int c = 0;
            while (xs_list_next(p, &v, &c)) {
                const char *name = xs_dict_get(v, "name");
                const xs_dict *replies = xs_dict_get(v, "replies");

                if (name) {
                    char *ti = (char *)xs_number_str(xs_dict_get(replies, "totalItems"));

                    xs_html *btn = xs_html_sctag("input",
                            xs_html_attr("id", name),
                            xs_html_attr("value", name),
                            xs_html_attr("name", "question"));

                    if (!xs_is_null(oo)) {
                        xs_html_add(btn,
                            xs_html_attr("type", "radio"),
                            xs_html_attr("required", "required"));
                    }
                    else
                        xs_html_add(btn,
                            xs_html_attr("type", "checkbox"));

                    xs_html_add(form,
                        btn,
                        xs_html_text(" "),
                        xs_html_tag("span",
                            xs_html_attr("title", ti),
                            xs_html_text(name)),
                        xs_html_sctag("br", NULL));
                }
            }

            xs_html_add(form,
                xs_html_tag("p", NULL),
                xs_html_sctag("input",
                    xs_html_attr("type", "submit"),
                    xs_html_attr("class", "button"),
                    xs_html_attr("value", L("Vote"))));

            xs_html_add(poll,
                poll_form);
        }

        /* if it's *really* closed, say it */
        if (closed == 2) {
            xs_html_add(poll,
                xs_html_tag("p",
                    xs_html_text(L("Closed"))));
        }
        else {
            /* show when the poll closes */
            const char *end_time = xs_dict_get(msg, "endTime");

            /* Pleroma does not have an endTime field;
               it has a closed time in the future */
            if (xs_is_null(end_time))
                end_time = xs_dict_get(msg, "closed");

            if (!xs_is_null(end_time)) {
                time_t t0 = time(NULL);
                time_t t1 = xs_parse_iso_date(end_time, 0);

                if (t1 > 0 && t1 > t0) {
                    time_t diff_time = t1 - t0;
                    xs *tf = xs_str_time_diff(diff_time);
                    char *p = tf;

                    /* skip leading zeros */
                    for (; *p == '0' || *p == ':'; p++);

                    xs_html_add(poll,
                        xs_html_tag("p",
                            xs_html_text(L("Closes in")),
                            xs_html_text(" "),
                            xs_html_text(p)));
                }
            }
        }

        xs_html_add(snac_content,
            poll);
    }

    /** attachments **/
    xs *attach = get_attachments(msg);

    {
        /* make custom css for attachments easier */
        xs_html *content_attachments = xs_html_tag("div",
            xs_html_attr("class", "snac-content-attachments"));

        xs_html_add(snac_content,
            content_attachments);

        const char *content = xs_dict_get(msg, "content");

        int c = 0;
        const xs_dict *a;
        while (xs_list_next(attach, &a, &c)) {
            const char *type = xs_dict_get(a, "type");
            const char *o_href = xs_dict_get(a, "href");
            const char *name = xs_dict_get(a, "name");

            /* if this image is already in the post content, skip */
            if (content && xs_str_in(content, o_href) != -1)
                continue;

            xs *href = make_url(o_href, proxy, 0);

            if (xs_startswith(type, "image/") || strcmp(type, "Image") == 0) {
                xs_html_add(content_attachments,
                    xs_html_tag("a",
                        xs_html_attr("href", href),
                        xs_html_attr("target", "_blank"),
                        xs_html_sctag("img",
                            xs_html_attr("loading", "lazy"),
                            xs_html_attr("src", href),
                            xs_html_attr("alt", name),
                            xs_html_attr("title", name))));
            }
            else
            if (xs_startswith(type, "video/")) {
                xs_html_add(content_attachments,
                    xs_html_tag("video",
                        xs_html_attr("preload", "none"),
                        xs_html_attr("style", "width: 100%"),
                        xs_html_attr("class", "snac-embedded-video"),
                        xs_html_attr("controls", NULL),
                        xs_html_attr("src", href),
                        xs_html_text(L("Video")),
                        xs_html_text(": "),
                        xs_html_tag("a",
                            xs_html_attr("href", href),
                            xs_html_text(name))));
            }
            else
            if (xs_startswith(type, "audio/")) {
                xs_html_add(content_attachments,
                    xs_html_tag("audio",
                        xs_html_attr("preload", "none"),
                        xs_html_attr("style", "width: 100%"),
                        xs_html_attr("class", "snac-embedded-audio"),
                        xs_html_attr("controls", NULL),
                        xs_html_attr("src", href),
                        xs_html_text(L("Audio")),
                        xs_html_text(": "),
                        xs_html_tag("a",
                            xs_html_attr("href", href),
                            xs_html_text(name))));
            }
            else
            if (strcmp(type, "Link") == 0) {
                xs_html_add(content_attachments,
                    xs_html_tag("p",
                        xs_html_tag("a",
                            xs_html_attr("href", o_href),
                            xs_html_text(href))));

                /* do not generate an Alt... */
                name = NULL;
            }
            else {
                xs_html_add(content_attachments,
                    xs_html_tag("p",
                        xs_html_tag("a",
                            xs_html_attr("href", o_href),
                            xs_html_text(L("Attachment")),
                            xs_html_text(": "),
                            xs_html_text(o_href))));

                /* do not generate an Alt... */
                name = NULL;
            }

            if (name != NULL && *name) {
                xs_html_add(content_attachments,
                    xs_html_tag("p",
                        xs_html_attr("class", "snac-alt-text"),
                        xs_html_tag("details",
                            xs_html_tag("summary",
                                xs_html_text(L("Alt..."))),
                            xs_html_text(name))));
            }
        }
    }

    /* has this message an audience (i.e., comes from a channel or community)? */
    const char *audience = xs_dict_get(msg, "audience");
    if (strcmp(type, "Page") == 0 && !xs_is_null(audience)) {
        xs_html *au_tag = xs_html_tag("p",
            xs_html_text("("),
            xs_html_tag("a",
                xs_html_attr("href", audience),
                xs_html_attr("title", L("Source channel or community")),
                xs_html_text(audience)),
            xs_html_text(")"));

        xs_html_add(snac_content_wrap,
            au_tag);
    }

    /** controls **/

    if (!read_only && user) {
        xs_html_add(entry,
            html_entry_controls(user, actor, msg, md5));
    }

    /** children **/
    if (!hide_children) {
        xs *children = object_children(id);
        int left     = xs_list_len(children);

        if (left) {
            xs_html *ch_details = xs_html_tag("details",
                xs_html_attr(collapse_threads ? "" : "open", NULL),
                xs_html_tag("summary",
                    xs_html_text("...")));

            xs_html_add(entry,
                ch_details);

            xs_html *ch_container = xs_html_tag("div",
                xs_html_attr("class", level < 4 ? "snac-children" : "snac-children-too-deep"));

            xs_html_add(ch_details,
                ch_container);

            xs_html *ch_older = NULL;
            if (left > 3) {
                xs_html_add(ch_container,
                    ch_older = xs_html_tag("details",
                        xs_html_tag("summary",
                            xs_html_text(L("Older...")))));
            }

            xs_list *p = children;
            const char *cmd5;
            int cnt = 0;
            int o_cnt = 0;

            while (xs_list_iter(&p, &cmd5)) {
                xs *chd = NULL;

                if (user)
                    timeline_get_by_md5(user, cmd5, &chd);
                else
                    object_get_by_md5(cmd5, &chd);

                if (chd != NULL) {
                    if (xs_is_null(xs_dict_get(chd, "name"))) {
                        xs_html *che = html_entry(user, chd, read_only,
                            level + 1, cmd5, hide_children);

                        if (che != NULL) {
                            if (left > 3) {
                                xs_html_add(ch_older,
                                    che);

                                o_cnt++;
                            }
                            else
                                xs_html_add(ch_container,
                                    che);

                            cnt++;
                        }
                    }

                    left--;
                }
                else
                    srv_debug(2, xs_fmt("cannot read child %s", cmd5));
            }

            /* if no children were finally added, hide the details */
            if (cnt == 0)
                xs_html_add(ch_details,
                    xs_html_attr("style", "display: none"));

            if (o_cnt == 0 && ch_older)
                xs_html_add(ch_older,
                    xs_html_attr("style", "display: none"));
        }
    }

    return entry_top;
}


xs_html *html_footer(void)
{
    return xs_html_tag("div",
        xs_html_attr("class", "snac-footer"),
        xs_html_tag("a",
            xs_html_attr("href", srv_baseurl),
            xs_html_text(L("about this site"))),
        xs_html_text(" - "),
        xs_html_text(L("powered by ")),
        xs_html_tag("a",
            xs_html_attr("href", WHAT_IS_SNAC_URL),
            xs_html_tag("abbr",
                xs_html_attr("title", "Social Networks Are Crap"),
                xs_html_text("snac"))));
}


xs_str *html_timeline(snac *user, const xs_list *list, int read_only,
                      int skip, int show, int show_more,
                      const char *title, const char *page,
                      int utl, const char *error)
/* returns the HTML for the timeline */
{
    xs_list *p = (xs_list *)list;
    const char *v;
    double t = ftime();
    int hide_children = xs_is_true(xs_dict_get(srv_config, "strict_public_timelines")) && read_only;

    xs *desc = NULL;
    xs *alternate = NULL;

    if (xs_list_len(list) == 1) {
        /* only one element? pick the description from the source */
        const char *id = xs_list_get(list, 0);
        xs *d = NULL;
        object_get_by_md5(id, &d);
        const char *sc = xs_dict_get(d, "sourceContent");
        if (d && sc != NULL)
            desc = xs_dup(sc);

        alternate = xs_dup(xs_dict_get(d, "id"));
    }

    xs_html *head;
    xs_html *body;

    if (user) {
        head = html_user_head(user, desc, alternate);
        body = html_user_body(user, read_only);
    }
    else {
        head = html_instance_head();
        body = html_instance_body();
    }

    xs_html *html = xs_html_tag("html",
        head,
        body);

    if (user && !read_only)
        xs_html_add(body,
            html_top_controls(user));

    if (error != NULL) {
        xs_html_add(body,
            xs_html_tag("dialog",
                xs_html_attr("open", NULL),
                xs_html_tag("p",
                    xs_html_text(error)),
                xs_html_tag("form",
                    xs_html_attr("method", "dialog"),
                    xs_html_sctag("input",
                        xs_html_attr("type", "submit"),
                        xs_html_attr("value", L("Dismiss"))))));
    }

    /* show links to the available lists */
    if (user && !read_only) {
        xs_html *lol = xs_html_tag("ul",
            xs_html_attr("class", "snac-list-of-lists"));
        xs_html_add(body, lol);

        xs *lists = list_maint(user, NULL, 0); /* get list of lists */

        int ct = 0;
        const char *v;

        while (xs_list_next(lists, &v, &ct)) {
            const char *lname = xs_list_get(v, 1);
            xs *url = xs_fmt("%s/list/%s", user->actor, xs_list_get(v, 0));
            xs *ttl = xs_fmt(L("Timeline for list '%s'"), lname);

            xs_html_add(lol,
                xs_html_tag("li",
                    xs_html_tag("a",
                        xs_html_attr("href", url),
                        xs_html_attr("class", "snac-list-link"),
                        xs_html_attr("title", ttl),
                        xs_html_text(lname))));
        }

        {
            /* show the list of pinned posts */
            xs *url = xs_fmt("%s/pinned", user->actor);
            xs_html_add(lol,
                xs_html_tag("li",
                    xs_html_tag("a",
                        xs_html_attr("href", url),
                        xs_html_attr("class", "snac-list-link"),
                        xs_html_attr("title", L("Pinned posts")),
                        xs_html_text("pinned"))));
        }

        {
            /* show the list of bookmarked posts */
            xs *url = xs_fmt("%s/bookmarks", user->actor);
            xs_html_add(lol,
                xs_html_tag("li",
                    xs_html_tag("a",
                        xs_html_attr("href", url),
                        xs_html_attr("class", "snac-list-link"),
                        xs_html_attr("title", L("Bookmarked posts")),
                        xs_html_text("bookmarks"))));
        }

        {
            /* show the list of drafts */
            xs *url = xs_fmt("%s/drafts", user->actor);
            xs_html_add(lol,
                xs_html_tag("li",
                    xs_html_tag("a",
                        xs_html_attr("href", url),
                        xs_html_attr("class", "snac-list-link"),
                        xs_html_attr("title", L("Post drafts")),
                        xs_html_text("drafts"))));
        }
    }

    xs_html_add(body,
        xs_html_tag("a",
            xs_html_attr("name", "snac-posts")));

    xs_html *posts = xs_html_tag("div",
        xs_html_attr("class", "snac-posts"));

    if (title) {
        xs_html_add(posts,
            xs_html_tag("h2",
                xs_html_attr("class", "snac-header"),
                xs_html_text(title)));
    }

    xs_html_add(body,
        posts);

    while (xs_list_iter(&p, &v)) {
        xs *msg = NULL;
        int status;

        if (utl && user && !is_pinned_by_md5(user, v))
            status = timeline_get_by_md5(user, v, &msg);
        else
            status = object_get_by_md5(v, &msg);

        if (!valid_status(status))
            continue;

        /* if it's an instance page, discard messages from private users */
        if (user == NULL && is_msg_from_private_user(msg))
            continue;

        /* is this message a non-public reply? */
        if (user != NULL && !is_msg_public(msg)) {
            const char *irt = get_in_reply_to(msg);

            /* is it a reply to something not in the storage? */
            if (!xs_is_null(irt) && !object_here(irt)) {
                /* is it for me? */
                const xs_list *to = xs_dict_get_def(msg, "to", xs_stock(XSTYPE_LIST));
                const xs_list *cc = xs_dict_get_def(msg, "cc", xs_stock(XSTYPE_LIST));

                if (xs_list_in(to, user->actor) == -1 && xs_list_in(cc, user->actor) == -1) {
                    snac_debug(user, 1, xs_fmt("skipping non-public reply to an unknown post %s", v));
                    continue;
                }
            }
        }

        xs_html *entry = html_entry(user, msg, read_only, 0, v, (user && !hide_children) ? 0 : 1);

        if (entry != NULL)
            xs_html_add(posts,
                entry);
    }

    if (list && user && read_only) {
        /** history **/
        if (xs_type(xs_dict_get(srv_config, "disable_history")) != XSTYPE_TRUE) {
            xs_html *ul = xs_html_tag("ul", NULL);

            xs_html *history = xs_html_tag("div",
                xs_html_attr("class", "snac-history"),
                xs_html_tag("p",
                    xs_html_attr("class", "snac-history-title"),
                    xs_html_text(L("History"))),
                    ul);

            xs *list = history_list(user);
            xs_list *p = list;
            const char *v;

            while (xs_list_iter(&p, &v)) {
                xs *fn  = xs_replace(v, ".html", "");
                xs *url = xs_fmt("%s/h/%s", user->actor, v);

                xs_html_add(ul,
                    xs_html_tag("li",
                        xs_html_tag("a",
                            xs_html_attr("href", url),
                            xs_html_text(fn))));
            }

            xs_html_add(body,
                history);
        }
    }

    {
        xs *s1 = xs_fmt("\n<!-- %lf seconds -->\n", ftime() - t);
        xs_html_add(body,
            xs_html_raw(s1));
    }

    if (show_more) {
        xs *m  = NULL;
        xs *ss = xs_fmt("skip=%d&show=%d", skip + show, show);

        xs *url = xs_dup(user == NULL ? srv_baseurl : user->actor);

        if (page != NULL)
            url = xs_str_cat(url, page);

        if (xs_str_in(url, "?") != -1)
            m = xs_fmt("%s&%s", url, ss);
        else
            m = xs_fmt("%s?%s", url, ss);

        xs_html *more_links = xs_html_tag("p",
            xs_html_tag("a",
                xs_html_attr("href", url),
                xs_html_attr("name", "snac-more"),
                xs_html_text(L("Back to top"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", m),
                xs_html_attr("name", "snac-more"),
                xs_html_text(L("More..."))));

        xs_html_add(body,
            more_links);
    }

    xs_html_add(body,
        html_footer());

    return xs_html_render_s(html, "<!DOCTYPE html>\n");
}


xs_html *html_people_list(snac *snac, xs_list *list, char *header, char *t, const char *proxy)
{
    xs_html *snac_posts;
    xs_html *people = xs_html_tag("div",
        xs_html_tag("h2",
            xs_html_attr("class", "snac-header"),
            xs_html_text(header)),
        snac_posts = xs_html_tag("details",
                xs_html_attr("open", NULL),
                xs_html_tag("summary",
                    xs_html_text("..."))));

    xs_list *p = list;
    const char *actor_id;

    while (xs_list_iter(&p, &actor_id)) {
        xs *md5 = xs_md5_hex(actor_id, strlen(actor_id));
        xs *actor = NULL;

        if (valid_status(actor_get(actor_id, &actor))) {
            xs_html *snac_post = xs_html_tag("div",
                xs_html_attr("class", "snac-post"),
                xs_html_tag("a",
                    xs_html_attr("name", md5)),
                xs_html_tag("div",
                    xs_html_attr("class", "snac-post-header"),
                    html_actor_icon(snac, actor, xs_dict_get(actor, "published"),
                                    NULL, NULL, 0, 1, proxy)));

            /* content (user bio) */
            const char *c = xs_dict_get(actor, "summary");

            if (!xs_is_null(c)) {
                xs *sc = sanitize(c);

                xs_html *snac_content = xs_html_tag("div",
                    xs_html_attr("class", "snac-content"));

                if (xs_startswith(sc, "<p>"))
                    xs_html_add(snac_content,
                        xs_html_raw(sc)); /* already sanitized */
                else
                    xs_html_add(snac_content,
                        xs_html_tag("p",
                            xs_html_raw(sc))); /* already sanitized */

                xs_html_add(snac_post, snac_content);
            }

            /* buttons */
            xs *btn_form_action = xs_fmt("%s/admin/action", snac->actor);

            xs_html *snac_controls = xs_html_tag("div",
                xs_html_attr("class",   "snac-controls"));

            xs_html *form = xs_html_tag("form",
                xs_html_attr("autocomplete",    "off"),
                xs_html_attr("method",          "post"),
                xs_html_attr("action",          btn_form_action),
                xs_html_sctag("input",
                    xs_html_attr("type",    "hidden"),
                    xs_html_attr("name",    "actor"),
                    xs_html_attr("value",   actor_id)),
                xs_html_sctag("input",
                    xs_html_attr("type",    "hidden"),
                    xs_html_attr("name",    "actor-form"),
                    xs_html_attr("value",   "yes")));

            xs_html_add(snac_controls, form);

            if (following_check(snac, actor_id)) {
                xs_html_add(form,
                    html_button("unfollow", L("Unfollow"),
                                L("Stop following this user's activity")));

                if (is_limited(snac, actor_id))
                    xs_html_add(form,
                        html_button("unlimit", L("Unlimit"),
                                L("Allow announces (boosts) from this user")));
                else
                    xs_html_add(form,
                        html_button("limit", L("Limit"),
                                L("Block announces (boosts) from this user")));
            }
            else {
                xs_html_add(form,
                    html_button("follow", L("Follow"),
                                L("Start following this user's activity")));

                if (follower_check(snac, actor_id))
                    xs_html_add(form,
                        html_button("delete", L("Delete"), L("Delete this user")));
            }

            if (is_muted(snac, actor_id))
                xs_html_add(form,
                    html_button("unmute", L("Unmute"),
                                L("Stop blocking activities from this user")));
            else
                xs_html_add(form,
                    html_button("mute", L("MUTE"),
                                L("Block any activity from this user")));

            /* the post textarea */
            xs *dm_div_id  = xs_fmt("%s_%s_dm", md5, t);
            xs *dm_form_id = xs_fmt("%s_reply_form", md5);

            xs_html_add(snac_controls,
                xs_html_tag("p", NULL),
                html_note(snac, L("Direct Message..."),
                    dm_div_id, dm_form_id,
                    "", "",
                    NULL, actor_id,
                    xs_stock(XSTYPE_FALSE), "",
                    xs_stock(XSTYPE_FALSE), NULL,
                    NULL, 0, "", "", 0),
                xs_html_tag("p", NULL));

            xs_html_add(snac_post, snac_controls);

            xs_html_add(snac_posts, snac_post);
        }
    }

    return people;
}


xs_str *html_people(snac *user)
{
    const char *proxy = NULL;

    if (xs_is_true(xs_dict_get(srv_config, "proxy_media")))
        proxy = user->actor;

    xs *wing = following_list(user);
    xs *wers = follower_list(user);

    xs_html *html = xs_html_tag("html",
        html_user_head(user, NULL, NULL),
        xs_html_add(html_user_body(user, 0),
            xs_html_tag("div",
                xs_html_attr("class", "snac-posts"),
                html_people_list(user, wing, L("People you follow"), "i", proxy),
                html_people_list(user, wers, L("People that follow you"), "e", proxy)),
            html_footer()));

    return xs_html_render_s(html, "<!DOCTYPE html>\n");
}


xs_str *html_notifications(snac *user, int skip, int show)
{
    const char *proxy = NULL;

    if (xs_is_true(xs_dict_get(srv_config, "proxy_media")))
        proxy = user->actor;

    xs *n_list = notify_list(user, skip, show);
    xs *n_time = notify_check_time(user, 0);

    xs_html *body = html_user_body(user, 0);

    xs_html *html = xs_html_tag("html",
        html_user_head(user, NULL, NULL),
        body);

    xs *clear_all_action = xs_fmt("%s/admin/clear-notifications", user->actor);

    xs_html_add(body,
        xs_html_tag("form",
            xs_html_attr("autocomplete", "off"),
            xs_html_attr("method",       "post"),
            xs_html_attr("action",       clear_all_action),
            xs_html_attr("id",           "clear"),
            xs_html_sctag("input",
                xs_html_attr("type",     "submit"),
                xs_html_attr("class",    "snac-btn-like"),
                xs_html_attr("value",    L("Clear all")))));

    xs_html *noti_new = NULL;
    xs_html *noti_seen = NULL;

    xs_html *posts = xs_html_tag("div",
        xs_html_attr("class", "snac-posts"));
    xs_html_add(body, posts);

    xs_list *p = n_list;
    const xs_str *v;
    while (xs_list_iter(&p, &v)) {
        xs *noti = notify_get(user, v);

        if (noti == NULL)
            continue;

        xs *obj = NULL;
        const char *type  = xs_dict_get(noti, "type");
        const char *utype = xs_dict_get(noti, "utype");
        const char *id    = xs_dict_get(noti, "objid");
        const char *date  = xs_dict_get(noti, "date");
        xs *wrk = NULL;

        if (xs_is_null(id))
            continue;

        if (is_hidden(user, id))
            continue;

        object_get(id, &obj);

        const char *actor_id = xs_dict_get(noti, "actor");
        xs *actor = NULL;

        if (!valid_status(actor_get(actor_id, &actor)))
            continue;

        xs *a_name = actor_name(actor, proxy);
        const char *label = type;

        if (strcmp(type, "Create") == 0)
            label = L("Mention");
        else
        if (strcmp(type, "Update") == 0 && strcmp(utype, "Question") == 0)
            label = L("Finished poll");
        else
        if (strcmp(type, "Undo") == 0 && strcmp(utype, "Follow") == 0)
            label = L("Unfollow");
        else
        if (strcmp(type, "EmojiReact") == 0) {
            const char *content = xs_dict_get_path(noti, "msg.content");

            if (xs_type(content) == XSTYPE_STRING) {
                wrk = xs_fmt("%s (%s)", type, content);
                label = wrk;
            }
        }

        xs *s_date = xs_crop_i(xs_dup(date), 0, 10);

        xs_html *entry = xs_html_tag("div",
            xs_html_attr("class", "snac-post-with-desc"),
            xs_html_tag("p",
                xs_html_tag("b",
                    xs_html_text(label),
                    xs_html_text(" by "),
                    xs_html_tag("a",
                        xs_html_attr("href", actor_id),
                        xs_html_raw(a_name))), /* a_name is already sanitized */
                xs_html_text(" "),
                xs_html_tag("time",
                    xs_html_attr("class", "dt-published snac-pubdate"),
                    xs_html_attr("title", date),
                    xs_html_text(s_date))));

        if (strcmp(type, "Follow") == 0 || strcmp(utype, "Follow") == 0 || strcmp(type, "Block") == 0) {
            xs_html_add(entry,
                xs_html_tag("div",
                    xs_html_attr("class", "snac-post"),
                    html_actor_icon(user, actor, NULL, NULL, NULL, 0, 0, proxy)));
        }
        else
        if (strcmp(type, "Move") == 0) {
            const xs_dict *o_msg = xs_dict_get(noti, "msg");
            const char *target;

            if (xs_type(o_msg) == XSTYPE_DICT && (target = xs_dict_get(o_msg, "target"))) {
                xs *old_actor = NULL;

                if (valid_status(actor_get(target, &old_actor))) {
                    xs_html_add(entry,
                        xs_html_tag("div",
                            xs_html_attr("class", "snac-post"),
                            html_actor_icon(user, old_actor, NULL, NULL, NULL, 0, 0, proxy)));
                }
            }
        }
        else
        if (obj != NULL) {
            xs *md5 = xs_md5_hex(id, strlen(id));

            xs_html *h = html_entry(user, obj, 0, 0, md5, 1);

            if (h != NULL) {
                xs_html_add(entry,
                    h);
            }
        }

        if (strcmp(v, n_time) > 0) {
            /* unseen notification */
            if (noti_new == NULL) {
                noti_new = xs_html_tag("div",
                    xs_html_tag("h2",
                        xs_html_attr("class", "snac-header"),
                        xs_html_text(L("New"))));

                xs_html_add(posts,
                    noti_new);
            }

            xs_html_add(noti_new,
                entry);
        }
        else {
            /* already seen notification */
            if (noti_seen == NULL) {
                noti_seen = xs_html_tag("div",
                    xs_html_tag("h2",
                        xs_html_attr("class", "snac-header"),
                        xs_html_text(L("Already seen"))));

                xs_html_add(posts,
                    noti_seen);
            }

            xs_html_add(noti_seen,
                entry);
        }
    }

    if (noti_new == NULL && noti_seen == NULL)
        xs_html_add(body,
            xs_html_tag("h2",
                xs_html_attr("class", "snac-header"),
                xs_html_text(L("None"))));

    /* add the navigation footer */
    xs *next_p = notify_list(user, skip + show, 1);
    if (xs_list_len(next_p)) {
        xs *url = xs_fmt("%s/notifications?skip=%d&show=%d",
            user->actor, skip + show, show);

        xs_html_add(body,
            xs_html_tag("p",
                xs_html_tag("a",
                    xs_html_attr("href", url),
                    xs_html_text(L("More...")))));
    }

    xs_html_add(body,
        html_footer());

    /* set the check time to now */
    xs *dummy = notify_check_time(user, 1);
    dummy = xs_free(dummy);

    timeline_touch(user);

    return xs_html_render_s(html, "<!DOCTYPE html>\n");
}


int html_get_handler(const xs_dict *req, const char *q_path,
                     char **body, int *b_size, char **ctype,
                     xs_str **etag, xs_str **last_modified)
{
    const char *accept = xs_dict_get(req, "accept");
    int status = HTTP_STATUS_NOT_FOUND;
    snac snac;
    xs *uid = NULL;
    const char *p_path;
    int cache = 1;
    int save = 1;
    int proxy = 0;
    const char *v;

    xs *l = xs_split_n(q_path, "/", 2);
    v = xs_list_get(l, 1);

    if (xs_is_null(v)) {
        srv_log(xs_fmt("html_get_handler bad query '%s'", q_path));
        return HTTP_STATUS_NOT_FOUND;
    }

    uid = xs_dup(v);

    /* rss extension? */
    if (xs_endswith(uid, ".rss")) {
        uid = xs_crop_i(uid, 0, -4);
        p_path = ".rss";
    }
    else
        p_path = xs_list_get(l, 2);

    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("html_get_handler bad user %s", uid));
        return HTTP_STATUS_NOT_FOUND;
    }

    if (xs_is_true(xs_dict_get(srv_config, "proxy_media")))
        proxy = 1;

    /* return the RSS if requested by Accept header */
    if (accept != NULL) {
        if (xs_str_in(accept, "text/xml") != -1 ||
            xs_str_in(accept, "application/rss+xml") != -1)
            p_path = ".rss";
    }

    /* check if server config variable 'disable_cache' is set */
    if ((v = xs_dict_get(srv_config, "disable_cache")) && xs_type(v) == XSTYPE_TRUE)
        cache = 0;

    int skip = 0;
    int def_show = xs_number_get(xs_dict_get(srv_config, "max_timeline_entries"));
    int show = def_show;

    const xs_dict *q_vars = xs_dict_get(req, "q_vars");
    if ((v = xs_dict_get(q_vars, "skip")) != NULL)
        skip = atoi(v), cache = 0, save = 0;
    if ((v = xs_dict_get(q_vars, "show")) != NULL)
        show = atoi(v), cache = 0, save = 0;
    if ((v = xs_dict_get(q_vars, "da")) != NULL) {
        /* user dismissed an announcement */
        if (login(&snac, req)) {
            double ts = atof(v);
            xs *timestamp = xs_number_new(ts);
            srv_log(xs_fmt("user dismissed announcements until %d", ts));
            snac.config = xs_dict_set(snac.config, "last_announcement", timestamp);
            user_persist(&snac, 0);
        }
    }

    /* get a possible error message */
    const char *error = xs_dict_get(q_vars, "error");
    if (error != NULL)
        cache = 0;

    /* a show of 0 has no sense */
    if (show == 0)
        show = def_show;

    if (p_path == NULL) { /** public timeline **/
        xs *h = xs_str_localtime(0, "%Y-%m.html");

        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE) {
            /** empty public timeline for private users **/
            *body = html_timeline(&snac, NULL, 1, 0, 0, 0, NULL, "", 1, error);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
        else
        if (cache && history_mtime(&snac, h) > timeline_mtime(&snac)) {
            snac_debug(&snac, 1, xs_fmt("serving cached local timeline"));

            status = history_get(&snac, h, body, b_size,
                        xs_dict_get(req, "if-none-match"), etag);
        }
        else {
            xs *list = NULL;
            xs *next = NULL;

            if (xs_is_true(xs_dict_get(srv_config, "strict_public_timelines"))) {
                list = timeline_simple_list(&snac, "public", skip, show);
                next = timeline_simple_list(&snac, "public", skip + show, 1);
            }
            else {
                list = timeline_list(&snac, "public", skip, show);
                next = timeline_list(&snac, "public", skip + show, 1);
            }

            xs *pins = pinned_list(&snac);
            pins = xs_list_cat(pins, list);

            *body = html_timeline(&snac, pins, 1, skip, show, xs_list_len(next), NULL, "", 1, error);

            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;

            if (save)
                history_add(&snac, h, *body, *b_size, etag);
        }
    }
    else
    if (strcmp(p_path, "admin") == 0) { /** private timeline **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            const char *q = xs_dict_get(q_vars, "q");

            if (q && *q) {
                if (*q == '#') {
                    /** search by tag **/
                    xs *tl = tag_search(q, skip, show + 1);
                    int more = 0;
                    if (xs_list_len(tl) >= show + 1) {
                        /* drop the last one */
                        tl = xs_list_del(tl, -1);
                        more = 1;
                    }

                    xs *page = xs_fmt("/admin?q=%%23%s", q + 1);
                    xs *title = xs_fmt(xs_list_len(tl) ?
                        L("Search results for tag %s") : L("Nothing found for tag %s"), q);

                    *body = html_timeline(&snac, tl, 0, skip, show, more, title, page, 0, error);
                    *b_size = strlen(*body);
                    status  = HTTP_STATUS_OK;
                }
                else {
                    /** search by content **/
                    int to = 0;
                    int msecs = atoi(xs_dict_get_def(q_vars, "msecs", "0"));
                    xs *tl = content_search(&snac, q, 1, skip, show, msecs, &to);
                    xs *title = NULL;
                    xs *page = xs_fmt("/admin?q=%s&msecs=%d", q, msecs + 10);
                    int tl_len = xs_list_len(tl);

                    if (to)
                        title = xs_fmt(L("Search results for '%s' (may be more)"), q);
                    else
                    if (tl_len)
                        title = xs_fmt(L("Search results for '%s'"), q);
                    else
                    if (skip)
                        title = xs_fmt(L("No more matches for '%s'"), q);
                    else
                        title = xs_fmt(L("Nothing found for '%s'"), q);

                    *body   = html_timeline(&snac, tl, 0, skip, tl_len, to || tl_len == show,
                                            title, page, 0, error);
                    *b_size = strlen(*body);
                    status  = HTTP_STATUS_OK;
                }
            }
            else {
                double t = history_mtime(&snac, "timeline.html_");

                /* if enabled by admin, return a cached page if its timestamp is:
                   a) newer than the timeline timestamp
                   b) newer than the start time of the server
                */
                if (cache && t > timeline_mtime(&snac) && t > p_state->srv_start_time) {
                    snac_debug(&snac, 1, xs_fmt("serving cached timeline"));

                    status = history_get(&snac, "timeline.html_", body, b_size,
                                xs_dict_get(req, "if-none-match"), etag);
                }
                else {
                    snac_debug(&snac, 1, xs_fmt("building timeline"));

                    xs *list = timeline_list(&snac, "private", skip, show);
                    xs *next = timeline_list(&snac, "private", skip + show, 1);

                    *body = html_timeline(&snac, list, 0, skip, show,
                            xs_list_len(next), NULL, "/admin", 1, error);

                    *b_size = strlen(*body);
                    status  = HTTP_STATUS_OK;

                    if (save)
                        history_add(&snac, "timeline.html_", *body, *b_size, etag);
                }
            }
        }
    }
    else
    if (xs_startswith(p_path, "admin/p/")) { /** unique post by md5 **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            xs *l = xs_split(p_path, "/");
            const char *md5 = xs_list_get(l, -1);

            if (md5 && *md5 && timeline_here(&snac, md5)) {
                xs *list = xs_list_append(xs_list_new(), md5);

                *body   = html_timeline(&snac, list, 0, 0, 0, 0, NULL, "/admin", 1, error);
                *b_size = strlen(*body);
                status  = HTTP_STATUS_OK;
            }
        }
    }
    else
    if (strcmp(p_path, "people") == 0) { /** the list of people **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            *body   = html_people(&snac);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
    }
    else
    if (strcmp(p_path, "notifications") == 0) { /** the list of notifications **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            *body   = html_notifications(&snac, skip, show);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
    }
    else
    if (strcmp(p_path, "instance") == 0) { /** instance timeline **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            xs *list = timeline_instance_list(skip, show);
            xs *next = timeline_instance_list(skip + show, 1);

            *body = html_timeline(&snac, list, 0, skip, show,
                xs_list_len(next), L("Showing instance timeline"), "/instance", 0, error);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
    }
    else
    if (strcmp(p_path, "pinned") == 0) { /** list of pinned posts **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            xs *list = pinned_list(&snac);

            *body = html_timeline(&snac, list, 0, skip, show,
                0, L("Pinned posts"), "", 0, error);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
    }
    else
    if (strcmp(p_path, "bookmarks") == 0) { /** list of bookmarked posts **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            xs *list = bookmark_list(&snac);

            *body = html_timeline(&snac, list, 0, skip, show,
                0, L("Bookmarked posts"), "", 0, error);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
    }
    else
    if (strcmp(p_path, "drafts") == 0) { /** list of drafts **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            xs *list = draft_list(&snac);

            *body = html_timeline(&snac, list, 0, skip, show,
                0, L("Post drafts"), "", 0, error);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
    }
    else
    if (xs_startswith(p_path, "list/")) { /** list timelines **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = HTTP_STATUS_UNAUTHORIZED;
        }
        else {
            xs *l = xs_split(p_path, "/");
            const char *lid = xs_list_get(l, -1);

            xs *list = list_timeline(&snac, lid, skip, show);
            xs *next = list_timeline(&snac, lid, skip + show, 1);

            if (list != NULL) {
                xs *ttl = timeline_top_level(&snac, list);

                xs *base = xs_fmt("/list/%s", lid);
                xs *name = list_maint(&snac, lid, 3);
                xs *title = xs_fmt(L("Showing timeline for list '%s'"), name);

                *body = html_timeline(&snac, ttl, 0, skip, show,
                    xs_list_len(next), title, base, 1, error);
                *b_size = strlen(*body);
                status  = HTTP_STATUS_OK;
            }
        }
    }
    else
    if (xs_startswith(p_path, "p/")) { /** a timeline with just one entry **/
        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE)
            return HTTP_STATUS_FORBIDDEN;

        xs *id  = xs_fmt("%s/%s", snac.actor, p_path);
        xs *msg = NULL;

        if (valid_status(object_get(id, &msg))) {
            xs *md5  = xs_md5_hex(id, strlen(id));
            xs *list = xs_list_new();

            list = xs_list_append(list, md5);

            *body   = html_timeline(&snac, list, 1, 0, 0, 0, NULL, "", 1, error);
            *b_size = strlen(*body);
            status  = HTTP_STATUS_OK;
        }
    }
    else
    if (xs_startswith(p_path, "s/")) { /** a static file **/
        xs *l    = xs_split(p_path, "/");
        const char *id = xs_list_get(l, 1);
        int sz;

        if (id && *id) {
            status = static_get(&snac, id, body, &sz,
                        xs_dict_get(req, "if-none-match"), etag);

            if (valid_status(status)) {
                *b_size = sz;
                *ctype  = (char *)xs_mime_by_ext(id);
            }
        }
    }
    else
    if (xs_startswith(p_path, "h/")) { /** an entry from the history **/
        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE)
            return HTTP_STATUS_FORBIDDEN;

        if (xs_type(xs_dict_get(srv_config, "disable_history")) == XSTYPE_TRUE)
            return HTTP_STATUS_FORBIDDEN;

        xs *l = xs_split(p_path, "/");
        const char *id = xs_list_get(l, 1);

        if (id && *id) {
            if (xs_endswith(id, "timeline.html_")) {
                /* Don't let them in */
                *b_size = 0;
                status = HTTP_STATUS_NOT_FOUND;
            }
            else
                status = history_get(&snac, id, body, b_size,
                            xs_dict_get(req, "if-none-match"), etag);
        }
    }
    else
    if (strcmp(p_path, ".rss") == 0) { /** public timeline in RSS format **/
        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE)
            return HTTP_STATUS_FORBIDDEN;

        xs *elems = timeline_simple_list(&snac, "public", 0, 20);
        xs *bio   = xs_dup(xs_dict_get(snac.config, "bio"));

        xs *rss_title = xs_fmt("%s (@%s@%s)",
            xs_dict_get(snac.config, "name"),
            snac.uid,
            xs_dict_get(srv_config, "host"));
        xs *rss_link = xs_fmt("%s.rss", snac.actor);

        *body   = timeline_to_rss(&snac, elems, rss_title, rss_link, bio);
        *b_size = strlen(*body);
        *ctype  = "application/rss+xml; charset=utf-8";
        status  = HTTP_STATUS_OK;

        snac_debug(&snac, 1, xs_fmt("serving RSS"));
    }
    else
    if (proxy && (xs_startswith(p_path, "x/") || xs_startswith(p_path, "y/"))) { /** remote media by proxy **/
        xs *proxy_prefix = NULL;

        if (xs_startswith(p_path, "x/")) {
            /* proxy usage authorized by http basic auth */
            if (login(&snac, req))
                proxy_prefix = xs_str_new("x/");
            else {
                *body  = xs_dup(uid);
                status = HTTP_STATUS_UNAUTHORIZED;
            }
        }
        else {
            /* proxy usage authorized by proxy_token */
            xs *tks = xs_fmt("%s:%s", srv_proxy_token_seed, snac.actor);
            xs *tk = xs_md5_hex(tks, strlen(tks));
            xs *p = xs_fmt("y/%s/", tk);

            if (xs_startswith(p_path, p))
                proxy_prefix = xs_dup(p);
        }

        if (proxy_prefix) {
            /* pick the raw path (including optional ? arguments) */
            const char *raw_path = xs_dict_get(req, "raw_path");

            /* skip to where the proxy/ string starts */
            raw_path += xs_str_in(raw_path, proxy_prefix);

            xs *url = xs_replace_n(raw_path, proxy_prefix, "https:/" "/", 1);
            xs *hdrs = xs_dict_new();

            hdrs = xs_dict_append(hdrs, "user-agent", USER_AGENT);

            const char *ims = xs_dict_get(req, "if-modified-since");
            const char *inm = xs_dict_get(req, "if-none-match");

            if (ims) hdrs = xs_dict_append(hdrs, "if-modified-since", ims);
            if (inm) hdrs = xs_dict_append(hdrs, "if-none-match", inm);

            xs *rsp = xs_http_request("GET", url, hdrs,
                        NULL, 0, &status, body, b_size, 0);

            if (valid_status(status)) {
                const char *ct = xs_or(xs_dict_get(rsp, "content-type"), "");
                const char *lm = xs_dict_get(rsp, "last-modified");
                const char *et = xs_dict_get(rsp, "etag");

                if (lm) *last_modified = xs_dup(lm);
                if (et) *etag = xs_dup(et);

                /* find the content-type in the static mime types,
                   and return that value instead of ct, which will
                   be destroyed when out of scope */
                for (int n = 0; xs_mime_types[n]; n += 2) {
                    if (strcmp(ct, xs_mime_types[n + 1]) == 0) {
                        *ctype = (char *)xs_mime_types[n + 1];
                        break;
                    }
                }
            }

            snac_debug(&snac, 1, xs_fmt("Proxy for %s %d", url, status));
        }
    }
    else
        status = HTTP_STATUS_NOT_FOUND;

    user_free(&snac);

    if (valid_status(status) && *ctype == NULL) {
        *ctype = "text/html; charset=utf-8";
    }

    return status;
}


int html_post_handler(const xs_dict *req, const char *q_path,
                      char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    (void)p_size;
    (void)ctype;

    int status = 0;
    snac snac;
    const char *uid;
    const char *p_path;
    const xs_dict *p_vars;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("html_post_handler bad user %s", uid));
        return HTTP_STATUS_NOT_FOUND;
    }

    p_path = xs_list_get(l, 2);

    /* all posts must be authenticated */
    if (!login(&snac, req)) {
        user_free(&snac);
        *body  = xs_dup(uid);
        return HTTP_STATUS_UNAUTHORIZED;
    }

    p_vars = xs_dict_get(req, "p_vars");

    if (p_path && strcmp(p_path, "admin/note") == 0) { /** **/
        snac_debug(&snac, 1, xs_fmt("web action '%s' received", p_path));

        /* post note */
        const xs_str *content      = xs_dict_get(p_vars, "content");
        const xs_str *in_reply_to  = xs_dict_get(p_vars, "in_reply_to");
        const xs_str *attach_url   = xs_dict_get(p_vars, "attach_url");
        const xs_list *attach_file = xs_dict_get(p_vars, "attach");
        const xs_str *to           = xs_dict_get(p_vars, "to");
        const xs_str *sensitive    = xs_dict_get(p_vars, "sensitive");
        const xs_str *summary      = xs_dict_get(p_vars, "summary");
        const xs_str *edit_id      = xs_dict_get(p_vars, "edit_id");
        const xs_str *alt_text     = xs_dict_get(p_vars, "alt_text");
        int priv             = !xs_is_null(xs_dict_get(p_vars, "mentioned_only"));
        int store_as_draft   = !xs_is_null(xs_dict_get(p_vars, "is_draft"));
        xs *attach_list      = xs_list_new();

        /* default alt text */
        if (xs_is_null(alt_text))
            alt_text = "";

        /* is attach_url set? */
        if (!xs_is_null(attach_url) && *attach_url != '\0') {
            xs *l = xs_list_new();

            l = xs_list_append(l, attach_url);
            l = xs_list_append(l, alt_text);

            attach_list = xs_list_append(attach_list, l);
        }

        /* is attach_file set? */
        if (!xs_is_null(attach_file) && xs_type(attach_file) == XSTYPE_LIST) {
            const char *fn = xs_list_get(attach_file, 0);

            if (*fn != '\0') {
                char *ext = strrchr(fn, '.');
                xs *hash  = xs_md5_hex(fn, strlen(fn));
                xs *id    = xs_fmt("%s%s", hash, ext);
                xs *url   = xs_fmt("%s/s/%s", snac.actor, id);
                int fo    = xs_number_get(xs_list_get(attach_file, 1));
                int fs    = xs_number_get(xs_list_get(attach_file, 2));

                /* store */
                static_put(&snac, id, payload + fo, fs);

                xs *l = xs_list_new();

                l = xs_list_append(l, url);
                l = xs_list_append(l, alt_text);

                attach_list = xs_list_append(attach_list, l);
            }
        }

        if (content != NULL) {
            xs *msg       = NULL;
            xs *c_msg     = NULL;
            xs *content_2 = xs_replace(content, "\r", "");
            xs *poll_opts = NULL;

            /* is there a valid set of poll options? */
            const char *v = xs_dict_get(p_vars, "poll_options");
            if (!xs_is_null(v) && *v) {
                xs *v2 = xs_strip_i(xs_replace(v, "\r", ""));

                poll_opts = xs_split(v2, "\n");
            }

            if (!xs_is_null(poll_opts) && xs_list_len(poll_opts)) {
                /* get the rest of poll configuration */
                const char *p_multiple = xs_dict_get(p_vars, "poll_multiple");
                const char *p_end_secs = xs_dict_get(p_vars, "poll_end_secs");
                int multiple = 0;

                int end_secs = atoi(!xs_is_null(p_end_secs) ? p_end_secs : "60");

                if (!xs_is_null(p_multiple) && strcmp(p_multiple, "on") == 0)
                    multiple = 1;

                msg = msg_question(&snac, content_2, attach_list,
                                   poll_opts, multiple, end_secs);

                enqueue_close_question(&snac, xs_dict_get(msg, "id"), end_secs);
            }
            else
                msg = msg_note(&snac, content_2, to, in_reply_to, attach_list, priv);

            if (sensitive != NULL) {
                msg = xs_dict_set(msg, "sensitive", xs_stock(XSTYPE_TRUE));
                msg = xs_dict_set(msg, "summary",   xs_is_null(summary) ? "..." : summary);
            }

            if (xs_is_null(edit_id)) {
                /* new message */
                const char *id = xs_dict_get(msg, "id");

                if (store_as_draft) {
                    draft_add(&snac, id, msg);
                }
                else {
                    c_msg = msg_create(&snac, msg);
                    timeline_add(&snac, id, msg);
                }
            }
            else {
                /* an edition of a previous message */
                xs *p_msg = NULL;

                if (valid_status(object_get(edit_id, &p_msg))) {
                    /* copy relevant fields from previous version */
                    char *fields[] = { "id", "context", "url", "published",
                                       "to", "inReplyTo", NULL };
                    int n;

                    for (n = 0; fields[n]; n++) {
                        const char *v = xs_dict_get(p_msg, fields[n]);
                        msg = xs_dict_set(msg, fields[n], v);
                    }

                    if (store_as_draft) {
                        draft_add(&snac, edit_id, msg);
                    }
                    else
                    if (is_draft(&snac, edit_id)) {
                        /* message was previously a draft; it's a create activity */

                        /* set the published field to now */
                        xs *published = xs_str_utctime(0, ISO_DATE_SPEC);
                        msg = xs_dict_set(msg, "published", published);

                        /* overwrite object */
                        object_add_ow(edit_id, msg);

                        c_msg = msg_create(&snac, msg);
                        timeline_add(&snac, edit_id, msg);
                        draft_del(&snac, edit_id);
                    }
                    else {
                        /* set the updated field */
                        xs *updated = xs_str_utctime(0, ISO_DATE_SPEC);
                        msg = xs_dict_set(msg, "updated", updated);

                        /* overwrite object, not updating the indexes */
                        object_add_ow(edit_id, msg);

                        /* update message */
                        c_msg = msg_update(&snac, msg);
                    }
                }
                else
                    snac_log(&snac, xs_fmt("cannot get object '%s' for editing", edit_id));
            }

            if (c_msg != NULL)
                enqueue_message(&snac, c_msg);

            history_del(&snac, "timeline.html_");
        }

        status = HTTP_STATUS_SEE_OTHER;
    }
    else
    if (p_path && strcmp(p_path, "admin/action") == 0) { /** **/
        /* action on an entry */
        const char *id     = xs_dict_get(p_vars, "id");
        const char *actor  = xs_dict_get(p_vars, "actor");
        const char *action = xs_dict_get(p_vars, "action");
        const char *group  = xs_dict_get(p_vars, "group");

        if (action == NULL)
            return HTTP_STATUS_NOT_FOUND;

        snac_debug(&snac, 1, xs_fmt("web action '%s' received", action));

        status = HTTP_STATUS_SEE_OTHER;

        if (strcmp(action, L("Like")) == 0) { /** **/
            xs *msg = msg_admiration(&snac, id, "Like");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 1);
            }
        }
        else
        if (strcmp(action, L("Boost")) == 0) { /** **/
            xs *msg = msg_admiration(&snac, id, "Announce");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 0);
            }
        }
        else
        if (strcmp(action, L("Unlike")) == 0) { /** **/
            xs *msg = msg_repulsion(&snac, id, "Like");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
            }
        }
        else
        if (strcmp(action, L("Unboost")) == 0) { /** **/
            xs *msg = msg_repulsion(&snac, id, "Announce");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
            }
        }
        else
        if (strcmp(action, L("MUTE")) == 0) { /** **/
            mute(&snac, actor);
        }
        else
        if (strcmp(action, L("Unmute")) == 0) { /** **/
            unmute(&snac, actor);
        }
        else
        if (strcmp(action, L("Hide")) == 0) { /** **/
            if (is_draft(&snac, id))
                draft_del(&snac, id);
            else
                hide(&snac, id);
        }
        else
        if (strcmp(action, L("Limit")) == 0) { /** **/
            limit(&snac, actor);
        }
        else
        if (strcmp(action, L("Unlimit")) == 0) { /** **/
            unlimit(&snac, actor);
        }
        else
        if (strcmp(action, L("Follow")) == 0) { /** **/
            xs *msg = msg_follow(&snac, actor);

            if (msg != NULL) {
                /* reload the actor from the message, in may be different */
                actor = xs_dict_get(msg, "object");

                following_add(&snac, actor, msg);

                enqueue_output_by_actor(&snac, msg, actor, 0);
            }
        }
        else
        if (strcmp(action, L("Unfollow")) == 0) { /** **/
            /* get the following object */
            xs *object = NULL;

            if (valid_status(following_get(&snac, actor, &object))) {
                xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                following_del(&snac, actor);

                enqueue_output_by_actor(&snac, msg, actor, 0);

                snac_log(&snac, xs_fmt("unfollowed actor %s", actor));
            }
            else
                snac_log(&snac, xs_fmt("actor is not being followed %s", actor));
        }
        else
        if (strcmp(action, L("Follow Group")) == 0) { /** **/
            xs *msg = msg_follow(&snac, group);

            if (msg != NULL) {
                /* reload the group from the message, in may be different */
                group = xs_dict_get(msg, "object");

                following_add(&snac, group, msg);

                enqueue_output_by_actor(&snac, msg, group, 0);
            }
        }
        else
        if (strcmp(action, L("Unfollow Group")) == 0) { /** **/
            /* get the following object */
            xs *object = NULL;

            if (valid_status(following_get(&snac, group, &object))) {
                xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                following_del(&snac, group);

                enqueue_output_by_actor(&snac, msg, group, 0);

                snac_log(&snac, xs_fmt("unfollowed group %s", group));
            }
            else
                snac_log(&snac, xs_fmt("actor is not being followed %s", actor));
        }
        else
        if (strcmp(action, L("Delete")) == 0) { /** **/
            const char *actor_form = xs_dict_get(p_vars, "actor-form");
            if (actor_form != NULL) {
                /* delete follower */
                if (valid_status(follower_del(&snac, actor)))
                    snac_log(&snac, xs_fmt("deleted follower %s", actor));
                else
                    snac_log(&snac, xs_fmt("error deleting follower %s", actor));
            }
            else {
                /* delete an entry */
                if (xs_startswith(id, snac.actor) && !is_draft(&snac, id)) {
                    /* it's a post by us: generate a delete */
                    xs *msg = msg_delete(&snac, id);

                    enqueue_message(&snac, msg);

                    snac_log(&snac, xs_fmt("posted tombstone for %s", id));
                }

                timeline_del(&snac, id);

                draft_del(&snac, id);

                snac_log(&snac, xs_fmt("deleted entry %s", id));
            }
        }
        else
        if (strcmp(action, L("Pin")) == 0) { /** **/
            pin(&snac, id);
            timeline_touch(&snac);
        }
        else
        if (strcmp(action, L("Unpin")) == 0) { /** **/
            unpin(&snac, id);
            timeline_touch(&snac);
        }
        else
        if (strcmp(action, L("Bookmark")) == 0) { /** **/
            bookmark(&snac, id);
            timeline_touch(&snac);
        }
        else
        if (strcmp(action, L("Unbookmark")) == 0) { /** **/
            unbookmark(&snac, id);
            timeline_touch(&snac);
        }
        else
            status = HTTP_STATUS_NOT_FOUND;

        /* delete the cached timeline */
        if (status == HTTP_STATUS_SEE_OTHER)
            history_del(&snac, "timeline.html_");
    }
    else
    if (p_path && strcmp(p_path, "admin/user-setup") == 0) { /** **/
        /* change of user data */
        const char *v;
        const char *p1, *p2;

        if ((v = xs_dict_get(p_vars, "name")) != NULL)
            snac.config = xs_dict_set(snac.config, "name", v);
        if ((v = xs_dict_get(p_vars, "avatar")) != NULL)
            snac.config = xs_dict_set(snac.config, "avatar", v);
        if ((v = xs_dict_get(p_vars, "bio")) != NULL)
            snac.config = xs_dict_set(snac.config, "bio", v);
        if ((v = xs_dict_get(p_vars, "cw")) != NULL &&
            strcmp(v, "on") == 0) {
            snac.config = xs_dict_set(snac.config, "cw", "open");
        } else { /* if the checkbox is not set, the parameter is missing */
            snac.config = xs_dict_set(snac.config, "cw", "");
        }
        if ((v = xs_dict_get(p_vars, "email")) != NULL)
            snac.config = xs_dict_set(snac.config, "email", v);
        if ((v = xs_dict_get(p_vars, "telegram_bot")) != NULL)
            snac.config = xs_dict_set(snac.config, "telegram_bot", v);
        if ((v = xs_dict_get(p_vars, "telegram_chat_id")) != NULL)
            snac.config = xs_dict_set(snac.config, "telegram_chat_id", v);
        if ((v = xs_dict_get(p_vars, "ntfy_server")) != NULL)
            snac.config = xs_dict_set(snac.config, "ntfy_server", v);
        if ((v = xs_dict_get(p_vars, "ntfy_token")) != NULL)
            snac.config = xs_dict_set(snac.config, "ntfy_token", v);
        if ((v = xs_dict_get(p_vars, "purge_days")) != NULL) {
            xs *days    = xs_number_new(atof(v));
            snac.config = xs_dict_set(snac.config, "purge_days", days);
        }
        if ((v = xs_dict_get(p_vars, "drop_dm_from_unknown")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "drop_dm_from_unknown", xs_stock(XSTYPE_TRUE));
        else
            snac.config = xs_dict_set(snac.config, "drop_dm_from_unknown", xs_stock(XSTYPE_FALSE));
        if ((v = xs_dict_get(p_vars, "bot")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "bot", xs_stock(XSTYPE_TRUE));
        else
            snac.config = xs_dict_set(snac.config, "bot", xs_stock(XSTYPE_FALSE));
        if ((v = xs_dict_get(p_vars, "private")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "private", xs_stock(XSTYPE_TRUE));
        else
            snac.config = xs_dict_set(snac.config, "private", xs_stock(XSTYPE_FALSE));
        if ((v = xs_dict_get(p_vars, "auto_boost")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "auto_boost", xs_stock(XSTYPE_TRUE));
        else
            snac.config = xs_dict_set(snac.config, "auto_boost", xs_stock(XSTYPE_FALSE));
        if ((v = xs_dict_get(p_vars, "collapse_threads")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "collapse_threads", xs_stock(XSTYPE_TRUE));
        else
            snac.config = xs_dict_set(snac.config, "collapse_threads", xs_stock(XSTYPE_FALSE));

        if ((v = xs_dict_get(p_vars, "metadata")) != NULL) {
            /* split the metadata and store it as a dict */
            xs_dict *md = xs_dict_new();
            xs *l = xs_split(v, "\n");
            xs_list *p = l;
            const xs_str *kp;

            while (xs_list_iter(&p, &kp)) {
                xs *kpl = xs_split_n(kp, "=", 1);
                if (xs_list_len(kpl) == 2) {
                    xs *k2 = xs_strip_i(xs_dup(xs_list_get(kpl, 0)));
                    xs *v2 = xs_strip_i(xs_dup(xs_list_get(kpl, 1)));

                    md = xs_dict_set(md, k2, v2);
                }
            }

            snac.config = xs_dict_set(snac.config, "metadata", md);
        }

        /* uploads */
        const char *uploads[] = { "avatar", "header", NULL };
        int n;
        for (n = 0; uploads[n]; n++) {
            xs *var_name = xs_fmt("%s_file", uploads[n]);

            const xs_list *uploaded_file = xs_dict_get(p_vars, var_name);
            if (xs_type(uploaded_file) == XSTYPE_LIST) {
                const char *fn = xs_list_get(uploaded_file, 0);

                if (fn && *fn) {
                    const char *mimetype = xs_mime_by_ext(fn);

                    if (xs_startswith(mimetype, "image/")) {
                        const char *ext = strrchr(fn, '.');
                        xs *hash        = xs_md5_hex(fn, strlen(fn));
                        xs *id          = xs_fmt("%s%s", hash, ext);
                        xs *url         = xs_fmt("%s/s/%s", snac.actor, id);
                        int fo          = xs_number_get(xs_list_get(uploaded_file, 1));
                        int fs          = xs_number_get(xs_list_get(uploaded_file, 2));

                        /* store */
                        static_put(&snac, id, payload + fo, fs);

                        snac.config = xs_dict_set(snac.config, uploads[n], url);
                    }
                }
            }
        }

        /* delete images by removing url from user.json */
        for (n = 0; uploads[n]; n++) {
            xs *var_name = xs_fmt("%s_delete", uploads[n]);
            const char *delete_var = xs_dict_get(p_vars, var_name);

            if (delete_var != NULL && strcmp(delete_var, "on") == 0) {
                    snac.config = xs_dict_set(snac.config, uploads[n], "");
            }
        }

        /* password change? */
        if ((p1 = xs_dict_get(p_vars, "passwd1")) != NULL &&
            (p2 = xs_dict_get(p_vars, "passwd2")) != NULL &&
            *p1 && strcmp(p1, p2) == 0) {
            xs *pw = hash_password(snac.uid, p1, NULL);
            snac.config = xs_dict_set(snac.config, "passwd", pw);
        }

        user_persist(&snac, 1);

        status = HTTP_STATUS_SEE_OTHER;
    }
    else
    if (p_path && strcmp(p_path, "admin/clear-notifications") == 0) { /** **/
        notify_clear(&snac);
        timeline_touch(&snac);

        status = HTTP_STATUS_SEE_OTHER;
    }
    else
    if (p_path && strcmp(p_path, "admin/vote") == 0) { /** **/
        const char *irt   = xs_dict_get(p_vars, "irt");
        const char *opt   = xs_dict_get(p_vars, "question");
        const char *actor = xs_dict_get(p_vars, "actor");

        xs *ls = NULL;

        /* multiple choices? */
        if (xs_type(opt) == XSTYPE_LIST)
            ls = xs_dup(opt);
        else
        if (xs_type(opt) == XSTYPE_STRING) {
            ls = xs_list_new();
            ls = xs_list_append(ls, opt);
        }

        const xs_str *v;
        int c = 0;

        while (xs_list_next(ls, &v, &c)) {
            xs *msg = msg_note(&snac, "", actor, irt, NULL, 1);

            /* set the option */
            msg = xs_dict_append(msg, "name", v);

            xs *c_msg = msg_create(&snac, msg);

            enqueue_message(&snac, c_msg);

            timeline_add(&snac, xs_dict_get(msg, "id"), msg);
        }

        if (ls != NULL) {
            /* get the poll object */
            xs *poll = NULL;

            if (valid_status(object_get(irt, &poll))) {
                const char *date = xs_dict_get(poll, "endTime");
                if (xs_is_null(date))
                    date = xs_dict_get(poll, "closed");

                if (!xs_is_null(date)) {
                    time_t t = xs_parse_iso_date(date, 0) - time(NULL);

                    /* request the poll when it's closed;
                       Pleroma does not send and update when the poll closes */
                    enqueue_object_request(&snac, irt, t + 2);
                }
            }
        }

        status = HTTP_STATUS_SEE_OTHER;
    }

    if (status == HTTP_STATUS_SEE_OTHER) {
        const char *redir = xs_dict_get(p_vars, "redir");

        if (xs_is_null(redir))
            redir = "top";

        *body   = xs_fmt("%s/admin#%s", snac.actor, redir);
        *b_size = strlen(*body);
    }

    user_free(&snac);

    return status;
}


xs_str *timeline_to_rss(snac *user, const xs_list *timeline,
                        const char *title, const char *link, const char *desc)
/* converts a timeline to rss */
{
    xs_html *rss = xs_html_tag("rss",
        xs_html_attr("xmlns:content", "http:/" "/purl.org/rss/1.0/modules/content/"),
        xs_html_attr("version",       "2.0"),
        xs_html_attr("xmlns:atom",    "http:/" "/www.w3.org/2005/Atom"));

    xs_html *channel = xs_html_tag("channel",
        xs_html_tag("title",
            xs_html_text(title)),
        xs_html_tag("language",
            xs_html_text("en")),
        xs_html_tag("link",
            xs_html_text(link)),
        xs_html_sctag("atom:link",
            xs_html_attr("href", link),
            xs_html_attr("rel", "self"),
            xs_html_attr("type", "application/rss+xml")),
        xs_html_tag("generator",
            xs_html_text(USER_AGENT)),
        xs_html_tag("description",
            xs_html_text(desc)));

    xs_html_add(rss, channel);

    int cnt = 0;
    const char *v;

    xs_list_foreach(timeline, v) {
        xs *msg = NULL;

        if (user) {
            if (!valid_status(timeline_get_by_md5(user, v, &msg)))
                continue;
        }
        else {
            if (!valid_status(object_get_by_md5(v, &msg)))
                continue;
        }

        const char *id = xs_dict_get(msg, "id");
        const char *content = xs_dict_get(msg, "content");
        const char *published = xs_dict_get(msg, "published");

        if (user && !xs_startswith(id, user->actor))
            continue;

        if (!id || !content || !published)
            continue;

        /* create a title with the first line of the content */
        xs *title = xs_replace(content, "<br>", "\n");
        title = xs_regex_replace_i(title, "<[^>]+>", " ");
        title = xs_regex_replace_i(title, "&[^;]+;", " ");
        int i;

        for (i = 0; title[i] && title[i] != '\n' && i < 50; i++);

        if (title[i] != '\0') {
            title[i] = '\0';
            title = xs_str_cat(title, "...");
        }

        title = xs_strip_i(title);

        /* convert the date */
        time_t t = xs_parse_iso_date(published, 0);
        xs *rss_date = xs_str_utctime(t, "%a, %d %b %Y %T +0000");

        /* if it's the first one, add it to the header */
        if (cnt == 0)
            xs_html_add(channel,
                xs_html_tag("lastBuildDate",
                    xs_html_text(rss_date)));

        xs_html_add(channel,
            xs_html_tag("item",
                xs_html_tag("title",
                    xs_html_text(title)),
                xs_html_tag("link",
                    xs_html_text(id)),
                xs_html_tag("guid",
                    xs_html_text(id)),
                xs_html_tag("pubDate",
                    xs_html_text(rss_date)),
                xs_html_tag("description",
                    xs_html_text(content))));

        cnt++;
    }

    return xs_html_render_s(rss, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
}
