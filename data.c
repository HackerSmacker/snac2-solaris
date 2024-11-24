/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_hex.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_openssl.h"
#include "xs_glob.h"
#include "xs_set.h"
#include "xs_time.h"
#include "xs_regex.h"
#include "xs_match.h"
#include "xs_unicode.h"
#include "xs_random.h"

#include "snac.h"

#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

double disk_layout = 2.7;

/* storage serializer */
pthread_mutex_t data_mutex = {0};

int snac_upgrade(xs_str **error);


int srv_open(const char *basedir, int auto_upgrade)
/* opens a server */
{
    int ret = 0;
    xs *cfg_file = NULL;
    FILE *f;
    xs_str *error = NULL;

    pthread_mutex_init(&data_mutex, NULL);

    srv_basedir = xs_str_new(basedir);

    if (xs_endswith(srv_basedir, "/"))
        srv_basedir = xs_crop_i(srv_basedir, 0, -1);

    cfg_file = xs_fmt("%s/server.json", basedir);

    if ((f = fopen(cfg_file, "r")) == NULL)
        error = xs_fmt("ERROR: cannot open '%s'", cfg_file);
    else {
        /* read full config file */
        srv_config = xs_json_load(f);
        fclose(f);

        /* parse */

        if (srv_config == NULL)
            error = xs_fmt("ERROR: cannot parse '%s'", cfg_file);
        else {
            const char *host;
            const char *prefix;
            const char *dbglvl;
            const char *proto;

            host   = xs_dict_get(srv_config, "host");
            prefix = xs_dict_get(srv_config, "prefix");
            dbglvl = xs_dict_get(srv_config, "dbglevel");
            proto  = xs_dict_get_def(srv_config, "protocol", "https");

            if (host == NULL || prefix == NULL)
                error = xs_str_new("ERROR: cannot get server data");
            else {
                srv_baseurl = xs_fmt("%s:/" "/%s%s", proto, host, prefix);

                dbglevel = (int) xs_number_get(dbglvl);

                if ((dbglvl = getenv("DEBUG")) != NULL) {
                    dbglevel = atoi(dbglvl);
                    error = xs_fmt("DEBUG level set to %d from environment", dbglevel);
                }

                if (auto_upgrade)
                    ret = snac_upgrade(&error);
                else {
                    if (xs_number_get(xs_dict_get(srv_config, "layout")) < disk_layout)
                        error = xs_fmt("ERROR: disk layout changed - execute 'snac upgrade' first");
                    else
                        ret = 1;
                }
            }

        }
    }

    if (error != NULL)
        srv_log(error);

    /* create the queue/ subdir, just in case */
    xs *qdir = xs_fmt("%s/queue", srv_basedir);
    mkdirx(qdir);

    xs *ibdir = xs_fmt("%s/inbox", srv_basedir);
    mkdirx(ibdir);

    xs *tmpdir = xs_fmt("%s/tmp", srv_basedir);
    mkdirx(tmpdir);

#ifdef __APPLE__
/* Apple uses st_atimespec instead of st_atim etc */
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#define st_mtim st_mtimespec
#endif

#ifdef __OpenBSD__
    if (xs_is_true(xs_dict_get(srv_config, "disable_openbsd_security"))) {
        srv_debug(1, xs_dup("OpenBSD security disabled by admin"));
    }
    else {
        int smail = !xs_is_true(xs_dict_get(srv_config, "disable_email_notifications"));
        const char *address = xs_dict_get(srv_config, "address");

        srv_debug(1, xs_fmt("Calling unveil()"));
        unveil(basedir,                "rwc");
        unveil("/tmp",                 "rwc");
        unveil("/etc/resolv.conf",     "r");
        unveil("/etc/hosts",           "r");
        unveil("/etc/ssl/openssl.cnf", "r");
        unveil("/etc/ssl/cert.pem",    "r");
        unveil("/usr/share/zoneinfo",  "r");

        if (smail)
            unveil("/usr/sbin/sendmail",   "x");

        if (*address == '/')
            unveil(address, "rwc");

        unveil(NULL,                   NULL);

        srv_debug(1, xs_fmt("Calling pledge()"));

        xs *p = xs_str_new("stdio rpath wpath cpath flock inet proc dns fattr");

        if (smail)
            p = xs_str_cat(p, " exec");

        if (*address == '/')
            p = xs_str_cat(p, " unix");

        pledge(p, NULL);
    }
#endif /* __OpenBSD__ */

    /* read (and drop) emojis.json, possibly creating it */
    xs_free(emojis());

    /* if style.css does not exist, create it */
    xs *css_fn = xs_fmt("%s/style.css", srv_basedir);

    if (mtime(css_fn) == 0) {
        srv_log(xs_fmt("Writing style.css"));
        write_default_css();
    }

    /* create the proxy token seed */
    {
        char rnd[16];
        xs_rnd_buf(rnd, sizeof(rnd));

        srv_proxy_token_seed = xs_hex_enc(rnd, sizeof(rnd));
    }

    return ret;
}


void srv_free(void)
{
    xs_free(srv_basedir);
    xs_free(srv_config);
    xs_free(srv_baseurl);

    pthread_mutex_destroy(&data_mutex);
}


void user_free(snac *snac)
/* frees a user snac */
{
    xs_free(snac->uid);
    xs_free(snac->basedir);
    xs_free(snac->config);
    xs_free(snac->config_o);
    xs_free(snac->key);
    xs_free(snac->links);
    xs_free(snac->actor);
    xs_free(snac->md5);
}


int user_open(snac *user, const char *uid)
/* opens a user */
{
    int ret = 0;

    *user = (snac){0};

    if (validate_uid(uid)) {
        xs *cfg_file = NULL;
        FILE *f;

        xs *t = xs_fmt("%s/user/%s", srv_basedir, uid);

        if (mtime(t) == 0.0) {
            /* user folder does not exist; try with a different case */
            xs *lcuid = xs_tolower_i(xs_dup(uid));
            xs *ulist = user_list();
            xs_list *p = ulist;
            const xs_str *v;

            while (xs_list_iter(&p, &v)) {
                xs *v2 = xs_tolower_i(xs_dup(v));

                if (strcmp(lcuid, v2) == 0) {
                    user->uid = xs_dup(v);
                    break;
                }
            }
        }
        else
            user->uid = xs_str_new(uid);

        if (user->uid == NULL)
            return ret;

        user->basedir = xs_fmt("%s/user/%s", srv_basedir, user->uid);

        cfg_file = xs_fmt("%s/user.json", user->basedir);

        if ((f = fopen(cfg_file, "r")) != NULL) {
            /* read full config file */
            user->config = xs_json_load(f);
            fclose(f);

            if (user->config != NULL) {
                xs *key_file = xs_fmt("%s/key.json", user->basedir);

                if ((f = fopen(key_file, "r")) != NULL) {
                    user->key = xs_json_load(f);
                    fclose(f);

                    if (user->key != NULL) {
                        user->actor = xs_fmt("%s/%s", srv_baseurl, user->uid);
                        user->md5   = xs_md5_hex(user->actor, strlen(user->actor));

                        /* everything is ok right now */
                        ret = 1;

                        /* does it have a configuration override? */
                        xs *cfg_file_o = xs_fmt("%s/user_o.json", user->basedir);
                        if ((f = fopen(cfg_file_o, "r")) != NULL) {
                            user->config_o = xs_json_load(f);
                            fclose(f);

                            if (user->config_o == NULL)
                                srv_log(xs_fmt("error parsing '%s'", cfg_file_o));
                        }

                        if (user->config_o == NULL)
                            user->config_o = xs_dict_new();
                    }
                    else
                        srv_log(xs_fmt("error parsing '%s'", key_file));
                }
                else
                    srv_log(xs_fmt("error opening '%s' %d", key_file, errno));
            }
            else
                srv_log(xs_fmt("error parsing '%s'", cfg_file));
        }
        else
            srv_debug(2, xs_fmt("error opening '%s' %d", cfg_file, errno));

        /* verified links */
        xs *links_file = xs_fmt("%s/links.json", user->basedir);

        if ((f = fopen(links_file, "r")) != NULL) {
            user->links = xs_json_load(f);
            fclose(f);
        }
    }
    else
        srv_debug(1, xs_fmt("invalid user '%s'", uid));

    if (!ret)
        user_free(user);

    return ret;
}


xs_list *user_list(void)
/* returns the list of user ids */
{
    xs *spec = xs_fmt("%s/user/" "*", srv_basedir);
    return xs_glob(spec, 1, 0);
}


int user_open_by_md5(snac *snac, const char *md5)
/* iterates all users searching by md5 */
{
    xs *ulist  = user_list();
    xs_list *p = ulist;
    const xs_str *v;

    while (xs_list_iter(&p, &v)) {
        user_open(snac, v);

        if (strcmp(snac->md5, md5) == 0)
            return 1;

        user_free(snac);
    }

    return 0;
}

int user_persist(snac *snac, int publish)
/* store user */
{
    xs *fn  = xs_fmt("%s/user.json", snac->basedir);
    xs *bfn = xs_fmt("%s.bak", fn);
    FILE *f;

    rename(fn, bfn);

    if ((f = fopen(fn, "w")) != NULL) {
        xs_json_dump(snac->config, 4, f);
        fclose(f);
    }
    else
        rename(bfn, fn);

    history_del(snac, "timeline.html_");
    timeline_touch(snac);

    if (publish) {
        xs *a_msg = msg_actor(snac);
        xs *u_msg = msg_update(snac, a_msg);

        enqueue_message(snac, u_msg);

        enqueue_verify_links(snac);
    }

    return 0;
}


double mtime_nl(const char *fn, int *n_link)
/* returns the mtime and number of links of a file or directory, or 0.0 */
{
    struct stat st;
    double r = 0.0;
    int n = 0;

    if (fn && stat(fn, &st) != -1) {
        r = (double) st.st_mtim.tv_sec;
        n = st.st_nlink;
    }

    if (n_link)
        *n_link = n;

    return r;
}


#define MIN(v1, v2) ((v1) < (v2) ? (v1) : (v2))

double f_ctime(const char *fn)
/* returns the ctime of a file or directory, or 0.0 */
{
    struct stat st;
    double r = 0.0;

    if (fn && stat(fn, &st) != -1) {
        /* return the lowest of ctime and mtime;
           there are operations that change the ctime, like link() */
        r = (double) MIN(st.st_ctim.tv_sec, st.st_mtim.tv_sec);
    }

    return r;
}


int is_md5_hex(const char *md5)
{
    return xs_is_hex(md5) && strlen(md5) == MD5_HEX_SIZE - 1;
}


/** database 2.1+ **/

/** indexes **/


int index_add_md5(const char *fn, const char *md5)
/* adds an md5 to an index */
{
    int status = HTTP_STATUS_CREATED;
    FILE *f;

    if (!is_md5_hex(md5)) {
        srv_log(xs_fmt("index_add_md5: bad md5 %s %s", fn, md5));
        return HTTP_STATUS_BAD_REQUEST;
    }

    pthread_mutex_lock(&data_mutex);

    if ((f = fopen(fn, "a")) != NULL) {
	struct flock lock;
	lock.l_type = F_WRLCK; 
        lock.l_whence = SEEK_SET;
        lock.l_start = 0; 
        lock.l_len = 0;
        if (fcntl(fileno(f), F_SETLK, &lock) == -1) {
            perror("fcntl");
        }

        /* ensure the position is at the end after getting the lock */
        fseek(f, 0, SEEK_END);

        fprintf(f, "%s\n", md5);
        fclose(f);
    }
    else
        status = HTTP_STATUS_INTERNAL_SERVER_ERROR;

    pthread_mutex_unlock(&data_mutex);

    return status;
}


