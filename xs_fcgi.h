/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

/*
    This is an intentionally-dead-simple FastCGI implementation;
    only FCGI_RESPONDER type with *no* FCGI_KEEP_CON flag is supported.
    This means only one simultaneous connection and no multiplexing.
    It seems it's enough for nginx and OpenBSD's httpd, so here it goes.
    Almost fully compatible with xs_httpd.h
*/

#ifndef _XS_FCGI_H

#define _XS_FCGI_H

 xs_dict *xs_fcgi_request(FILE *f, xs_str **payload, int *p_size, int *id);
 void xs_fcgi_response(FILE *f, int status, xs_dict *headers, xs_str *body, int b_size, int id);


#ifdef XS_IMPLEMENTATION

struct fcgi_record_header {
    unsigned char  version;
    unsigned char  type;
    unsigned short id;
    unsigned short content_len;
    unsigned char  padding_len;
    unsigned char  reserved;
} __attribute__((packed));

/* version */

#define FCGI_VERSION_1           1

/* types */

#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE (FCGI_UNKNOWN_TYPE)

struct fcgi_begin_request {
    unsigned short role;
    unsigned char  flags;
    unsigned char  reserved[5];
} __attribute__((packed));

/* roles */

#define FCGI_RESPONDER  1
#define FCGI_AUTHORIZER 2
#define FCGI_FILTER     3

/* flags */

#define FCGI_KEEP_CONN  1

struct fcgi_end_request {
    unsigned int app_status;
    unsigned char protocol_status;
    unsigned char reserved[3];
} __attribute__((packed));

/* protocol statuses */

#define FCGI_REQUEST_COMPLETE 0
#define FCGI_CANT_MPX_CONN    1
#define FCGI_OVERLOADED       2
#define FCGI_UNKNOWN_ROLE     3


xs_dict *xs_fcgi_request(FILE *f, xs_str **payload, int *p_size, int *fcgi_id)
/* keeps receiving FCGI packets until a complete request is finished */
{
    unsigned char p_buf[100000];
    struct fcgi_record_header hdr;
    struct fcgi_begin_request *breq = (struct fcgi_begin_request *)&p_buf;
    unsigned char *buf = NULL;
    int b_size = 0;
    xs_dict *req = NULL;
    unsigned char p_status = FCGI_REQUEST_COMPLETE;
    xs *q_vars = NULL;
    xs *p_vars = NULL;

    *fcgi_id = -1;

    for (;;) {
        int sz, psz;

        /* read the packet header */
        if (fread(&hdr, sizeof(hdr), 1, f) != 1)
            break;

        /* read the packet body */
        if ((psz = ntohs(hdr.content_len)) > 0) {
            if ((sz = fread(p_buf, 1, psz, f)) != psz)
                break;
        }

        /* read (and drop) the padding */
        if (hdr.padding_len > 0)
            fread(p_buf + sz, 1, hdr.padding_len, f);

        switch (hdr.type) {
        case FCGI_BEGIN_REQUEST:
            /* fail on unsupported roles */
            if (ntohs(breq->role) != FCGI_RESPONDER) {
                p_status = FCGI_UNKNOWN_ROLE;
                goto end;
            }

            /* fail on unsupported flags */
            if (breq->flags & FCGI_KEEP_CONN) {
                p_status = FCGI_CANT_MPX_CONN;
                goto end;
            }

            /* store the id for later */
            *fcgi_id = (int) hdr.id;

            break;

        case FCGI_PARAMS:
            /* unknown id? fail */
            if (hdr.id != *fcgi_id) {
                p_status = FCGI_CANT_MPX_CONN;
                goto end;
            }

            if (psz) {
                /* add to the buffer */
                buf = xs_realloc(buf, b_size + psz);
                memcpy(buf + b_size, p_buf, psz);
                b_size += psz;
            }
            else {
                /* no size, so the packet is complete; process it */
                xs *cgi_vars = xs_dict_new();

                req = xs_dict_new();

                int offset = 0;
                while (offset < b_size) {
                    unsigned int ksz = buf[offset++];

                    if (ksz & 0x80) {
                        ksz &= 0x7f;
                        ksz = (ksz << 8) | buf[offset++];
                        ksz = (ksz << 8) | buf[offset++];
                        ksz = (ksz << 8) | buf[offset++];
                    }

                    unsigned int vsz = buf[offset++];
                    if (vsz & 0x80) {
                        vsz &= 0x7f;
                        vsz = (vsz << 8) | buf[offset++];
                        vsz = (vsz << 8) | buf[offset++];
                        vsz = (vsz << 8) | buf[offset++];
                    }

                    /* get the key */
                    xs *k = xs_str_new_sz((char *)&buf[offset], ksz);
                    offset += ksz;

                    /* get the value */
                    xs *v = xs_str_new_sz((char *)&buf[offset], vsz);
                    offset += vsz;

                    cgi_vars = xs_dict_append(cgi_vars, k, v);

                    if (strcmp(k, "REQUEST_METHOD") == 0)
                        req = xs_dict_append(req, "method", v);
                    else
                    if (strcmp(k, "REQUEST_URI") == 0) {
                        req = xs_dict_append(req, "raw_path", v);

                        xs *pnv = xs_split_n(v, "?", 1);

                        /* store the path */
                        req = xs_dict_append(req, "path", xs_list_get(pnv, 0));

                        /* get the variables */
                        q_vars = xs_url_vars(xs_list_get(pnv, 1));
                    }
                    else
                    if (xs_match(k, "CONTENT_TYPE|CONTENT_LENGTH|HTTP_*")) {
                        if (xs_startswith(k, "HTTP_"))
                            k = xs_crop_i(k, 5, 0);

                        k = xs_tolower_i(k);
                        k = xs_replace_i(k, "_", "-");

                        req = xs_dict_append(req, k, v);
                    }
                }

                req = xs_dict_append(req, "cgi_vars", cgi_vars);

                buf    = xs_free(buf);
                b_size = 0;
            }

            break;

        case FCGI_STDIN:
            /* unknown id? fail */
            if (hdr.id != *fcgi_id) {
                p_status = FCGI_CANT_MPX_CONN;
                goto end;
            }

            if (psz) {
                /* add to the buffer */
                buf = xs_realloc(buf, b_size + psz);
                memcpy(buf + b_size, p_buf, psz);
                b_size += psz;
            }
            else {
                /* add an asciiz to be able to treat it as a string */
                buf = xs_realloc(buf, _xs_blk_size(b_size + 1));
                buf[b_size] = '\0';

                /* fill the payload info and finish */
                *payload = (xs_str *)buf;
                *p_size  = b_size;

                const char *ct = xs_dict_get(req, "content-type");

                if (*payload && ct && strcmp(ct, "application/x-www-form-urlencoded") == 0) {
                    p_vars  = xs_url_vars(*payload);
                }
                else
                if (*payload && ct && xs_startswith(ct, "multipart/form-data")) {
                    p_vars = xs_multipart_form_data(*payload, *p_size, ct);
                }
                else
                    p_vars = xs_dict_new();

                if (q_vars == NULL)
                    q_vars = xs_dict_new();

                req = xs_dict_append(req, "q_vars", q_vars);
                req = xs_dict_append(req, "p_vars", p_vars);

                /* disconnect the payload from the buf variable */
                buf = NULL;

                goto end;
            }

            break;
        }
    }

end:
    /* any kind of error? notify and cleanup */
    if (p_status != FCGI_REQUEST_COMPLETE) {
        struct fcgi_end_request ereq = {0};

        /* complete the connection */
        ereq.app_status      = 0;
        ereq.protocol_status = p_status;

        /* reuse header */
        hdr.type        = FCGI_ABORT_REQUEST;
        hdr.content_len = htons(sizeof(ereq));

        fwrite(&hdr, sizeof(hdr), 1, f);
        fwrite(&ereq, sizeof(ereq), 1, f);

        /* session closed */
        *fcgi_id = -1;

        /* request dict is not valid */
        req = xs_free(req);
    }

    xs_free(buf);
    return req;
}


