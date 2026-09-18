#include "utilities.h"

#include <ctype.h>
#include <string.h>

static size_t mime_length(const char* mime)
{
    if (!mime)
        return 0;

    size_t len = strcspn(mime, ";");
    while (len > 0 && isspace((unsigned char)mime[len - 1]))
        len--;

    return len;
}

static int mime_equals(const char* mime, const char* expected)
{
    if (!mime || !expected)
        return 0;

    size_t mime_len = mime_length(mime);
    size_t expected_len = strlen(expected);
    if (mime_len != expected_len)
        return 0;

    for (size_t i = 0; i < mime_len; ++i)
    {
        if (tolower((unsigned char)mime[i]) != tolower((unsigned char)expected[i]))
            return 0;
    }

    return 1;
}

static int mime_family(const char* mime, const char* family)
{
    if (!mime || !family)
        return 0;

    size_t family_len = strlen(family);
    size_t mime_len = mime_length(mime);
    if (mime_len < family_len)
        return 0;

    for (size_t i = 0; i < family_len; ++i)
    {
        if (tolower((unsigned char)mime[i]) != tolower((unsigned char)family[i]))
            return 0;
    }

    return 1;
}

const char* map_mime_to_msg_type(const char* mime)
{
    if (!mime || *mime == '\0')
        return "file";

    /* ---------- IMAGE ---------- */
    if (mime_family(mime, "image/"))
        return "image";

    if (mime_equals(mime, "application/photoshop") || mime_equals(mime, "image/vnd.adobe.photoshop"))
        return "file";

    /* ---------- VIDEO ---------- */
    if (mime_family(mime, "video/"))
        return "video";

    if (mime_equals(mime, "application/x-mpegurl") || mime_equals(mime, "application/vnd.apple.mpegurl"))
        return "video";

    /* ---------- VOICE / AUDIO ---------- */
    if (mime_family(mime, "audio/"))
        return "voice";

    if (mime_equals(mime, "application/ogg"))
        return "voice";

    /* ---------- TEXT ---------- */
    if (mime_family(mime, "text/"))
        return "file";

    if (mime_equals(mime, "application/json") || mime_equals(mime, "application/xml"))
        return "file";

    /* ---------- DOCUMENTS / FILE ---------- */
    if (mime_equals(mime, "application/pdf") || mime_equals(mime, "application/zip") ||
        mime_equals(mime, "application/x-zip-compressed") || mime_equals(mime, "application/x-rar-compressed") ||
        mime_equals(mime, "application/vnd.rar") || mime_equals(mime, "application/x-7z-compressed"))
        return "file";

    /* Microsoft Office */
    if (mime_equals(mime, "application/msword") ||
        mime_equals(mime, "application/vnd.openxmlformats-officedocument.wordprocessingml.document") ||
        mime_equals(mime, "application/vnd.ms-excel") ||
        mime_equals(mime, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") ||
        mime_equals(mime, "application/vnd.ms-powerpoint") ||
        mime_equals(mime, "application/vnd.openxmlformats-officedocument.presentationml.presentation"))
        return "file";

    /* Apple formats */
    if (mime_equals(mime, "application/vnd.apple.pages") || mime_equals(mime, "application/vnd.apple.numbers") ||
        mime_equals(mime, "application/vnd.apple.keynote"))
        return "file";

    return "file";
}
