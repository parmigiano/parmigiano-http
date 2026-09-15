#include "utilities.h"

#include <string.h>

static int starts_with(const char* s, const char* p)
{
    return s && p && strncmp(s, p, strlen(p)) == 0;
}

const char* map_mime_to_msg_type(const char* mime)
{
    if (!mime || *mime == '\0')
        return "file";

    /* ---------- IMAGE ---------- */
    if (starts_with(mime, "image/"))
        return "image";

    if (strcmp(mime, "application/photoshop") == 0 || strcmp(mime, "image/vnd.adobe.photoshop") == 0)
        return "file";

    /* ---------- VIDEO ---------- */
    if (starts_with(mime, "video/"))
        return "video";

    if (strcmp(mime, "application/x-mpegURL") == 0 || // m3u8
        strcmp(mime, "application/vnd.apple.mpegurl") == 0)
        return "video";

    /* ---------- VOICE / AUDIO ---------- */
    if (starts_with(mime, "audio/"))
        return "voice";

    if (strcmp(mime, "application/ogg") == 0 || strcmp(mime, "audio/ogg") == 0 || strcmp(mime, "audio/opus") == 0 || strcmp(mime, "audio/webm") == 0)
        return "voice";

    /* ---------- TEXT ---------- */
    if (starts_with(mime, "text/"))
        return "file";

    if (strcmp(mime, "application/json") == 0 || strcmp(mime, "application/xml") == 0)
        return "file";

    /* ---------- DOCUMENTS / FILE ---------- */
    if (strcmp(mime, "application/pdf") == 0 || strcmp(mime, "application/zip") == 0 || strcmp(mime, "application/x-zip-compressed") == 0 ||
        strcmp(mime, "application/x-rar-compressed") == 0 || strcmp(mime, "application/vnd.rar") == 0 ||
        strcmp(mime, "application/x-7z-compressed") == 0)
        return "file";

    /* Microsoft Office */
    if (strcmp(mime, "application/msword") == 0 || strcmp(mime, "application/vnd.openxmlformats-officedocument.wordprocessingml.document") == 0 ||
        strcmp(mime, "application/vnd.ms-excel") == 0 || strcmp(mime, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") == 0 ||
        strcmp(mime, "application/vnd.ms-powerpoint") == 0 ||
        strcmp(mime, "application/vnd.openxmlformats-officedocument.presentationml.presentation") == 0)
        return "file";

    /* Apple formats */
    if (strcmp(mime, "application/vnd.apple.pages") == 0 || strcmp(mime, "application/vnd.apple.numbers") == 0 ||
        strcmp(mime, "application/vnd.apple.keynote") == 0)
        return "file";

    if (strcmp(mime, "application/octet-stream") == 0)
        return "file";

    return "file";
}