int index_add(const char *fn, const char *id)
/* adds an id to an index */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return index_add_md5(fn, md5);
}


int index_del_md5(const char *fn, const char *md5)
/* deletes an md5 from an index */
{
    int status = HTTP_STATUS_NOT_FOUND;
    FILE *f;

    pthread_mutex_lock(&data_mutex);

    if ((f = fopen(fn, "r+")) != NULL) {
        char line[256];

        while (fgets(line, sizeof(line), f) != NULL) {
            line[MD5_HEX_SIZE - 1] = '\0';

            if (strcmp(line, md5) == 0) {
                /* found! just rewind, overwrite it with garbage
                   and an eventual call to index_gc() will clean it
                   [yes: this breaks index_len()] */
                fseek(f, -MD5_HEX_SIZE, SEEK_CUR);
                fwrite("-", 1, 1, f);
                status = HTTP_STATUS_OK;

                break;
            }
        }

        fclose(f);
    }
    else
        status = HTTP_STATUS_GONE;

    pthread_mutex_unlock(&data_mutex);

    return status;
}


int index_del(const char *fn, const char *id)
/* deletes an id from an index */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return index_del_md5(fn, md5);
}


int index_gc(const char *fn)
/* garbage-collects an index, deleting objects that are not here */
{
    FILE *i, *o;
    int gc = -1;

    pthread_mutex_lock(&data_mutex);

    if ((i = fopen(fn, "r")) != NULL) {
        xs *nfn = xs_fmt("%s.new", fn);
        char line[256];

        if ((o = fopen(nfn, "w")) != NULL) {
            gc = 0;

            while (fgets(line, sizeof(line), i) != NULL) {
                line[MD5_HEX_SIZE - 1] = '\0';

                if (line[0] != '-' && object_here_by_md5(line))
                    fprintf(o, "%s\n", line);
                else
                    gc++;
            }

            fclose(o);

            xs *ofn = xs_fmt("%s.bak", fn);

            unlink(ofn);
            link(fn, ofn);
            rename(nfn, fn);
        }

        fclose(i);
    }

    pthread_mutex_unlock(&data_mutex);

    return gc;
}


int index_in_md5(const char *fn, const char *md5)
/* checks if the md5 is already in the index */
{
    FILE *f;
    int ret = 0;

    if ((f = fopen(fn, "r")) != NULL) {
        struct flock lock;
        char line[256];
        lock.l_type = F_RDLCK; 
        lock.l_whence = SEEK_SET;
        lock.l_start = 0; 
        lock.l_len = 0; 
        if (fcntl(fileno(f), F_SETLK, &lock) == -1) {
            perror("fcntl");
        }

        while (!ret && fgets(line, sizeof(line), f) != NULL) {
            line[MD5_HEX_SIZE - 1] = '\0';

            if (strcmp(line, md5) == 0)
                ret = 1;
        }

        fclose(f);
    }

    return ret;
}


int index_in(const char *fn, const char *id)
/* checks if the object id is already in the index */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return index_in_md5(fn, md5);
}


int index_first(const char *fn, char md5[MD5_HEX_SIZE])
/* reads the first entry of an index */
{
    FILE *f;
    int ret = 0;

    if ((f = fopen(fn, "r")) != NULL) {
        if (fread(md5, MD5_HEX_SIZE, 1, f)) {
            md5[MD5_HEX_SIZE - 1] = '\0';
            ret = 1;
        }

        fclose(f);
    }

    return ret;
}


int index_len(const char *fn)
/* returns the number of elements in an index */
{
    struct stat st;
    int len = 0;

    if (stat(fn, &st) != -1)
        len = st.st_size / MD5_HEX_SIZE;

    return len;
}


xs_list *index_list(const char *fn, int max)
/* returns an index as a list */
{
    xs_list *list = xs_list_new();
    FILE *f;
    int n = 0;

    if ((f = fopen(fn, "r")) != NULL) {

        char line[256];

        while (n < max && fgets(line, sizeof(line), f) != NULL) {
            if (line[0] != '-') {
                line[MD5_HEX_SIZE - 1] = '\0';
                list = xs_list_append(list, line);
                n++;
            }
        }

        fclose(f);
    }

    return list;
}


int index_desc_next(FILE *f, char md5[MD5_HEX_SIZE])
/* reads the next entry of a desc index */
{
    for (;;) {
        /* move backwards 2 entries */
        if (fseek(f, MD5_HEX_SIZE * -2, SEEK_CUR) == -1)
            return 0;

        /* read and md5 */
        if (!fread(md5, MD5_HEX_SIZE, 1, f))
            return 0;

        if (md5[0] != '-')
            break;
    }

    md5[MD5_HEX_SIZE - 1] = '\0';

    return 1;
}


int index_desc_first(FILE *f, char md5[MD5_HEX_SIZE], int skip)
/* reads the first entry of a desc index */
{
    /* try to position at the end and then back to the first element */
    if (fseek(f, 0, SEEK_END) || fseek(f, (skip + 1) * -MD5_HEX_SIZE, SEEK_CUR))
        return 0;

    /* try to read an md5 */
    if (!fread(md5, MD5_HEX_SIZE, 1, f))
        return 0;

    /* null-terminate */
    md5[MD5_HEX_SIZE - 1] = '\0';

    /* deleted? retry next */
    if (md5[0] == '-')
        return index_desc_next(f, md5);

    return 1;
}


xs_list *index_list_desc(const char *fn, int skip, int show)
/* returns an index as a list, in reverse order */
{
    xs_list *list = xs_list_new();
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        char md5[MD5_HEX_SIZE];

        if (index_desc_first(f, md5, skip)) {
            int n = 1;

            do {
                list = xs_list_append(list, md5);
            } while (n++ < show && index_desc_next(f, md5));
        }

        fclose(f);
    }

    return list;
}


/** objects **/

static xs_str *_object_fn_by_md5(const char *md5, const char *func)
{
    xs *bfn = xs_fmt("%s/object/%c%c", srv_basedir, md5[0], md5[1]);
    xs_str *ret;
    int ok = 1;

    /* an object deleted from an index; fail but don't bark */
    if (md5[0] == '-')
        ok = 0;
    else
    if (!is_md5_hex(md5)) {
        srv_log(xs_fmt("_object_fn_by_md5() [from %s()]: bad md5 '%s'", func, md5));
        ok = 0;
    }

    if (ok) {
        mkdirx(bfn);
        ret = xs_fmt("%s/%s.json", bfn, md5);
    }
    else
        ret = xs_fmt("%s/object/invalid/invalid.json", srv_basedir);

    return ret;
}


static xs_str *_object_fn(const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return _object_fn_by_md5(md5, "_object_fn");
}


int object_here_by_md5(const char *id)
/* checks if an object is already downloaded */
{
    xs *fn = _object_fn_by_md5(id, "object_here_by_md5");
    return mtime(fn) > 0.0;
}


int object_here(const char *id)
/* checks if an object is already downloaded */
{
    xs *fn = _object_fn(id);
    return mtime(fn) > 0.0;
}


int object_get_by_md5(const char *md5, xs_dict **obj)
/* returns a stored object, optionally of the requested type */
{
    int status = HTTP_STATUS_NOT_FOUND;
    xs *fn     = _object_fn_by_md5(md5, "object_get_by_md5");
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        *obj = xs_json_load(f);
        fclose(f);

        if (*obj)
            status = HTTP_STATUS_OK;
    }
    else
        *obj = NULL;

    return status;
}


int object_get(const char *id, xs_dict **obj)
/* returns a stored object, optionally of the requested type */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_get_by_md5(md5, obj);
}


int _object_add(const char *id, const xs_dict *obj, int ow)
/* stores an object */
{
    int status = HTTP_STATUS_CREATED; /* Created */
    xs *fn     = _object_fn(id);
    FILE *f;

    if (mtime(fn) > 0.0) {
        if (!ow) {
            /* object already here */
            srv_debug(1, xs_fmt("object_add object already here %s", id));
            return HTTP_STATUS_NO_CONTENT;
        }
        else
            status = HTTP_STATUS_OK;
    }

    if ((f = fopen(fn, "w")) != NULL) {
        struct flock lock;
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0; 
        lock.l_len = 0; 

        if (fcntl(fileno(f), F_SETLK, &lock) == -1) {
            perror("fcntl");
        }

        xs_json_dump(obj, 4, f);
        fclose(f);

        /* does this object has a parent? */
        const char *in_reply_to = get_in_reply_to(obj);

        if (!xs_is_null(in_reply_to) && *in_reply_to) {
            /* update the children index of the parent */
            xs *c_idx = _object_fn(in_reply_to);

            c_idx = xs_replace_i(c_idx, ".json", "_c.idx");

            if (!index_in(c_idx, id)) {
                index_add(c_idx, id);
                srv_debug(1, xs_fmt("object_add added child %s to %s", id, c_idx));
            }
            else
                srv_debug(1, xs_fmt("object_add %s child already in %s", id, c_idx));

            /* create a one-element index with the parent */
            xs *p_idx = xs_replace(fn, ".json", "_p.idx");

            if (mtime(p_idx) == 0.0) {
                index_add(p_idx, in_reply_to);
                srv_debug(1, xs_fmt("object_add added parent %s to %s", in_reply_to, p_idx));
            }
        }
    }
    else {
        srv_log(xs_fmt("object_add error writing %s (errno: %d)", fn, errno));
        status = HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    srv_debug(1, xs_fmt("object_add %s %s %d", id, fn, status));

    return status;
}


int object_add(const char *id, const xs_dict *obj)
/* stores an object */
{
    return _object_add(id, obj, 0);
}


int object_add_ow(const char *id, const xs_dict *obj)
/* stores an object (overwriting allowed) */
{
    return _object_add(id, obj, 1);
}


int object_del_by_md5(const char *md5)
/* deletes an object by its md5 */
{
    int status = HTTP_STATUS_NOT_FOUND;
    xs *fn     = _object_fn_by_md5(md5, "object_del_by_md5");

    if (unlink(fn) != -1) {
        status = HTTP_STATUS_OK;

        /* also delete associated indexes */
        xs *spec  = xs_dup(fn);
        spec      = xs_replace_i(spec, ".json", "*.idx");
        xs *files = xs_glob(spec, 0, 0);
        char *p;
        const char *v;

        p = files;
        while (xs_list_iter(&p, &v)) {
            srv_debug(1, xs_fmt("object_del index %s", v));
            unlink(v);
        }
    }

    srv_debug(1, xs_fmt("object_del %s %d", fn, status));

    return status;
}


int object_del(const char *id)
/* deletes an object */
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_del_by_md5(md5);
}


int object_del_if_unref(const char *id)
/* deletes an object if its n_links < 2 */
{
    xs *fn = _object_fn(id);
    int n_links;
    int ret = 0;

    if (mtime_nl(fn, &n_links) > 0.0 && n_links < 2)
        ret = object_del(id);

    return ret;
}


double object_ctime_by_md5(const char *md5)
{
    xs *fn = _object_fn_by_md5(md5, "object_ctime_by_md5");
    return f_ctime(fn);
}


double object_ctime(const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_ctime_by_md5(md5);
}


double object_mtime_by_md5(const char *md5)
{
    xs *fn = _object_fn_by_md5(md5, "object_mtime_by_md5");
    return mtime(fn);
}


double object_mtime(const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_mtime_by_md5(md5);
}


void object_touch(const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    xs *fn = _object_fn_by_md5(md5, "object_touch");

    if (mtime(fn))
        utimes(fn, NULL);
}


xs_str *_object_index_fn(const char *id, const char *idxsfx)
/* returns the filename of an object's index */
{
    xs_str *fn = _object_fn(id);
    return xs_replace_i(fn, ".json", idxsfx);
}


