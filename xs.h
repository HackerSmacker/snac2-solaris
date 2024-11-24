/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_H

#define _XS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>

typedef enum {
    XSTYPE_STRING = 0x02,       /* C string (\0 delimited) (NOT STORED) */
    XSTYPE_NUMBER = 0x17,       /* double in spirit, stored as a C string (\0 delimited) */
    XSTYPE_NULL   = 0x18,       /* Special NULL value */
    XSTYPE_TRUE   = 0x06,       /* Boolean */
    XSTYPE_FALSE  = 0x15,       /* Boolean */
    XSTYPE_LIST   = 0x1d,       /* Sequence of LITEMs up to EOM (with size) */
    XSTYPE_LITEM  = 0x1f,       /* Element of a list (any type) */
    XSTYPE_DICT   = 0x1c,       /* Sequence of KEYVALs up to EOM (with size) */
    XSTYPE_KEYVAL = 0x1e,       /* key + value (STRING key + any type) */
    XSTYPE_DATA   = 0x10        /* A block of anonymous data */
} xstype;


/* types */
typedef char xs_val;
typedef char xs_str;
typedef char xs_list;
typedef char xs_keyval;
typedef char xs_dict;
typedef char xs_number;
typedef char xs_data;

/* size in bytes of the type size */
#define _XS_TYPE_SIZE 4

/* auto-destroyable strings */
#define xs __attribute__ ((__cleanup__ (_xs_destroy))) xs_val

/* not really all, just very much */
#define XS_ALL 0xfffffff

#ifndef xs_countof
#define xs_countof(a) (sizeof((a)) / sizeof((*a)))
#endif

void *xs_free(void *ptr);
void *_xs_realloc(void *ptr, size_t size, const char *file, int line, const char *func);
#define xs_realloc(ptr, size) _xs_realloc(ptr, size, __FILE__, __LINE__, __func__)
int _xs_blk_size(int sz);
void _xs_destroy(char **var);
#define xs_debug() raise(SIGTRAP)
xstype xs_type(const xs_val *data);
int xs_size(const xs_val *data);
int xs_is_null(const xs_val *data);
int xs_cmp(const xs_val *v1, const xs_val *v2);
const xs_val *xs_or(const xs_val *v1, const xs_val *v2);
xs_val *xs_dup(const xs_val *data);
xs_val *xs_expand(xs_val *data, int offset, int size);
xs_val *xs_collapse(xs_val *data, int offset, int size);
xs_val *xs_insert_m(xs_val *data, int offset, const char *mem, int size);
#define xs_insert(data, offset, data2) xs_insert_m(data, offset, data2, xs_size(data2))
#define xs_append_m(data, mem, size) xs_insert_m(data, xs_size(data) - 1, mem, size)
xs_val *xs_stock(int type);

xs_str *xs_str_new(const char *str);
xs_str *xs_str_new_sz(const char *mem, int sz);
xs_str *xs_str_wrap_i(const char *prefix, xs_str *str, const char *suffix);
#define xs_str_prepend_i(str, prefix) xs_str_wrap_i(prefix, str, NULL)
xs_str *_xs_str_cat(xs_str *str, const char *strs[]);
#define xs_str_cat(str, ...) _xs_str_cat(str, (const char *[]){ __VA_ARGS__, NULL })
xs_str *xs_replace_in(xs_str *str, const char *sfrom, const char *sto, int times);
#define xs_replace_i(str, sfrom, sto) xs_replace_in(str, sfrom, sto, XS_ALL)
#define xs_replace(str, sfrom, sto) xs_replace_in(xs_dup(str), sfrom, sto, XS_ALL)
#define xs_replace_n(str, sfrom, sto, times) xs_replace_in(xs_dup(str), sfrom, sto, times)
xs_str *xs_fmt(const char *fmt, ...);
int xs_str_in(const char *haystack, const char *needle);
int xs_between(const char *prefix, const char *str, const char *suffix);
#define xs_startswith(str, prefix) xs_between(prefix, str, NULL)
#define xs_endswith(str, suffix) xs_between(NULL, str, suffix)
xs_str *xs_crop_i(xs_str *str, int start, int end);
xs_str *xs_lstrip_chars_i(xs_str *str, const char *chars);
xs_str *xs_rstrip_chars_i(xs_str *str, const char *chars);
xs_str *xs_strip_chars_i(xs_str *str, const char *chars);
#define xs_strip_i(str) xs_strip_chars_i(str, " \r\n\t\v\f")
xs_str *xs_tolower_i(xs_str *str);

