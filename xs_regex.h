/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_REGEX_H

#define _XS_REGEX_H

int xs_regex_match(const char *str, const char *rx);
xs_list *xs_regex_split_n(const char *str, const char *rx, int count);
#define xs_regex_split(str, rx) xs_regex_split_n(str, rx, XS_ALL)
xs_list *xs_regex_select_n(const char *str, const char *rx, int count);
#define xs_regex_select(str, rx) xs_regex_select_n(str, rx, XS_ALL)
xs_list *xs_regex_replace_in(xs_str *str, const char *rx, const char *rep, int count);
#define xs_regex_replace_i(str, rx, rep) xs_regex_replace_in(str, rx, rep, XS_ALL)
#define xs_regex_replace_n(str, rx, rep, count) xs_regex_replace_in(xs_dup(str), rx, rep, count)
#define xs_regex_replace(str, rx, rep) xs_regex_replace_in(xs_dup(str), rx, rep, XS_ALL)

#ifdef XS_IMPLEMENTATION

#ifdef __TINYC__
/* fix a compilation error in tcc */
#define _REGEX_NELTS(n)
#endif

#include <regex.h>

xs_list *xs_regex_split_n(const char *str, const char *rx, int count)
/* splits str using regex as a separator, at most count times.
    Always returns a list:
    len == 0: regcomp error
    len == 1: full string (no matches)
    len == odd: first part [ separator / next part ]...
*/
{
    regex_t re;
    regmatch_t rm;
    int offset = 0;
    xs_list *list = xs_list_new();
    const char *p;

    if (regcomp(&re, rx, REG_EXTENDED))
        return list;

    while (count > 0 && !regexec(&re, (p = str + offset), 1, &rm, offset > 0 ? REG_NOTBOL : 0)) {
        /* add first the leading part of the string */
        xs *s1 = xs_str_new_sz(p, rm.rm_so);
        list = xs_list_append(list, s1);

        /* add now the matched text as the separator */
        xs *s2 = xs_str_new_sz(p + rm.rm_so, rm.rm_eo - rm.rm_so);
        list = xs_list_append(list, s2);

        /* move forward */
        offset += rm.rm_eo;

        count--;
    }

    /* add the rest of the string */
    list = xs_list_append(list, p);

    regfree(&re);

    return list;
}


xs_list *xs_regex_select_n(const char *str, const char *rx, int count)
/* selects all matches and return them as a list */
{
    xs_list *list = xs_list_new();
    xs *split = NULL;
    const xs_val *v;
    int n = 0;

    /* split */
    split = xs_regex_split_n(str, rx, count);

    /* now iterate to get only the 'separators' (odd ones) */
    xs_list_foreach(split, v) {
        if (n & 0x1)
            list = xs_list_append(list, v);

        n++;
    }

    return list;
}


xs_list *xs_regex_replace_in(xs_str *str, const char *rx, const char *rep, int count)
/* replaces all matches with the rep string. If it contains unescaped &,
   they are replaced with the match */
{
    xs_str *s = xs_str_new(NULL);
    xs *split = xs_regex_split_n(str, rx, count);
    const xs_val *v;
    int n = 0;
    int pholder = !!strchr(rep, '&');

    xs_list_foreach(split, v) {
        if (n & 0x1) {
            if (pholder) {
                /* rep has a placeholder; process char by char */
                const char *p = rep;

                while (*p) {
                    if (*p == '&')
                        s = xs_str_cat(s, v);
                    else {
                        if (*p == '\\')
                            p++;

                        if (!*p)
                            break;

                        s = xs_append_m(s, p, 1);
                    }

                    p++;
                }
            }
            else
                s = xs_str_cat(s, rep);
        }
        else
            s = xs_str_cat(s, v);

        n++;
    }

    xs_free(str);

    return s;
}


int xs_regex_match(const char *str, const char *rx)
/* returns if str matches the regex at least once */
{
    xs *l = xs_regex_select_n(str, rx, 1);

    return xs_list_len(l) == 1;
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_REGEX_H */
