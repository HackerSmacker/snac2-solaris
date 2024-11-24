/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_time.h"
#include "xs_openssl.h"
#include "xs_random.h"
#include "xs_glob.h"
#include "xs_curl.h"
#include "xs_regex.h"

#include "snac.h"

#include <sys/stat.h>
#include <stdlib.h>

static const char *default_srv_config = "{"
    "\"host\":                 \"\","
    "\"prefix\":               \"\","
    "\"address\":              \"127.0.0.1\","
    "\"port\":                 8001,"
    "\"layout\":               0.0,"
    "\"dbglevel\":             0,"
    "\"queue_retry_minutes\":  2,"
    "\"queue_retry_max\":      10,"
    "\"queue_timeout\":        6,"
    "\"queue_timeout_2\":      8,"
    "\"cssurls\":              [\"\"],"
    "\"max_timeline_entries\": 50,"
    "\"timeline_purge_days\":  120,"
    "\"local_purge_days\":     0,"
    "\"min_account_age\":      0,"
    "\"admin_email\":          \"\","
    "\"admin_account\":        \"\","
    "\"title\":                \"\","
    "\"short_description\":    \"\","
    "\"protocol\":             \"https\","
    "\"fastcgi\":              false"
    "}";

static const char *default_css =
    "body { max-width: 48em; margin: auto; line-height: 1.5; padding: 0.8em; word-wrap: break-word; }\n"
    "pre { overflow-x: scroll; }\n"
    ".snac-embedded-video, img { max-width: 100% }\n"
    ".snac-origin { font-size: 85% }\n"
    ".snac-score { float: right; font-size: 85% }\n"
    ".snac-top-user { text-align: center; padding-bottom: 2em }\n"
    ".snac-top-user-name { font-size: 200% }\n"
    ".snac-top-user-id { font-size: 150% }\n"
    ".snac-announcement { border: black 1px solid; padding: 0.5em }\n"
    ".snac-avatar { float: left; height: 2.5em; width: 2.5em; padding: 0.25em }\n"
    ".snac-author { font-size: 90%; text-decoration: none }\n"
    ".snac-author-tag { font-size: 80% }\n"
    ".snac-pubdate { color: #a0a0a0; font-size: 90% }\n"
    ".snac-top-controls { padding-bottom: 1.5em }\n"
    ".snac-post { border-top: 1px solid #a0a0a0; padding-top: 0.5em; padding-bottom: 0.5em; }\n"
    ".snac-children { padding-left: 1em; border-left: 1px solid #a0a0a0; }\n"
    ".snac-textarea { font-family: inherit; width: 100% }\n"
    ".snac-history { border: 1px solid #606060; border-radius: 3px; margin: 2.5em 0; padding: 0 2em }\n"
    ".snac-btn-mute { float: right; margin-left: 0.5em }\n"
    ".snac-btn-unmute { float: right; margin-left: 0.5em }\n"
    ".snac-btn-follow { float: right; margin-left: 0.5em }\n"
    ".snac-btn-unfollow { float: right; margin-left: 0.5em }\n"
    ".snac-btn-hide { float: right; margin-left: 0.5em }\n"
    ".snac-btn-delete { float: right; margin-left: 0.5em }\n"
    ".snac-btn-limit { float: right; margin-left: 0.5em }\n"
    ".snac-btn-unlimit { float: right; margin-left: 0.5em }\n"
    ".snac-footer { margin-top: 2em; font-size: 75% }\n"
    ".snac-poll-result { margin-left: auto; margin-right: auto; }\n"
    ".snac-list-of-lists { padding-left: 0; }\n"
    ".snac-list-of-lists li { display: inline; border: 1px solid #a0a0a0; border-radius: 25px;\n"
    "  margin-right: 0.5em; padding-left: 0.5em; padding-right: 0.5em; }\n"
    "@media (prefers-color-scheme: dark) { \n"
    "  body, input, textarea { background-color: #000; color: #fff; }\n"
    "  a { color: #7799dd }\n"
    "  a:visited { color: #aa99dd }\n"
    "}\n"
;