int object_likes_len(const char *id)
/* returns the number of likes (without reading the index) */
{
    xs *fn = _object_index_fn(id, "_l.idx");
    return index_len(fn);
}


int object_announces_len(const char *id)
/* returns the number of announces (without reading the index) */
{
    xs *fn = _object_index_fn(id, "_a.idx");
    return index_len(fn);
}


xs_list *object_children(const char *id)
/* returns the list of an object's children */
{
    xs *fn = _object_index_fn(id, "_c.idx");
    return index_list(fn, XS_ALL);
}


xs_list *object_likes(const char *id)
{
    xs *fn = _object_index_fn(id, "_l.idx");
    return index_list(fn, XS_ALL);
}


xs_list *object_announces(const char *id)
{
    xs *fn = _object_index_fn(id, "_a.idx");
    return index_list(fn, XS_ALL);
}


int object_parent(const char *md5, char parent[MD5_HEX_SIZE])
/* returns the object parent, if any */
{
    xs *fn = _object_fn_by_md5(md5, "object_parent");

    fn = xs_replace_i(fn, ".json", "_p.idx");
    return index_first(fn, parent);
}


int object_admire(const char *id, const char *actor, int like)
/* actor likes or announces this object */
{
    int status = HTTP_STATUS_OK;
    xs *fn     = _object_fn(id);

    fn = xs_replace_i(fn, ".json", like ? "_l.idx" : "_a.idx");

    if (!index_in(fn, actor)) {
        status = index_add(fn, actor);

        srv_debug(1, xs_fmt("object_admire (%s) %s %s", like ? "Like" : "Announce", actor, fn));
    }

    return status;
}


int object_unadmire(const char *id, const char *actor, int like)
/* actor no longer likes or announces this object */
{
    int status;
    xs *fn = _object_fn(id);

    fn = xs_replace_i(fn, ".json", like ? "_l.idx" : "_a.idx");

    status = index_del(fn, actor);

    if (valid_status(status))
        index_gc(fn);

    srv_debug(0,
        xs_fmt("object_unadmire (%s) %s %s %d", like ? "Like" : "Announce", actor, fn, status));

    return status;
}


xs_str *object_user_cache_fn_by_md5(snac *user, const char *md5, const char *cachedir)
{
    return xs_fmt("%s/%s/%s.json", user->basedir, cachedir, md5);
}


xs_str *object_user_cache_fn(snac *user, const char *id, const char *cachedir)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return object_user_cache_fn_by_md5(user, md5, cachedir);
}


xs_str *object_user_cache_index_fn(snac *user, const char *cachedir)
{
    return xs_fmt("%s/%s.idx", user->basedir, cachedir);
}


int _object_user_cache(snac *user, const char *id, const char *cachedir, int del)
/* adds or deletes from a user cache */
{
    xs *ofn = _object_fn(id);
    xs *cfn = object_user_cache_fn(user, id, cachedir);
    xs *idx = object_user_cache_index_fn(user, cachedir);
    int ret;

    if (del) {
        ret = unlink(cfn);
        index_del(idx, id);
    }
    else {
        /* create the subfolder, if it does not exist */
        xs *dir = xs_fmt("%s/%s/", user->basedir, cachedir);
        mkdirx(dir);

        if ((ret = link(ofn, cfn)) != -1)
            index_add(idx, id);
    }

    return ret;
}


int object_user_cache_add(snac *user, const char *id, const char *cachedir)
/* caches an object into a user cache */
{
    return _object_user_cache(user, id, cachedir, 0);
}


int object_user_cache_del(snac *user, const char *id, const char *cachedir)
/* deletes an object from a user cache */
{
    return _object_user_cache(user, id, cachedir, 1);
}


int object_user_cache_in(snac *user, const char *id, const char *cachedir)
/* checks if an object is stored in a cache */
{
    xs *cfn = object_user_cache_fn(user, id, cachedir);
    return !!(mtime(cfn) != 0.0);
}


int object_user_cache_in_by_md5(snac *user, const char *md5, const char *cachedir)
/* checks if an object is stored in a cache */
{
    xs *cfn = object_user_cache_fn_by_md5(user, md5, cachedir);
    return !!(mtime(cfn) != 0.0);
}


xs_list *object_user_cache_list(snac *user, const char *cachedir, int max, int inv)
/* returns the objects in a cache as a list */
{
    xs *idx = xs_fmt("%s/%s.idx", user->basedir, cachedir);
    return inv ? index_list_desc(idx, 0, max) : index_list(idx, max);
}


/** specialized functions **/

/** followers **/

int follower_add(snac *snac, const char *actor)
/* adds a follower */
{
    int ret = object_user_cache_add(snac, actor, "followers");

    snac_debug(snac, 2, xs_fmt("follower_add %s", actor));

    return ret == -1 ? HTTP_STATUS_INTERNAL_SERVER_ERROR : HTTP_STATUS_OK;
}


int follower_del(snac *snac, const char *actor)
/* deletes a follower */
{
    int ret = object_user_cache_del(snac, actor, "followers");

    snac_debug(snac, 2, xs_fmt("follower_del %s", actor));

    return ret == -1 ? HTTP_STATUS_NOT_FOUND : HTTP_STATUS_OK;
}


int follower_check(snac *snac, const char *actor)
/* checks if someone is a follower */
{
    return object_user_cache_in(snac, actor, "followers");
}


xs_list *follower_list(snac *snac)
/* returns the list of followers */
{
    xs *list       = object_user_cache_list(snac, "followers", XS_ALL, 0);
    xs_list *fwers = xs_list_new();
    char *p;
    const char *v;

    /* resolve the list of md5 to be a list of actors */
    p = list;
    while (xs_list_iter(&p, &v)) {
        xs *a_obj = NULL;

        if (valid_status(object_get_by_md5(v, &a_obj))) {
            const char *actor = xs_dict_get(a_obj, "id");

            if (!xs_is_null(actor)) {
                /* check if the actor is still cached */
                xs *fn = xs_fmt("%s/followers/%s.json", snac->basedir, v);

                if (mtime(fn) > 0.0)
                    fwers = xs_list_append(fwers, actor);
            }
        }
    }

    return fwers;
}


/** timeline **/

double timeline_mtime(snac *snac)
{
    xs *fn = xs_fmt("%s/private.idx", snac->basedir);
    return mtime(fn);
}


int timeline_touch(snac *snac)
/* changes the date of the timeline index */
{
    xs *fn = xs_fmt("%s/private.idx", snac->basedir);
    return utimes(fn, NULL);
}


xs_str *timeline_fn_by_md5(snac *snac, const char *md5)
/* get the filename of an entry by md5 from any timeline */
{
    xs_str *fn = NULL;

    if (is_md5_hex(md5)) {
        fn = xs_fmt("%s/private/%s.json", snac->basedir, md5);

        if (mtime(fn) == 0.0) {
            fn = xs_free(fn);
            fn = xs_fmt("%s/public/%s.json", snac->basedir, md5);

            if (mtime(fn) == 0.0)
                fn = xs_free(fn);
        }
    }

    return fn;
}


int timeline_here(snac *snac, const char *md5)
/* checks if an object is in the user cache */
{
    xs *fn = timeline_fn_by_md5(snac, md5);

    return !(fn == NULL);
}


int timeline_get_by_md5(snac *snac, const char *md5, xs_dict **msg)
/* gets a message from the timeline */
{
    int status = HTTP_STATUS_NOT_FOUND;
    FILE *f    = NULL;

    xs *fn = timeline_fn_by_md5(snac, md5);

    if (fn != NULL && (f = fopen(fn, "r")) != NULL) {
        *msg = xs_json_load(f);
        fclose(f);

        if (*msg != NULL)
            status = HTTP_STATUS_OK;
    }

    return status;
}


int timeline_del(snac *snac, const char *id)
/* deletes a message from the timeline */
{
    /* delete from the user's caches */
    object_user_cache_del(snac, id, "public");
    object_user_cache_del(snac, id, "private");

    unpin(snac, id);
    unbookmark(snac, id);

    /* try to delete the object if it's not used elsewhere */
    return object_del_if_unref(id);
}


void timeline_update_indexes(snac *snac, const char *id)
/* updates the indexes */
{
    object_user_cache_add(snac, id, "private");

    if (xs_startswith(id, snac->actor)) {
        xs *msg = NULL;

        if (valid_status(object_get(id, &msg))) {
            /* if its ours and is public, also store in public */
            if (is_msg_public(msg)) {
                object_user_cache_add(snac, id, "public");

                /* also add it to the instance public timeline */
                xs *ipt = xs_fmt("%s/public.idx", srv_basedir);
                index_add(ipt, id);
            }
        }
    }
}


int timeline_add(snac *snac, const char *id, const xs_dict *o_msg)
/* adds a message to the timeline */
{
    int ret = object_add(id, o_msg);
    timeline_update_indexes(snac, id);

    tag_index(id, o_msg);

    list_distribute(snac, NULL, o_msg);

    snac_debug(snac, 1, xs_fmt("timeline_add %s", id));

    return ret;
}


int timeline_admire(snac *snac, const char *id, const char *admirer, int like)
/* updates a timeline entry with a new admiration */
{
    /* if we are admiring this, add to both timelines */
    if (!like && strcmp(admirer, snac->actor) == 0) {
        object_user_cache_add(snac, id, "public");
        object_user_cache_add(snac, id, "private");
    }

    int ret = object_admire(id, admirer, like);

    snac_debug(snac, 1, xs_fmt("timeline_admire (%s) %s %s",
            like ? "Like" : "Announce", id, admirer));

    return ret;
}


xs_list *timeline_top_level(snac *snac, const xs_list *list)
/* returns the top level md5 entries from this index */
{
    xs_set seen;
    const xs_str *v;

    xs_set_init(&seen);

    int c = 0;
    while (xs_list_next(list, &v, &c)) {
        char line[MD5_HEX_SIZE] = "";

        strncpy(line, v, sizeof(line));

        for (;;) {
            char line2[MD5_HEX_SIZE];

            /* if it doesn't have a parent, use this */
            if (!object_parent(line, line2))
                break;

            /* well, there is a parent... but is it here? */
            if (!timeline_here(snac, line2))
                break;

            /* it's here! try again with its own parent */
            strncpy(line, line2, sizeof(line));
        }

        xs_set_add(&seen, line);
    }

    return xs_set_result(&seen);
}


xs_str *user_index_fn(snac *user, const char *idx_name)
/* returns the filename of a user index */
{
    return xs_fmt("%s/%s.idx", user->basedir, idx_name);
}


xs_list *timeline_simple_list(snac *user, const char *idx_name, int skip, int show)
/* returns a timeline (with all entries) */
{
    xs *idx = user_index_fn(user, idx_name);

    return index_list_desc(idx, skip, show);
}


xs_list *timeline_list(snac *snac, const char *idx_name, int skip, int show)
/* returns a timeline (only top level entries) */
{
    int c_max;

    /* maximum number of items in the timeline */
    c_max = xs_number_get(xs_dict_get(srv_config, "max_timeline_entries"));

    /* never more timeline entries than the configured maximum */
    if (show > c_max)
        show = c_max;

    xs *list = timeline_simple_list(snac, idx_name, skip, show);

    return timeline_top_level(snac, list);
}


xs_str *instance_index_fn(void)
{
    return xs_fmt("%s/public.idx", srv_basedir);
}


xs_list *timeline_instance_list(int skip, int show)
/* returns the timeline for the full instance */
{
    xs *idx = instance_index_fn();

    return index_list_desc(idx, skip, show);
}


/** following **/

/* this needs special treatment and cannot use the object db as is,
   with a link to a cached author, because we need the Follow object
   in case we need to unfollow (Undo + original Follow) */

