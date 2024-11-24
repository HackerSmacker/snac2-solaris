/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_HEX_H

#define _XS_HEX_H

 int xs_is_hex_digit(char str);
 void xs_hex_enc_1(char **dst, const char **src);
 int xs_hex_dec_1(char **dst, const char **src);
 char *_xs_hex_enc(char *dst, const char *src, int src_size);
 char *_xs_hex_dec(char *dst, const char *src, int src_size);

#ifdef _XS_H
 xs_str *xs_hex_enc(const xs_val *data, int size);
 xs_val *xs_hex_dec(const xs_str *hex, int *size);
 int xs_is_hex(const char *str);
#endif /* _XS_H */


#ifdef XS_IMPLEMENTATION

#include <string.h>

/** hex **/

static char rev_hex_digits[] = "fedcba9876543210FEDCBA";

int xs_is_hex_digit(char str)
/* checks if the char is an hex digit */
{
    return strchr(rev_hex_digits, str) != NULL;
}


void xs_hex_enc_1(char **dst, const char **src)
/* decodes one character into two hex digits */
{
    const char *i = *src;
    char *o = *dst;

    *o++ = rev_hex_digits[0xf - (*i >> 4 & 0xf)];
    *o++ = rev_hex_digits[0xf - (*i      & 0xf)];

    *src = i + 1;
    *dst = o;
}


int xs_hex_dec_1(char **dst, const char **src)
/* decodes two hex digits (returns 0 on error) */
{
    const char *i = *src;
    char *o = *dst;

    char *d1 = strchr(rev_hex_digits, *i++);
    char *d2 = strchr(rev_hex_digits, *i++);

    if (!d1 || !d2) {
        /* decoding error */
        return 0;
    }

    *o++ = (0xf - ((d1 - rev_hex_digits) & 0xf)) << 4 |
           (0xf - ((d2 - rev_hex_digits) & 0xf));

    *src = i;
    *dst = o;
    return 1;
}


char *_xs_hex_enc(char *dst, const char *src, int src_size)
/* hex-encodes the src buffer into dst, which has enough size */
{
    const char *e = src + src_size;

    while (src < e)
        xs_hex_enc_1(&dst, &src);

    return dst;
}


char *_xs_hex_dec(char *dst, const char *src, int src_size)
/* hex-decodes the src string int dst, which has enough size.
   return NULL on decoding errors or the final position of dst */
{
    if (src_size % 2)
        return NULL;

    const char *e = src + src_size;

    while (src < e) {
        if (!xs_hex_dec_1(&dst, &src))
            return NULL;
    }

    return dst;
}


#ifdef _XS_H

xs_str *xs_hex_enc(const xs_val *data, int size)
/* returns an hexdump of data */
{
    xs_str *s = xs_realloc(NULL, _xs_blk_size(size * 2 + 1));

    char *q = _xs_hex_enc(s, data, size);

    *q = '\0';

    return s;
}


xs_val *xs_hex_dec(const xs_str *hex, int *size)
/* decodes an hexdump into data */
{
    int sz = strlen(hex);
    xs_val *s = NULL;

    *size = sz / 2;
    s = xs_realloc(NULL, _xs_blk_size(*size + 1));

    if (!_xs_hex_dec(s, hex, sz))
        return xs_free(s);

    s[*size] = '\0';

    return s;
}


int xs_is_hex(const char *str)
/* returns 1 if str is an hex string */
{
    if (strlen(str) % 2)
        return 0;

    while (*str) {
        if (!xs_is_hex_digit(*str++))
            return 0;
    }

    return 1;
}

#endif /* _XS_H */

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_HEX_H */
