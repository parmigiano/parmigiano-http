#include "handlers.h"

#include "httpx.h"
#include "logger.h"
#include "profanity.h"
#include "utilities.h"
#include "middlewarex.h"
#include "postgres/validate.h"
#include "postgres/postgres_support.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char* first_name;
    char* last_name;
    char* email;
    char* subject;
    char* description;
} support_request_t;

typedef struct {
    char* answer;
} support_answer_t;

typedef struct {
    char* token;
    char* description;
} support_append_t;

static int support_token_hex(const char* token)
{
    if (!token || strlen(token) != 64)
        return 0;

    for (size_t i = 0; i < 64; ++i)
    {
        char c = token[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0;
    }

    return 1;
}

void support_request_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    support_request_t payload = {0};

    char name[194];

    char* body = NULL;

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("first_name", &payload.first_name, true, 1, 96, CHTTPX_TRIM, validate_name),
        cHTTPX_StringField("last_name", &payload.last_name, true, 1, 96, CHTTPX_TRIM, validate_name),
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        cHTTPX_StringField("subject", &payload.subject, true, 1, 255, CHTTPX_TRIM, NULL),
        cHTTPX_StringField("description", &payload.description, true, 1, 4000, CHTTPX_TRIM, NULL),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    const char* profanity_texts[] = {payload.first_name, payload.last_name, payload.subject, payload.description};
    if (!profanity_reject_from_env(req, res, "support.request", profanity_texts, 4))
        return;

    snprintf(name, sizeof(name), "%s %s", payload.first_name, payload.last_name);

    db_support_ticket_t ticket = {
        .email = payload.email,
        .name = name,
        .subject = payload.subject,
        .description = payload.description,
    };

    db_result_t result = db_support_ticket_create(app_context->db, &ticket);
    if (db_reply_result(req, res, result))
        goto cleanup;

    char mail_subject[160];
    snprintf(mail_subject, sizeof(mail_subject), cHTTPX_i18n_t("email.support.subject", req->language), ticket.ticket_id);

    body = html_support_letter(req->language, payload.subject, ticket.ticket_id, name, payload.description, ticket.edit_token);
    if (!body)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }

    if (send_email(payload.email, mail_subject, body, NULL) != 0)
    {
        logger_error("support_request_handler_v2 req={%s}: failed to send support email ticket_id={%s}", req->request_id, ticket.ticket_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.sending-email", req->language));
        goto cleanup;
    }

    chttpx_json_t* root = cHTTPX_JsonObject(req);
    chttpx_json_t* message = cHTTPX_JsonObject(req);

    if (!root || !message || cHTTPX_JsonString(message, "ticket_id", ticket.ticket_id) != 0 ||
        cHTTPX_JsonString(message, "token", ticket.edit_token) != 0 || cHTTPX_JsonChild(root, "message", message) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }

    *res = cHTTPX_ResJsonObject(cHTTPX_StatusCreated, root);

cleanup:
    free(body);
    free(ticket.ticket_id);
    free(ticket.edit_token);
}

static int support_ticket_json(chttpx_json_t* item, const db_support_ticket_t* ticket)
{
    return !item || cHTTPX_JsonNumber(item, "id", (double)ticket->id) != 0 ||
           cHTTPX_JsonNumber(item, "created_at", (double)ticket->created_at) != 0 ||
           cHTTPX_JsonNumber(item, "updated_at", (double)ticket->updated_at) != 0 ||
           cHTTPX_JsonString(item, "ticket_id", ticket->ticket_id) != 0 || cHTTPX_JsonString(item, "email", ticket->email) != 0 ||
           cHTTPX_JsonString(item, "name", ticket->name) != 0 || cHTTPX_JsonString(item, "subject", ticket->subject) != 0 ||
           cHTTPX_JsonString(item, "description", ticket->description) != 0 || cHTTPX_JsonString(item, "status", ticket->status) != 0 ||
           cHTTPX_JsonNumber(item, "assigned_to", (double)ticket->assigned_to) != 0 ||
           cHTTPX_JsonString(item, "answer", ticket->answer) != 0 || cHTTPX_JsonBool(item, "data_updated", ticket->data_updated) != 0;
}

static chttpx_response_t support_tickets_response(chttpx_request_t* req, const db_support_ticket_LIST_t* tickets)
{
    chttpx_json_t* root = cHTTPX_JsonObject(req);
    chttpx_json_t* message = cHTTPX_JsonArray(req);

    if (!root || !message)
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));

    if (tickets)
    {
        for (size_t i = 0; i < tickets->count; ++i)
        {
            chttpx_json_t* item = cHTTPX_JsonObject(req);
            if (support_ticket_json(item, &tickets->items[i]) || cHTTPX_JsonArrayChild(message, item) != 0)
            {
                return cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
            }
        }
    }

    if (cHTTPX_JsonChild(root, "message", message) != 0)
    {
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
    }

    return cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);
}