xs_list *xs_list_new(void);
xs_list *xs_list_append_m(xs_list *list, const char *mem, int dsz);
xs_list *_xs_list_append(xs_list *list, const xs_val *vals[]);
#define xs_list_append(list, ...) _xs_list_append(list, (const xs_val *[]){ __VA_ARGS__, NULL })
int xs_list_iter(xs_list **list, const xs_val **value);
int xs_list_next(const xs_list *list, const xs_val **value, int *ctxt);
int xs_list_len(const xs_list *list);
const xs_val *xs_list_get(const xs_list *list, int num);
xs_list *xs_list_del(xs_list *list, int num);
xs_list *xs_list_insert(xs_list *list, int num, const xs_val *data);
xs_list *xs_list_set(xs_list *list, int num, const xs_val *data);
xs_list *xs_list_dequeue(xs_list *list, xs_val **data, int last);
#define xs_list_pop(list, data) xs_list_dequeue(list, data, 1)
#define xs_list_shift(list, data) xs_list_dequeue(list, data, 0)
int xs_list_in(const xs_list *list, const xs_val *val);
xs_str *xs_join(const xs_list *list, const char *sep);
xs_list *xs_split_n(const char *str, const char *sep, int times);
#define xs_split(str, sep) xs_split_n(str, sep, XS_ALL)
xs_list *xs_list_cat(xs_list *l1, const xs_list *l2);

int xs_keyval_size(const xs_str *key, const xs_val *value);
xs_str *xs_keyval_key(const xs_keyval *keyval);
xs_val *xs_keyval_value(const xs_keyval *keyval);
xs_keyval *xs_keyval_make(xs_keyval *keyval, const xs_str *key, const xs_val *value);

xs_dict *xs_dict_new(void);
xs_dict *xs_dict_append(xs_dict *dict, const xs_str *key, const xs_val *value);
xs_dict *xs_dict_prepend(xs_dict *dict, const xs_str *key, const xs_val *value);
int xs_dict_next(const xs_dict *dict, const xs_str **key, const xs_val **value, int *ctxt);
const xs_val *xs_dict_get(const xs_dict *dict, const xs_str *key);
#define xs_dict_get_def(dict, key, def) xs_or(xs_dict_get(dict, key), def)
xs_dict *xs_dict_del(xs_dict *dict, const xs_str *key);
xs_dict *xs_dict_set(xs_dict *dict, const xs_str *key, const xs_val *data);
xs_dict *xs_dict_gc(const xs_dict *dict);

const xs_val *xs_dict_get_path_sep(const xs_dict *dict, const char *path, const char *sep);
#define xs_dict_get_path(dict, path) xs_dict_get_path_sep(dict, path, ".")
xs_dict *xs_dict_set_path_sep(xs_dict *dict, const char *path, const xs_val *value, const char *sep);
#define xs_dict_set_path(dict, path, value) xs_dict_set_path_sep(dict, path, value, ".")

xs_val *xs_val_new(xstype t);
xs_number *xs_number_new(double f);
double xs_number_get(const xs_number *v);
const char *xs_number_str(const xs_number *v);

xs_data *xs_data_new(const void *data, int size);
int xs_data_size(const xs_data *value);
void xs_data_get(void *data, const xs_data *value);

void *xs_memmem(const char *haystack, int h_size, const char *needle, int n_size);

unsigned int xs_hash_func(const char *data, int size);

#ifdef XS_ASSERT
#include <assert.h>
#define XS_ASSERT_TYPE(v, t) assert(xs_type(v) == t)
#define XS_ASSERT_TYPE_NULL(v, t) assert(v == NULL || xs_type(v) == t)
#else
#define XS_ASSERT_TYPE(v, t) (void)(0)
#define XS_ASSERT_TYPE_NULL(v, t) (void)(0)
#endif

#define xs_return(v) xs_val *__r = v; v = NULL; return __r

#define xs_is_true(v) (xs_type((v)) == XSTYPE_TRUE)
#define xs_is_false(v) (xs_type((v)) == XSTYPE_FALSE)
#define xs_not(v) xs_stock(xs_is_true((v)) ? XSTYPE_FALSE : XSTYPE_TRUE)

#define xs_list_foreach(l, v) for (int ct_##__LINE__ = 0; xs_list_next(l, &v, &ct_##__LINE__); )
#define xs_dict_foreach(l, k, v) for (int ct_##__LINE__ = 0; xs_dict_next(l, &k, &v, &ct_##__LINE__); )


#ifdef XS_IMPLEMENTATION

void *_xs_realloc(void *ptr, size_t size, const char *file, int line, const char *func)
{
    xs_val *ndata = realloc(ptr, size);

    if (ndata == NULL) {
        fprintf(stderr, "ERROR: out of memory at %s:%d: %s()\n", file, line, func);
        abort();
    }

#ifdef XS_DEBUG
    if (ndata != ptr) {
        int n;
        FILE *f = fopen("xs_memory.out", "a");

        if (ptr != NULL)
            fprintf(f, "%p r\n", ptr);

        fprintf(f, "%p a %ld %s:%d: %s", ndata, size, file, line, func);

        if (ptr != NULL) {
            fprintf(f, " [");
            for (n = 0; n < 32 && ndata[n]; n++) {
                if (ndata[n] >= 32 && ndata[n] <= 127)
                    fprintf(f, "%c", ndata[n]);
                else
                    fprintf(f, "\\%02x", (unsigned char)ndata[n]);
            }
            fprintf(f, "]");
        }

        fprintf(f, "\n");

        fclose(f);
    }
#else
    (void)file;
    (void)line;
    (void)func;
#endif

    return ndata;
}


