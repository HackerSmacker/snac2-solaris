/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_UNICODE_H

#define _XS_UNICODE_H

 int xs_utf8_enc(char buf[4], unsigned int cpoint);
 int xs_is_utf8_cont_byte(char c);
 unsigned int xs_utf8_dec(const char **str);
 int xs_unicode_width(unsigned int cpoint);
 int xs_is_surrogate(unsigned int cpoint);
 int xs_is_diacritic(unsigned int cpoint);
 unsigned int xs_surrogate_dec(unsigned int p1, unsigned int p2);
 unsigned int xs_surrogate_enc(unsigned int cpoint);
 unsigned int *_xs_unicode_upper_search(unsigned int cpoint);
 unsigned int *_xs_unicode_lower_search(unsigned int cpoint);
 #define xs_unicode_is_upper(cpoint) (!!_xs_unicode_upper_search(cpoint))
 #define xs_unicode_is_lower(cpoint) (!!_xs_unicode_lower_search(cpoint))
 unsigned int xs_unicode_to_upper(unsigned int cpoint);
 unsigned int xs_unicode_to_lower(unsigned int cpoint);
 int xs_unicode_nfd(unsigned int cpoint, unsigned int *base, unsigned int *diac);
 int xs_unicode_nfc(unsigned int base, unsigned int diac, unsigned int *cpoint);
 int xs_unicode_is_alpha(unsigned int cpoint);
 int xs_unicode_is_right_to_left(unsigned int cpoint);

#ifdef _XS_H
 xs_str *xs_utf8_insert(xs_str *str, unsigned int cpoint, int *offset);
 xs_str *xs_utf8_cat(xs_str *str, unsigned int cpoint);
 xs_str *xs_utf8_to_upper(const char *str);
 xs_str *xs_utf8_to_lower(const char *str);
 xs_str *xs_utf8_to_nfd(const char *str);
 xs_str *xs_utf8_to_nfc(const char *str);
#endif

#ifdef XS_IMPLEMENTATION

#include <ctype.h>

#ifndef xs_countof
#define xs_countof(a) (sizeof((a)) / sizeof((*a)))
#endif

int xs_utf8_enc(char buf[4], unsigned int cpoint)
/* encodes an Unicode codepoint to utf-8 into buf and returns the size in bytes */
{
    char *p = buf;

    if (cpoint < 0x80) /* 1 byte char */
        *p++ = cpoint & 0xff;
    else {
        if (cpoint < 0x800) /* 2 byte char */
            *p++ = 0xc0 | (cpoint >> 6);
        else {
            if (cpoint < 0x10000) /* 3 byte char */
                *p++ = 0xe0 | (cpoint >> 12);
            else { /* 4 byte char */
                *p++ = 0xf0 | (cpoint >> 18);
                *p++ = 0x80 | ((cpoint >> 12) & 0x3f);
            }

            *p++ = 0x80 | ((cpoint >> 6) & 0x3f);
        }

        *p++ = 0x80 | (cpoint & 0x3f);
    }

    return p - buf;
}


int xs_is_utf8_cont_byte(char c)
/* returns true if c is an utf8 continuation byte */
{
    return ((c & 0xc0) == 0x80);
}


unsigned int xs_utf8_dec(const char **str)
/* decodes an utf-8 char inside str and updates the pointer */
{
    const char *p = *str;
    unsigned int cpoint = 0;
    unsigned char c = *p++;
    int cb = 0;

    if ((c & 0x80) == 0) { /* 1 byte char */
        cpoint = c;
    }
    else
    if ((c & 0xe0) == 0xc0) { /* 2 byte char */
        cpoint = (c & 0x1f) << 6;
        cb = 1;
    }
    else
    if ((c & 0xf0) == 0xe0) { /* 3 byte char */
        cpoint = (c & 0x0f) << 12;
        cb = 2;
    }
    else
    if ((c & 0xf8) == 0xf0) { /* 4 byte char */
        cpoint = (c & 0x07) << 18;
        cb = 3;
    }

    /* process the continuation bytes */
    while (cb > 0 && *p && xs_is_utf8_cont_byte(*p))
        cpoint |= (*p++ & 0x3f) << (--cb * 6);

    /* incomplete or broken? */
    if (cb)
        cpoint = 0xfffd;

    *str = p;
    return cpoint;
}


/** Unicode character width: intentionally dead simple **/