static chttpx_response_t support_ticket_response(chttpx_request_t* req, const db_support_ticket_t* ticket)
{
    chttpx_json_t* root = cHTTPX_JsonObject(req);
    chttpx_json_t* message = cHTTPX_JsonObject(req);

    if (!root || support_ticket_json(message, ticket) || cHTTPX_JsonChild(root, "message", message) != 0)
    {
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
    }

    return cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);
}

void support_get_tickets_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    uint64_t offset_value = 0;
    if (!cHTTPX_QueryU64Default(req, "offset", &offset_value, 0))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-offset", req->language));
        return;
    }
    size_t offset = (size_t)offset_value;

    db_support_ticket_LIST_t* support_tickets = NULL;
    db_result_t result = db_support_ticket_get_all(app_context->db, offset, &support_tickets);
    if (db_reply_result(req, res, result))
        return;

    *res = support_tickets_response(req, support_tickets);
    db_support_ticket_list_free(support_tickets);
}

void support_get_ticket_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* ticket_id = cHTTPX_Param(req, "ticket_id");
    if (!ticket_id || !*ticket_id || strlen(ticket_id) > 32)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-ticket-id", req->language));
        return;
    }

    const char* token = cHTTPX_Query(req, "token");

    db_support_ticket_t* ticket = NULL;
    db_result_t result;

    if (token)
    {
        if (!support_token_hex(token))
        {
            *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-ticket-token", req->language));
            return;
        }

        result = db_support_ticket_get_by_ticket_id_and_token(app_context->db, (char*)ticket_id, (char*)token, &ticket);
    }
    else
    {
        if (authenticate_middleware(req, res) != next)
            return;

        result = db_support_ticket_get_by_ticket_id(app_context->db, (char*)ticket_id, &ticket);
    }

    if (db_reply_result(req, res, result))
        return;

    if (!ticket)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusNotFound, cHTTPX_i18n_t("error.support-ticket-not-found", req->language));
        return;
    }

    *res = support_ticket_response(req, ticket);
    db_support_ticket_free(ticket);
}

void support_answer_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    const char* ticket_id = cHTTPX_Param(req, "ticket_id");
    if (!ticket_id || !*ticket_id || strlen(ticket_id) > 32)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-ticket-id", req->language));
        return;
    }

    support_answer_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("answer", &payload.answer, true, 1, 4000, CHTTPX_TRIM, NULL),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    const char* profanity_texts[] = {payload.answer};
    if (!profanity_reject_from_env(req, res, "support.answer", profanity_texts, 1))
        return;

    db_support_ticket_t* ticket = NULL;
    char* body = NULL;
    
    db_result_t result = db_support_ticket_answer(app_context->db, (char*)ticket_id, payload.answer, ctx->user->user_uid, &ticket);
    if (db_reply_result(req, res, result))
        return;

    if (!ticket)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusNotFound, cHTTPX_i18n_t("error.support-ticket-not-found", req->language));
        return;
    }

    char mail_subject[160];
    snprintf(mail_subject, sizeof(mail_subject), cHTTPX_i18n_t("email.support.answer-subject", req->language), ticket->ticket_id);

    body = html_support_answer_letter(req->language, ticket->ticket_id, ticket->answer, ticket->edit_token);
    if (!body)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }

    if (send_email(ticket->email, mail_subject, body, NULL) != 0)
    {
        logger_error("support_answer_handler_v2 req={%s}: failed to send support answer ticket_id={%s}", req->request_id, ticket->ticket_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.sending-email", req->language));
        goto cleanup;
    }

    *res = support_ticket_response(req, ticket);

cleanup:
    free(body);
    db_support_ticket_free(ticket);
}

void support_append_description_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* ticket_id = cHTTPX_Param(req, "ticket_id");
    if (!ticket_id || !*ticket_id || strlen(ticket_id) > 32)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-ticket-id", req->language));
        return;
    }

    support_append_t payload = {0};
    
    chttpx_validation_t fields[] = {
        cHTTPX_StringField("token", &payload.token, true, 64, 64, CHTTPX_TRIM, NULL),
        cHTTPX_StringField("description", &payload.description, true, 1, 4000, CHTTPX_TRIM, NULL),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    if (!support_token_hex(payload.token))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-ticket-token", req->language));
        return;
    }

    const char* profanity_texts[] = {payload.description};
    if (!profanity_reject_from_env(req, res, "support.append", profanity_texts, 1))
        return;

    db_support_ticket_t* ticket = NULL;
    db_result_t result = db_support_ticket_append_description(app_context->db, (char*)ticket_id, payload.token, payload.description, &ticket);
    if (db_reply_result(req, res, result))
        return;

    if (!ticket)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusNotFound, cHTTPX_i18n_t("error.support-ticket-not-found", req->language));
        return;
    }

    *res = support_ticket_response(req, ticket);
    db_support_ticket_free(ticket);
}