void *xs_free(void *ptr)
{
#ifdef XS_DEBUG
    if (ptr != NULL) {
        FILE *f = fopen("xs_memory.out", "a");
        fprintf(f, "%p b\n", ptr);
        fclose(f);
    }
#endif

    free(ptr);
    return NULL;
}


void _xs_destroy(char **var)
{
/*
    if (_xs_debug)
        printf("_xs_destroy %p\n", var);
*/
    xs_free(*var);
}


int _xs_blk_size(int sz)
/* calculates the block size */
{
    int blk_size = 4096;

    if (sz < 256)
        blk_size = 32;
    else
    if (sz < 4096)
        blk_size = 256;

    return ((((sz) + blk_size) / blk_size) * blk_size);
}


xstype xs_type(const xs_val *data)
/* return the type of data */
{
    xstype t;

    if (data == NULL)
        t = XSTYPE_NULL;
    else
    switch (data[0]) {
    case XSTYPE_NULL:
    case XSTYPE_TRUE:
    case XSTYPE_FALSE:
    case XSTYPE_LIST:
    case XSTYPE_LITEM:
    case XSTYPE_DICT:
    case XSTYPE_KEYVAL:
    case XSTYPE_NUMBER:
    case XSTYPE_DATA:
        t = data[0];
        break;
    default:
        t = XSTYPE_STRING;
        break;
    }

    return t;
}


void _xs_put_size(xs_val *ptr, int i)
/* must match _XS_TYPE_SIZE */
{
    memcpy(ptr + 1, &i, sizeof(i));
}


int _xs_get_size(const xs_val *ptr)
/* must match _XS_TYPE_SIZE */
{
    int i;
    memcpy(&i, ptr + 1, sizeof(i));
    return i;
}


int xs_size(const xs_val *data)
/* returns the size of data in bytes */
{
    int len = 0;
    const char *p;

    if (data == NULL)
        return 0;

    switch (xs_type(data)) {
    case XSTYPE_STRING:
        len = strlen(data) + 1;
        break;

    case XSTYPE_LIST:
    case XSTYPE_DICT:
    case XSTYPE_DATA:
        len = _xs_get_size(data);

        break;

    case XSTYPE_KEYVAL:
        /* calculate the size of the key and the value */
        p = data + 1;
        p += xs_size(p);
        p += xs_size(p);

        len = p - data;

        break;

    case XSTYPE_LITEM:
        /* it's the size of the item + 1 */
        p = data + 1;
        p += xs_size(p);

        len = p - data;

        break;

    case XSTYPE_NUMBER:
        len = 1 + xs_size(data + 1);

        break;

    default:
        len = 1;
    }

    return len;
}


int xs_is_null(const xs_val *data)
/* checks for null */
{
    return (xs_type(data) == XSTYPE_NULL);
}


int xs_cmp(const xs_val *v1, const xs_val *v2)
/* compares two values */
{
    if (xs_type(v1) == XSTYPE_STRING && xs_type(v2) == XSTYPE_STRING)
        return strcmp(v1, v2);

    int s1 = xs_size(v1);
    int s2 = xs_size(v2);
    int d = s1 - s2;

    return d == 0 ? memcmp(v1, v2, s1) : d;
}


const xs_val *xs_or(const xs_val *v1, const xs_val *v2)
/* returns v1 if it's not NULL, else v2 */
{
    return v1 == NULL ? v2 : v1;
}


xs_val *xs_dup(const xs_val *data)
/* creates a duplicate of data */
{
    xs_val *s = NULL;

    if (data) {
        int sz = xs_size(data);
        s = xs_realloc(NULL, _xs_blk_size(sz));

        memcpy(s, data, sz);
    }

    return s;
}


xs_val *xs_expand(xs_val *data, int offset, int size)
/* opens a hole in data */
{
    int sz = xs_size(data);
    int n;

    sz += size;

    /* open room */
    data = xs_realloc(data, _xs_blk_size(sz));

    /* move up the rest of the data */
    for (n = sz - 1; n >= offset + size; n--)
        data[n] = data[n - size];

    if (xs_type(data) == XSTYPE_LIST ||
        xs_type(data) == XSTYPE_DICT ||
        xs_type(data) == XSTYPE_DATA)
        _xs_put_size(data, sz);

    return data;
}


xs_val *xs_collapse(xs_val *data, int offset, int size)
/* shrinks data */
{
    int sz = xs_size(data);
    int n;

    /* don't try to delete beyond the limit */
    if (offset + size > sz)
        size = sz - offset;

    /* shrink total size */
    sz -= size;

    for (n = offset; n < sz; n++)
        data[n] = data[n + size];

    if (xs_type(data) == XSTYPE_LIST ||
        xs_type(data) == XSTYPE_DICT ||
        xs_type(data) == XSTYPE_DATA)
        _xs_put_size(data, sz);

    return xs_realloc(data, _xs_blk_size(sz));
}