xs_str *_following_fn(snac *snac, const char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/following/%s.json", snac->basedir, md5);
}


int following_add(snac *snac, const char *actor, const xs_dict *msg)
/* adds to the following list */
{
    int ret = HTTP_STATUS_CREATED;
    xs *fn = _following_fn(snac, actor);
    FILE *f;
    xs *p_object = NULL;

    if (valid_status(following_get(snac, actor, &p_object))) {
        /* object already exists; if it's of type Accept,
           the actor is already being followed and confirmed,
           so do nothing */
        const char *type = xs_dict_get(p_object, "type");

        if (!xs_is_null(type) && strcmp(type, "Accept") == 0) {
            snac_debug(snac, 1, xs_fmt("following_add actor already confirmed %s", actor));
            return HTTP_STATUS_OK;
        }
    }

    if ((f = fopen(fn, "w")) != NULL) {
        xs_json_dump(msg, 4, f);
        fclose(f);

        /* get the filename of the actor object */
        xs *actor_fn = _object_fn(actor);

        /* increase its reference count */
        fn = xs_replace_i(fn, ".json", "_a.json");
        link(actor_fn, fn);
    }
    else
        ret = HTTP_STATUS_INTERNAL_SERVER_ERROR;

    snac_debug(snac, 2, xs_fmt("following_add %s %s", actor, fn));

    return ret;
}


int following_del(snac *snac, const char *actor)
/* we're not following this actor any longer */
{
    xs *fn = _following_fn(snac, actor);

    snac_debug(snac, 2, xs_fmt("following_del %s %s", actor, fn));

    unlink(fn);

    /* also delete the reference to the author */
    fn = xs_replace_i(fn, ".json", "_a.json");
    unlink(fn);

    return HTTP_STATUS_OK;
}


int following_check(snac *snac, const char *actor)
/* checks if we are following this actor */
{
    xs *fn = _following_fn(snac, actor);

    return !!(mtime(fn) != 0.0);
}


int following_get(snac *snac, const char *actor, xs_dict **data)
/* returns the 'Follow' object */
{
    xs *fn = _following_fn(snac, actor);
    FILE *f;
    int status = HTTP_STATUS_OK;

    if ((f = fopen(fn, "r")) != NULL) {
        *data = xs_json_load(f);
        fclose(f);
    }
    else
        status = HTTP_STATUS_NOT_FOUND;

    return status;
}


xs_list *following_list(snac *snac)
/* returns the list of people being followed */
{
    xs *spec = xs_fmt("%s/following/" "*.json", snac->basedir);
    xs *glist = xs_glob(spec, 0, 0);
    xs_list *p;
    const xs_str *v;
    xs_list *list = xs_list_new();

    /* iterate the list of files */
    p = glist;
    while (xs_list_iter(&p, &v)) {
        FILE *f;

        /* load the follower data */
        if ((f = fopen(v, "r")) != NULL) {
            xs *o = xs_json_load(f);
            fclose(f);

            if (o != NULL) {
                const char *type = xs_dict_get(o, "type");

                if (!xs_is_null(type) && strcmp(type, "Accept") == 0) {
                    const char *actor = xs_dict_get(o, "actor");

                    if (!xs_is_null(actor)) {
                        list = xs_list_append(list, actor);

                        /* check if there is a link to the actor object */
                        xs *v2 = xs_replace(v, ".json", "_a.json");

                        if (mtime(v2) == 0.0) {
                            /* no; add a link to it */
                            xs *actor_fn = _object_fn(actor);
                            link(actor_fn, v2);
                        }
                    }
                }
            }
        }
    }

    return list;
}


xs_str *_muted_fn(snac *snac, const char *actor)
{
    xs *md5 = xs_md5_hex(actor, strlen(actor));
    return xs_fmt("%s/muted/%s", snac->basedir, md5);
}


void mute(snac *snac, const char *actor)
/* mutes a moron */
{
    xs *fn = _muted_fn(snac, actor);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "%s\n", actor);
        fclose(f);

        snac_debug(snac, 2, xs_fmt("muted %s %s", actor, fn));
    }
}


void unmute(snac *snac, const char *actor)
/* actor is no longer a moron */
{
    xs *fn = _muted_fn(snac, actor);

    unlink(fn);

    snac_debug(snac, 2, xs_fmt("unmuted %s %s", actor, fn));
}


int is_muted(snac *snac, const char *actor)
/* check if someone is muted */
{
    xs *fn = _muted_fn(snac, actor);

    return !!(mtime(fn) != 0.0);
}


xs_list *muted_list(snac *user)
/* returns the list (actor URLs) of the muted morons */
{
    xs_list *l = xs_list_new();
    xs *spec = xs_fmt("%s/muted/" "*", user->basedir);
    xs *files = xs_glob(spec, 0, 0);
    const char *fn;

    xs_list_foreach(files, fn) {
        FILE *f;

        if ((f = fopen(fn, "r")) != NULL) {
            xs *actor = xs_strip_i(xs_readline(f));
            fclose(f);

            l = xs_list_append(l, actor);
        }
    }

    return l;
}


/** bookmarking **/

int is_bookmarked(snac *user, const char *id)
/* returns true if this note is bookmarked */
{
    return object_user_cache_in(user, id, "bookmark");
}


int bookmark(snac *user, const char *id)
/* bookmarks a post */
{
    if (is_bookmarked(user, id))
        return -3;

    return object_user_cache_add(user, id, "bookmark");
}


int unbookmark(snac *user, const char *id)
/* unbookmarks a post */
{
    return object_user_cache_del(user, id, "bookmark");
}


xs_list *bookmark_list(snac *user)
/* return the lists of bookmarked posts */
{
    return object_user_cache_list(user, "bookmark", XS_ALL, 1);
}


xs_str *bookmark_index_fn(snac *user)
{
    return object_user_cache_index_fn(user, "bookmark");
}


/** pinning **/

int is_pinned(snac *user, const char *id)
/* returns true if this note is pinned */
{
    return object_user_cache_in(user, id, "pinned");
}


int is_pinned_by_md5(snac *user, const char *md5)
{
    return object_user_cache_in_by_md5(user, md5, "pinned");
}


int pin(snac *user, const char *id)
/* pins a message */
{
    int ret = -2;

    if (xs_startswith(id, user->actor)) {
        if (is_pinned(user, id))
            ret = -3;
        else
            ret = object_user_cache_add(user, id, "pinned");
    }

    return ret;
}


int unpin(snac *user, const char *id)
/* unpin a message */
{
    return object_user_cache_del(user, id, "pinned");
}


xs_list *pinned_list(snac *user)
/* return the lists of pinned posts */
{
    return object_user_cache_list(user, "pinned", XS_ALL, 1);
}


/** drafts **/

int is_draft(snac *user, const char *id)
/* returns true if this note is a draft */
{
    return object_user_cache_in(user, id, "draft");
}


void draft_del(snac *user, const char *id)
/* delete a message from the draft cache */
{
    object_user_cache_del(user, id, "draft");
}


void draft_add(snac *user, const char *id, const xs_dict *msg)
/* store the message as a draft */
{
    /* delete from the index, in case it was already there */
    draft_del(user, id);

    /* overwrite object */
    object_add_ow(id, msg);

    /* [re]add to the index */
    object_user_cache_add(user, id, "draft");
}


xs_list *draft_list(snac *user)
/* return the lists of drafts */
{
    return object_user_cache_list(user, "draft", XS_ALL, 1);
}


/** hiding **/

xs_str *_hidden_fn(snac *snac, const char *id)
{
    xs *md5 = xs_md5_hex(id, strlen(id));
    return xs_fmt("%s/hidden/%s", snac->basedir, md5);
}


void hide(snac *snac, const char *id)
/* hides a message tree */
{
    xs *fn = _hidden_fn(snac, id);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "%s\n", id);
        fclose(f);

        snac_debug(snac, 2, xs_fmt("hidden %s %s", id, fn));

        /* hide all the children */
        xs *chld = object_children(id);
        char *p;
        const char *v;

        p = chld;
        while (xs_list_iter(&p, &v)) {
            xs *co = NULL;

            /* resolve to get the id */
            if (valid_status(object_get_by_md5(v, &co))) {
                const char *id = xs_dict_get(co, "id");
                if (id != NULL)
                    hide(snac, id);
            }
        }
    }
}


int is_hidden(snac *snac, const char *id)
/* check is id is hidden */
{
    xs *fn = _hidden_fn(snac, id);

    return !!(mtime(fn) != 0.0);
}


int actor_add(const char *actor, const xs_dict *msg)
/* adds an actor */
{
    return object_add_ow(actor, msg);
}


int actor_get(const char *actor, xs_dict **data)
/* returns an already downloaded actor */
{
    int status = HTTP_STATUS_OK;
    xs_dict *d = NULL;

    if (xs_startswith(actor, srv_baseurl)) {
        /* it's a (possible) local user */
        xs *l = xs_split(actor, "/");
        const char *uid = xs_list_get(l, -1);
        snac user;

        if (!xs_is_null(uid) && user_open(&user, uid)) {
            if (data)
                *data = msg_actor(&user);

            user_free(&user);
            return HTTP_STATUS_OK;
        }
        else
            return HTTP_STATUS_NOT_FOUND;
    }

    /* read the object */
    if (!valid_status(status = object_get(actor, &d))) {
        d = xs_free(d);
        return status;
    }

    /* if the object is corrupted, discard it */
    if (xs_is_null(xs_dict_get(d, "id")) || xs_is_null(xs_dict_get(d, "type"))) {
        srv_debug(1, xs_fmt("corrupted actor object %s", actor));
        d = xs_free(d);
        return HTTP_STATUS_NOT_FOUND;
    }

    if (data)
        *data = d;
    else
        d = xs_free(d);

    xs *fn = _object_fn(actor);
    double max_time;

    /* maximum time for the actor data to be considered stale */
    max_time = 3600.0 * 36.0;

    if (mtime(fn) + max_time < (double) time(NULL)) {
        /* actor data exists but also stinks */
        status = HTTP_STATUS_RESET_CONTENT; /* "110: Response Is Stale" */
    }

    return status;
}


int actor_get_refresh(snac *user, const char *actor, xs_dict **data)
/* gets an actor and requests a refresh if it's stale */
{
    int status = actor_get(actor, data);

    if (status == HTTP_STATUS_RESET_CONTENT && user && !xs_startswith(actor, srv_baseurl))
        enqueue_actor_refresh(user, actor, 0);

    return status;
}


/** user limiting (announce blocks) **/

int limited(snac *user, const char *id, int cmd)
/* announce messages from a followed (0: check, 1: limit; 2: unlimit) */
{
    int ret = 0;
    xs *dir = xs_fmt("%s/limited", user->basedir);
    xs *md5 = xs_md5_hex(id, strlen(id));
    xs *fn  = xs_fmt("%s/%s", dir, md5);

    switch (cmd) {
    case 0: /** check **/
        ret = !!(mtime(fn) > 0.0);
        break;

    case 1: /** limit **/
        mkdirx(dir);

        if (mtime(fn) > 0.0)
            ret = -1;
        else {
            FILE *f;

            if ((f = fopen(fn, "w")) != NULL) {
                fprintf(f, "%s\n", id);
                fclose(f);
            }
            else
                ret = -2;
        }
        break;

    case 2: /** unlimit **/
        if (mtime(fn) > 0.0)
            ret = unlink(fn);
        else
            ret = -1;
        break;
    }

    return ret;
}


/** tag indexing **/