const char *snac_blurb =
    "<p><b>%host%</b> is a <a href=\"https:/"
    "/en.wikipedia.org/wiki/Fediverse\">Fediverse</a> "
    "instance that uses the <a href=\"https:/"
    "/en.wikipedia.org/wiki/ActivityPub\">ActivityPub</a> "
    "protocol. In other words, users at this host can communicate with people "
    "that use software like Mastodon, Pleroma, Friendica, etc. "
    "all around the world.</p>\n"
    "<p>This server runs the "
    "<a href=\"" WHAT_IS_SNAC_URL "\">snac</a> software and there is no "
    "automatic sign-up process.</p>\n"
;

static const char *greeting_html =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"
    "<link rel=\"icon\" type=\"image/x-icon\" href=\"https://%host%/favicon.ico\"/>\n"
    "<title>Welcome to %host%</title>\n"
    "<body style=\"margin: auto; max-width: 50em\">\n"
    "%blurb%"
    "<p>The following users are part of this community:</p>\n"
    "\n"
    "%userlist%\n"
    "\n"
    "<p>This site is powered by <abbr title=\"Social Networks Are Crap\">snac</abbr>.</p>\n"
    "</body></html>\n";


int write_default_css(void)
{
    FILE *f;

    xs *sfn = xs_fmt("%s/style.css", srv_basedir);
    if ((f = fopen(sfn, "w")) == NULL)
        return 1;

    fwrite(default_css, strlen(default_css), 1, f);
    fclose(f);

    return 0;
}