xs_val *xs_insert_m(xs_val *data, int offset, const char *mem, int size)
/* inserts a memory block */
{
    data = xs_expand(data, offset, size);
    memcpy(data + offset, mem, size);

    return data;
}


xs_val *xs_stock(int type)
/* returns stock values */
{
    static xs_val stock_null[]  = { XSTYPE_NULL };
    static xs_val stock_true[]  = { XSTYPE_TRUE };
    static xs_val stock_false[] = { XSTYPE_FALSE };
    static xs_val stock_0[]     = { XSTYPE_NUMBER, '0', '\0' };
    static xs_val stock_1[]     = { XSTYPE_NUMBER, '1', '\0' };
    static xs_list *stock_list = NULL;
    static xs_dict *stock_dict = NULL;

    switch (type) {
    case 0:            return stock_0;
    case 1:            return stock_1;
    case XSTYPE_NULL:  return stock_null;
    case XSTYPE_TRUE:  return stock_true;
    case XSTYPE_FALSE: return stock_false;

    case XSTYPE_LIST:
        if (stock_list == NULL)
            stock_list = xs_list_new();
        return stock_list;

    case XSTYPE_DICT:
        if (stock_dict == NULL)
            stock_dict = xs_dict_new();
        return stock_dict;
    }

    return NULL;
}


/** strings **/

xs_str *xs_str_new(const char *str)
/* creates a new string */
{
    return xs_insert(NULL, 0, str ? str : "");
}


xs_str *xs_str_new_sz(const char *mem, int sz)
/* creates a new string from a memory block, adding an asciiz */
{
    xs_str *s = xs_realloc(NULL, _xs_blk_size(sz + 1));
    memcpy(s, mem, sz);
    s[sz] = '\0';

    return s;
}


xs_str *xs_str_wrap_i(const char *prefix, xs_str *str, const char *suffix)
/* wraps str with prefix and suffix */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    if (prefix)
        str = xs_insert_m(str, 0, prefix, strlen(prefix));

    if (suffix)
        str = xs_insert_m(str, strlen(str), suffix, strlen(suffix));

    return str;
}


xs_str *_xs_str_cat(xs_str *str, const char *strs[])
/* concatenates all strings after str */
{
    int o = strlen(str);

    while (*strs) {
        int sz = strlen(*strs);
        str = xs_insert_m(str, o, *strs, sz);
        o += sz;
        strs++;
    }

    return str;
}


xs_str *xs_replace_in(xs_str *str, const char *sfrom, const char *sto, int times)
/* replaces inline all sfrom with sto */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    int sfsz = strlen(sfrom);
    int stsz = strlen(sto);
    int diff = stsz - sfsz;
    char *ss;
    int offset = 0;

    while (times > 0 && (ss = strstr(str + offset, sfrom)) != NULL) {
        int n_offset = ss - str;

        if (diff < 0)
            str = xs_collapse(str, n_offset, -diff);
        else
        if (diff > 0)
            str = xs_expand(str, n_offset, diff);

        memcpy(str + n_offset, sto, stsz);

        offset = n_offset + stsz;

        times--;
    }

    return str;
}


xs_str *xs_fmt(const char *fmt, ...)
/* formats a string with printf()-like marks */
{
    int n;
    xs_str *s = NULL;
    va_list ap;

    va_start(ap, fmt);
    n = vsnprintf(s, 0, fmt, ap);
    va_end(ap);

    if (n > 0) {
        s = xs_realloc(NULL, _xs_blk_size(n + 1));

        va_start(ap, fmt);
        vsnprintf(s, n + 1, fmt, ap);
        va_end(ap);
    }

    return s;
}


int xs_str_in(const char *haystack, const char *needle)
/* finds needle in haystack and returns the offset or -1 */
{
    char *s;
    int r = -1;

    if ((s = strstr(haystack, needle)) != NULL)
        r = s - haystack;

    return r;
}


int xs_between(const char *prefix, const char *str, const char *suffix)
/* returns true if str starts with prefix and ends with suffix */
{
    int sz = strlen(str);
    int psz = prefix ? strlen(prefix) : 0;
    int ssz = suffix ? strlen(suffix) : 0;

    if (sz < psz || sz < ssz)
        return 0;

    if (prefix && memcmp(str, prefix, psz) != 0)
        return 0;

    if (suffix && memcmp(str + sz - ssz, suffix, ssz) != 0)
        return 0;

    return 1;
}


xs_str *xs_crop_i(xs_str *str, int start, int end)
/* crops the string to be only from start to end */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    int sz = strlen(str);

    if (end <= 0)
        end = sz + end;

    /* crop from the top */
    str[end] = '\0';

    /* crop from the bottom */
    str = xs_collapse(str, 0, start);

    return str;
}


