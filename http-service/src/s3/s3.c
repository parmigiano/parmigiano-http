#include "s3.h"

#include "logger.h"

#include <libs3.h>
#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <uuid/uuid.h>

typedef struct
{
    FILE* file;
    bool completed;
    bool success;
} s3_request_ctx_t;

static pthread_mutex_t s3_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s3_initialized = false;
static bool s3_cleanup_registered = false;

static void s3_global_deinitialize(void)
{
    pthread_mutex_lock(&s3_init_mutex);

    if (s3_initialized)
    {
        S3_deinitialize();
        s3_initialized = false;
    }

    pthread_mutex_unlock(&s3_init_mutex);
}

static bool ensure_s3_initialized(const char* endpoint)
{
    bool ready = false;

    pthread_mutex_lock(&s3_init_mutex);

    if (s3_initialized)
    {
        ready = true;
    }
    else if (S3_initialize("parmigianochat/v2", S3_INIT_ALL, endpoint) == S3StatusOK)
    {
        s3_initialized = true;
        ready = true;

        if (!s3_cleanup_registered)
        {
            if (atexit(s3_global_deinitialize) == 0)
                s3_cleanup_registered = true;
            else
                logger_warn("ensure_s3_initialized: failed to register libs3 cleanup");
        }
    }

    pthread_mutex_unlock(&s3_init_mutex);
    return ready;
}

static int put_object_cb(int bufferSize, char* buffer, void* callbackData)
{
    s3_request_ctx_t* ctx = (s3_request_ctx_t*)callbackData;
    if (!ctx || !ctx->file || bufferSize <= 0)
        return 0;

    return (int)fread(buffer, 1, (size_t)bufferSize, ctx->file);
}

static void response_complete_cb(S3Status status, const S3ErrorDetails* error, void* callbackData)
{
    s3_request_ctx_t* ctx = (s3_request_ctx_t*)callbackData;
    if (ctx)
    {
        ctx->completed = true;
        ctx->success = status == S3StatusOK;
    }

    if (status != S3StatusOK)
    {
        logger_error("response_complete_cb: s3 error: %s", error && error->message ? error->message : "unknown");
    }
}

static bool config_valid(const s3_config_t* cfg)
{
    return cfg && cfg->endpoint && *cfg->endpoint && cfg->bucket && *cfg->bucket && cfg->access_key && *cfg->access_key &&
           cfg->secret_key && *cfg->secret_key;
}

static S3BucketContext make_bucket_ctx(s3_config_t* cfg)
{
    S3BucketContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.hostName = cfg->endpoint;
    ctx.bucketName = cfg->bucket;
    ctx.protocol = S3ProtocolHTTPS;
    ctx.uriStyle = S3UriStylePath;
    ctx.accessKeyId = cfg->access_key;
    ctx.secretAccessKey = cfg->secret_key;
    ctx.authRegion = cfg->region;

    return ctx;
}

static const char* url_to_key(const char* url, const char* bucket)
{
    if (!url || !*url || !bucket || !*bucket)
        return NULL;

    char pattern[256];
    int written = snprintf(pattern, sizeof(pattern), "/%s/", bucket);
    if (written < 0 || (size_t)written >= sizeof(pattern))
        return NULL;

    const char* p = strstr(url, pattern);
    if (!p)
        return NULL;

    p += strlen(pattern);
    return *p ? p : NULL;
}

static bool mime_is(const char* content_type, const char* expected)
{
    if (!content_type || !expected)
        return false;

    size_t content_len = strcspn(content_type, ";");
    while (content_len > 0 && isspace((unsigned char)content_type[content_len - 1]))
        content_len--;

    size_t expected_len = strlen(expected);
    if (content_len != expected_len)
        return false;

    for (size_t i = 0; i < content_len; ++i)
    {
        if (tolower((unsigned char)content_type[i]) != tolower((unsigned char)expected[i]))
            return false;
    }

    return true;
}