int snac_init(const char *basedir)
{
    FILE *f;

    if (basedir == NULL) {
        printf("Base directory: "); fflush(stdout);
        srv_basedir = xs_strip_i(xs_readline(stdin));
    }
    else
        srv_basedir = xs_str_new(basedir);

    if (srv_basedir == NULL || *srv_basedir == '\0')
        return 1;

    if (xs_endswith(srv_basedir, "/"))
        srv_basedir = xs_crop_i(srv_basedir, 0, -1);

    if (mtime(srv_basedir) != 0.0) {
        printf("ERROR: directory '%s' must not exist.\n", srv_basedir);
        return 1;
    }

    srv_config = xs_json_loads(default_srv_config);

    xs *layout = xs_number_new(disk_layout);
    srv_config = xs_dict_set(srv_config, "layout", layout);

    int is_unix_socket = 0;

    printf("Network address or full path to unix socket [%s]: ", xs_dict_get(srv_config, "address")); fflush(stdout);
    {
        xs *i = xs_strip_i(xs_readline(stdin));
        if (*i) {
            srv_config = xs_dict_set(srv_config, "address", i);

            if (*i == '/')
                is_unix_socket = 1;
        }
    }

    if (!is_unix_socket) {
        printf("Network port [%d]: ", (int)xs_number_get(xs_dict_get(srv_config, "port"))); fflush(stdout);
        {
            xs *i = xs_strip_i(xs_readline(stdin));
            if (*i) {
                xs *n = xs_number_new(atoi(i));
                srv_config = xs_dict_set(srv_config, "port", n);
            }
        }
    }
    else {
        xs *n = xs_number_new(0);
        srv_config = xs_dict_set(srv_config, "port", n);
    }

    printf("Host name: "); fflush(stdout);
    {
        xs *i = xs_strip_i(xs_readline(stdin));
        if (*i == '\0')
            return 1;

        srv_config = xs_dict_set(srv_config, "host", i);
    }

    printf("URL prefix: "); fflush(stdout);
    {
        xs *i = xs_strip_i(xs_readline(stdin));

        if (*i) {
            if (xs_endswith(i, "/"))
                i = xs_crop_i(i, 0, -1);

            srv_config = xs_dict_set(srv_config, "prefix", i);
        }
    }

    printf("Admin email address (optional): "); fflush(stdout);
    {
        xs *i = xs_strip_i(xs_readline(stdin));

        srv_config = xs_dict_set(srv_config, "admin_email", i);
    }

    if (mkdirx(srv_basedir) == -1) {
        printf("ERROR: cannot create directory '%s'\n", srv_basedir);
        return 1;
    }

    xs *udir = xs_fmt("%s/user", srv_basedir);
    mkdirx(udir);

    xs *odir = xs_fmt("%s/object", srv_basedir);
    mkdirx(odir);

    xs *qdir = xs_fmt("%s/queue", srv_basedir);
    mkdirx(qdir);

    xs *ibdir = xs_fmt("%s/inbox", srv_basedir);
    mkdirx(ibdir);

    xs *gfn = xs_fmt("%s/greeting.html", srv_basedir);
    if ((f = fopen(gfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", gfn);
        return 1;
    }

    xs *gh = xs_replace(greeting_html, "%blurb%", snac_blurb);
    fwrite(gh, strlen(gh), 1, f);
    fclose(f);

    if (write_default_css()) {
        printf("ERROR: cannot create style.css\n");
        return 1;
    }

    xs *cfn = xs_fmt("%s/server.json", srv_basedir);
    if ((f = fopen(cfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", cfn);
        return 1;
    }

    xs_json_dump(srv_config, 4, f);
    fclose(f);

    printf("Done.\n");
    return 0;
}


void new_password(const char *uid, xs_str **clear_pwd, xs_str **hashed_pwd)
/* creates a random password */
{
    int rndbuf[3];

    xs_rnd_buf(rndbuf, sizeof(rndbuf));

    *clear_pwd  = xs_base64_enc((char *)rndbuf, sizeof(rndbuf));
    *hashed_pwd = hash_password(uid, *clear_pwd, NULL);
}


int adduser(const char *uid)
/* creates a new user */
{
    snac snac;
    xs *config = xs_dict_new();
    xs *date = xs_str_utctime(0, ISO_DATE_SPEC);
    xs *pwd = NULL;
    xs *pwd_f = NULL;
    xs *key = NULL;
    FILE *f;

    if (uid == NULL) {
        printf("Username: "); fflush(stdout);
        uid = xs_strip_i(xs_readline(stdin));
    }

    if (!validate_uid(uid)) {
        printf("ERROR: only alphanumeric characters and _ are allowed in user ids.\n");
        return 1;
    }

    if (user_open(&snac, uid)) {
        printf("ERROR: user '%s' already exists\n", snac.uid);
        return 1;
    }

    new_password(uid, &pwd, &pwd_f);

    config = xs_dict_append(config, "uid",       uid);
    config = xs_dict_append(config, "name",      uid);
    config = xs_dict_append(config, "avatar",    "");
    config = xs_dict_append(config, "bio",       "");
    config = xs_dict_append(config, "cw",        "");
    config = xs_dict_append(config, "published", date);
    config = xs_dict_append(config, "passwd",    pwd_f);

    xs *basedir = xs_fmt("%s/user/%s", srv_basedir, uid);

    if (mkdirx(basedir) == -1) {
        printf("ERROR: cannot create directory '%s'\n", basedir);
        return 0;
    }

    const char *dirs[] = {
        "followers", "following", "muted", "hidden",
        "public", "private", "queue", "history",
        "static", NULL };
    int n;

    for (n = 0; dirs[n]; n++) {
        xs *d = xs_fmt("%s/%s", basedir, dirs[n]);
        mkdirx(d);
    }

    xs *cfn = xs_fmt("%s/user.json", basedir);

    if ((f = fopen(cfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", cfn);
        return 1;
    }
    else {
        xs_json_dump(config, 4, f);
        fclose(f);
    }

    printf("\nCreating RSA key...\n");
    key = xs_evp_genkey(4096);
    printf("Done.\n");

    xs *kfn = xs_fmt("%s/key.json", basedir);

    if ((f = fopen(kfn, "w")) == NULL) {
        printf("ERROR: cannot create '%s'\n", kfn);
        return 1;
    }
    else {
        xs_json_dump(key, 4, f);
        fclose(f);
    }

    printf("\nUser password is %s\n", pwd);

    printf("\nGo to %s/%s and continue configuring your user there.\n", srv_baseurl, uid);

    return 0;
}


int resetpwd(snac *snac)
/* creates a new password for the user */
{
    xs *clear_pwd  = NULL;
    xs *hashed_pwd = NULL;
    xs *fn         = xs_fmt("%s/user.json", snac->basedir);
    FILE *f;
    int ret = 0;

    new_password(snac->uid, &clear_pwd, &hashed_pwd);

    snac->config = xs_dict_set(snac->config, "passwd", hashed_pwd);

    if ((f = fopen(fn, "w")) != NULL) {
        xs_json_dump(snac->config, 4, f);
        fclose(f);

        printf("New password for user %s is %s\n", snac->uid, clear_pwd);
    }
    else {
        printf("ERROR: cannot write to %s\n", fn);
        ret = 1;
    }

    return ret;
}


void rm_rf(const char *dir)
/* does an rm -rf (yes, I'm also scared) */
{
    xs *d = xs_str_cat(xs_dup(dir), "/" "*");
    xs *l = xs_glob(d, 0, 0);
    xs_list *p = l;
    const xs_str *v;

    if (dbglevel >= 1)
        printf("Deleting directory %s\n", dir);

    while (xs_list_iter(&p, &v)) {
        struct stat st;

        if (stat(v, &st) != -1) {
            if (st.st_mode & S_IFDIR) {
                rm_rf(v);
            }
            else {
                if (dbglevel >= 1)
                    printf("Deleting file %s\n", v);

                if (unlink(v) == -1)
                    printf("ERROR: cannot delete file %s\n", v);
            }
        }
        else
            printf("ERROR: stat() fail for %s\n", v);
    }

    if (rmdir(dir) == -1)
        printf("ERROR: cannot delete directory %s\n", dir);
}


int deluser(snac *user)
/* deletes a user */
{
    int ret = 0;
    xs *fwers = following_list(user);
    xs_list *p = fwers;
    const xs_str *v;

    while (xs_list_iter(&p, &v)) {
        xs *object = NULL;

        if (valid_status(following_get(user, v, &object))) {
            xs *msg = msg_undo(user, xs_dict_get(object, "object"));

            following_del(user, v);

            enqueue_output_by_actor(user, msg, v, 0);

            printf("Unfollowing actor %s\n", v);
        }
    }

    rm_rf(user->basedir);

    return ret;
}


void verify_links(snac *user)
/* verifies a user's links */
{
    const xs_dict *p = xs_dict_get(user->config, "metadata");
    const char *k, *v;
    int changed = 0;

    xs *headers = xs_dict_new();
    headers = xs_dict_append(headers, "accept", "text/html");
    headers = xs_dict_append(headers, "user-agent", USER_AGENT " (link verify)");

    int c = 0;
    while (p && xs_dict_next(p, &k, &v, &c)) {
        /* not an https link? skip */
        if (!xs_startswith(v, "https:/" "/"))
            continue;

        int status;
        xs *req = NULL;
        xs *payload = NULL;
        int p_size = 0;

        req = xs_http_request("GET", v, headers, NULL, 0, &status,
            &payload, &p_size, 0);

        if (!valid_status(status)) {
            snac_log(user, xs_fmt("link %s verify error %d", v, status));
            continue;
        }

        /* extract the links */
        xs *ls = xs_regex_select(payload, "< *(a|link) +[^>]+>");

        xs_list *lp = ls;
        const char *ll;
        int vfied = 0;

        while (!vfied && xs_list_iter(&lp, &ll)) {
            /* extract href and rel */
            xs *r = xs_regex_select(ll, "(href|rel) *= *(\"[^\"]*\"|'[^']*')");

            /* must have both attributes */
            if (xs_list_len(r) != 2)
                continue;

            xs *href = NULL;
            int is_rel_me = 0;
            xs_list *pr = r;
            const char *ar;

            while (xs_list_iter(&pr, &ar)) {
                xs *nq = xs_dup(ar);

                nq = xs_replace_i(nq, "\"", "");
                nq = xs_replace_i(nq, "'", "");

                xs *r2 = xs_split_n(nq, "=", 1);
                if (xs_list_len(r2) != 2)
                    continue;

                xs *ak = xs_strip_i(xs_dup(xs_list_get(r2, 0)));
                xs *av = xs_strip_i(xs_dup(xs_list_get(r2, 1)));

                if (strcmp(ak, "href") == 0)
                    href = xs_dup(av);
                else
                if (strcmp(ak, "rel") == 0) {
                    /* split the value by spaces */
                    xs *vbs = xs_split(av, " ");

                    /* is any of it "me"? */
                    if (xs_list_in(vbs, "me") != -1)
                        is_rel_me = 1;
                }
            }

            /* after all this acrobatics, do we have an href and a rel="me"? */
            if (href != NULL && is_rel_me) {
                /* is it the same as the actor? */
                if (strcmp(href, user->actor) == 0) {
                    /* got it! */
                    xs *verified_time = xs_number_new((double)time(NULL));

                    if (user->links == NULL)
                        user->links = xs_dict_new();

                    user->links = xs_dict_set(user->links, v, verified_time);

                    vfied = 1;
                }
                else
                    snac_debug(user, 1,
                        xs_fmt("verify link %s rel='me' found but not related (%s)", v, href));
            }
        }

        if (vfied) {
            changed++;
            snac_log(user, xs_fmt("link %s verified", v));
        }
        else {
            snac_log(user, xs_fmt("link %s not verified (rel='me' not found)", v));
        }
    }

    if (changed) {
        FILE *f;

        /* update the links.json file */
        xs *fn = xs_fmt("%s/links.json", user->basedir);
        xs *bfn = xs_fmt("%s.bak", fn);

        rename(fn, bfn);

        if ((f = fopen(fn, "w")) != NULL) {
            xs_json_dump(user->links, 4, f);
            fclose(f);
        }
        else
            rename(bfn, fn);
    }
}


void export_csv(snac *user)
/* exports user data to current directory in a way that pleases Mastodon */
{
    FILE *f;
    const char *fn;

    fn = "bookmarks.csv";
    if ((f = fopen(fn, "w")) != NULL) {
        snac_log(user, xs_fmt("Creating %s...", fn));

        xs *l = bookmark_list(user);
        const char *md5;

        xs_list_foreach(l, md5) {
            xs *post = NULL;

            if (valid_status(object_get_by_md5(md5, &post))) {
                const char *id = xs_dict_get(post, "id");

                if (xs_type(id) == XSTYPE_STRING)
                    fprintf(f, "%s\n", id);
            }
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot create file %s", fn));

    fn = "blocked_accounts.csv";
    if ((f = fopen(fn, "w")) != NULL) {
        snac_log(user, xs_fmt("Creating %s...", fn));

        xs *l = muted_list(user);
        const char *actor;

        xs_list_foreach(l, actor) {
            xs *uid = NULL;

            webfinger_request_fake(actor, NULL, &uid);
            fprintf(f, "%s\n", uid);
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot create file %s", fn));

    fn = "lists.csv";
    if ((f = fopen(fn, "w")) != NULL) {
        snac_log(user, xs_fmt("Creating %s...", fn));

        xs *lol = list_maint(user, NULL, 0);
        const xs_list *li;

        xs_list_foreach(lol, li) {
            const char *lid = xs_list_get(li, 0);
            const char *ltitle = xs_list_get(li, 1);

            xs *actors = list_content(user, lid, NULL, 0);
            const char *md5;

            xs_list_foreach(actors, md5) {
                xs *actor = NULL;

                if (valid_status(object_get_by_md5(md5, &actor))) {
                    const char *id = xs_dict_get(actor, "id");
                    xs *uid = NULL;

                    webfinger_request_fake(id, NULL, &uid);
                    fprintf(f, "%s,%s\n", ltitle, uid);
                }
            }
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot create file %s", fn));

    fn = "following_accounts.csv";
    if ((f = fopen(fn, "w")) != NULL) {
        snac_log(user, xs_fmt("Creating %s...", fn));

        fprintf(f, "Account address,Show boosts,Notify on new posts,Languages\n");

        xs *fwing = following_list(user);
        const char *actor;

        xs_list_foreach(fwing, actor) {
            xs *uid = NULL;

            webfinger_request_fake(actor, NULL, &uid);
            fprintf(f, "%s,%s,false,\n", uid, limited(user, actor, 0) ? "false" : "true");
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot create file %s", fn));
}


void import_csv(snac *user)
/* import CSV files from Mastodon */
{
    FILE *f;
    const char *fn;

    fn = "blocked_accounts.csv";
    if ((f = fopen(fn, "r")) != NULL) {
        snac_log(user, xs_fmt("Importing from %s...", fn));

        while (!feof(f)) {
            xs *l = xs_strip_i(xs_readline(f));

            if (*l) {
                xs *url = NULL;
                xs *uid = NULL;

                if (valid_status(webfinger_request(l, &url, &uid))) {
                    if (is_muted(user, url))
                        snac_log(user, xs_fmt("Actor %s already MUTEd", url));
                    else {
                        mute(user, url);
                        snac_log(user, xs_fmt("MUTEd actor %s", url));
                    }
                }
                else
                    snac_log(user, xs_fmt("Webfinger error for account %s", l));
            }
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot open file %s", fn));

    fn = "following_accounts.csv";
    if ((f = fopen(fn, "r")) != NULL) {
        snac_log(user, xs_fmt("Importing from %s...", fn));

        while (!feof(f)) {
            xs *l = xs_strip_i(xs_readline(f));

            if (*l) {
                xs *l2 = xs_split(l, ",");
                const char *acct = xs_list_get(l2, 0);
                const char *show = xs_list_get(l2, 1);

                if (acct) {
                    /* not a valid account? skip (probably the CSV header) */
                    if (strchr(acct, '@') == NULL)
                        continue;

                    xs *msg = msg_follow(user, acct);

                    if (msg == NULL) {
                        snac_log(user, xs_fmt("Cannot follow %s -- server down?", acct));
                        continue;
                    }

                    const char *actor = xs_dict_get(msg, "object");

                    if (following_check(user, actor))
                        snac_log(user, xs_fmt("Actor %s already followed", actor));
                    else {
                        following_add(user, actor, msg);

                        enqueue_output_by_actor(user, msg, actor, 0);

                        snac_log(user, xs_fmt("Following %s", actor));
                    }

                    if (show && strcmp(show, "false") == 0) {
                        limit(user, actor);
                        snac_log(user, xs_fmt("Limiting boosts from actor %s", actor));
                    }
                    else {
                        unlimit(user, actor);
                        snac_log(user, xs_fmt("Unlimiting boosts from actor %s", actor));
                    }
                }
            }
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot open file %s", fn));

    fn = "lists.csv";
    if ((f = fopen(fn, "r")) != NULL) {
        snac_log(user, xs_fmt("Importing from %s...", fn));

        while (!feof(f)) {
            xs *l = xs_strip_i(xs_readline(f));

            if (*l) {
                xs *l2 = xs_split(l, ",");
                const char *lname = xs_list_get(l2, 0);
                const char *acct  = xs_list_get(l2, 1);

                if (lname && acct) {
                    /* create the list */
                    xs *list_id = list_maint(user, lname, 1);

                    xs *url = NULL;
                    xs *uid = NULL;

                    if (valid_status(webfinger_request(acct, &url, &uid))) {
                        xs *actor_md5 = xs_md5_hex(url, strlen(url));

                        list_content(user, list_id, actor_md5, 1);
                        snac_log(user, xs_fmt("Added %s to list %s", url, lname));
                    }
                    else
                        snac_log(user, xs_fmt("Webfinger error while adding %s to list %s", acct, lname));
                }
            }
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot open file %s", fn));

    fn = "bookmarks.csv";
    if ((f = fopen(fn, "r")) != NULL) {
        snac_log(user, xs_fmt("Importing from %s...", fn));

        while (!feof(f)) {
            xs *l = xs_strip_i(xs_readline(f));

            if (*l) {
                xs *post = NULL;

                if (!object_get(l, &post)) {
                    if (!valid_status(activitypub_request(user, l, &post))) {
                        snac_log(user, xs_fmt("Error getting object %s for bookmarking", l));
                        continue;
                    }
                }

                if (post == NULL)
                    continue;

                /* request the actor that created the post */
                const char *actor = get_atto(post);

                if (xs_type(actor) == XSTYPE_STRING)
                    actor_request(user, actor, NULL);

                object_add_ow(l, post);
                timeline_add(user, l, post);

                bookmark(user, l);

                snac_log(user, xs_fmt("Bookmarked %s", l));
            }
        }

        fclose(f);
    }
    else
        snac_log(user, xs_fmt("Cannot open file %s", fn));
}