static unsigned int xs_unicode_width_table[] = {
    0x300,      0x36f,      0,      /* diacritics */
    0x1100,     0x11ff,     2,      /* Hangul */
    0x2e80,     0xa4cf,     2,      /* CJK */
    0xac00,     0xd7a3,     2,      /* more Hangul */
    0xe000,     0xf8ff,     0,      /* private use */
    0xf900,     0xfaff,     2,      /* CJK compatibility */
    0xff00,     0xff60,     2,      /* full width things */
    0xffdf,     0xffe6,     2,      /* full width things */
    0x1f200,    0x1ffff,    2,      /* emojis */
    0x20000,    0x2fffd,    2       /* more CJK */
};

int xs_unicode_width(unsigned int cpoint)
/* returns the width in columns of a Unicode codepoint (somewhat simplified) */
{
    int b = 0;
    int t = xs_countof(xs_unicode_width_table) / 3 - 1;

    while (t >= b) {
        int n = (b + t) / 2;
        unsigned int *p = &xs_unicode_width_table[n * 3];

        if (cpoint < p[0])
            t = n - 1;
        else
        if (cpoint > p[1])
            b = n + 1;
        else
            return p[2];
    }

    return 1;
}


int xs_is_diacritic(unsigned int cpoint)
{
    return cpoint >= 0x300 && cpoint <= 0x36f;
}


/** surrogate pairs **/

int xs_is_surrogate(unsigned int cpoint)
/* checks if cpoint is the first element of a Unicode surrogate pair */
{
    return cpoint >= 0xd800 && cpoint <= 0xdfff;
}


unsigned int xs_surrogate_dec(unsigned int p1, unsigned int p2)
/* "decodes" a surrogate pair into a codepoint */
{
    return 0x10000 | ((p1 & 0x3ff) << 10) | (p2 & 0x3ff);
}


unsigned int xs_surrogate_enc(unsigned int cpoint)
/* "encodes" a Unicode into a surrogate pair (p1 in the MSB word) */
{
    unsigned int p1 = 0xd7c0 + (cpoint >> 10);
    unsigned int p2 = 0xdc00 + (cpoint & 0x3ff);

    return (p1 << 16) | p2;
}


#ifdef _XS_H

xs_str *xs_utf8_insert(xs_str *str, unsigned int cpoint, int *offset)
/* encodes an Unicode codepoint to utf-8 into str */
{
    char tmp[4];

    int c = xs_utf8_enc(tmp, cpoint);

    str = xs_insert_m(str, *offset, tmp, c);

    *offset += c;

    return str;
}


xs_str *xs_utf8_cat(xs_str *str, unsigned int cpoint)
/* encodes an Unicode codepoint to utf-8 into str */
{
    int offset = strlen(str);

    return xs_utf8_insert(str, cpoint, &offset);
}

#endif /* _XS_H */


#ifdef _XS_UNICODE_TBL_H

/* include xs_unicode_tbl.h before this one to use these functions */

unsigned int *_xs_unicode_upper_search(unsigned int cpoint)
/* searches for an uppercase codepoint in the case fold table */
{
    int b = 0;
    int t = xs_countof(xs_unicode_case_fold_table) / 2 + 1;

    while (t >= b) {
        int n = (b + t) / 2;
        unsigned int *p = &xs_unicode_case_fold_table[n * 2];

        if (cpoint < p[0])
            t = n - 1;
        else
        if (cpoint > p[0])
            b = n + 1;
        else
            return p;
    }

    return NULL;
}


unsigned int *_xs_unicode_lower_search(unsigned int cpoint)
/* searches for a lowercase codepoint in the case fold table */
{
    unsigned int *p = xs_unicode_case_fold_table;
    unsigned int *e = p + xs_countof(xs_unicode_case_fold_table);

    while (p < e) {
        if (cpoint == p[1])
            return p;

        p += 2;
    }

    return NULL;
}


unsigned int xs_unicode_to_lower(unsigned int cpoint)
/* returns the cpoint to lowercase */
{
    if (cpoint < 0x80)
        return tolower(cpoint);

    unsigned int *p = _xs_unicode_upper_search(cpoint);

    return p == NULL ? cpoint : p[1];
}


