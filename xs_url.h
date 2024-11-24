/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_URL_H

#define _XS_URL_H

xs_str *xs_url_dec(const char *str);
xs_dict *xs_url_vars(const char *str);
xs_dict *xs_multipart_form_data(const char *payload, int p_size, const char *header);

#ifdef XS_IMPLEMENTATION

xs_str *xs_url_dec(const char *str)
/* decodes an URL */
{
    xs_str *s = xs_str_new(NULL);

    while (*str) {
        if (*str == '%') {
            unsigned int i;

            if (sscanf(str + 1, "%02x", &i) == 1) {
                unsigned char uc = i;

                s = xs_append_m(s, (char *)&uc, 1);
                str += 2;
            }
        }
        else
        if (*str == '+')
            s = xs_append_m(s, " ", 1);
        else
            s = xs_append_m(s, str, 1);

        str++;
    }

    return s;
}


xs_dict *xs_url_vars(const char *str)
/* parse url variables */
{
    xs_dict *vars;

    vars = xs_dict_new();

    if (str != NULL) {
        /* split by arguments */
        xs *args = xs_split(str, "&");

        const xs_val *v;

        xs_list_foreach(args, v) {
            xs *dv = xs_url_dec(v);
            xs *kv = xs_split_n(dv, "=", 1);

            if (xs_list_len(kv) == 2) {
                const char *key = xs_list_get(kv, 0);
                const char *pv  = xs_dict_get(vars, key);

                if (!xs_is_null(pv)) {
                    /* there is a previous value: convert to a list and append */
                    xs *vlist = NULL;
                    if (xs_type(pv) == XSTYPE_LIST)
                        vlist = xs_dup(pv);
                    else {
                        vlist = xs_list_new();
                        vlist = xs_list_append(vlist, pv);
                    }

                    vlist = xs_list_append(vlist, xs_list_get(kv, 1));
                    vars  = xs_dict_set(vars, key, vlist);
                }
                else {
                    /* ends with []? force to always be a list */
                    if (xs_endswith(key, "[]")) {
                        xs *vlist = xs_list_new();
                        vlist = xs_list_append(vlist, xs_list_get(kv, 1));
                        vars = xs_dict_append(vars, key, vlist);
                    }
                    else
                        vars = xs_dict_append(vars, key, xs_list_get(kv, 1));
                }
            }
        }
    }

    return vars;
}


xs_dict *xs_multipart_form_data(const char *payload, int p_size, const char *header)
/* parses a multipart/form-data payload */
{
    xs *boundary = NULL;
    int offset = 0;
    int bsz;
    char *p;

    /* build the boundary string */
    {
        xs *l1 = xs_split(header, "=");

        if (xs_list_len(l1) != 2)
            return NULL;

        boundary = xs_dup(xs_list_get(l1, 1));

        /* Tokodon sends the boundary header with double quotes surrounded */
        if (xs_between("\"", boundary, "\"") != 0)
            boundary = xs_strip_chars_i(boundary, "\"");

        boundary = xs_fmt("--%s", boundary);
    }

    bsz = strlen(boundary);

    xs_dict *p_vars = xs_dict_new();

    /* iterate searching the boundaries */
    while ((p = xs_memmem(payload + offset, p_size - offset, boundary, bsz)) != NULL) {
        xs *s1 = NULL;
        xs *l1 = NULL;
        const char *vn = NULL;
        const char *fn = NULL;
        const char *ct = NULL;
        char *q;
        int po, ps;

        /* final boundary? */
        p += bsz;

        if (p[0] == '-' && p[1] == '-')
            break;

        /* skip the \r\n */
        p += 2;

        /* Tokodon sends also a Content-Type headers,
           let's use it to determine the file type */
        do {
            if (p[0] == 13 && p[1] == 10)
                break;
            q = strchr(p, '\r');

            /* unexpected formatting, fail immediately */
            if (q == NULL)
                return p_vars;

            s1 = xs_realloc(NULL, q - p + 1);
            memcpy(s1, p, q - p);
            s1[q - p] = '\0';

            if (xs_startswith(s1, "Content-Disposition") || xs_startswith(s1, "content-disposition")) {
                /* split by " like a primitive man */
                l1 = xs_split(s1, "\"");

                /* get the variable name */
                vn = xs_list_get(l1, 1);

                /* is it an attached file? */
                if (xs_list_len(l1) >= 4 && strcmp(xs_list_get(l1, 2), "; filename=") == 0) {
                    /* get the file name */
                    fn = xs_list_get(l1, 3);
                }
            }
            else
            if (xs_startswith(s1, "Content-Type") || xs_startswith(s1, "content-type")) {
                l1 = xs_split(s1, ":");

                if (xs_list_len(l1) >= 2) {
                    ct = xs_lstrip_chars_i(xs_dup(xs_list_get(l1, 1)), " ");
                }
            }

            p += (q - p);
            p += 2; // Skip /r/n
        } while (1);

        /* find the start of the part content */
        if ((p = xs_memmem(p, p_size - (p - payload), "\r\n", 2)) == NULL)
            break;

        p += 2; // Skip empty line

        /* find the next boundary */
        if ((q = xs_memmem(p, p_size - (p - payload), boundary, bsz)) == NULL)
            break;

        po = p - payload;
        ps = q - p - 2;     /* - 2 because the final \r\n */

        /* is it a filename? */
        if (fn != NULL) {
            /* p_var value is a list */
            /* if filename has no extension and content-type is image, attach extension to the filename */
            if (strchr(fn, '.') == NULL && ct && xs_startswith(ct, "image/")) {
                char *ext = strchr(ct, '/');
                ext++;
                fn = xs_str_cat(xs_str_new(""), fn, ".", ext);
            }

            xs *l1 = xs_list_new();
            xs *vpo = xs_number_new(po);
            xs *vps = xs_number_new(ps);

            l1 = xs_list_append(l1, fn);
            l1 = xs_list_append(l1, vpo);
            l1 = xs_list_append(l1, vps);

            p_vars = xs_dict_append(p_vars, vn, l1);
        }
        else {
            /* regular variable; just copy */
            xs *vc = xs_realloc(NULL, ps + 1);
            memcpy(vc, payload + po, ps);
            vc[ps] = '\0';

            p_vars = xs_dict_append(p_vars, vn, vc);
        }

        /* move on */
        offset = q - payload;
    }

    return p_vars;
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_URL_H */
