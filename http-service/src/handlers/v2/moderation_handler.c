#include "handlers.h"

#include "httpx.h"
#include "logger.h"
#include "moderation.h"
#include "rabbitmq.h"

#include "postgres/postgres_moderation.h"

#include <cjson/cJSON.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char target_type[32];
    uint64_t target_id;
    uint64_t reporter_uid;
} moderation_task_t;

static bool moderation_target_type_valid(const char* target_type)
{
    return target_type &&
           (strcmp(target_type, MODERATION_TARGET_AVATAR) == 0 ||
            strcmp(target_type, MODERATION_TARGET_CHAT_MEDIA) == 0);
}

static void lowercase_ascii(char* text)
{
    if (!text)
        return;

    for (; *text; ++text)
    {
        if (*text >= 'A' && *text <= 'Z')
            *text = (char)(*text - 'A' + 'a');
    }
}

static bool parse_u64_text(const char* text, uint64_t* out)
{
    if (!text || !*text || !out || *text == '-')
        return false;

    errno = 0;
    char* end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || value == 0)
        return false;

    *out = (uint64_t)value;
    return true;
}

static bool parse_u64_json(const cJSON* item, uint64_t* out)
{
    if (!item || !out)
        return false;

    if (cJSON_IsString(item))
        return parse_u64_text(item->valuestring, out);

    if (!cJSON_IsNumber(item))
        return false;

    /* JSON numbers above 2^53-1 are not reliably exact in common clients.
     * Larger BIGINT identifiers are accepted as strings. */
    double value = item->valuedouble;
    if (value < 1.0 || value > 9007199254740991.0)
        return false;

    uint64_t parsed = (uint64_t)value;
    if ((double)parsed != value)
        return false;

    *out = parsed;
    return true;
}

static bool parse_http_task(chttpx_request_t* req, moderation_task_t* task)
{
    if (!req || !task || !req->body || req->body_size == 0)
        return false;

    char* body = cHTTPX_Alloc(req, req->body_size + 1);
    if (!body)
        return false;

    memcpy(body, req->body, req->body_size);
    body[req->body_size] = '\0';

    cJSON* root = cJSON_Parse(body);
    if (!root)
        return false;

    const cJSON* target_type = cJSON_GetObjectItemCaseSensitive(root, "target_type");
    const cJSON* target_id = cJSON_GetObjectItemCaseSensitive(root, "target_id");

    bool ok = false;
    if (cJSON_IsString(target_type) && target_type->valuestring &&
        strlen(target_type->valuestring) < sizeof(task->target_type) &&
        parse_u64_json(target_id, &task->target_id))
    {
        snprintf(task->target_type, sizeof(task->target_type), "%s", target_type->valuestring);
        lowercase_ascii(task->target_type);
        ok = moderation_target_type_valid(task->target_type);
    }

    cJSON_Delete(root);
    return ok;
}

static bool parse_queue_task(const rmq_message_t* message, moderation_task_t* task)
{
    if (!message || !task || !message->body.data || message->body.size == 0 || message->body.size > 4096)
        return false;

    char* body = malloc(message->body.size + 1);
    if (!body)
        return false;

    memcpy(body, message->body.data, message->body.size);
    body[message->body.size] = '\0';

    cJSON* root = cJSON_Parse(body);
    free(body);
    if (!root)
        return false;

    const cJSON* target_type = cJSON_GetObjectItemCaseSensitive(root, "target_type");
    const cJSON* target_id = cJSON_GetObjectItemCaseSensitive(root, "target_id");
    const cJSON* reporter_uid = cJSON_GetObjectItemCaseSensitive(root, "reporter_uid");

    bool ok = false;
    if (cJSON_IsString(target_type) && target_type->valuestring &&
        strlen(target_type->valuestring) < sizeof(task->target_type) &&
        parse_u64_json(target_id, &task->target_id) &&
        parse_u64_json(reporter_uid, &task->reporter_uid))
    {
        snprintf(task->target_type, sizeof(task->target_type), "%s", target_type->valuestring);
        lowercase_ascii(task->target_type);
        ok = moderation_target_type_valid(task->target_type);
    }

    cJSON_Delete(root);
    return ok;
}

