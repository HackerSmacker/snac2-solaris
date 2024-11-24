/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_UNIX_SOCKET_H

#define _XS_UNIX_SOCKET_H

 int xs_unix_socket_server(const char *path, const char *grp);
 int xs_unix_socket_connect(const char *path);


#ifdef XS_IMPLEMENTATION

#include <sys/stat.h>
#include <sys/un.h>
#include <grp.h>

int xs_unix_socket_server(const char *path, const char *grp)
/* opens a unix-type server socket */
{
    int rs = -1;

    if ((rs = socket(AF_UNIX, SOCK_STREAM, 0)) != -1) {
        struct sockaddr_un su = {0};
        mode_t mode = 0666;

        su.sun_family = AF_UNIX;
        strncpy(su.sun_path, path, sizeof(su.sun_path));

        unlink(path);

        if (bind(rs, (struct sockaddr *)&su, sizeof(su)) == -1) {
            close(rs);
            return -1;
        }

        listen(rs, SOMAXCONN);

        if (grp != NULL) {
            struct group *g = NULL;

            /* if there is a group name, get its gid_t */
            g = getgrnam(grp);

            if (g != NULL && chown(path, -1, g->gr_gid) != -1)
                mode = 0660;
        }

        chmod(path, mode);
    }

    return rs;
}


int xs_unix_socket_connect(const char *path)
/* connects to a unix-type socket */
{
    int d = -1;

    if ((d = socket(AF_UNIX, SOCK_STREAM, 0)) != -1) {
        struct sockaddr_un su = {0};

        su.sun_family = AF_UNIX;
        strncpy(su.sun_path, path, sizeof(su.sun_path));

        if (connect(d, (struct sockaddr *)&su, sizeof(su)) == -1) {
            close(d);
            d = -1;
        }
    }

    return d;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_UNIX_SOCKET_H */
