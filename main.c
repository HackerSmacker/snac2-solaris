/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_time.h"
#include "xs_openssl.h"

#include "snac.h"

#include <sys/stat.h>

int usage(void)
{
    printf("snac " VERSION " - A simple, minimalistic ActivityPub instance\n");
    printf("Copyright (c) 2022 - 2024 grunfink et al. / MIT license\n");
    printf("\n");
    printf("Commands:\n");
    printf("\n");
    printf("init [{basedir}]                     Initializes the data storage\n");
    printf("upgrade {basedir}                    Upgrade to a new version\n");
    printf("adduser {basedir} [{uid}]            Adds a new user\n");
    printf("deluser {basedir} {uid}              Deletes a user\n");
    printf("httpd {basedir}                      Starts the HTTPD daemon\n");
    printf("purge {basedir}                      Purges old data\n");
    printf("state {basedir}                      Prints server state\n");
    printf("webfinger {basedir} {account}        Queries about an account (@user@host or actor url)\n");
    printf("queue {basedir} {uid}                Processes a user queue\n");
    printf("follow {basedir} {uid} {actor}       Follows an actor\n");
    printf("unfollow {basedir} {uid} {actor}     Unfollows an actor\n");
    printf("request {basedir} {uid} {url}        Requests an object\n");
    printf("insert {basedir} {uid} {url}         Requests an object and inserts it into the timeline\n");
    printf("actor {basedir} [{uid}] {url}        Requests an actor\n");
    printf("note {basedir} {uid} {text} [files...] Sends a note with optional attachments\n");
    printf("boost|announce {basedir} {uid} {url} Boosts (announces) a post\n");
    printf("unboost {basedir} {uid} {url}        Unboosts a post\n");
    printf("resetpwd {basedir} {uid}             Resets the password of a user\n");
    printf("ping {basedir} {uid} {actor}         Pings an actor\n");
    printf("webfinger_s {basedir} {uid} {account} Queries about an account (@user@host or actor url)\n");
    printf("pin {basedir} {uid} {msg_url}        Pins a message\n");
    printf("unpin {basedir} {uid} {msg_url}      Unpins a message\n");
    printf("bookmark {basedir} {uid} {msg_url}   Bookmarks a message\n");
    printf("unbookmark {basedir} {uid} {msg_url} Unbookmarks a message\n");
    printf("block {basedir} {instance_url}       Blocks a full instance\n");
    printf("unblock {basedir} {instance_url}     Unblocks a full instance\n");
    printf("limit {basedir} {uid} {actor}        Limits an actor (drops their announces)\n");
    printf("unlimit {basedir} {uid} {actor}      Unlimits an actor\n");
    printf("verify_links {basedir} {uid}         Verifies a user's links (in the metadata)\n");
    printf("search {basedir} {uid} {regex}       Searches posts by content\n");
    printf("export_csv {basedir} {uid}           Exports data as CSV files into current directory\n");
    printf("alias {basedir} {uid} {account}      Sets account (@user@host or actor url) as an alias\n");
    printf("migrate {basedir} {uid}              Migrates to the account defined as the alias\n");
    printf("import_csv {basedir} {uid}           Imports data from CSV files into current directory\n");

    return 1;
}


char *get_argv(int *argi, int argc, char *argv[])
{
    if (*argi < argc)
        return argv[(*argi)++];
    else
        return NULL;
}


#define GET_ARGV() get_argv(&argi, argc, argv)