xs_str *xs_lstrip_chars_i(xs_str *str, const char *chars)
/* strips all chars from the start of str */
{
    int n;

    for (n = 0; str[n] && strchr(chars, str[n]); n++);

    if (n)
        str = xs_collapse(str, 0, n);

    return str;
}


xs_str *xs_rstrip_chars_i(xs_str *str, const char *chars)
/* strips all chars from the end of str */
{
    int n;

    for (n = strlen(str); n > 0 && strchr(chars, str[n - 1]); n--);
    str[n] = '\0';

    return str;
}


xs_str *xs_strip_chars_i(xs_str *str, const char *chars)
/* strips the string of chars from the start and the end */
{
    return xs_lstrip_chars_i(xs_rstrip_chars_i(str, chars), chars);
}


xs_str *xs_tolower_i(xs_str *str)
/* convert to lowercase */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    int n;

    for (n = 0; str[n]; n++)
        str[n] = tolower(str[n]);

    return str;
}


/** lists **/

xs_list *xs_list_new(void)
/* creates a new list */
{
    int sz = 1 + _XS_TYPE_SIZE + 1;
    xs_list *l = xs_realloc(NULL, sz);
    memset(l, '\0', sz);

    l[0] = XSTYPE_LIST;
    _xs_put_size(l, sz);

    return l;
}


xs_list *_xs_list_write_litem(xs_list *list, int offset, const char *mem, int dsz)
/* writes a list item */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    if (mem == NULL) {
        mem = xs_stock(XSTYPE_NULL);
        dsz = xs_size(mem);
    }

    list = xs_expand(list, offset, dsz + 1);

    list[offset] = XSTYPE_LITEM;
    memcpy(list + offset + 1, mem, dsz);

    return list;
}


xs_list *xs_list_append_m(xs_list *list, const char *mem, int dsz)
/* adds a memory block to the list */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    return _xs_list_write_litem(list, xs_size(list) - 1, mem, dsz);
}


xs_list *_xs_list_append(xs_list *list, const xs_val *vals[])
/* adds several values to the list */
{
    /* special case: if the first argument is NULL, just insert it */
    if (*vals == NULL)
        return xs_list_append_m(list, NULL, 0);

    while (*vals) {
        list = xs_list_append_m(list, *vals, xs_size(*vals));
        vals++;
    }

    return list;
}


int xs_list_iter(xs_list **list, const xs_val **value)
/* iterates a list value */
{
    int goon = 1;

    xs_val *p = *list;

    /* skip the start of the list */
    if (xs_type(p) == XSTYPE_LIST)
        p += 1 + _XS_TYPE_SIZE;

    /* an element? */
    if (xs_type(p) == XSTYPE_LITEM) {
        p++;

        *value = p;

        p += xs_size(*value);
    }
    else {
        /* end of list */
        goon = 0;
    }

    /* store back the pointer */
    *list = p;

    return goon;
}


int xs_list_next(const xs_list *list, const xs_val **value, int *ctxt)
/* iterates a list, with context */
{
    if (xs_type(list) != XSTYPE_LIST)
        return 0;

    int goon = 1;

    const char *p = list;

    /* skip the start of the list */
    if (*ctxt == 0)
        *ctxt = 1 + _XS_TYPE_SIZE;

    p += *ctxt;

    /* an element? */
    if (xs_type(p) == XSTYPE_LITEM) {
        p++;

        *value = p;

        p += xs_size(*value);
    }
    else {
        /* end of list */
        goon = 0;
    }

    /* update the context */
    *ctxt = p - list;

    return goon;
}


int xs_list_len(const xs_list *list)
/* returns the number of elements in the list */
{
    XS_ASSERT_TYPE_NULL(list, XSTYPE_LIST);

    int c = 0;
    const xs_val *v;

    xs_list_foreach(list, v)
        c++;

    return c;
}


const xs_val *xs_list_get(const xs_list *list, int num)
/* returns the element #num */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    if (num < 0)
        num = xs_list_len(list) + num;

    int c = 0;
    const xs_val *v;

    xs_list_foreach(list, v) {
        if (c == num)
            return v;

        c++;
    }

    return NULL;
}


xs_list *xs_list_del(xs_list *list, int num)
/* deletes element #num */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    const xs_val *v;

    if ((v = xs_list_get(list, num)) != NULL)
        list = xs_collapse(list, v - 1 - list, xs_size(v - 1));

    return list;
}


xs_list *xs_list_insert(xs_list *list, int num, const xs_val *data)
/* inserts an element at #num position */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    const xs_val *v;
    int offset;

    if ((v = xs_list_get(list, num)) != NULL)
        offset = v - list;
    else
        offset = xs_size(list);

    return _xs_list_write_litem(list, offset - 1, data, xs_size(data));
}


xs_list *xs_list_set(xs_list *list, int num, const xs_val *data)
/* sets the element at #num position */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    list = xs_list_del(list, num);
    list = xs_list_insert(list, num, data);

    return list;
}