static const char* extension_from_mime(const char* content_type)
{
    if (mime_is(content_type, "image/jpeg"))
        return ".jpg";
    if (mime_is(content_type, "image/png"))
        return ".png";
    if (mime_is(content_type, "image/gif"))
        return ".gif";
    if (mime_is(content_type, "image/webp"))
        return ".webp";
    if (mime_is(content_type, "video/mp4"))
        return ".mp4";
    if (mime_is(content_type, "video/webm"))
        return ".webm";
    if (mime_is(content_type, "video/quicktime"))
        return ".mov";
    if (mime_is(content_type, "audio/mpeg"))
        return ".mp3";
    if (mime_is(content_type, "audio/ogg") || mime_is(content_type, "application/ogg"))
        return ".ogg";
    if (mime_is(content_type, "audio/wav") || mime_is(content_type, "audio/x-wav"))
        return ".wav";
    if (mime_is(content_type, "application/pdf"))
        return ".pdf";
    if (mime_is(content_type, "application/zip") || mime_is(content_type, "application/x-zip-compressed"))
        return ".zip";
    if (mime_is(content_type, "application/json"))
        return ".json";
    if (mime_is(content_type, "text/plain"))
        return ".txt";
    if (mime_is(content_type, "application/vnd.openxmlformats-officedocument.wordprocessingml.document"))
        return ".docx";
    if (mime_is(content_type, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"))
        return ".xlsx";
    if (mime_is(content_type, "application/vnd.openxmlformats-officedocument.presentationml.presentation"))
        return ".pptx";

    return ".bin";
}

static bool extract_safe_extension(const char* filename, char* output, size_t output_size)
{
    if (!filename || !*filename || !output || output_size < 3)
        return false;

    const char* basename = filename;
    const char* slash = strrchr(filename, '/');
    const char* backslash = strrchr(filename, '\\');

    if (slash && slash + 1 > basename)
        basename = slash + 1;
    if (backslash && backslash + 1 > basename)
        basename = backslash + 1;

    const char* dot = strrchr(basename, '.');
    if (!dot || dot == basename)
        return false;

    size_t len = strlen(dot);
    if (len < 2 || len >= output_size || len > 16)
        return false;

    output[0] = '.';
    for (size_t i = 1; i < len; ++i)
    {
        unsigned char ch = (unsigned char)dot[i];
        if (!isalnum(ch))
            return false;
        output[i] = (char)tolower(ch);
    }
    output[len] = '\0';

    return true;
}

static char* create_unique_key(const char* filename, const char* content_type, const char* key_p)
{
    if (!key_p || !*key_p)
        return NULL;

    char ext[17] = {0};
    if (!extract_safe_extension(filename, ext, sizeof(ext)) || strcmp(ext, ".tmp") == 0 || strcmp(ext, ".bin") == 0)
        snprintf(ext, sizeof(ext), "%s", extension_from_mime(content_type));

    uuid_t binuuid;
    char uuid_str[37];

    uuid_generate_random(binuuid);
    uuid_unparse_lower(binuuid, uuid_str);

    char uniq_key[512];
    int written = snprintf(uniq_key, sizeof(uniq_key), "%s/%s%s", key_p, uuid_str, ext);
    if (written < 0 || (size_t)written >= sizeof(uniq_key))
        return NULL;

    return strdup(uniq_key);
}

static char* upload_file(FILE* f, const char* filename, const char* content_type, const char* key, s3_config_t* cfg)
{
    if (!f || !key || !*key || !config_valid(cfg))
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
        return NULL;

    long size = ftell(f);
    if (size < 0)
        return NULL;

    rewind(f);

    char* unique_key = create_unique_key(filename, content_type, key);
    if (!unique_key)
        return NULL;

    if (!ensure_s3_initialized(cfg->endpoint))
    {
        free(unique_key);
        return NULL;
    }

    s3_request_ctx_t request_ctx = {
        .file = f,
        .completed = false,
        .success = false,
    };

    S3BucketContext bucket = make_bucket_ctx(cfg);

    S3PutObjectHandler handler = {
        .responseHandler = {.completeCallback = response_complete_cb, .propertiesCallback = NULL},
        .putObjectDataCallback = put_object_cb,
    };

    S3PutProperties put_props;
    memset(&put_props, 0, sizeof(put_props));
    put_props.contentType = content_type && *content_type ? content_type : "application/octet-stream";

    S3_put_object(&bucket, unique_key, (uint64_t)size, &put_props, NULL, 0, &handler, &request_ctx);

    if (!request_ctx.completed || !request_ctx.success)
    {
        free(unique_key);
        return NULL;
    }

    return unique_key;
}

char* s3_upload_file_pub(FILE* f, const char* filename, const char* content_type, const char* key, s3_config_t* cfg)
{
    char* unique_key = upload_file(f, filename, content_type, key, cfg);
    if (!unique_key)
        return NULL;

    char url[1024];
    int written = snprintf(url, sizeof(url), "https://%s/%s/%s", cfg->endpoint, cfg->bucket, unique_key);
    free(unique_key);

    if (written < 0 || (size_t)written >= sizeof(url))
        return NULL;

    return strdup(url);
}

char* s3_upload_file_prv(FILE* f, const char* filename, const char* content_type, const char* key, s3_config_t* cfg)
{
    return upload_file(f, filename, content_type, key, cfg);
}

int s3_delete_key(const char* key, s3_config_t* cfg)
{
    if (!key || !*key || !config_valid(cfg))
        return 1;

    if (!ensure_s3_initialized(cfg->endpoint))
        return 1;

    s3_request_ctx_t request_ctx = {
        .file = NULL,
        .completed = false,
        .success = false,
    };

    S3BucketContext bucket = make_bucket_ctx(cfg);
    S3ResponseHandler handler = {.completeCallback = response_complete_cb, .propertiesCallback = NULL};

    S3_delete_object(&bucket, key, NULL, 0, &handler, &request_ctx);

    return request_ctx.completed && request_ctx.success ? 0 : 1;
}

int s3_delete_file(const char* url, s3_config_t* cfg)
{
    if (!url || !*url || !config_valid(cfg))
        return 1;

    const char* key = url_to_key(url, cfg->bucket);
    if (!key)
        return 1;

    return s3_delete_key(key, cfg);
}
