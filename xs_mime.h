/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_MIME_H

#define _XS_MIME_H

const char *xs_mime_by_ext(const char *file);

extern const char *xs_mime_types[];

#ifdef XS_IMPLEMENTATION

/* intentionally brain-dead simple */
/* CAUTION: sorted */

const char *xs_mime_types[] = {
    "3gp",      "video/3gpp",
    "aac",      "audio/aac",
    "css",      "text/css",
    "flac",     "audio/flac",
    "flv",      "video/flv",
    "gif",      "image/gif",
    "gmi",      "text/gemini",
    "html",     "text/html",
    "jpeg",     "image/jpeg",
    "jpg",      "image/jpeg",
    "json",     "application/json",
    "m4a",      "audio/aac",
    "m4v",      "video/mp4",
    "md",       "text/markdown",
    "mov",      "video/quicktime",
    "mp3",      "audio/mp3",
    "mp4",      "video/mp4",
    "mpg4",     "video/mp4",
    "oga",      "audio/ogg",
    "ogg",      "audio/ogg",
    "ogv",      "video/ogg",
    "opus",     "audio/ogg",
    "png",      "image/png",
    "svg",      "image/svg+xml",
    "svgz",     "image/svg+xml",
    "txt",      "text/plain",
    "wav",      "audio/wav",
    "webm",     "video/webm",
    "webp",     "image/webp",
    "wma",      "audio/wma",
    "xml",      "text/xml",
    NULL,       NULL,
};


const char *xs_mime_by_ext(const char *file)
/* returns the MIME type by file extension */
{
    const char *ext = strrchr(file, '.');

    if (ext) {
        xs *uext = xs_tolower_i(xs_dup(ext + 1));
        int b = 0;
        int t = xs_countof(xs_mime_types) / 2 - 2;

        while (t >= b) {
            int n = (b + t) / 2;
            const char *p = xs_mime_types[n * 2];

            int c = strcmp(uext, p);

            if (c < 0)
                t = n - 1;
            else
            if (c > 0)
                b = n + 1;
            else
                return xs_mime_types[(n * 2) + 1];
        }
    }

    return "application/octet-stream";
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_MIME_H */