static rmq_result_t publish_moderation_task(const moderation_task_t* task,
                                            const char* message_id,
                                            rmq_error_t* error)
{
    if (!task || !moderation_target_type_valid(task->target_type) || !task->target_id || !task->reporter_uid)
        return RMQ_INVALID_ARGUMENT;

    const char* url = getenv("RABBITMQ_URL");
    if (!url || !*url)
        return RMQ_NOT_CONFIGURED;

    rmq_config_t config = {
        .url = url,
        .connect_timeout_ms = 3000,
        .rpc_timeout_ms = 5000,
        .heartbeat_seconds = 120,
        .tls_ca_file = NULL,
    };

    rmq_client_t* client = NULL;
    rmq_result_t result = rmq_connect(&client, &config, error);
    if (result != RMQ_OK)
        return result;

    result = rabbitmq_setup(client, error);
    if (result != RMQ_OK)
    {
        rmq_disconnect(client);
        return result;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root)
    {
        rmq_disconnect(client);
        return RMQ_OUT_OF_MEMORY;
    }

    char target_id[32];
    char reporter_uid[32];
    snprintf(target_id, sizeof(target_id), "%" PRIu64, task->target_id);
    snprintf(reporter_uid, sizeof(reporter_uid), "%" PRIu64, task->reporter_uid);

    if (!cJSON_AddNumberToObject(root, "version", 1) ||
        !cJSON_AddStringToObject(root, "target_type", task->target_type) ||
        !cJSON_AddStringToObject(root, "target_id", target_id) ||
        !cJSON_AddStringToObject(root, "reporter_uid", reporter_uid))
    {
        cJSON_Delete(root);
        rmq_disconnect(client);
        return RMQ_OUT_OF_MEMORY;
    }

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json)
    {
        rmq_disconnect(client);
        return RMQ_OUT_OF_MEMORY;
    }

    rmq_publish_t publication = {
        .exchange = MODERATION_EXCHANGE,
        .routing_key = MODERATION_ROUTING_KEY,
        .body = {.data = json, .size = strlen(json)},
        .content_type = "application/json",
        .message_id = message_id && *message_id ? message_id : NULL,
        .correlation_id = NULL,
        .reply_to = NULL,
        .persistent = true,
        .mandatory = true,
    };

    result = rmq_publish(client, &publication, 5000, error);
    free(json);
    rmq_disconnect(client);
    return result;
}

void moderation_wtype_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    moderation_task_t task = {0};
    if (!parse_http_task(req, &task))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-moderation-target", req->language));
        return;
    }
    task.reporter_uid = ctx->user->user_uid;

    rmq_error_t error = {0};
    rmq_result_t result = publish_moderation_task(&task, req->request_id, &error);
    if (result != RMQ_OK)
    {
        logger_error("moderation_wtype_handler_v2 req={%s}: RabbitMQ operation=%s error=%s",
                     req->request_id, error.operation, error.detail);

        uint16_t status = result == RMQ_TIMEOUT ? cHTTPX_StatusGatewayTimeout : cHTTPX_StatusServiceUnavailable;
        *res = cHTTPX_ResError(status, cHTTPX_i18n_t(rmq_result_locale_key(result), req->language));
        return;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusAccepted, cHTTPX_i18n_t("moderation.queued", req->language));
}

rmq_action_t moderation_data_handler_v2(const rmq_message_t* message, void* userdata)
{
    (void)userdata;

    moderation_task_t task = {0};
    if (!parse_queue_task(message, &task))
    {
        logger_error("moderation_data_handler_v2: invalid task payload size=%zu", message ? message->body.size : 0);
        return RMQ_REJECT;
    }

    moderation_target_t* target = NULL;
    db_result_t db_result = db_moderation_target_get(http_server->conn, task.target_type, task.target_id, &target);
    if (db_result != DB_OK)
    {
        logger_error("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: target lookup failed",
                     task.target_type, task.target_id);
        return RMQ_REQUEUE;
    }

    if (!target)
    {
        logger_info("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: target no longer exists",
                    task.target_type, task.target_id);
        return RMQ_ACK;
    }

    char detail[512] = {0};
    moderation_media_result_t scan = moderation_media_scan(target, detail, sizeof(detail));

    if (scan == MODERATION_MEDIA_CLEAN)
    {
        logger_info("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: clean",
                    target->target_type, target->target_id);
        db_moderation_target_free(target);
        return RMQ_ACK;
    }

    if (scan == MODERATION_MEDIA_NOT_FOUND)
    {
        logger_warn("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: %s",
                    target->target_type, target->target_id, detail);
        db_moderation_target_free(target);
        return RMQ_ACK;
    }

    if (scan == MODERATION_MEDIA_INVALID)
    {
        logger_warn("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: invalid media: %s",
                    target->target_type, target->target_id, detail);
        db_moderation_target_free(target);
        return RMQ_REJECT;
    }

    if (scan == MODERATION_MEDIA_RETRY || scan == MODERATION_MEDIA_ERROR)
    {
        logger_error("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: scan failed: %s",
                     target->target_type, target->target_id, detail);
        db_moderation_target_free(target);
        return RMQ_REQUEUE;
    }

    moderation_apply_result_t apply = {0};
    db_result = db_moderation_apply_violation(http_server->conn, target, task.reporter_uid, &apply);
    if (db_result != DB_OK)
    {
        logger_error("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: failed to apply violation",
                     target->target_type, target->target_id);
        db_moderation_target_free(target);
        return RMQ_REQUEUE;
    }

    if (!apply.applied)
    {
        logger_info("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 "}: stale or duplicate violation",
                    target->target_type, target->target_id);
        db_moderation_target_free(target);
        return RMQ_ACK;
    }

    if (moderation_media_delete_object(target) != 0)
    {
        logger_error("moderation_data_handler_v2 target_type={%s} target_id={%" PRIu64 ": DB target removed but S3 cleanup failed",
                     target->target_type, target->target_id);
    }

    if (apply.permanent)
    {
        logger_warn("moderation_data_handler_v2 user_uid={%" PRIu64 "}: prohibited content removed, offense=%" PRIu64 ", permanent block",
                    target->user_uid, apply.offense_count);
    }
    else
    {
        logger_warn("moderation_data_handler_v2 user_uid={%" PRIu64 "}: prohibited content removed, offense=%" PRIu64 ", blocked_until=%lld",
                    target->user_uid, apply.offense_count, (long long)apply.blocked_until);
    }

    db_moderation_target_free(target);
    return RMQ_ACK;
}