unsigned int xs_unicode_to_upper(unsigned int cpoint)
/* returns the cpoint to uppercase */
{
    if (cpoint < 0x80)
        return toupper(cpoint);

    unsigned int *p = _xs_unicode_lower_search(cpoint);

    return p == NULL ? cpoint : p[0];
}


int xs_unicode_nfd(unsigned int cpoint, unsigned int *base, unsigned int *diac)
/* applies unicode Normalization Form D */
{
    int b = 0;
    int t = xs_countof(xs_unicode_nfd_table) / 3 - 1;

    while (t >= b) {
        int n = (b + t) / 2;
        unsigned int *p = &xs_unicode_nfd_table[n * 3];

        int c = cpoint - p[0];

        if (c < 0)
            t = n - 1;
        else
        if (c > 0)
            b = n + 1;
        else {
            *base = p[1];
            *diac = p[2];
            return 1;
        }
    }

    return 0;
}


int xs_unicode_nfc(unsigned int base, unsigned int diac, unsigned int *cpoint)
/* applies unicode Normalization Form C */
{
    unsigned int *p = xs_unicode_nfd_table;
    unsigned int *e = p + xs_countof(xs_unicode_nfd_table);

    while (p < e) {
        if (p[1] == base && p[2] == diac) {
            *cpoint = p[0];
            return 1;
        }

        p += 3;
    }

    return 0;
}


int xs_unicode_is_alpha(unsigned int cpoint)
/* checks if a codepoint is an alpha (i.e. a letter) */
{
    int b = 0;
    int t = xs_countof(xs_unicode_alpha_table) / 2 - 1;

    while (t >= b) {
        int n = (b + t) / 2;
        unsigned int *p = &xs_unicode_alpha_table[n * 2];

        if (cpoint < p[0])
            t = n - 1;
        else
        if (cpoint > p[1])
            b = n + 1;
        else
            return 1;
    }

    return 0;
}


int xs_unicode_is_right_to_left(unsigned int cpoint)
/* checks if a codepoint is a right-to-left letter */
{
    int b = 0;
    int t = xs_countof(xs_unicode_right_to_left_table) / 2 - 1;

    while (t >= b) {
        int n = (b + t) / 2;
        unsigned int *p = &xs_unicode_right_to_left_table[n * 2];

        if (cpoint < p[0])
            t = n - 1;
        else
        if (cpoint > p[1])
            b = n + 1;
        else
            return 1;
    }

    return 0;
}


#ifdef _XS_H

xs_str *xs_utf8_to_upper(const char *str)
{
    xs_str *s = xs_str_new(NULL);
    unsigned int cpoint;
    int offset = 0;

    while ((cpoint = xs_utf8_dec(&str))) {
        cpoint = xs_unicode_to_upper(cpoint);
        s = xs_utf8_insert(s, cpoint, &offset);
    }

    return s;
}


xs_str *xs_utf8_to_lower(const char *str)
{
    xs_str *s = xs_str_new(NULL);
    unsigned int cpoint;
    int offset = 0;

    while ((cpoint = xs_utf8_dec(&str))) {
        cpoint = xs_unicode_to_lower(cpoint);
        s = xs_utf8_insert(s, cpoint, &offset);
    }

    return s;
}


xs_str *xs_utf8_to_nfd(const char *str)
{
    xs_str *s = xs_str_new(NULL);
    unsigned int cpoint;
    int offset = 0;

    while ((cpoint = xs_utf8_dec(&str))) {
        unsigned int base;
        unsigned int diac;

        if (xs_unicode_nfd(cpoint, &base, &diac)) {
            s = xs_utf8_insert(s, base, &offset);
            s = xs_utf8_insert(s, diac, &offset);
        }
        else
            s = xs_utf8_insert(s, cpoint, &offset);
    }

    return s;
}


xs_str *xs_utf8_to_nfc(const char *str)
{
    xs_str *s = xs_str_new(NULL);
    unsigned int cpoint;
    unsigned int base = 0;
    int offset = 0;

    while ((cpoint = xs_utf8_dec(&str))) {
        if (xs_is_diacritic(cpoint)) {
            if (xs_unicode_nfc(base, cpoint, &base))
                continue;
        }

        if (base)
            s = xs_utf8_insert(s, base, &offset);

        base = cpoint;
    }

    if (base)
        s = xs_utf8_insert(s, base, &offset);

    return s;
}

#endif /* _XS_H */

#endif /* _XS_UNICODE_TBL_H */

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_UNICODE_H */