void xs_fcgi_response(FILE *f, int status, xs_dict *headers, xs_str *body, int b_size, int fcgi_id)
/* writes an FCGI response */
{
    struct fcgi_record_header hdr = {0};
    struct fcgi_end_request ereq = {0};
    xs *out = xs_str_new(NULL);
    const xs_str *k;
    const xs_str *v;

    /* no previous id? it's an error */
    if (fcgi_id == -1)
        return;

    /* create the headers */
    {
        xs *s1 = xs_fmt("status: %d\r\n", status);
        out = xs_str_cat(out, s1);
    }

    xs_dict_foreach(headers, k, v) {
        xs *s1 = xs_fmt("%s: %s\r\n", k, v);
        out = xs_str_cat(out, s1);
    }

    if (b_size > 0) {
        xs *s1 = xs_fmt("content-length: %d\r\n", b_size);
        out = xs_str_cat(out, s1);
    }

    out = xs_str_cat(out, "\r\n");

    /* everything is text by now */
    int size = strlen(out);

    /* add the body */
    if (body != NULL && b_size > 0) {
        out = xs_append_m(out, body, b_size);
        size += b_size;
    }

    /* now send all the STDOUT in packets */
    hdr.version = FCGI_VERSION_1;
    hdr.type    = FCGI_STDOUT;
    hdr.id      = fcgi_id;

    int offset = 0;

    while (offset < size) {
        size_t sz = size - offset;
        if (sz > 0xffff)
            sz = 0xffff;

        hdr.content_len = htons(sz);

        /* write or fail */
        if (!fwrite(&hdr, sizeof(hdr), 1, f) || fwrite(out + offset, 1, sz, f) != sz)
            return;

        offset += sz;
    }

    /* final STDOUT packet with 0 size */
    hdr.content_len = 0;
    if (!fwrite(&hdr, sizeof(hdr), 1, f))
        return;

    /* complete the connection */
    ereq.app_status      = 0;
    ereq.protocol_status = FCGI_REQUEST_COMPLETE;

    hdr.type        = FCGI_END_REQUEST;
    hdr.content_len = htons(sizeof(ereq));

    if (fwrite(&hdr, sizeof(hdr), 1, f))
        fwrite(&ereq, sizeof(ereq), 1, f);
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_URL_H */