void tag_index(const char *id, const xs_dict *obj)
/* update the tag indexes for this object */
{
    const xs_list *tags = xs_dict_get(obj, "tag");
    xs *md5_id = xs_md5_hex(id, strlen(id));

    if (is_msg_public(obj) && xs_type(tags) == XSTYPE_LIST && xs_list_len(tags) > 0) {
        xs *g_tag_dir = xs_fmt("%s/tag", srv_basedir);

        mkdirx(g_tag_dir);

        const xs_dict *v;
        int ct = 0;
        while (xs_list_next(tags, &v, &ct)) {
            const char *type = xs_dict_get(v, "type");
            const char *name = xs_dict_get(v, "name");

            if (!xs_is_null(type) && !xs_is_null(name) && strcmp(type, "Hashtag") == 0) {
                while (*name == '#' || *name == '@')
                    name++;

                if (*name == '\0')
                    continue;

                name = xs_tolower_i((xs_str *)name);

                xs *md5_tag   = xs_md5_hex(name, strlen(name));
                xs *tag_dir   = xs_fmt("%s/%c%c", g_tag_dir, md5_tag[0], md5_tag[1]);
                mkdirx(tag_dir);

                xs *g_tag_idx = xs_fmt("%s/%s.idx", tag_dir, md5_tag);

                if (!index_in_md5(g_tag_idx, md5_id))
                    index_add_md5(g_tag_idx, md5_id);

                FILE *f;
                xs *g_tag_name = xs_replace(g_tag_idx, ".idx", ".tag");
                if ((f = fopen(g_tag_name, "w")) != NULL) {
                    fprintf(f, "%s\n", name);
                    fclose(f);
                }

                srv_debug(0, xs_fmt("tagged %s #%s (#%s)", id, name, md5_tag));
            }
        }
    }
}


xs_str *tag_fn(const char *tag)
{
    if (*tag == '#')
        tag++;

    xs *lw_tag = xs_tolower_i(xs_dup(tag));
    xs *md5    = xs_md5_hex(lw_tag, strlen(lw_tag));

    return xs_fmt("%s/tag/%c%c/%s.idx", srv_basedir, md5[0], md5[1], md5);
}


xs_list *tag_search(const char *tag, int skip, int show)
/* returns the list of posts tagged with tag */
{
    xs *idx = tag_fn(tag);

    return index_list_desc(idx, skip, show);
}


/** lists **/

xs_val *list_maint(snac *user, const char *list, int op)
/* list maintenance */
{
    xs_val *l = NULL;

    switch (op) {
    case 0: /** list of lists **/
        {
            FILE *f;
            xs *spec = xs_fmt("%s/list/" "*.id", user->basedir);
            xs *ls   = xs_glob(spec, 0, 0);
            int c = 0;
            const char *v;

            l = xs_list_new();

            while (xs_list_next(ls, &v, &c)) {
                if ((f = fopen(v, "r")) != NULL) {
                    xs *title = xs_readline(f);
                    fclose(f);

                    title = xs_strip_i(title);

                    xs *v2 = xs_replace(v, ".id", "");
                    xs *l2 = xs_split(v2, "/");

                    /* return [ list_id, list_title ] */
                    l = xs_list_append(l, xs_list_append(xs_list_new(), xs_list_get(l2, -1), title));
                }
            }
        }

        break;

    case 1: /** create new list (list is the name) **/
        {
            xs *lol = list_maint(user, NULL, 0);
            int c = 0;
            const xs_list *v;
            int add = 1;

            /* check if this list name already exists */
            while (xs_list_next(lol, &v, &c)) {
                if (strcmp(xs_list_get(v, 1), list) == 0) {
                    add = 0;

                    l = xs_dup(xs_list_get(v, 0));

                    break;
                }
            }

            if (add) {
                FILE *f;
                xs *dir = xs_fmt("%s/list/", user->basedir);

                l = xs_fmt("%010x", time(NULL));

                mkdirx(dir);

                xs *fn = xs_fmt("%s%s.id", dir, l);

                if ((f = fopen(fn, "w")) != NULL) {
                    fprintf(f, "%s\n", list);
                    fclose(f);
                }
            }
        }

        break;

    case 2: /** delete list (list is the id) **/
        {
            if (xs_is_hex(list)) {
                xs *fn = xs_fmt("%s/list/%s.id", user->basedir, list);
                unlink(fn);

                fn = xs_replace_i(fn, ".id", ".lst");
                unlink(fn);

                fn = xs_replace_i(fn, ".lst", ".idx");
                unlink(fn);

                fn = xs_str_cat(fn, ".bak");
                unlink(fn);
            }
        }

        break;

    case 3: /** get list name **/
        if (xs_is_hex(list)) {
            FILE *f;
            xs *fn = xs_fmt("%s/list/%s.id", user->basedir, list);

            if ((f = fopen(fn, "r")) != NULL) {
                l = xs_strip_i(xs_readline(f));
                fclose(f);
            }
        }

        break;
    }

    return l;
}


xs_str *list_timeline_fn(snac *user, const char *list)
{
    return xs_fmt("%s/list/%s.idx", user->basedir, list);
}


xs_list *list_timeline(snac *user, const char *list, int skip, int show)
/* returns the timeline of a list */
{
    xs_list *l = NULL;

    if (!xs_is_hex(list))
        return NULL;

    xs *fn = list_timeline_fn(user, list);

    if (mtime(fn) > 0.0)
        l = index_list_desc(fn, skip, show);
    else
        l = xs_list_new();

    return l;
}


xs_val *list_content(snac *user, const char *list, const char *actor_md5, int op)
/* list content management */
{
    xs_val *l = NULL;

    if (!xs_is_hex(list))
        return NULL;

    if (actor_md5 != NULL && !xs_is_hex(actor_md5))
        return NULL;

    xs *fn = xs_fmt("%s/list/%s.lst", user->basedir, list);

    switch (op) {
    case 0: /** list content **/
        l = index_list(fn, XS_ALL);

        break;

    case 1: /** append actor to list **/
        if (actor_md5 != NULL) {
            if (!index_in_md5(fn, actor_md5))
                index_add_md5(fn, actor_md5);
        }

        break;

    case 2: /** delete actor from list **/
        if (actor_md5 != NULL)
            index_del_md5(fn, actor_md5);

        break;

    default:
        srv_log(xs_fmt("ERROR: list_content: bad op %d", op));
        break;
    }

    return l;
}


void list_distribute(snac *user, const char *who, const xs_dict *post)
/* distributes the post to all appropriate lists */
{
    const char *id = xs_dict_get(post, "id");

    /* if who is not set, use the attributedTo in the message */
    if (xs_is_null(who))
        who = get_atto(post);

    if (xs_type(who) == XSTYPE_STRING && xs_type(id) == XSTYPE_STRING) {
        xs *a_md5 = xs_md5_hex(who, strlen(who));
        xs *i_md5 = xs_md5_hex(id, strlen(id));
        xs *spec  = xs_fmt("%s/list/" "*.lst", user->basedir);
        xs *ls    = xs_glob(spec, 0, 0);
        int c = 0;
        const char *v;

        while (xs_list_next(ls, &v, &c)) {
            /* is the actor in this list? */
            if (index_in_md5(v, a_md5)) {
                /* it is; add post md5 to its timeline */
                xs *idx = xs_replace(v, ".lst", ".idx");
                index_add_md5(idx, i_md5);

                snac_debug(user, 1, xs_fmt("listed post %s in %s", id, idx));
            }
        }
    }
}


/** static data **/

static int _load_raw_file(const char *fn, xs_val **data, int *size,
                        const char *inm, xs_str **etag)
/* loads a cached file */
{
    int status = HTTP_STATUS_NOT_FOUND;

    if (fn) {
        double tm = mtime(fn);

        if (tm > 0.0) {
            /* file exists; build the etag */
            xs *e = xs_fmt("W/\"snac-%.0lf\"", tm);

            /* if if-none-match is set, check if it's the same */
            if (!xs_is_null(inm) && strcmp(e, inm) == 0) {
                /* client has the newest version */
                status = HTTP_STATUS_NOT_MODIFIED;
            }
            else {
                /* newer or never downloaded; read the full file */
                FILE *f;

                if ((f = fopen(fn, "rb")) != NULL) {
                    *size = XS_ALL;
                    *data = xs_read(f, size);
                    fclose(f);

                    status = HTTP_STATUS_OK;
                }
            }

            /* if caller wants the etag, return it */
            if (etag != NULL)
                *etag = xs_dup(e);

            srv_debug(1, xs_fmt("_load_raw_file(): %s %d", fn, status));
        }
    }

    return status;
}


xs_str *_static_fn(snac *snac, const char *id)
/* gets the filename for a static file */
{
    if (strchr(id, '/'))
        return NULL;
    else
        return xs_fmt("%s/static/%s", snac->basedir, id);
}


int static_get(snac *snac, const char *id, xs_val **data, int *size,
                const char *inm, xs_str **etag)
/* returns static content */
{
    xs *fn = _static_fn(snac, id);

    return _load_raw_file(fn, data, size, inm, etag);
}


void static_put(snac *snac, const char *id, const char *data, int size)
/* writes status content */
{
    xs *fn = _static_fn(snac, id);
    FILE *f;

    if (fn && (f = fopen(fn, "wb")) != NULL) {
        fwrite(data, size, 1, f);
        fclose(f);
    }
}


void static_put_meta(snac *snac, const char *id, const char *str)
/* puts metadata (i.e. a media description string) to id */
{
    xs *fn = _static_fn(snac, id);

    if (fn) {
        fn = xs_str_cat(fn, ".txt");
        FILE *f;

        if ((f = fopen(fn, "w")) != NULL) {
            fprintf(f, "%s\n", str);
            fclose(f);
        }
    }
}


xs_str *static_get_meta(snac *snac, const char *id)
/* gets metadata from a media */
{
    xs *fn    = _static_fn(snac, id);
    xs_str *r = NULL;

    if (fn) {
        fn = xs_str_cat(fn, ".txt");
        FILE *f;

        if ((f = fopen(fn, "r")) != NULL) {
            r = xs_strip_i(xs_readline(f));
            fclose(f);
        }
    }
    else
        r = xs_str_new("");

    return r;
}


/** history **/

xs_str *_history_fn(snac *snac, const char *id)
/* gets the filename for the history */
{
    if (strchr(id, '/'))
        return NULL;
    else
        return xs_fmt("%s/history/%s", snac->basedir, id);
}


double history_mtime(snac *snac, const char *id)
{
    double t = 0.0;
    xs *fn = _history_fn(snac, id);

    if (fn != NULL)
        t = mtime(fn);

    return t;
}


void history_add(snac *snac, const char *id, const char *content, int size,
                    xs_str **etag)
/* adds something to the history */
{
    xs *fn = _history_fn(snac, id);
    FILE *f;

    if (fn && (f = fopen(fn, "w")) != NULL) {
        fwrite(content, size, 1, f);
        fclose(f);

        if (etag) {
            double tm = mtime(fn);
            *etag = xs_fmt("W/\"snac-%.0lf\"", tm);
        }
    }
}


int history_get(snac *snac, const char *id, xs_str **content, int *size,
                const char *inm, xs_str **etag)
{
    xs *fn = _history_fn(snac, id);

    return _load_raw_file(fn, content, size, inm, etag);
}


int history_del(snac *snac, const char *id)
{
    xs *fn = _history_fn(snac, id);

    if (fn)
        return unlink(fn);
    else
        return -1;
}


xs_list *history_list(snac *snac)
{
    xs *spec = xs_fmt("%s/history/" "*.html", snac->basedir);

    return xs_glob(spec, 1, 1);
}


void lastlog_write(snac *snac, const char *source)
/* writes the last time the user logged in */
{
    xs *fn = xs_fmt("%s/lastlog.txt", snac->basedir);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "%lf %s\n", ftime(), source);
        fclose(f);
    }
}


/** inbox collection **/