xs_list *xs_list_dequeue(xs_list *list, xs_val **data, int last)
/* gets a copy of the first or last element of a list, shrinking it */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    int ct = 0;
    const xs_val *v = NULL;

    if (!last) {
        /* get the first */
        xs_list_next(list, &v, &ct);
    }
    else {
        /* iterate to the end */
        while (xs_list_next(list, &v, &ct));
    }

    if (v != NULL) {
        *data = xs_dup(v);

        /* collapse from the address of the element */
        list = xs_collapse(list, v - 1 - list, xs_size(v - 1));
    }

    return list;
}


int xs_list_in(const xs_list *list, const xs_val *val)
/* returns the position of val in list or -1 */
{
    XS_ASSERT_TYPE_NULL(list, XSTYPE_LIST);

    int n = 0;
    const xs_val *v;
    int sz = xs_size(val);

    xs_list_foreach(list, v) {
        if (sz == xs_size(v) && memcmp(val, v, sz) == 0)
            return n;

        n++;
    }

    return -1;
}


xs_str *xs_join(const xs_list *list, const char *sep)
/* joins a list into a string */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    xs_str *s = NULL;
    const xs_val *v;
    int c = 0;
    int offset = 0;
    int ssz = strlen(sep);

    xs_list_foreach(list, v) {
        /* refuse to join non-string values */
        if (xs_type(v) == XSTYPE_STRING) {
            int sz;

            /* add the separator */
            if (c != 0 && ssz) {
                s = xs_realloc(s, offset + ssz);
                memcpy(s + offset, sep, ssz);
                offset += ssz;
            }

            /* add the element */
            if ((sz = strlen(v)) > 0) {
                s = xs_realloc(s, offset + sz);
                memcpy(s + offset, v, sz);
                offset += sz;
            }

            c++;
        }
    }

    /* null-terminate */
    s = xs_realloc(s, _xs_blk_size(offset + 1));
    s[offset] = '\0';

    return s;
}


xs_list *xs_split_n(const char *str, const char *sep, int times)
/* splits a string into a list upto n times */
{
    int sz = strlen(sep);
    char *ss;
    xs_list *list;

    list = xs_list_new();

    while (times > 0 && (ss = strstr(str, sep)) != NULL) {
        /* create a new string with this slice and add it to the list */
        xs *s = xs_str_new_sz(str, ss - str);
        list = xs_list_append(list, s);

        /* skip past the separator */
        str = ss + sz;

        times--;
    }

    /* add the rest of the string */
    list = xs_list_append(list, str);

    return list;
}


xs_list *xs_list_cat(xs_list *l1, const xs_list *l2)
/* concatenates list l2 to l1 */
{
    XS_ASSERT_TYPE(l1, XSTYPE_LIST);
    XS_ASSERT_TYPE(l2, XSTYPE_LIST);

    /* inserts at the end of l1 the content of l2 (skipping header and footer) */
    return xs_insert_m(l1, xs_size(l1) - 1,
        l2 + 1 + _XS_TYPE_SIZE, xs_size(l2) - (1 + _XS_TYPE_SIZE + 1));
}


/** keyvals **/

int xs_keyval_size(const xs_str *key, const xs_val *value)
/* returns the needed size for a keyval */
{
    return 1 + xs_size(key) + xs_size(value);
}


xs_str *xs_keyval_key(const xs_keyval *keyval)
/* returns a pointer to the key of the keyval */
{
    return (xs_str *)&keyval[1];
}


xs_val *xs_keyval_value(const xs_keyval *keyval)
/* returns a pointer to the value of the keyval */
{
    return (xs_val *)&keyval[1 + xs_size(xs_keyval_key(keyval))];
}


xs_keyval *xs_keyval_make(xs_keyval *keyval, const xs_str *key, const xs_val *value)
/* builds a keyval into mem (should have enough size) */
{
    keyval[0] = XSTYPE_KEYVAL;
    memcpy(xs_keyval_key(keyval),   key,   xs_size(key));
    memcpy(xs_keyval_value(keyval), value, xs_size(value));

    return keyval;
}


/** dicts **/

typedef struct {
    int value_offset;   /* offset to value (from dict start) */
    int next;           /* next node in sequential search */
    int child[4];       /* child nodes in hashed search */
    char key[];         /* C string key */
} ditem_hdr;

typedef struct {
    int size;           /* size of full dict (_XS_TYPE_SIZE) */
    int first;          /* first node for sequential search */
    int root;           /* root node for hashed search */
    /* a bunch of ditem_hdr and value follows */
} dict_hdr;


xs_dict *xs_dict_new(void)
/* creates a new dict */
{
    /* size of dict */
    int sz = 1 + sizeof(dict_hdr);

    xs_dict *d = xs_realloc(NULL, sz);
    memset(d, '\0', sz);

    d[0] = XSTYPE_DICT;
    _xs_put_size(d, sz);

    return d;
}

