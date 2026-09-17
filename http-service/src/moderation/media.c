#define _POSIX_C_SOURCE 200809L

#include "moderation.h"

#include "logger.h"
#include "nsfw.h"
#include "s3.h"

#include <libs3.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#define MODERATION_MAX_DOWNLOAD_BYTES (500ULL * 1024ULL * 1024ULL)
#define MODERATION_PROCESS_TIMEOUT_SEC 20

typedef struct {
    FILE* file;
    size_t written;
    size_t max_bytes;
    bool too_large;
    S3Status status;
    char content_type[128];
} moderation_s3_download_t;

typedef struct {
    S3Status status;
} moderation_s3_action_t;

static int ascii_ieq(const char* a, const char* b)
{
    if (!a || !b)
        return 0;

    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int ascii_starts_with_ci(const char* value, const char* prefix)
{
    if (!value || !prefix)
        return 0;

    while (*prefix)
    {
        if (!*value || tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
            return 0;
        ++value;
        ++prefix;
    }
    return 1;
}

static bool s3_config_valid(const s3_config_t* cfg)
{
    return cfg && cfg->endpoint && *cfg->endpoint && cfg->bucket && *cfg->bucket &&
           cfg->access_key && *cfg->access_key && cfg->secret_key && *cfg->secret_key &&
           cfg->region && *cfg->region;
}

static S3BucketContext make_bucket_ctx(const s3_config_t* cfg)
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

static const char* avatar_url_to_key(const char* url, const char* bucket)
{
    if (!url || !bucket)
        return NULL;

    char pattern[256];
    int written = snprintf(pattern, sizeof(pattern), "/%s/", bucket);
    if (written <= 0 || (size_t)written >= sizeof(pattern))
        return NULL;

    const char* key = strstr(url, pattern);
    if (!key)
        return NULL;

    key += strlen(pattern);
    return *key ? key : NULL;
}

static S3Status download_properties_cb(const S3ResponseProperties* properties, void* callback_data)
{
    moderation_s3_download_t* ctx = callback_data;
    if (!ctx || !properties)
        return S3StatusAbortedByCallback;

    if (properties->contentLength > ctx->max_bytes)
    {
        ctx->too_large = true;
        return S3StatusAbortedByCallback;
    }

    if (properties->contentType)
        snprintf(ctx->content_type, sizeof(ctx->content_type), "%s", properties->contentType);

    return S3StatusOK;
}

static S3Status download_data_cb(int buffer_size, const char* buffer, void* callback_data)
{
    moderation_s3_download_t* ctx = callback_data;
    if (!ctx || !ctx->file || buffer_size < 0 || (!buffer && buffer_size > 0))
        return S3StatusAbortedByCallback;

    size_t size = (size_t)buffer_size;
    if (size > ctx->max_bytes - ctx->written)
    {
        ctx->too_large = true;
        return S3StatusAbortedByCallback;
    }

    if (size && fwrite(buffer, 1, size, ctx->file) != size)
        return S3StatusAbortedByCallback;

    ctx->written += size;
    return S3StatusOK;
}

static void download_complete_cb(S3Status status, const S3ErrorDetails* error, void* callback_data)
{
    moderation_s3_download_t* ctx = callback_data;
    if (ctx)
        ctx->status = status;

    if (status != S3StatusOK && status != S3StatusAbortedByCallback)
        logger_error("moderation S3 download failed: %s", error && error->message ? error->message : S3_get_status_name(status));
}

static void action_complete_cb(S3Status status, const S3ErrorDetails* error, void* callback_data)
{
    moderation_s3_action_t* ctx = callback_data;
    if (ctx)
        ctx->status = status;

    if (status != S3StatusOK && status != S3StatusErrorNoSuchKey && status != S3StatusHttpErrorNotFound)
        logger_error("moderation S3 action failed: %s", error && error->message ? error->message : S3_get_status_name(status));
}

static int create_temp_file(char* path, size_t path_size, FILE** out_file)
{
    if (!path || path_size == 0 || !out_file)
        return -1;

#ifdef _WIN32
    char directory[MAX_PATH];
    if (!GetTempPathA(sizeof(directory), directory) || !GetTempFileNameA(directory, "pmg", 0, path))
        return -1;
    FILE* file = fopen(path, "w+b");
#else
    if (snprintf(path, path_size, "/tmp/parmigiano-moderation-XXXXXX") <= 0)
        return -1;
    int fd = mkstemp(path);
    if (fd < 0)
        return -1;
    FILE* file = fdopen(fd, "w+b");
    if (!file)
        close(fd);
#endif

    if (!file)
    {
        remove(path);
        return -1;
    }

    *out_file = file;
    return 0;
}

static int create_temp_dir(char* path, size_t path_size)
{
#ifdef _WIN32
    char directory[MAX_PATH];
    char temp_file[MAX_PATH];
    if (!GetTempPathA(sizeof(directory), directory) || !GetTempFileNameA(directory, "pmf", 0, temp_file))
        return -1;
    DeleteFileA(temp_file);
    if (!CreateDirectoryA(temp_file, NULL))
        return -1;
    if (snprintf(path, path_size, "%s", temp_file) <= 0)
        return -1;
    return 0;
#else
    if (snprintf(path, path_size, "/tmp/parmigiano-frames-XXXXXX") <= 0)
        return -1;
    return mkdtemp(path) ? 0 : -1;
#endif
}

static void remove_temp_dir(const char* path)
{
    if (!path)
        return;
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static int run_process(char* const argv[])
{
#ifdef _WIN32
    intptr_t rc = _spawnvp(_P_WAIT, argv[0], (const char* const*)argv);
    return rc == 0 ? 0 : -1;
#else
    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0)
    {
        FILE* devnull = fopen("/dev/null", "w");
        if (devnull)
        {
            dup2(fileno(devnull), STDOUT_FILENO);
            dup2(fileno(devnull), STDERR_FILENO);
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    const int loops = MODERATION_PROCESS_TIMEOUT_SEC * 10;
    for (int i = 0; i < loops; ++i)
    {
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
        if (result < 0)
            return -1;

        struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000000L};
        nanosleep(&pause, NULL);
    }

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return -1;
#endif
}

static const char* infer_mime(const char* reference)
{
    if (!reference)
        return "application/octet-stream";

    const char* ext = strrchr(reference, '.');
    if (!ext)
        return "application/octet-stream";

    if (ascii_ieq(ext, ".jpg") || ascii_ieq(ext, ".jpeg"))
        return "image/jpeg";
    if (ascii_ieq(ext, ".png"))
        return "image/png";
    if (ascii_ieq(ext, ".gif"))
        return "image/gif";
    if (ascii_ieq(ext, ".webp"))
        return "image/webp";
    if (ascii_ieq(ext, ".mp4"))
        return "video/mp4";
    if (ascii_ieq(ext, ".webm"))
        return "video/webm";
    if (ascii_ieq(ext, ".mov"))
        return "video/quicktime";
    if (ascii_ieq(ext, ".mkv"))
        return "video/x-matroska";
    if (ascii_ieq(ext, ".avi"))
        return "video/x-msvideo";
    return "application/octet-stream";
}

static moderation_media_result_t download_target(const moderation_target_t* target,
                                                  char* path,
                                                  size_t path_size,
                                                  char* mime,
                                                  size_t mime_size,
                                                  char* detail,
                                                  size_t detail_size)
{
    if (!target || !target->content_ref || !path || !mime)
        return MODERATION_MEDIA_ERROR;

    s3_config_t cfg = {
        .endpoint = getenv("S3_ENDPOINT"),
        .bucket = getenv(strcmp(target->target_type, MODERATION_TARGET_AVATAR) == 0 ? "S3_BUCKET_PUB" : "S3_BUCKET_PRV"),
        .access_key = getenv("S3_ACCESS_KEY"),
        .secret_key = getenv("S3_SECRET_KEY"),
        .region = getenv("S3_REGION"),
    };

    if (!s3_config_valid(&cfg))
    {
        snprintf(detail, detail_size, "S3 is not configured");
        return MODERATION_MEDIA_RETRY;
    }

    const char* key = target->content_ref;
    if (strcmp(target->target_type, MODERATION_TARGET_AVATAR) == 0)
    {
        key = avatar_url_to_key(target->content_ref, cfg.bucket);
        if (!key)
        {
            snprintf(detail, detail_size, "avatar URL does not belong to configured S3 bucket");
            return MODERATION_MEDIA_INVALID;
        }
    }

    FILE* file = NULL;
    if (create_temp_file(path, path_size, &file) != 0)
    {
        snprintf(detail, detail_size, "failed to create moderation temporary file");
        return MODERATION_MEDIA_RETRY;
    }

    moderation_s3_download_t download = {
        .file = file,
        .max_bytes = MODERATION_MAX_DOWNLOAD_BYTES,
        .status = S3StatusInternalError,
    };

    S3Status init_status = S3_initialize("parmigianochat/moderation", S3_INIT_ALL, cfg.endpoint);
    if (init_status != S3StatusOK)
    {
        fclose(file);
        remove(path);
        snprintf(detail, detail_size, "failed to initialize S3: %s", S3_get_status_name(init_status));
        return MODERATION_MEDIA_RETRY;
    }

    S3BucketContext bucket = make_bucket_ctx(&cfg);
    S3GetObjectHandler handler = {
        .responseHandler = {
            .propertiesCallback = download_properties_cb,
            .completeCallback = download_complete_cb,
        },
        .getObjectDataCallback = download_data_cb,
    };

    S3_get_object(&bucket, key, NULL, 0, 0, NULL, 0, &handler, &download);
    S3_deinitialize();

    if (fflush(file) != 0)
        download.status = S3StatusAbortedByCallback;
    fclose(file);

    if (download.too_large)
    {
        remove(path);
        snprintf(detail, detail_size, "media is too large to moderate");
        return MODERATION_MEDIA_INVALID;
    }

    if (download.status == S3StatusErrorNoSuchKey || download.status == S3StatusHttpErrorNotFound)
    {
        remove(path);
        snprintf(detail, detail_size, "media object no longer exists");
        return MODERATION_MEDIA_NOT_FOUND;
    }

    if (download.status != S3StatusOK || download.written == 0)
    {
        remove(path);
        snprintf(detail, detail_size, "S3 download failed: %s", S3_get_status_name(download.status));
        return S3_status_is_retryable(download.status) ? MODERATION_MEDIA_RETRY : MODERATION_MEDIA_ERROR;
    }

    const char* effective_mime = download.content_type[0] ? download.content_type : infer_mime(target->content_ref);
    snprintf(mime, mime_size, "%s", effective_mime);
    return MODERATION_MEDIA_CLEAN;
}

static moderation_media_result_t map_nsfw_result(nsfw_result_t result,
                                                  const nsfw_error_t* error,
                                                  char* detail,
                                                  size_t detail_size)
{
    if (result == NSFW_OK)
        return MODERATION_MEDIA_CLEAN;
    if (result == NSFW_REJECTED)
        return MODERATION_MEDIA_REJECTED;

    snprintf(detail, detail_size, "NSFW service: %s", error && error->detail[0] ? error->detail : nsfw_result_locale_key(result));

    if (nsfw_result_retryable(result))
        return MODERATION_MEDIA_RETRY;

    switch (result)
    {
    case NSFW_EMPTY_IMAGE:
    case NSFW_IMAGE_TOO_LARGE:
    case NSFW_INVALID_IMAGE:
    case NSFW_UNSUPPORTED_MEDIA:
    case NSFW_INVALID_ARGUMENT:
        return MODERATION_MEDIA_INVALID;
    default:
        return MODERATION_MEDIA_ERROR;
    }
}

static moderation_media_result_t scan_frame(const char* path, char* detail, size_t detail_size)
{
    nsfw_error_t error = {0};
    nsfw_result_t result = nsfw_check_file_from_env(path, "image/jpeg", &error);
    return map_nsfw_result(result, &error, detail, detail_size);
}

static moderation_media_result_t normalize_and_scan_image(const char* input,
                                                           char* detail,
                                                           size_t detail_size)
{
    char directory[512];
    if (create_temp_dir(directory, sizeof(directory)) != 0)
    {
        snprintf(detail, detail_size, "failed to create image normalization directory");
        return MODERATION_MEDIA_RETRY;
    }

    char output[640];
    snprintf(output, sizeof(output), "%s/image.jpg", directory);

    char* argv[] = {
        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-i", (char*)input,
        "-frames:v", "1",
        "-vf", "scale=min(1024\\,iw):-2",
        "-q:v", "6",
        output,
        NULL,
    };

    if (run_process(argv) != 0)
    {
        remove(output);
        remove_temp_dir(directory);
        snprintf(detail, detail_size, "ffmpeg could not decode image");
        return MODERATION_MEDIA_INVALID;
    }

    moderation_media_result_t result = scan_frame(output, detail, detail_size);
    remove(output);
    remove_temp_dir(directory);
    return result;
}

static moderation_media_result_t extract_and_scan_frames(const char* input,
                                                          char* detail,
                                                          size_t detail_size)
{
    char directory[512];
    if (create_temp_dir(directory, sizeof(directory)) != 0)
    {
        snprintf(detail, detail_size, "failed to create frame directory");
        return MODERATION_MEDIA_RETRY;
    }

    char start_pattern[640];
    char end_pattern[640];
    snprintf(start_pattern, sizeof(start_pattern), "%s/start-%%02d.jpg", directory);
    snprintf(end_pattern, sizeof(end_pattern), "%s/end-%%02d.jpg", directory);

    char* start_argv[] = {
        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-i", (char*)input,
        "-vf", "fps=1/15,scale=min(1024\\,iw):-2",
        "-frames:v", "3",
        "-q:v", "6",
        start_pattern,
        NULL,
    };

    char* end_argv[] = {
        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-sseof", "-60",
        "-i", (char*)input,
        "-vf", "fps=1/20,scale=min(1024\\,iw):-2",
        "-frames:v", "3",
        "-q:v", "6",
        end_pattern,
        NULL,
    };

    int start_ok = run_process(start_argv) == 0;
    int end_ok = run_process(end_argv) == 0;

    moderation_media_result_t final_result = MODERATION_MEDIA_INVALID;
    size_t scanned = 0;

    for (int group = 0; group < 2; ++group)
    {
        const char* prefix = group == 0 ? "start" : "end";
        for (int i = 1; i <= 3; ++i)
        {
            char frame[640];
            snprintf(frame, sizeof(frame), "%s/%s-%02d.jpg", directory, prefix, i);

            FILE* check = fopen(frame, "rb");
            if (!check)
                continue;
            fclose(check);

            ++scanned;
            moderation_media_result_t result = scan_frame(frame, detail, detail_size);
            remove(frame);

            if (result == MODERATION_MEDIA_REJECTED)
            {
                final_result = result;
                goto cleanup;
            }
            if (result == MODERATION_MEDIA_RETRY || result == MODERATION_MEDIA_ERROR)
            {
                final_result = result;
                goto cleanup;
            }
            if (result == MODERATION_MEDIA_CLEAN)
                final_result = MODERATION_MEDIA_CLEAN;
        }
    }

    if (scanned == 0)
    {
        snprintf(detail, detail_size, "ffmpeg produced no moderation frames (start=%d end=%d)", start_ok, end_ok);
        final_result = MODERATION_MEDIA_INVALID;
    }

cleanup:
    for (int group = 0; group < 2; ++group)
    {
        const char* prefix = group == 0 ? "start" : "end";
        for (int i = 1; i <= 3; ++i)
        {
            char frame[640];
            snprintf(frame, sizeof(frame), "%s/%s-%02d.jpg", directory, prefix, i);
            remove(frame);
        }
    }
    remove_temp_dir(directory);
    return final_result;
}

moderation_media_result_t moderation_media_scan(const moderation_target_t* target,
                                                 char* detail,
                                                 size_t detail_size)
{
    if (!target || !detail || detail_size == 0)
        return MODERATION_MEDIA_ERROR;

    detail[0] = '\0';

    char path[512];
    char mime[128];
    moderation_media_result_t download = download_target(target, path, sizeof(path), mime, sizeof(mime), detail, detail_size);
    if (download != MODERATION_MEDIA_CLEAN)
        return download;

    moderation_media_result_t result;
    bool animated_or_video = strcmp(target->content_type, "video") == 0 ||
                             ascii_ieq(mime, "image/gif") ||
                             ascii_ieq(mime, "image/webp");

    if (animated_or_video)
    {
        result = extract_and_scan_frames(path, detail, detail_size);
    }
    else if (ascii_starts_with_ci(mime, "image/"))
    {
        FILE* file = fopen(path, "rb");
        long size = -1;
        if (file)
        {
            if (fseek(file, 0, SEEK_END) == 0)
                size = ftell(file);
            fclose(file);
        }

        if (size > 0 && (size_t)size <= NSFW_MAX_IMAGE_BYTES &&
            (ascii_ieq(mime, "image/jpeg") || ascii_ieq(mime, "image/png")))
        {
            nsfw_error_t error = {0};
            nsfw_result_t nsfw = nsfw_check_file_from_env(path, mime, &error);
            result = map_nsfw_result(nsfw, &error, detail, detail_size);
        }
        else
        {
            result = normalize_and_scan_image(path, detail, detail_size);
        }
    }
    else
    {
        snprintf(detail, detail_size, "unsupported moderation media type: %s", mime);
        result = MODERATION_MEDIA_INVALID;
    }

    remove(path);
    return result;
}

int moderation_media_delete_object(const moderation_target_t* target)
{
    if (!target || !target->content_ref || !target->target_type)
        return -1;

    s3_config_t cfg = {
        .endpoint = getenv("S3_ENDPOINT"),
        .bucket = getenv(strcmp(target->target_type, MODERATION_TARGET_AVATAR) == 0 ? "S3_BUCKET_PUB" : "S3_BUCKET_PRV"),
        .access_key = getenv("S3_ACCESS_KEY"),
        .secret_key = getenv("S3_SECRET_KEY"),
        .region = getenv("S3_REGION"),
    };

    if (!s3_config_valid(&cfg))
        return -1;

    const char* key = target->content_ref;
    if (strcmp(target->target_type, MODERATION_TARGET_AVATAR) == 0)
    {
        key = avatar_url_to_key(target->content_ref, cfg.bucket);
        if (!key)
            return -1;
    }

    S3Status init_status = S3_initialize("parmigianochat/moderation", S3_INIT_ALL, cfg.endpoint);
    if (init_status != S3StatusOK)
        return -1;

    moderation_s3_action_t action = {.status = S3StatusInternalError};
    S3BucketContext bucket = make_bucket_ctx(&cfg);
    S3ResponseHandler handler = {
        .propertiesCallback = NULL,
        .completeCallback = action_complete_cb,
    };

    S3_delete_object(&bucket, key, NULL, 0, &handler, &action);
    S3_deinitialize();

    return action.status == S3StatusOK || action.status == S3StatusErrorNoSuchKey || action.status == S3StatusHttpErrorNotFound ? 0 : -1;
}