void inbox_add(const char *inbox)
/* collects a shared inbox */
{
    /* don't collect ourselves */
    if (xs_startswith(inbox, srv_baseurl))
        return;

    xs *md5 = xs_md5_hex(inbox, strlen(inbox));
    xs *fn  = xs_fmt("%s/inbox/%s", srv_basedir, md5);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "%s\n", inbox);
        fclose(f);
    }
}


void inbox_add_by_actor(const xs_dict *actor)
/* collects an actor's shared inbox, if it has one */
{
    const char *v;

    if (!xs_is_null(v = xs_dict_get(actor, "endpoints")) &&
        !xs_is_null(v = xs_dict_get(v, "sharedInbox"))) {
        /* only collect this inbox if its instance is not blocked */
        if (!is_instance_blocked(v))
            inbox_add(v);
    }
}


xs_list *inbox_list(void)
/* returns the collected inboxes as a list */
{
    xs_list *ibl = xs_list_new();
    xs *spec     = xs_fmt("%s/inbox/" "*", srv_basedir);
    xs *files    = xs_glob(spec, 0, 0);
    xs_list *p   = files;
    const xs_val *v;

    while (xs_list_iter(&p, &v)) {
        FILE *f;

        if ((f = fopen(v, "r")) != NULL) {
            xs *line = xs_readline(f);

            if (line) {
                line = xs_strip_i(line);
                ibl  = xs_list_append(ibl, line);
            }

            fclose(f);
        }
    }

    return ibl;
}


/** instance-wide operations **/

xs_str *_instance_block_fn(const char *instance)
{
    xs *s   = xs_replace(instance, "http:/" "/", "");
    xs *s1  = xs_replace(s, "https:/" "/", "");
    xs *l   = xs_split(s1, "/");
    const char *p = xs_list_get(l, 0);
    xs *md5 = xs_md5_hex(p, strlen(p));

    return xs_fmt("%s/block/%s", srv_basedir, md5);
}


int is_instance_blocked(const char *instance)
{
    xs *fn = _instance_block_fn(instance);

    return !!(mtime(fn) != 0.0);
}


int instance_block(const char *instance)
/* blocks a full instance */
{
    int ret;

    /* create the subdir */
    xs *dir = xs_fmt("%s/block/", srv_basedir);
    mkdirx(dir);

    if (!is_instance_blocked(instance)) {
        xs *fn = _instance_block_fn(instance);
        FILE *f;

        if ((f = fopen(fn, "w")) != NULL) {
            fprintf(f, "%s\n", instance);
            fclose(f);

            ret = 0;
        }
        else
            ret = -1;
    }
    else
        ret = -2;

    return ret;
}


int instance_unblock(const char *instance)
/* unblocks a full instance */
{
    int ret;

    if (is_instance_blocked(instance)) {
        xs *fn = _instance_block_fn(instance);
        ret = unlink(fn);
    }
    else
        ret = -2;

    return ret;
}


/** operations by content **/

int content_match(const char *file, const xs_dict *msg)
/* checks if a message's content matches any of the regexes in file */
/* file format: one regex per line */
{
    xs *fn = xs_fmt("%s/%s", srv_basedir, file);
    FILE *f;
    int r = 0;
    const char *v = xs_dict_get(msg, "content");

    if (xs_type(v) == XSTYPE_STRING && *v) {
        if ((f = fopen(fn, "r")) != NULL) {
            srv_debug(1, xs_fmt("content_match: loading regexes from %s", fn));

            /* massage content (strip HTML tags, etc.) */
            xs *c = xs_regex_replace(v, "<[^>]+>", " ");
            c = xs_regex_replace_i(c, " {2,}", " ");
            c = xs_tolower_i(c);

            while (!r && !feof(f)) {
                xs *rx = xs_strip_i(xs_readline(f));

                if (*rx && xs_regex_match(c, rx)) {
                    srv_debug(1, xs_fmt("content_match: match for '%s'", rx));
                    r = 1;
                }
            }

            fclose(f);
        }
    }

    return r;
}


xs_list *content_search(snac *user, const char *regex,
                int priv, int skip, int show, int max_secs, int *timeout)
/* returns a list of posts which content matches the regex */
{
    if (regex == NULL || *regex == '\0')
        return xs_list_new();

    xs *i_regex = xs_utf8_to_lower(regex);

    xs_set seen;

    xs_set_init(&seen);

    if (max_secs == 0)
        max_secs = 3;

    time_t t = time(NULL) + max_secs;
    *timeout = 0;

    /* iterate all timelines simultaneously */
    xs_list *tls[3] = {0};
    const char *md5s[3] = {0};
    int c[3] = {0};

    tls[0] = timeline_simple_list(user, "public", 0, XS_ALL);   /* public */
    tls[1] = timeline_instance_list(0, XS_ALL); /* instance */
    tls[2] = priv ? timeline_simple_list(user, "private", 0, XS_ALL) : xs_list_new(); /* private or none */

    /* first positioning */
    for (int n = 0; n < 3; n++)
        xs_list_next(tls[n], &md5s[n], &c[n]);

    show += skip;

    while (show > 0) {
        /* timeout? */
        if (time(NULL) > t) {
            *timeout = 1;
            break;
        }

        /* find the newest post */
        int newest = -1;
        double mtime = 0.0;

        for (int n = 0; n < 3; n++) {
            if (md5s[n] != NULL) {
                xs *fn = _object_fn_by_md5(md5s[n], "content_search");
                double mt = mtime(fn);

                if (mt > mtime) {
                    newest = n;
                    mtime = mt;
                }
            }
        }

        if (newest == -1)
            break;

        const char *md5 = md5s[newest];

        /* advance the chosen timeline */
        if (!xs_list_next(tls[newest], &md5s[newest], &c[newest]))
            md5s[newest] = NULL;

        xs *post = NULL;

        if (!valid_status(object_get_by_md5(md5, &post)))
            continue;

        if (!xs_match(xs_dict_get_def(post, "type", "-"), POSTLIKE_OBJECT_TYPE))
            continue;

        const char *id = xs_dict_get(post, "id");

        if (id == NULL || is_hidden(user, id))
            continue;

        xs *c = xs_str_new(NULL);
        const char *content = xs_dict_get(post, "content");
        const char *name    = xs_dict_get(post, "name");

        if (!xs_is_null(content))
            c = xs_str_cat(c, content);
        if (!xs_is_null(name))
            c = xs_str_cat(c, " ", name);

        /* add alt-texts from attachments */
        const xs_list *atts = xs_dict_get(post, "attachment");
        int tc = 0;
        const xs_dict *att;

        while (xs_list_next(atts, &att, &tc)) {
            const char *name = xs_dict_get(att, "name");

            if (name != NULL)
                c = xs_str_cat(c, " ", name);
        }

        /* strip HTML */
        c = xs_regex_replace_i(c, "<[^>]+>", " ");
        c = xs_regex_replace_i(c, " {2,}", " ");

        /* convert to lowercase */
        xs *lc = xs_utf8_to_lower(c);

        /* apply regex */
        if (xs_regex_match(lc, i_regex)) {
            if (xs_set_add(&seen, md5) == 1)
            show--;
        }
    }

    xs_list *r = xs_set_result(&seen);

    if (skip) {
        /* BAD */
        while (skip--) {
            r = xs_list_del(r, 0);
        }
    }

    xs_free(tls[0]);
    xs_free(tls[1]);
    xs_free(tls[2]);

    return r;
}


/** notifications **/

xs_str *notify_check_time(snac *snac, int reset)
/* gets or resets the latest notification check time */
{
    xs_str *t = NULL;
    xs *fn    = xs_fmt("%s/notifydate.txt", snac->basedir);
    FILE *f;

    if (reset) {
        if ((f = fopen(fn, "w")) != NULL) {
            t = tid(0);
            fprintf(f, "%s\n", t);
            fclose(f);
        }
    }
    else {
        if ((f = fopen(fn, "r")) != NULL) {
            t = xs_readline(f);
            fclose(f);
        }
        else
            /* never set before */
            t = xs_fmt("%16.6f", 0.0);
    }

    return t;
}


void notify_add(snac *snac, const char *type, const char *utype,
                const char *actor, const char *objid, const xs_dict *msg)
/* adds a new notification */
{
    xs *ntid = tid(0);
    xs *fn   = xs_fmt("%s/notify/", snac->basedir);
    xs *date = xs_str_utctime(0, ISO_DATE_SPEC);
    FILE *f;

    /* create the directory */
    mkdirx(fn);
    fn = xs_str_cat(fn, ntid);
    fn = xs_str_cat(fn, ".json");

    xs *noti = xs_dict_new();

    noti = xs_dict_append(noti, "id",    ntid);
    noti = xs_dict_append(noti, "type",  type);
    noti = xs_dict_append(noti, "utype", utype);
    noti = xs_dict_append(noti, "actor", actor);
    noti = xs_dict_append(noti, "date",  date);
    noti = xs_dict_append(noti, "msg",   msg);

    if (!xs_is_null(objid))
        noti = xs_dict_append(noti, "objid", objid);

    if ((f = fopen(fn, "w")) != NULL) {
        xs_json_dump(noti, 4, f);
        fclose(f);
    }

    /* add it to the index if it already exists */
    xs *idx = xs_fmt("%s/notify.idx", snac->basedir);

    if (mtime(idx) != 0.0) {
        pthread_mutex_lock(&data_mutex);

        if ((f = fopen(idx, "a")) != NULL) {
            fprintf(f, "%-32s\n", ntid);
            fclose(f);
        }

        pthread_mutex_unlock(&data_mutex);
    }
}


xs_dict *notify_get(snac *snac, const char *id)
/* gets a notification */
{
    /* base file */
    xs *fn = xs_fmt("%s/notify/%s", snac->basedir, id);

    /* strip spaces and add extension */
    fn = xs_strip_i(fn);
    fn = xs_str_cat(fn, ".json");

    FILE *f;
    xs_dict *out = NULL;

    if ((f = fopen(fn, "r")) != NULL) {
        out = xs_json_load(f);
        fclose(f);
    }

    return out;
}


xs_list *notify_list(snac *snac, int skip, int show)
/* returns a list of notification ids */
{
    xs *idx = xs_fmt("%s/notify.idx", snac->basedir);

    if (mtime(idx) == 0.0) {
        /* create the index from scratch */
        FILE *f;

        pthread_mutex_lock(&data_mutex);

        if ((f = fopen(idx, "w")) != NULL) {
            xs *spec = xs_fmt("%s/notify/" "*.json", snac->basedir);
            xs *lst  = xs_glob(spec, 1, 0);
            xs_list *p = lst;
            const char *v;

            while (xs_list_iter(&p, &v)) {
                char *p = strrchr(v, '.');
                if (p) {
                    *p = '\0';
                    fprintf(f, "%-32s\n", v);
                }
            }

            fclose(f);
        }

        pthread_mutex_unlock(&data_mutex);
    }

    return index_list_desc(idx, skip, show);
}


int notify_new_num(snac *snac)
/* counts the number of new notifications */
{
    xs *t = notify_check_time(snac, 0);
    xs *lst = notify_list(snac, 0, XS_ALL);
    int cnt = 0;

    xs_list *p = lst;
    const xs_str *v;

    while (xs_list_iter(&p, &v)) {
        xs *id = xs_strip_i(xs_dup(v));

        /* old? count no more */
        if (strcmp(id, t) < 0)
            break;

        cnt++;
    }

    return cnt;
}


void notify_clear(snac *snac)
/* clears all notifications */
{
    xs *spec   = xs_fmt("%s/notify/" "*", snac->basedir);
    xs *lst    = xs_glob(spec, 0, 0);
    xs_list *p = lst;
    const xs_str *v;

    while (xs_list_iter(&p, &v))
        unlink(v);

    xs *idx = xs_fmt("%s/notify.idx", snac->basedir);

    if (mtime(idx) != 0.0) {
        pthread_mutex_lock(&data_mutex);
        truncate(idx, 0);
        pthread_mutex_unlock(&data_mutex);
    }
}