static int *_xs_dict_locate(const xs_dict *dict, const char *key)
/* locates a ditem */
{
    unsigned int h = xs_hash_func(key, strlen(key));

    /* start from the root */
    dict_hdr *dh = (dict_hdr *)(dict + 1);
    int *off = &dh->root;

    while (*off) {
        /* pointer to ditem */
        ditem_hdr *di = (ditem_hdr *)(dict + *off);

        /* pointer to the key */
        const char *d_key = di->key;

        if (strcmp(key, d_key) == 0)
            break;

        off = &di->child[h >> 30];
        h <<= 2;
    }

    return off;
}


xs_dict *xs_dict_set(xs_dict *dict, const xs_str *key, const xs_val *value)
/* sets a key/value pair */
{
    if (value == NULL)
        value = xs_stock(XSTYPE_NULL);

    if (xs_type(dict) == XSTYPE_DICT) {
        int *o = _xs_dict_locate(dict, key);
        int end = xs_size(dict);

        if (!*o) {
            /* ditem does not exist yet: append to the end */
            *o = end;

            int ksz = xs_size(key);
            int vsz = xs_size(value);
            int dsz = sizeof(ditem_hdr) + ksz + vsz;

            /* open room in the dict for the full ditem */
            dict = xs_expand(dict, end, dsz);

            dict_hdr *dh = (dict_hdr *)(dict + 1);

            /* build the ditem */
            ditem_hdr *di = (ditem_hdr *)(dict + end);
            memset(di, '\0', dsz);

            /* set the offset to the value */
            di->value_offset = end + sizeof(ditem_hdr) + ksz;

            /* copy the key */
            memcpy(di->key, key, ksz);

            /* copy the value */
            memcpy(dict + di->value_offset, value, vsz);

            /* chain to the sequential list */
            di->next = dh->first;
            dh->first = end;
        }
        else {
            /* ditem already exists */
            ditem_hdr *di = (ditem_hdr *)(dict + *o);

            /* get pointer to the value offset */
            int *i = &di->value_offset;

            /* deleted? recover offset */
            if (*i < 0)
                *i *= -1;

            /* get old value */
            xs_val *o_value = dict + *i;

            /* will new value fit over the old one? */
            if (xs_size(value) <= xs_size(o_value)) {
                /* just overwrite */
                /* (difference is leaked inside the dict) */
                memcpy(o_value, value, xs_size(value));
            }
            else {
                /* not enough room: new value will live at the end of the dict */
                /* (old value is leaked inside the dict) */
                *i = end;

                dict = xs_insert(dict, end, value);
            }
        }
    }

    return dict;
}


xs_dict *xs_dict_append(xs_dict *dict, const xs_str *key, const xs_val *value)
/* just an alias (for this implementation it's the same) */
{
    return xs_dict_set(dict, key, value);
}


xs_dict *xs_dict_prepend(xs_dict *dict, const xs_str *key, const xs_val *value)
/* just an alias (for this implementation it's the same) */
{
    return xs_dict_set(dict, key, value);
}


xs_dict *xs_dict_del(xs_dict *dict, const xs_str *key)
/* deletes a key/value pair */
{
    if (xs_type(dict) == XSTYPE_DICT) {
        int *o = _xs_dict_locate(dict, key);

        if (*o) {
            /* found ditem */
            ditem_hdr *di = (ditem_hdr *)(dict + *o);

            /* deleted ditems have a negative value offset */
            di->value_offset *= -1;
        }
    }

    return dict;
}


const xs_val *xs_dict_get(const xs_dict *dict, const xs_str *key)
/* gets a value by key, or NULL */
{
    if (xs_type(dict) == XSTYPE_DICT) {
        int *o = _xs_dict_locate(dict, key);

        if (*o) {
            /* found ditem */
            ditem_hdr *di = (ditem_hdr *)(dict + *o);

            if (di->value_offset > 0)
                return dict + di->value_offset;
        }
    }

    return NULL;
}


int xs_dict_next(const xs_dict *dict, const xs_str **key, const xs_val **value, int *ctxt)
/* dict iterator, with context */
{
    if (xs_type(dict) != XSTYPE_DICT)
        return 0;

    if (*ctxt == 0) {
        /* at the beginning: get the first sequential item */
        const dict_hdr *dh = (dict_hdr *)(dict + 1);
        *ctxt = dh->first;
    }

    *value = NULL;

    while (*value == NULL && *ctxt > 0) {
        const ditem_hdr *di = (ditem_hdr *)(dict + *ctxt);

        /* get value */
        if (di->value_offset > 0) {
            *value = (xs_val *)(dict + di->value_offset);

            /* get key */
            *key = (xs_str *)&di->key;
        }

        /* get offset to next ditem */
        *ctxt = di->next ? di->next : -1;
    }

    return *value != NULL;
}


