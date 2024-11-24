/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_MATCH_H

#define _XS_MATCH_H

/* spec is very similar to shell file globbing:
   an * matches anything;
   a ? matches any character;
   | select alternative strings to match;
   a \\ escapes a special character;
   any other char matches itself. */

int xs_match(const char *str, const char *spec);

#ifdef XS_IMPLEMENTATION

int xs_match(const char *str, const char *spec)
{
    const char *b_str;
    const char *b_spec = NULL;
    const char *o_str  = str;

retry:

    for (;;) {
        char c = *str++;
        char p = *spec++;

        if (c == '\0') {
            /* end of string; also end of spec? */
            if (p == '\0' || p == '|')
                return 1;
            else
                break;
        }
        else
        if (p == '?') {
            /* match anything except the end */
            if (c == '\0')
                return 0;
        }
        else
        if (p == '*') {
            /* end of spec? match */
            if (*spec == '\0')
                return 1;

            /* store spec for later */
            b_spec = spec;

            /* back one char */
            b_str  = --str;
        }
        else {
            if (p == '\\')
                p = *spec++;

            if (c != p) {
                /* mismatch; do we have a backtrack? */
                if (b_spec) {
                    /* continue where we left, one char forward */
                    spec = b_spec;
                    str  = ++b_str;
                }
                else
                    break;
            }
        }
    }

    /* try to find an alternative mark */
    while (*spec) {
        char p = *spec++;

        if (p == '\\')
            p = *spec++;

        if (p == '|') {
            /* no backtrack spec, restart str from the beginning */
            b_spec = NULL;
            str    = o_str;

            goto retry;
        }
    }

    return 0;
}

#endif /* XS_IMPLEMENTATION */

#endif /* XS_MATCH_H */
