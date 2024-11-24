/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_JSON_H

#define _XS_JSON_H

int xs_json_dump(const xs_val *data, int indent, FILE *f);
xs_str *xs_json_dumps(const xs_val *data, int indent);

xs_val *xs_json_load(FILE *f);
xs_val *xs_json_loads(const xs_str *json);

xstype xs_json_load_type(FILE *f);
int xs_json_load_array_iter(FILE *f, xs_val **value, xstype *pt, int *c);
int xs_json_load_object_iter(FILE *f, xs_str **key, xs_val **value, xstype *pt, int *c);
xs_list *xs_json_load_array(FILE *f);
xs_dict *xs_json_load_object(FILE *f);


#ifdef XS_IMPLEMENTATION

/** IMPLEMENTATION **/

/** JSON dumps **/

static void _xs_json_dump_str(const char *data, FILE *f)
/* dumps a string in JSON format */
{
    unsigned char c;
    fputs("\"", f);

    while ((c = *data)) {
        if (c == '\n')
            fputs("\\n", f);
        else
        if (c == '\r')
            fputs("\\r", f);
        else
        if (c == '\t')
            fputs("\\t", f);
        else
        if (c == '\\')
            fputs("\\\\", f);
        else
        if (c == '"')
            fputs("\\\"", f);
        else
        if (c < 32)
            fprintf(f, "\\u%04x", (unsigned int) c);
        else
            fputc(c, f);

        data++;
    }

    fputs("\"", f);
}


static void _xs_json_indent(int level, int indent, FILE *f)
/* adds indentation */
{
    if (indent) {
        int n;

        fputc('\n', f);

        for (n = 0; n < level * indent; n++)
            fputc(' ', f);
    }
}


static void _xs_json_dump(const xs_val *data, int level, int indent, FILE *f)
/* dumps partial data as JSON */
{
    int c = 0;
    const xs_val *v;

    switch (xs_type(data)) {
    case XSTYPE_NULL:
        fputs("null", f);
        break;

    case XSTYPE_TRUE:
        fputs("true", f);
        break;

    case XSTYPE_FALSE:
        fputs("false", f);
        break;

    case XSTYPE_NUMBER:
        fputs(xs_number_str(data), f);
        break;

    case XSTYPE_LIST:
        fputc('[', f);

        xs_list_foreach(data, v) {
            if (c != 0)
                fputc(',', f);

            _xs_json_indent(level + 1, indent, f);
            _xs_json_dump(v, level + 1, indent, f);

            c++;
        }

        _xs_json_indent(level, indent, f);
        fputc(']', f);

        break;

    case XSTYPE_DICT:
        fputc('{', f);

        const xs_str *k;

        xs_dict_foreach(data, k, v) {
            if (c != 0)
                fputc(',', f);

            _xs_json_indent(level + 1, indent, f);

            _xs_json_dump_str(k, f);
            fputc(':', f);

            if (indent)
                fputc(' ', f);

            _xs_json_dump(v, level + 1, indent, f);

            c++;
        }

        _xs_json_indent(level, indent, f);
        fputc('}', f);
        break;

    case XSTYPE_STRING:
        _xs_json_dump_str(data, f);
        break;

    default:
        break;
    }
}


xs_str *xs_json_dumps(const xs_val *data, int indent)
/* dumps data as a JSON string */
{
    xs_str *s = NULL;
    size_t sz;
    FILE *f;

    if ((f = open_memstream(&s, &sz)) != NULL) {
        int r = xs_json_dump(data, indent, f);
        fclose(f);

        if (!r)
            s = xs_free(s);
    }

    return s;
}


int xs_json_dump(const xs_val *data, int indent, FILE *f)
/* dumps data into a file as JSON */
{
    xstype t = xs_type(data);

    if (t == XSTYPE_LIST || t == XSTYPE_DICT) {
        _xs_json_dump(data, 0, indent, f);
        return 1;
    }

    return 0;
}


/** JSON loads **/