xs_dict *xs_dict_gc(const xs_dict *dict)
/* creates a copy of dict, but garbage-collected */
{
    xs_dict *nd = xs_dict_new();
    const xs_str *k;
    const xs_val *v;

    xs_dict_foreach(dict, k, v) {
        if (xs_type(v) == XSTYPE_DICT) {
            xs *sd = xs_dict_gc(v);
            nd = xs_dict_set(nd, k, sd);
        }
        else
            nd = xs_dict_set(nd, k, v);
    }

    return nd;
}


const xs_val *xs_dict_get_path_sep(const xs_dict *dict, const char *path, const char *sep)
/* gets a value from dict given a path separated by sep */
{
    /* split by the separator */
    xs *l = xs_split_n(path, sep, 1);

    /* only one part? just get */
    if (xs_list_len(l) == 1)
        return xs_dict_get(dict, path);

    const char *prefix = xs_list_get(l, 0);
    const char *rest   = xs_list_get(l, 1);
    const xs_dict *sd  = xs_dict_get(dict, prefix);

    if (xs_type(sd) == XSTYPE_DICT)
        return xs_dict_get_path_sep(sd, rest, sep);

    return NULL;
}


xs_dict *xs_dict_set_path_sep(xs_dict *dict, const char *path, const xs_val *value, const char *sep)
/* sets a value into dict given a path separated by sep;
   intermediate dicts are created if needed */
{
    /* split by the separator */
    xs *l = xs_split_n(path, sep, 1);

    /* only one part? just set */
    if (xs_list_len(l) == 1)
        return xs_dict_set(dict, path, value);

    const char *prefix = xs_list_get(l, 0);
    const char *rest   = xs_list_get(l, 1);

    xs *nd = NULL;

    /* does the first part of path exist? */
    const xs_dict *cd = xs_dict_get(dict, prefix);

    if (xs_type(cd) == XSTYPE_DICT)
        nd = xs_dup(cd);
    else
        nd = xs_dict_new();

    /* move down the path */
    nd = xs_dict_set_path_sep(nd, rest, value, sep);

    /* set */
    return xs_dict_set(dict, prefix, nd);
}


/** other values **/

xs_val *xs_val_new(xstype t)
/* adds a new special value */
{
    xs_val *v = xs_realloc(NULL, _xs_blk_size(1));

    v[0] = t;

    return v;
}


/** numbers */

xs_number *xs_number_new(double f)
/* adds a new number value */
{
    xs_number *v;
    char tmp[64];

    snprintf(tmp, sizeof(tmp), "%.15lf", f);

    /* strip useless zeros */
    if (strchr(tmp, '.') != NULL) {
        char *ptr;

        for (ptr = tmp + strlen(tmp) - 1; *ptr == '0'; ptr--);

        if (*ptr != '.')
            ptr++;

        *ptr = '\0';
    }

    /* alloc for the marker and the full string */
    v = xs_realloc(NULL, _xs_blk_size(1 + xs_size(tmp)));

    v[0] = XSTYPE_NUMBER;
    memcpy(&v[1], tmp, xs_size(tmp));

    return v;
}


double xs_number_get(const xs_number *v)
/* gets the number as a double */
{
    double f = 0.0;

    if (xs_type(v) == XSTYPE_NUMBER)
        f = atof(&v[1]);
    else
    if (xs_type(v) == XSTYPE_STRING)
        f = atof(v);

    return f;
}


const char *xs_number_str(const xs_number *v)
/* gets the number as a string */
{
    const char *p = NULL;

    if (xs_type(v) == XSTYPE_NUMBER)
        p = &v[1];

    return p;
}


/** raw data blocks **/

xs_data *xs_data_new(const void *data, int size)
/* returns a new raw data value */
{
    xs_data *v;

    /* add the overhead (data type + size) */
    int total_size = size + 1 + _XS_TYPE_SIZE;

    v = xs_realloc(NULL, _xs_blk_size(total_size));
    v[0] = XSTYPE_DATA;

    _xs_put_size(v, total_size);

    memcpy(&v[1 + _XS_TYPE_SIZE], data, size);

    return v;
}


int xs_data_size(const xs_data *value)
/* returns the size of the data stored inside value */
{
    return _xs_get_size(value) - (1 + _XS_TYPE_SIZE);
}


void xs_data_get(void *data, const xs_data *value)
/* copies the raw data stored inside value into data */
{
    memcpy(data, &value[1 + _XS_TYPE_SIZE], xs_data_size(value));
}


void *xs_memmem(const char *haystack, int h_size, const char *needle, int n_size)
/* clone of memmem */
{
    char *p, *r = NULL;
    int offset = 0;

    while (!r && h_size - offset > n_size &&
           (p = memchr(haystack + offset, *needle, h_size - offset))) {
        if (memcmp(p, needle, n_size) == 0)
            r = p;
        else
            offset = p - haystack + 1;
    }

    return r;
}


unsigned int xs_hash_func(const char *data, int size)
/* a general purpose hashing function */
{
    unsigned int hash = 0x666;
    int n;

    for (n = 0; n < size; n++) {
        hash ^= (unsigned char)data[n];
        hash *= 111111111;
    }

    return hash ^ hash >> 16;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_H */