int main(int argc, char *argv[])
{
    char *cmd;
    char *basedir;
    char *user;
    char *url;
    int argi = 1;
    snac snac;

    /* ensure group has write access */
    umask(0007);

    if ((cmd = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "init") == 0) { /** **/
        /* initialize the data storage */
        /* ... */
        basedir = GET_ARGV();

        return snac_init(basedir);
    }

    if (strcmp(cmd, "upgrade") == 0) { /** **/
        int ret;

        /* upgrade */
        if ((basedir = GET_ARGV()) == NULL)
            return usage();

        if ((ret = srv_open(basedir, 1)) == 1)
            srv_log(xs_dup("OK"));

        return ret;
    }

    if (strcmp(cmd, "markdown") == 0) { /** **/
        /* undocumented, for testing only */
        xs *c = xs_readall(stdin);
        xs *fc = not_really_markdown(c, NULL, NULL);

        printf("<html>\n%s\n</html>\n", fc);
        return 0;
    }

    if ((basedir = GET_ARGV()) == NULL)
        return usage();

    if (!srv_open(basedir, 0)) {
        srv_log(xs_fmt("error opening data storage at %s", basedir));
        return 1;
    }

    if (strcmp(cmd, "adduser") == 0) { /** **/
        user = GET_ARGV();

        return adduser(user);

        return 0;
    }

    if (strcmp(cmd, "httpd") == 0) { /** **/
        httpd();
        srv_free();
        return 0;
    }

    if (strcmp(cmd, "purge") == 0) { /** **/
        purge_all();
        return 0;
    }

    if (strcmp(cmd, "state") == 0) { /** **/
        xs *shm_name = NULL;
        srv_state *p_state = srv_state_op(&shm_name, 1);

        if (p_state == NULL)
            return 1;

        srv_state ss = *p_state;
        int n;

        printf("server: %s (%s)\n", xs_dict_get(srv_config, "host"), USER_AGENT);
        xs *uptime = xs_str_time_diff(time(NULL) - ss.srv_start_time);
        printf("uptime: %s\n", uptime);
        printf("job fifo size (cur): %d\n", ss.job_fifo_size);
        printf("job fifo size (peak): %d\n", ss.peak_job_fifo_size);
        char *th_states[] = { "stopped", "waiting", "input", "output" };

        for (n = 0; n < ss.n_threads; n++)
            printf("thread #%d state: %s\n", n, th_states[ss.th_state[n]]);

        return 0;
    }

    if ((user = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "block") == 0) { /** **/
        int ret = instance_block(user);

        if (ret < 0) {
            fprintf(stderr, "Error blocking instance %s: %d\n", user, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "unblock") == 0) { /** **/
        int ret = instance_unblock(user);

        if (ret < 0) {
            fprintf(stderr, "Error unblocking instance %s: %d\n", user, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "webfinger") == 0) { /** **/
        xs *actor = NULL;
        xs *uid = NULL;
        int status;

        status = webfinger_request(user, &actor, &uid);

        printf("status: %d\n", status);
        if (actor != NULL)
            printf("actor: %s\n", actor);
        if (uid != NULL)
            printf("uid: %s\n", uid);

        return 0;
    }

    if (argi == argc && strcmp(cmd, "actor") == 0) { /** **/
        /* query an actor without user (non-signed) */
        xs *actor = NULL;
        int status;

        status = actor_request(NULL, user, &actor);

        printf("status: %d\n", status);
        if (valid_status(status)) {
            xs_json_dump(actor, 4, stdout);
            printf("\n");
        }

        return 0;
    }

    if (!user_open(&snac, user)) {
        printf("invalid user '%s'\n", user);
        return 1;
    }

    lastlog_write(&snac, "cmdline");

    if (strcmp(cmd, "resetpwd") == 0) { /** **/
        return resetpwd(&snac);
    }

    if (strcmp(cmd, "deluser") == 0) { /** **/
        return deluser(&snac);
    }

    if (strcmp(cmd, "update") == 0) { /** **/
        xs *a_msg = msg_actor(&snac);
        xs *u_msg = msg_update(&snac, a_msg);
        enqueue_message(&snac, u_msg);
        return 0;
    }

    if (strcmp(cmd, "queue") == 0) { /** **/
        process_user_queue(&snac);
        return 0;
    }

    if (strcmp(cmd, "verify_links") == 0) { /** **/
        verify_links(&snac);
        return 0;
    }

    if (strcmp(cmd, "timeline") == 0) { /** **/
#if 0
        xs *list = local_list(&snac, XS_ALL);
        xs *body = html_timeline(&snac, list, 1);

        printf("%s\n", body);
        user_free(&snac);
        srv_free();
#endif

        xs *idx  = xs_fmt("%s/private.idx", snac.basedir);
        xs *list = index_list_desc(idx, 0, 256);
        xs *tl   = timeline_top_level(&snac, list);

        xs_json_dump(tl, 4, stdout);

        return 0;
    }

    if (strcmp(cmd, "export_csv") == 0) { /** **/
        export_csv(&snac);
        return 0;
    }

    if (strcmp(cmd, "import_csv") == 0) { /** **/
        import_csv(&snac);
        return 0;
    }

    if (strcmp(cmd, "migrate") == 0) { /** **/
        return migrate_account(&snac);
    }

    if ((url = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "alias") == 0) { /** **/
        xs *actor = NULL;
        xs *uid = NULL;
        int status = HTTP_STATUS_OK;

        if (*url == '\0')
            actor = xs_dup("");
        else
            status = webfinger_request(url, &actor, &uid);

        if (valid_status(status)) {
            if (strcmp(actor, snac.actor) == 0) {
                snac_log(&snac, xs_fmt("You can't be your own alias"));
                return 1;
            }
            else {
                snac.config = xs_dict_set(snac.config, "alias", actor);
                snac.config = xs_dict_set(snac.config, "alias_raw", url);

                user_persist(&snac, 1);
            }
        }
        else {
            snac_log(&snac, xs_fmt("Webfinger error for %s %d", url, status));
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "webfinger_s") == 0) { /** **/
        xs *actor = NULL;
        xs *uid = NULL;
        int status;

        status = webfinger_request_signed(&snac, url, &actor, &uid);

        printf("status: %d\n", status);
        if (actor != NULL)
            printf("actor: %s\n", actor);
        if (uid != NULL)
            printf("uid: %s\n", uid);

        return 0;
    }

    if (strcmp(cmd, "boost") == 0 || strcmp(cmd, "announce") == 0) { /** **/
        xs *msg = msg_admiration(&snac, url, "Announce");

        if (msg != NULL) {
            enqueue_message(&snac, msg);

            if (dbglevel) {
                xs_json_dump(msg, 4, stdout);
            }
        }

        return 0;
    }

    if (strcmp(cmd, "unboost") == 0) { /** **/
        xs *msg = msg_repulsion(&snac, url, "Announce");

        if (msg != NULL) {
            enqueue_message(&snac, msg);

            if (dbglevel) {
                xs_json_dump(msg, 4, stdout);
            }
        }

        return 0;
    }

    if (strcmp(cmd, "follow") == 0) { /** **/
        xs *msg = msg_follow(&snac, url);

        if (msg != NULL) {
            const char *actor = xs_dict_get(msg, "object");

            following_add(&snac, actor, msg);

            enqueue_output_by_actor(&snac, msg, actor, 0);

            if (dbglevel) {
                xs_json_dump(msg, 4, stdout);
            }
        }

        return 0;
    }

    if (strcmp(cmd, "unfollow") == 0) { /** **/
        xs *object = NULL;

        if (valid_status(following_get(&snac, url, &object))) {
            xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

            following_del(&snac, url);

            enqueue_output_by_actor(&snac, msg, url, 0);

            snac_log(&snac, xs_fmt("unfollowed actor %s", url));
        }
        else
            snac_log(&snac, xs_fmt("actor is not being followed %s", url));

        return 0;
    }

    if (strcmp(cmd, "limit") == 0) { /** **/
        int ret;

        if (!following_check(&snac, url))
            snac_log(&snac, xs_fmt("actor %s is not being followed", url));
        else
        if ((ret = limit(&snac, url)) == 0)
            snac_log(&snac, xs_fmt("actor %s is now limited", url));
        else
            snac_log(&snac, xs_fmt("error limiting actor %s (%d)", url, ret));

        return 0;
    }

    if (strcmp(cmd, "unlimit") == 0) { /** **/
        int ret;

        if (!following_check(&snac, url))
            snac_log(&snac, xs_fmt("actor %s is not being followed", url));
        else
        if ((ret = unlimit(&snac, url)) == 0)
            snac_log(&snac, xs_fmt("actor %s is no longer limited", url));
        else
            snac_log(&snac, xs_fmt("error unlimiting actor %s (%d)", url, ret));

        return 0;
    }

    if (strcmp(cmd, "search") == 0) { /** **/
        int to;

        /* 'url' contains the regex */
        xs *r = content_search(&snac, url, 1, 0, XS_ALL, 10, &to);

        int c = 0;
        const char *v;

        /* print results as standalone links */
        while (xs_list_next(r, &v, &c)) {
            printf("%s/admin/p/%s\n", snac.actor, v);
        }

        return 0;
    }

    if (strcmp(cmd, "ping") == 0) { /** **/
        xs *actor_o = NULL;

        if (!xs_startswith(url, "https:/")) {
            /* try to resolve via webfinger */
            if (!valid_status(webfinger_request(url, &url, NULL))) {
                srv_log(xs_fmt("cannot resolve %s via webfinger", url));
                return 1;
            }
        }

        if (valid_status(actor_request(&snac, url, &actor_o))) {
            xs *msg = msg_ping(&snac, url);

            enqueue_output_by_actor(&snac, msg, url, 0);

            if (dbglevel) {
                xs_json_dump(msg, 4, stdout);
            }

            srv_log(xs_fmt("Ping sent to %s -- see log for Pong reply", url));
        }
        else {
            srv_log(xs_fmt("Error getting actor %s", url));
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "pin") == 0) { /** **/
        int ret = pin(&snac, url);
        if (ret < 0) {
            fprintf(stderr, "error pinning %s %d\n", url, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "unpin") == 0) { /** **/
        int ret = unpin(&snac, url);
        if (ret < 0) {
            fprintf(stderr, "error unpinning %s %d\n", url, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "bookmark") == 0) { /** **/
        int ret = bookmark(&snac, url);
        if (ret < 0) {
            fprintf(stderr, "error bookmarking %s %d\n", url, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "unbookmark") == 0) { /** **/
        int ret = unbookmark(&snac, url);
        if (ret < 0) {
            fprintf(stderr, "error unbookmarking %s %d\n", url, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "question") == 0) { /** **/
        int end_secs = 5 * 60;
        xs *opts = xs_split(url, ";");

        xs *msg = msg_question(&snac, "Poll", NULL, opts, 0, end_secs);
        xs *c_msg = msg_create(&snac, msg);

        if (dbglevel) {
            xs_json_dump(c_msg, 4, stdout);
        }

        enqueue_message(&snac, c_msg);
        enqueue_close_question(&snac, xs_dict_get(msg, "id"), end_secs);

        timeline_add(&snac, xs_dict_get(msg, "id"), msg);

        return 0;
    }

    if (strcmp(cmd, "request") == 0) { /** **/
        int status;
        xs *data = NULL;

        status = activitypub_request(&snac, url, &data);

        printf("status: %d\n", status);

        if (data != NULL) {
            xs_json_dump(data, 4, stdout);
        }

        return 0;
    }

    if (strcmp(cmd, "insert") == 0) { /** **/
        int status;
        xs *data = NULL;

        status = activitypub_request(&snac, url, &data);

        printf("status: %d\n", status);

        if (data != NULL) {
            xs_json_dump(data, 4, stdout);
            enqueue_actor_refresh(&snac, xs_dict_get(data, "attributedTo"), 0);

            if (!timeline_here(&snac, url))
                timeline_add(&snac, url, data);
            else
                printf("Post %s already here\n", url);
        }

        return 0;
    }

    if (strcmp(cmd, "request2") == 0) { /** **/
        enqueue_object_request(&snac, url, 2);

        return 0;
    }

    if (strcmp(cmd, "actor") == 0) { /** **/
        int status;
        xs *data = NULL;

        status = actor_request(&snac, url, &data);

        printf("status: %d\n", status);

        if (valid_status(status)) {
            xs_json_dump(data, 4, stdout);
        }

        return 0;
    }

    if (strcmp(cmd, "note") == 0) { /** **/
        xs *content = NULL;
        xs *msg = NULL;
        xs *c_msg = NULL;
        xs *attl = xs_list_new();
        char *fn = NULL;

        /* iterate possible attachments */
        while ((fn = GET_ARGV())) {
            FILE *f;

            if ((f = fopen(fn, "rb")) != NULL) {
                /* get the file size and content */
                fseek(f, 0, SEEK_END);
                int sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                xs *atc = xs_readall(f);
                fclose(f);

                char *ext = strrchr(fn, '.');
                xs *hash  = xs_md5_hex(fn, strlen(fn));
                xs *id    = xs_fmt("%s%s", hash, ext);
                xs *url   = xs_fmt("%s/s/%s", snac.actor, id);

                /* store */
                static_put(&snac, id, atc, sz);

                xs *l = xs_list_new();

                l = xs_list_append(l, url);
                l = xs_list_append(l, ""); /* alt text */

                attl = xs_list_append(attl, l);
            }
            else
                fprintf(stderr, "Error opening '%s' as attachment\n", fn);
        }

        if (strcmp(url, "-e") == 0) {
            /* get the content from an editor */
            FILE *f;

            unlink("/tmp/snac-edit.txt");
            system("$EDITOR /tmp/snac-edit.txt");

            if ((f = fopen("/tmp/snac-edit.txt", "r")) != NULL) {
                content = xs_readall(f);
                fclose(f);

                unlink("/tmp/snac-edit.txt");
            }
            else {
                printf("Nothing to send\n");
                return 1;
            }
        }
        else
        if (strcmp(url, "-") == 0) {
            /* get the content from stdin */
            content = xs_readall(stdin);
        }
        else
            content = xs_dup(url);

        msg = msg_note(&snac, content, NULL, NULL, attl, 0);

        c_msg = msg_create(&snac, msg);

        if (dbglevel) {
            xs_json_dump(c_msg, 4, stdout);
        }

        enqueue_message(&snac, c_msg);

        timeline_add(&snac, xs_dict_get(msg, "id"), msg);

        return 0;
    }

    fprintf(stderr, "ERROR: bad command '%s'\n", cmd);

    return 1;
}