typedef enum {
    JS_ERROR = -1,
    JS_INCOMPLETE,
    JS_OCURLY,
    JS_OBRACK,
    JS_CCURLY,
    JS_CBRACK,
    JS_COMMA,
    JS_COLON,
    JS_VALUE,
    JS_STRING,
    JS_NUMBER,
    JS_TRUE,
    JS_FALSE,
    JS_NULL,
    JS_ARRAY,
    JS_OBJECT
} js_type;


static xs_val *_xs_json_load_lexer(FILE *f, js_type *t)
{
    int c;
    xs_val *v = NULL;
    int offset;

    *t = JS_ERROR;

    /* skip blanks */
    while ((c = fgetc(f)) == ' ' || c == '\t' || c == '\n' || c == '\r');

    if (c == '{')
        *t = JS_OCURLY;
    else
    if (c == '}')
        *t = JS_CCURLY;
    else
    if (c == '[')
        *t = JS_OBRACK;
    else
    if (c == ']')
        *t = JS_CBRACK;
    else
    if (c == ',')
        *t = JS_COMMA;
    else
    if (c == ':')
        *t = JS_COLON;
    else
    if (c == '"') {
        *t = JS_STRING;

        v = xs_str_new(NULL);
        offset = 0;

        while ((c = fgetc(f)) != '"' && c != EOF && *t != JS_ERROR) {
            if (c == '\\') {
                unsigned int cp = fgetc(f);

                switch (cp) {
                case 'n': cp = '\n'; break;
                case 'r': cp = '\r'; break;
                case 't': cp = '\t'; break;
                case 'u': /* Unicode codepoint as an hex char */
                    if (fscanf(f, "%04x", &cp) != 1) {
                        *t = JS_ERROR;
                        break;
                    }

                    if (xs_is_surrogate(cp)) {
                        /* \u must follow */
                        if (fgetc(f) != '\\' || fgetc(f) != 'u') {
                            *t = JS_ERROR;
                            break;
                        }

                        unsigned int p2;
                        if (fscanf(f, "%04x", &p2) != 1) {
                            *t = JS_ERROR;
                            break;
                        }

                        cp = xs_surrogate_dec(cp, p2);
                    }

                    /* replace dangerous control codes with their visual representations */
                    if (cp < ' ' && !strchr("\r\n\t", cp))
                        cp += 0x2400;

                    break;
                }

                v = xs_utf8_insert(v, cp, &offset);
            }
            else {
                char cc = c;
                v = xs_insert_m(v, offset, &cc, 1);
                offset++;
            }
        }

        if (c == EOF)
            *t = JS_ERROR;
    }
    else
    if (c == '-' || (c >= '0' && c <= '9') || c == '.') {
        double d;

        ungetc(c, f);
        if (fscanf(f, "%lf", &d) == 1) {
            *t = JS_NUMBER;
            v = xs_number_new(d);
        }
    }
    else
    if (c == 't') {
        if (fgetc(f) == 'r' && fgetc(f) == 'u' && fgetc(f) == 'e') {
            *t = JS_TRUE;
            v = xs_val_new(XSTYPE_TRUE);
        }
    }
    else
    if (c == 'f') {
        if (fgetc(f) == 'a' && fgetc(f) == 'l' &&
            fgetc(f) == 's' && fgetc(f) == 'e') {
            *t = JS_FALSE;
            v = xs_val_new(XSTYPE_FALSE);
        }
    }
    else
    if (c == 'n') {
        if (fgetc(f) == 'u' && fgetc(f) == 'l' && fgetc(f) == 'l') {
            *t = JS_NULL;
            v = xs_val_new(XSTYPE_NULL);
        }
    }

    if (*t == JS_ERROR)
        v = xs_free(v);

    return v;
}


int xs_json_load_array_iter(FILE *f, xs_val **value, xstype *pt, int *c)
/* loads the next scalar value from the JSON stream */
/* if the value ahead is compound, value is NULL and pt is set */
{
    js_type t;

    *value = _xs_json_load_lexer(f, &t);

    if (t == JS_ERROR)
        return -1;

    if (t == JS_CBRACK)
        return 0;

    if (*c > 0) {
        if (t == JS_COMMA)
            *value = _xs_json_load_lexer(f, &t);
        else
            return -1;
    }

    if (*value == NULL) {
        /* possible compound type ahead */
        if (t == JS_OBRACK)
            *pt = XSTYPE_LIST;
        else
        if (t == JS_OCURLY)
            *pt = XSTYPE_DICT;
        else
            return -1;
    }

    *c = *c + 1;

    return 1;
}