/** the queue **/

static xs_dict *_enqueue_put(const char *fn, xs_dict *msg)
/* writes safely to the queue */
{
    xs *tfn = xs_fmt("%s.tmp", fn);
    FILE *f;

    if ((f = fopen(tfn, "w")) != NULL) {
        xs_json_dump(msg, 4, f);
        fclose(f);

        rename(tfn, fn);
    }

    return msg;
}


static xs_dict *_new_qmsg(const char *type, const xs_val *msg, int retries)
/* creates a queue message */
{
    int qrt  = xs_number_get(xs_dict_get(srv_config, "queue_retry_minutes"));
    xs *ntid = tid(retries * 60 * qrt);
    xs *rn   = xs_number_new(retries);

    xs_dict *qmsg = xs_dict_new();

    qmsg = xs_dict_append(qmsg, "type",    type);
    qmsg = xs_dict_append(qmsg, "message", msg);
    qmsg = xs_dict_append(qmsg, "retries", rn);
    qmsg = xs_dict_append(qmsg, "ntid",    ntid);

    return qmsg;
}


void enqueue_input(snac *snac, const xs_dict *msg, const xs_dict *req, int retries)
/* enqueues an input message */
{
    xs *qmsg   = _new_qmsg("input", msg, retries);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);

    qmsg = xs_dict_append(qmsg, "req", req);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(snac, 1, xs_fmt("enqueue_input %s", xs_dict_get(msg, "id")));
}


void enqueue_shared_input(const xs_dict *msg, const xs_dict *req, int retries)
/* enqueues an input message from the shared input */
{
    xs *qmsg   = _new_qmsg("input", msg, retries);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    qmsg = xs_dict_append(qmsg, "req", req);

    qmsg = _enqueue_put(fn, qmsg);

    srv_debug(1, xs_fmt("enqueue_shared_input %s", xs_dict_get(msg, "id")));
}


void enqueue_output_raw(const char *keyid, const char *seckey,
                        const xs_dict *msg, const xs_str *inbox,
                        int retries, int p_status)
/* enqueues an output message to an inbox */
{
    xs *qmsg   = _new_qmsg("output", msg, retries);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    xs *ns = xs_number_new(p_status);
    qmsg = xs_dict_append(qmsg, "p_status", ns);

    qmsg = xs_dict_append(qmsg, "inbox",  inbox);
    qmsg = xs_dict_append(qmsg, "keyid",  keyid);
    qmsg = xs_dict_append(qmsg, "seckey", seckey);

    /* if it's to be sent right now, bypass the disk queue and post the job */
    if (retries == 0 && p_state != NULL)
        job_post(qmsg, 0);
    else {
        qmsg = _enqueue_put(fn, qmsg);
        srv_debug(1, xs_fmt("enqueue_output %s %s %d", inbox, fn, retries));
    }
}


void enqueue_output(snac *snac, const xs_dict *msg,
                    const xs_str *inbox, int retries, int p_status)
/* enqueues an output message to an inbox */
{
    if (xs_startswith(inbox, snac->actor)) {
        snac_debug(snac, 1, xs_str_new("refusing enqueue to myself"));
        return;
    }

    const char *seckey = xs_dict_get(snac->key, "secret");

    enqueue_output_raw(snac->actor, seckey, msg, inbox, retries, p_status);
}


void enqueue_output_by_actor(snac *snac, const xs_dict *msg,
                            const xs_str *actor, int retries)
/* enqueues an output message for an actor */
{
    xs *inbox = get_actor_inbox(actor, 1);

    if (!xs_is_null(inbox))
        enqueue_output(snac, msg, inbox, retries, 0);
    else
        snac_log(snac, xs_fmt("enqueue_output_by_actor cannot get inbox %s", actor));
}


void enqueue_email(const xs_str *msg, int retries)
/* enqueues an email message to be sent */
{
    xs *qmsg   = _new_qmsg("email", msg, retries);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    qmsg = _enqueue_put(fn, qmsg);

    srv_debug(1, xs_fmt("enqueue_email %d", retries));
}


void enqueue_telegram(const xs_str *msg, const char *bot, const char *chat_id)
/* enqueues a message to be sent via Telegram */
{
    xs *qmsg   = _new_qmsg("telegram", msg, 0);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    qmsg = xs_dict_append(qmsg, "bot",      bot);
    qmsg = xs_dict_append(qmsg, "chat_id",  chat_id);

    qmsg = _enqueue_put(fn, qmsg);

    srv_debug(1, xs_fmt("enqueue_telegram %s %s", bot, chat_id));
}

void enqueue_ntfy(const xs_str *msg, const char *ntfy_server, const char *ntfy_token)
/* enqueues a message to be sent via ntfy */
{
    xs *qmsg   = _new_qmsg("ntfy", msg, 0);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", srv_basedir, ntid);

    qmsg = xs_dict_append(qmsg, "ntfy_server", ntfy_server);
    qmsg = xs_dict_append(qmsg, "ntfy_token",  ntfy_token);


    qmsg = _enqueue_put(fn, qmsg);

    srv_debug(1, xs_fmt("enqueue_ntfy %s %s", ntfy_server, ntfy_token));
}

void enqueue_message(snac *snac, const xs_dict *msg)
/* enqueues an output message */
{
    xs *qmsg   = _new_qmsg("message", msg, 0);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", snac->basedir, ntid);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(snac, 0, xs_fmt("enqueue_message %s", xs_dict_get(msg, "id")));
}


void enqueue_close_question(snac *user, const char *id, int end_secs)
/* enqueues the closing of a question */
{
    xs *qmsg = _new_qmsg("close_question", id, 0);
    xs *ntid = tid(end_secs);
    xs *fn   = xs_fmt("%s/queue/%s.json", user->basedir, ntid);

    qmsg = xs_dict_set(qmsg, "ntid", ntid);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(user, 0, xs_fmt("enqueue_close_question %s", id));
}


void enqueue_object_request(snac *user, const char *id, int forward_secs)
/* enqueues the request of an object in the future */
{
    xs *qmsg = _new_qmsg("object_request", id, 0);
    xs *ntid = tid(forward_secs);
    xs *fn   = xs_fmt("%s/queue/%s.json", user->basedir, ntid);

    qmsg = xs_dict_set(qmsg, "ntid", ntid);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(user, 0, xs_fmt("enqueue_object_request %s %d", id, forward_secs));
}


void enqueue_verify_links(snac *user)
/* enqueues a link verification */
{
    xs *qmsg   = _new_qmsg("verify_links", "", 0);
    const char *ntid = xs_dict_get(qmsg, "ntid");
    xs *fn     = xs_fmt("%s/queue/%s.json", user->basedir, ntid);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(user, 1, xs_fmt("enqueue_verify_links %s", user->actor));
}


void enqueue_actor_refresh(snac *user, const char *actor, int forward_secs)
/* enqueues an actor refresh */
{
    xs *qmsg = _new_qmsg("actor_refresh", "", 0);
    xs *ntid = tid(forward_secs);
    xs *fn   = xs_fmt("%s/queue/%s.json", user->basedir, ntid);

    qmsg = xs_dict_set(qmsg, "ntid", ntid);
    qmsg = xs_dict_append(qmsg, "actor", actor);

    qmsg = _enqueue_put(fn, qmsg);

    snac_debug(user, 1, xs_fmt("enqueue_actor_refresh %s", actor));
}


int was_question_voted(snac *user, const char *id)
/* returns true if the user voted in this poll */
{
    xs *children = object_children(id);
    int voted = 0;
    xs_list *p;
    const xs_str *md5;

    p = children;
    while (xs_list_iter(&p, &md5)) {
        xs *obj = NULL;

        if (valid_status(object_get_by_md5(md5, &obj))) {
            const char *atto = get_atto(obj);
            if (atto && strcmp(atto, user->actor) == 0 &&
                !xs_is_null(xs_dict_get(obj, "name"))) {
                voted = 1;
                break;
            }
        }
    }

    return voted;
}


xs_list *user_queue(snac *snac)
/* returns a list with filenames that can be dequeued */
{
    xs *spec      = xs_fmt("%s/queue/" "*.json", snac->basedir);
    xs_list *list = xs_list_new();
    time_t t      = time(NULL);
    xs_list *p;
    const xs_val *v;

    xs *fns = xs_glob(spec, 0, 0);

    p = fns;
    while (xs_list_iter(&p, &v)) {
        /* get the retry time from the basename */
        char *bn  = strrchr(v, '/');
        time_t t2 = atol(bn + 1);

        if (t2 > t)
            snac_debug(snac, 2, xs_fmt("user_queue not yet time for %s [%ld]", v, t));
        else {
            list = xs_list_append(list, v);
            snac_debug(snac, 2, xs_fmt("user_queue ready for %s", v));
        }
    }

    return list;
}


xs_list *queue(void)
/* returns a list with filenames that can be dequeued */
{
    xs *spec      = xs_fmt("%s/queue/" "*.json", srv_basedir);
    xs_list *list = xs_list_new();
    time_t t      = time(NULL);
    xs_list *p;
    const xs_val *v;

    xs *fns = xs_glob(spec, 0, 0);

    p = fns;
    while (xs_list_iter(&p, &v)) {
        /* get the retry time from the basename */
        char *bn  = strrchr(v, '/');
        time_t t2 = atol(bn + 1);

        if (t2 > t)
            srv_debug(2, xs_fmt("queue not yet time for %s [%ld]", v, t));
        else {
            list = xs_list_append(list, v);
            srv_debug(2, xs_fmt("queue ready for %s", v));
        }
    }

    return list;
}


xs_dict *queue_get(const char *fn)
/* gets a file from a queue */
{
    FILE *f;
    xs_dict *obj = NULL;

    if ((f = fopen(fn, "r")) != NULL) {
        obj = xs_json_load(f);
        fclose(f);
    }

    return obj;
}


xs_dict *dequeue(const char *fn)
/* dequeues a message */
{
    xs_dict *obj = queue_get(fn);

    unlink(fn);

    return obj;
}


/** the purge **/

static int _purge_file(const char *fn, time_t mt)
/* purge fn if it's older than days */
{
    int ret = 0;

    if (mtime(fn) < mt) {
        /* older than the minimum time: delete it */
        unlink(fn);
        srv_debug(2, xs_fmt("purged %s", fn));
        ret = 1;
    }

    return ret;
}


static void _purge_dir(const char *dir, int days)
/* purges all files in a directory older than days */
{
    int cnt = 0;

    if (days) {
        time_t mt = time(NULL) - days * 24 * 3600;
        xs *spec  = xs_fmt("%s/" "*", dir);
        xs *list  = xs_glob(spec, 0, 0);
        xs_list *p;
        const xs_str *v;

        p = list;
        while (xs_list_iter(&p, &v))
            cnt += _purge_file(v, mt);

        srv_debug(1, xs_fmt("purge: %s %d", dir, cnt));
    }
}


static void _purge_user_subdir(snac *snac, const char *subdir, int days)
/* purges all files in a user subdir older than days */
{
    xs *u_subdir = xs_fmt("%s/%s", snac->basedir, subdir);

    _purge_dir(u_subdir, days);
}


