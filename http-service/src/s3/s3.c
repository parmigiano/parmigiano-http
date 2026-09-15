#include "s3.h"

#include "logger.h"

#include <libs3.h>
#include <string.h>
#include <uuid/uuid.h>

typedef struct
{
    FILE* file;
} s3_upload_ctx_t;

static int put_object_cb(int bufferSize, char* buffer, void* callbackData)
{
    s3_upload_ctx_t* ctx = (s3_upload_ctx_t*)callbackData;
    return fread(buffer, 1, bufferSize, ctx->file);
}

static void response_complete_cb(S3Status status, const S3ErrorDetails* error, void* callbackData)
{
    (void)callbackData;

    if (status != S3StatusOK)
    {
        logger_error("response_complete_cb: s3 error: %s", error && error->message ? error->message : "unknown");
    }
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
    if (!url || !bucket)
        return NULL;

    char pattern[256];
    snprintf(pattern, sizeof(pattern), "/%s/", bucket);

    const char* p = strstr(url, pattern);
    if (!p)
        return NULL;

    p += strlen(pattern);
    return p;
}

static char* create_unique_key(const char* filename, const char* key_p)
{
    if (!filename || !key_p)
        return NULL;

    /* Get .ext file */
    const char* ext = strrchr(filename, '.');
    if (!ext)
        ext = ".tmp";

    /* UUID */
    uuid_t binuuid;
    char uuid_str[37];

    uuid_generate_random(binuuid);
    uuid_unparse_lower(binuuid, uuid_str);
    /* UUID */

    char uniq_key[512];
    snprintf(uniq_key, sizeof(uniq_key), "%s/%s%s", key_p, uuid_str, ext);

    return strdup(uniq_key);
}

char* s3_upload_file_pub(FILE* f, const char* filename, char* content_type, const char* key, s3_config_t* cfg)
{
    if (!f || !filename || !cfg)
        return NULL;

    if (S3_initialize("parmigianochat/v2", S3_INIT_ALL, cfg->endpoint) != S3StatusOK)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    s3_upload_ctx_t upload_ctx = {.file = f};
    S3BucketContext bucket = make_bucket_ctx(cfg);

    S3PutObjectHandler handler = {.responseHandler = {.completeCallback = response_complete_cb, .propertiesCallback = NULL},
                                  .putObjectDataCallback = put_object_cb};

    char* unique_key = create_unique_key(filename, key);
    if (!unique_key)
        unique_key = strdup(key);

    S3PutProperties put_props;
    memset(&put_props, 0, sizeof(put_props));

    put_props.contentType = content_type;

    S3_put_object(&bucket, unique_key, (uint64_t)size, &put_props, NULL, 0, &handler, &upload_ctx);

    S3_deinitialize();

    char url[1024];
    snprintf(url, sizeof(url), "https://%s/%s/%s", cfg->endpoint, cfg->bucket, unique_key);

    return strdup(url);
}

char* s3_upload_file_prv(FILE* f, const char* filename, char* content_type, const char* key, s3_config_t* cfg)
{
    if (!f || !filename || !cfg)
        return NULL;

    if (S3_initialize("parmigianochat/v2", S3_INIT_ALL, cfg->endpoint) != S3StatusOK)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    s3_upload_ctx_t upload_ctx = {.file = f};
    S3BucketContext bucket = make_bucket_ctx(cfg);

    S3PutObjectHandler handler = {.responseHandler = {.completeCallback = response_complete_cb, .propertiesCallback = NULL},
                                  .putObjectDataCallback = put_object_cb};

    char* unique_key = create_unique_key(filename, key);
    if (!unique_key)
        unique_key = strdup(key);

    S3PutProperties put_props;
    memset(&put_props, 0, sizeof(put_props));

    put_props.contentType = content_type;

    S3_put_object(&bucket, unique_key, (uint64_t)size, &put_props, NULL, 0, &handler, &upload_ctx);

    S3_deinitialize();

    return strdup(unique_key);
}

int s3_delete_file(const char* url, s3_config_t* cfg)
{
    if (!url || !cfg)
        return 1;

    const char* key = url_to_key(url, cfg->bucket);

    if (S3_initialize("parmigianochat/v2", S3_INIT_ALL, cfg->endpoint) != S3StatusOK)
        return 1;

    S3BucketContext bucket = make_bucket_ctx(cfg);

    S3ResponseHandler handler = {.completeCallback = response_complete_cb, .propertiesCallback = NULL};

    S3_delete_object(&bucket, key, NULL, 0, &handler, NULL);

    S3_deinitialize();

    return 0;
}