xs_list *xs_json_load_array(FILE *f)
/* loads a full JSON array (after the initial OBRACK) */
{
    xstype t;
    xs_list *l = xs_list_new();
    int c = 0;

    for (;;) {
        xs *v = NULL;
        int r = xs_json_load_array_iter(f, &v, &t, &c);

        if (r == -1)
            l = xs_free(l);

        if (r == 1) {
            /* partial load? */
            if (v == NULL) {
                if (t == XSTYPE_LIST)
                    v = xs_json_load_array(f);
                else
                if (t == XSTYPE_DICT)
                    v = xs_json_load_object(f);
            }

            /* still null? fail */
            if (v == NULL) {
                l = xs_free(l);
                break;
            }

            l = xs_list_append(l, v);
        }
        else
            break;
    }

    return l;
}


int xs_json_load_object_iter(FILE *f, xs_str **key, xs_val **value, xstype *pt, int *c)
/* loads the next key and scalar value from the JSON stream */
/* if the value ahead is compound, value is NULL and pt is set */
{
    js_type t;

    *key = _xs_json_load_lexer(f, &t);

    if (t == JS_ERROR)
        return -1;

    if (t == JS_CCURLY)
        return 0;

    if (*c > 0) {
        if (t == JS_COMMA)
            *key = _xs_json_load_lexer(f, &t);
        else
            return -1;
    }

    if (t != JS_STRING)
        return -1;

    xs_free(_xs_json_load_lexer(f, &t));

    if (t != JS_COLON)
        return -1;

    *value = _xs_json_load_lexer(f, &t);

    if (*value == NULL) {
        /* possible complex type ahead */
        if (t == JS_OBRACK)
            *pt = XSTYPE_LIST;
        else
        if (t == JS_OCURLY)
            *pt = XSTYPE_DICT;
        else
            return -1;
    }

    *c = *c + 1;

    return 1;
}


xs_dict *xs_json_load_object(FILE *f)
/* loads a full JSON object (after the initial OCURLY) */
{
    xstype t;
    xs_dict *d = xs_dict_new();
    int c = 0;

    for (;;) {
        xs *k = NULL;
        xs *v = NULL;
        int r = xs_json_load_object_iter(f, &k, &v, &t, &c);

        if (r == -1)
            d = xs_free(d);

        if (r == 1) {
            /* partial load? */
            if (v == NULL) {
                if (t == XSTYPE_LIST)
                    v = xs_json_load_array(f);
                else
                if (t == XSTYPE_DICT)
                    v = xs_json_load_object(f);
            }

            /* still null? fail */
            if (v == NULL) {
                d = xs_free(d);
                break;
            }

            d = xs_dict_append(d, k, v);
        }
        else
            break;
    }

    return d;
}


xs_val *xs_json_loads(const xs_str *json)
/* loads a string in JSON format and converts to a multiple data */
{
    FILE *f;
    xs_val *v = NULL;

    if ((f = fmemopen((char *)json, strlen(json), "r")) != NULL) {
        v = xs_json_load(f);
        fclose(f);
    }

    return v;
}


xstype xs_json_load_type(FILE *f)
/* identifies the type of a JSON stream */
{
    xstype t = XSTYPE_NULL;
    js_type jt;

    xs_free(_xs_json_load_lexer(f, &jt));

    if (jt == JS_OBRACK)
        t = XSTYPE_LIST;
    else
    if (jt == JS_OCURLY)
        t = XSTYPE_DICT;

    return t;
}


xs_val *xs_json_load(FILE *f)
/* loads a JSON file */
{
    xs_val *v = NULL;
    xstype t = xs_json_load_type(f);

    if (t == XSTYPE_LIST)
        v = xs_json_load_array(f);
    else
    if (t == XSTYPE_DICT)
        v = xs_json_load_object(f);

    return v;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_JSON_H */