void purge_server(void)
/* purge global server data */
{
    xs *spec = xs_fmt("%s/object/??", srv_basedir);
    xs *dirs = xs_glob(spec, 0, 0);
    xs_list *p;
    const xs_str *v;
    int cnt = 0;
    int icnt = 0;

    time_t mt = time(NULL) - 7 * 24 * 3600;

    p = dirs;
    while (xs_list_iter(&p, &v)) {
        xs_list *p2;
        const xs_str *v2;

        {
            xs *spec2 = xs_fmt("%s/" "*.json", v);
            xs *files = xs_glob(spec2, 0, 0);

            p2 = files;
            while (xs_list_iter(&p2, &v2)) {
                int n_link;

                /* old and with no hard links? */
                if (mtime_nl(v2, &n_link) < mt && n_link < 2) {
                    xs *s1    = xs_replace(v2, ".json", "");
                    xs *l     = xs_split(s1, "/");
                    const char *md5 = xs_list_get(l, -1);

                    object_del_by_md5(md5);
                    cnt++;
                }
            }
        }

        {
            /* look for stray indexes */
            xs *speci = xs_fmt("%s/" "*_?.idx", v);
            xs *idxfs = xs_glob(speci, 0, 0);

            p2 = idxfs;
            while (xs_list_iter(&p2, &v2)) {
                /* old enough to consider? */
                if (mtime(v2) < mt) {
                    /* check if the indexed object is here */
                    xs *o = xs_dup(v2);
                    char *ext = strchr(o, '_');

                    if (ext) {
                        *ext = '\0';
                        o = xs_str_cat(o, ".json");

                        if (mtime(o) == 0.0) {
                            /* delete */
                            unlink(v2);
                            srv_debug(1, xs_fmt("purged %s", v2));
                            icnt++;
                        }
                    }
                }
            }

            /* delete index backups */
            xs *specb = xs_fmt("%s/" "*.bak", v);
            xs *bakfs = xs_glob(specb, 0, 0);

            p2 = bakfs;
            while (xs_list_iter(&p2, &v2)) {
                unlink(v2);
                srv_debug(1, xs_fmt("purged %s", v2));
            }
        }
    }

    /* purge collected inboxes */
    xs *ib_dir = xs_fmt("%s/inbox", srv_basedir);
    _purge_dir(ib_dir, 7);

    /* purge the instance timeline */
    xs *itl_fn = xs_fmt("%s/public.idx", srv_basedir);
    int itl_gc = index_gc(itl_fn);

    /* purge tag indexes */
    xs *tag_spec = xs_fmt("%s/tag/??", srv_basedir);
    xs *tag_dirs = xs_glob(tag_spec, 0, 0);
    p = tag_dirs;

    int tag_gc = 0;
    while (xs_list_iter(&p, &v)) {
        xs *spec2 = xs_fmt("%s/" "*.idx", v);
        xs *files = xs_glob(spec2, 0, 0);
        xs_list *p2;
        const xs_str *v2;

        p2 = files;
        while (xs_list_iter(&p2, &v2)) {
            tag_gc += index_gc(v2);
            xs *bak = xs_fmt("%s.bak", v2);
            unlink(bak);

            if (index_len(v2) == 0) {
                /* there are no longer any entry with this tag;
                   purge it completely */
                unlink(v2);
                xs *dottag = xs_replace(v2, ".idx", ".tag");
                unlink(dottag);
            }
        }
    }

    srv_debug(1, xs_fmt("purge: global "
            "(obj: %d, idx: %d, itl: %d, tag: %d)", cnt, icnt, itl_gc, tag_gc));
}


void purge_user(snac *snac)
/* do the purge for this user */
{
    int priv_days, pub_days, user_days = 0;
    const char *v;
    int n;

    priv_days = xs_number_get(xs_dict_get(srv_config, "timeline_purge_days"));
    pub_days  = xs_number_get(xs_dict_get(srv_config, "local_purge_days"));

    if ((v = xs_dict_get(snac->config_o, "purge_days")) != NULL ||
        (v = xs_dict_get(snac->config, "purge_days")) != NULL)
        user_days = xs_number_get(v);

    if (user_days) {
        /* override admin settings only if they are lesser */
        if (priv_days == 0 || user_days < priv_days)
            priv_days = user_days;

        if (pub_days == 0 || user_days < pub_days)
            pub_days = user_days;
    }

    _purge_user_subdir(snac, "hidden",  priv_days);
    _purge_user_subdir(snac, "private", priv_days);

    _purge_user_subdir(snac, "public",  pub_days);

    const char *idxs[] = { "followers.idx", "private.idx", "public.idx",
                           "pinned.idx", "bookmark.idx", "draft.idx", NULL };

    for (n = 0; idxs[n]; n++) {
        xs *idx = xs_fmt("%s/%s", snac->basedir, idxs[n]);
        int gc = index_gc(idx);
        srv_debug(1, xs_fmt("purge: %s %d", idx, gc));
    }

    /* purge lists */
    {
        xs *spec = xs_fmt("%s/list/" "*.idx", snac->basedir);
        xs *lol  = xs_glob(spec, 0, 0);
        int c = 0;
        const char *v;

        while (xs_list_next(lol, &v, &c)) {
            int gc = index_gc(v);
            srv_debug(1, xs_fmt("purge: %s %d", v, gc));
        }
    }

    /* unrelated to purging, but it's a janitorial process, so what the hell */
    verify_links(snac);
}


void purge_all(void)
/* purge all users */
{
    snac snac;
    xs *list = user_list();
    char *p;
    const char *uid;

    p = list;
    while (xs_list_iter(&p, &uid)) {
        if (user_open(&snac, uid)) {
            purge_user(&snac);
            user_free(&snac);
        }
    }

    purge_server();

#ifndef NO_MASTODON_API
    mastoapi_purge();
#endif
}


/** archive **/

void srv_archive(const char *direction, const char *url, xs_dict *req,
                 const char *payload, int p_size,
                 int status, xs_dict *headers,
                 const char *body, int b_size)
/* archives a connection */
{
    /* obsessive archiving */
    xs *date = tid(0);
    xs *dir  = xs_fmt("%s/archive/%s_%s", srv_basedir, date, direction);
    FILE *f;

    if (mkdirx(dir) != -1) {
        xs *meta_fn = xs_fmt("%s/_META", dir);

        if ((f = fopen(meta_fn, "w")) != NULL) {
            xs *j1 = xs_json_dumps(req, 4);
            xs *j2 = xs_json_dumps(headers, 4);

            fprintf(f, "dir: %s\n", direction);

            if (url)
                fprintf(f, "url: %s\n", url);

            fprintf(f, "req: %s\n", j1);
            fprintf(f, "p_size: %d\n", p_size);
            fprintf(f, "status: %d\n", status);
            fprintf(f, "response: %s\n", j2);
            fprintf(f, "b_size: %d\n", b_size);
            fclose(f);
        }

        if (p_size && payload) {
            xs *payload_fn = NULL;
            xs *payload_fn_raw = NULL;
            const char *v = xs_dict_get(req, "content-type");

            if (v && xs_str_in(v, "json") != -1) {
                payload_fn = xs_fmt("%s/payload.json", dir);

                if ((f = fopen(payload_fn, "w")) != NULL) {
                    xs *v1 = xs_json_loads(payload);
                    xs *j1 = NULL;

                    if (v1 != NULL)
                        j1 = xs_json_dumps(v1, 4);

                    if (j1 != NULL)
                        fwrite(j1, strlen(j1), 1, f);
                    else
                        fwrite(payload, p_size, 1, f);

                    fclose(f);
                }
            }

            payload_fn_raw = xs_fmt("%s/payload", dir);

            if ((f = fopen(payload_fn_raw, "w")) != NULL) {
                fwrite(payload, p_size, 1, f);
                fclose(f);
            }
        }

        if (b_size && body) {
            xs *body_fn = NULL;
            const char *v = xs_dict_get(headers, "content-type");

            if (v && xs_str_in(v, "json") != -1) {
                body_fn = xs_fmt("%s/body.json", dir);

                if ((f = fopen(body_fn, "w")) != NULL) {
                    xs *v1 = xs_json_loads(body);
                    xs *j1 = NULL;

                    if (v1 != NULL)
                        j1 = xs_json_dumps(v1, 4);

                    if (j1 != NULL)
                        fwrite(j1, strlen(j1), 1, f);
                    else
                        fwrite(body, b_size, 1, f);

                    fclose(f);
                }
            }
            else {
                body_fn = xs_fmt("%s/body", dir);

                if ((f = fopen(body_fn, "w")) != NULL) {
                    fwrite(body, b_size, 1, f);
                    fclose(f);
                }
            }
        }
    }
}


void srv_archive_error(const char *prefix, const xs_str *err,
                       const xs_dict *req, const xs_val *data)
/* archives an error */
{
    xs *ntid = tid(0);
    xs *fn   = xs_fmt("%s/error/%s_%s", srv_basedir, ntid, prefix);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        fprintf(f, "Error: %s\n", err);

        if (req) {
            fprintf(f, "Request headers:\n");

            xs_json_dump(req, 4, f);

            fprintf(f, "\n");
        }

        if (data) {
            fprintf(f, "Data:\n");

            if (xs_type(data) == XSTYPE_LIST || xs_type(data) == XSTYPE_DICT) {
                xs_json_dump(data, 4, f);
            }
            else
                fprintf(f, "%s", data);

            fprintf(f, "\n");
        }

        fclose(f);
    }
}


void srv_archive_qitem(const char *prefix, xs_dict *q_item)
/* archives a q_item in the error folder */
{
    xs *ntid = tid(0);
    xs *fn   = xs_fmt("%s/error/%s_qitem_%s", srv_basedir, ntid, prefix);
    FILE *f;

    if ((f = fopen(fn, "w")) != NULL) {
        xs_json_dump(q_item, 4, f);
        fclose(f);
    }
}


t_announcement *announcement(const double after)
/* returns announcement text or NULL if none exists or it is olde than "after" */
{
    static const long int MAX_SIZE = 2048;
    static t_announcement a = {
        .text = NULL,
        .timestamp = 0.0,
    };
    static xs_str *fn = NULL;
    if (fn == NULL)
        fn = xs_fmt("%s/announcement.txt", srv_basedir);

    const double ts = mtime(fn);

    /* file does not exist or other than what was requested */
    if (ts == 0.0 || ts <= after)
        return NULL;

    /* nothing changed, just return the current announcement */
    if (a.text != NULL && ts <= a.timestamp)
        return &a;

    /* read and store new announcement */
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        fseek (f, 0, SEEK_END);
        const long int length = ftell(f);

        if (length > MAX_SIZE) {
            /* this is probably unintentional */
            srv_log(xs_fmt("announcement.txt too big: %ld bytes, max is %ld, ignoring.", length, MAX_SIZE));
        }
        else
        if (length > 0) {
            fseek (f, 0, SEEK_SET);
            char *buffer = malloc(length + 1);
            if (buffer) {
                fread(buffer, 1, length, f);
                buffer[length] = '\0';

                free(a.text);
                a.text = buffer;
                a.timestamp = ts;
            }
            else {
                srv_log("Error allocating memory for announcement");
            }
        }
        else {
            /* an empty file means no announcement */
            free(a.text);
            a.text = NULL;
            a.timestamp = 0.0;
        }

        fclose (f);
    }

    if (a.text != NULL)
        return &a;

    return NULL;
}


xs_str *make_url(const char *href, const char *proxy, int by_token)
/* makes an URL, possibly including proxying */
{
    xs_str *url = NULL;

    if (proxy && !xs_startswith(href, srv_baseurl)) {
        xs *p = NULL;

        if (by_token) {
            xs *tks = xs_fmt("%s:%s", srv_proxy_token_seed, proxy);
            xs *tk = xs_md5_hex(tks, strlen(tks));

            p = xs_fmt("%s/y/%s/", proxy, tk);
        }
        else
            p = xs_fmt("%s/x/", proxy);

        url = xs_replace(href, "https:/" "/", p);
    }
    else
        url = xs_dup(href);

    return url;
}
