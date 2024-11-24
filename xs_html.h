/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_HTML_H

#define _XS_HTML_H

typedef struct xs_html xs_html;

xs_str *xs_html_encode(const char *str);

xs_html *xs_html_attr(const char *key, const char *value);
xs_html *xs_html_text(const char *content);
xs_html *xs_html_raw(const char *content);

xs_html *_xs_html_add(xs_html *tag, xs_html *var[]);
#define xs_html_add(tag, ...) _xs_html_add(tag, (xs_html *[]) { __VA_ARGS__, NULL })

xs_html *_xs_html_tag(const char *tag, xs_html *var[]);
#define xs_html_tag(tag, ...) _xs_html_tag(tag, (xs_html *[]) { __VA_ARGS__, NULL })

xs_html *_xs_html_sctag(const char *tag, xs_html *var[]);
#define xs_html_sctag(tag, ...) _xs_html_sctag(tag, (xs_html *[]) { __VA_ARGS__, NULL })

xs_html *_xs_html_container(xs_html *var[]);
#define xs_html_container(...) _xs_html_container((xs_html *[]) { __VA_ARGS__, NULL })

void xs_html_render_f(xs_html *h, FILE *f);
xs_str *xs_html_render_s(xs_html *tag, const char *prefix);
#define xs_html_render(tag) xs_html_render_s(tag, NULL)


#ifdef XS_IMPLEMENTATION

typedef enum {
    XS_HTML_TAG,
    XS_HTML_SCTAG,
    XS_HTML_CONTAINER,
    XS_HTML_ATTR,
    XS_HTML_TEXT
} xs_html_type;

struct xs_html {
    xs_html_type type;
    xs_str *content;
    xs_html *attrs;
    xs_html *tags;
    xs_html *next;
};

xs_str *xs_html_encode(const char *str)
/* encodes str using HTML entities */
{
    xs_str *s = xs_str_new(NULL);
    int o = 0;
    const char *e = str + strlen(str);

    for (;;) {
        char *ec = "<>\"'&";   /* characters to escape */
        const char *q = e;
        int z;

        /* find the nearest happening of a char */
        while (*ec) {
            char *m = memchr(str, *ec++, q - str);
            if (m)
                q = m;
        }

        /* copy string to here */
        z = q - str;
        s = xs_insert_m(s, o, str, z);
        o += z;

        /* if q points to the end, nothing more to do */
        if (q == e)
            break;

        /* insert the escaped char */
        char tmp[8];
        z = snprintf(tmp, sizeof(tmp), "&#%d;", *q);
        s = xs_insert_m(s, o, tmp, z);
        o += z;

        str = q + 1;
    }

    return s;
}


#define XS_HTML_NEW() memset(xs_realloc(NULL, sizeof(xs_html)), '\0', sizeof(xs_html))

xs_html *xs_html_attr(const char *key, const char *value)
/* creates an HTML block with an attribute */
{
    xs_html *a = XS_HTML_NEW();

    a->type = XS_HTML_ATTR;

    if (value) {
        xs *ev = xs_html_encode(value);
        a->content = xs_fmt("%s=\"%s\"", key, ev);
    }
    else
        a->content = xs_dup(key);

    return a;
}


xs_html *xs_html_text(const char *content)
/* creates an HTML block of text, escaping it previously */
{
    xs_html *a = XS_HTML_NEW();

    a->type    = XS_HTML_TEXT;
    a->content = xs_html_encode(content);

    return a;
}


xs_html *xs_html_raw(const char *content)
/* creates an HTML block without escaping (for pre-formatted HTML, comments, etc) */
{
    xs_html *a = XS_HTML_NEW();

    a->type    = XS_HTML_TEXT;
    a->content = xs_dup(content);

    return a;
}


xs_html *_xs_html_add(xs_html *tag, xs_html *var[])
/* add data (attrs, tags or text) to a tag */
{
    while (*var) {
        xs_html *data = *var++;

        if (data->type == XS_HTML_ATTR) {
            data->next = tag->attrs;
            tag->attrs = data;
        }
        else {
            data->next = tag->tags;
            tag->tags = data;
        }
    }

    return tag;
}


static xs_html *_xs_html_tag_t(xs_html_type type, const char *tag, xs_html *var[])
/* creates a tag with a variable list of attributes and subtags */
{
    xs_html *a = XS_HTML_NEW();

    a->type = type;

    /* a tag can be NULL, to be a kind of 'wrapper' */
    if (tag)
        a->content = xs_dup(tag);

    _xs_html_add(a, var);

    return a;
}


xs_html *_xs_html_tag(const char *tag, xs_html *var[])
{
    return _xs_html_tag_t(XS_HTML_TAG, tag, var);
}


xs_html *_xs_html_sctag(const char *tag, xs_html *var[])
{
    return _xs_html_tag_t(XS_HTML_SCTAG, tag, var);
}


xs_html *_xs_html_container(xs_html *var[])
{
    return _xs_html_tag_t(XS_HTML_CONTAINER, NULL, var);
}


void xs_html_render_f(xs_html *h, FILE *f)
/* renders the tag and its subtags into a file */
{
    if (h == NULL)
        return;

    /* follow the chain */
    xs_html_render_f(h->next, f);

    switch (h->type) {
    case XS_HTML_TAG:
        fprintf(f, "<%s", h->content);

        /* attributes */
        xs_html_render_f(h->attrs, f);

        fprintf(f, ">");

        /* sub-tags */
        xs_html_render_f(h->tags, f);

        fprintf(f, "</%s>", h->content);
        break;

    case XS_HTML_SCTAG:
        fprintf(f, "<%s", h->content);

        /* attributes */
        xs_html_render_f(h->attrs, f);

        fprintf(f, "/>");
        break;

    case XS_HTML_CONTAINER:
        /* sub-tags */
        xs_html_render_f(h->tags, f);
        break;

    case XS_HTML_ATTR:
        fprintf(f, " ");
        /* fallthrough */

    case XS_HTML_TEXT:
        fprintf(f, "%s", h->content);
        break;
    }

    xs_free(h->content);
    xs_free(h);
}


xs_str *xs_html_render_s(xs_html *tag, const char *prefix)
/* renders to a string */
{
    xs_str *s = NULL;
    size_t sz;
    FILE *f;

    if ((f = open_memstream(&s, &sz)) != NULL) {
        if (prefix)
            fprintf(f, "%s", prefix);

        xs_html_render_f(tag, f);
        fclose(f);
    }

    return s;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_HTML_H */
