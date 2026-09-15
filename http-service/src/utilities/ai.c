#include "utilities.h"

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>

#include <cjson/cJSON.h>

#include <libchttpx/libchttpx.h>

#define AI_QUEUE_MAX 1024

typedef struct Request
{
    char* text;
    void (*callback)(const char* result, void* arg);
    void* arg;
    struct Request* next;
} Request;

typedef struct
{
    Request* head;
    Request* tail;
    size_t size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} RequestQueue;

static RequestQueue queue;

static void init_ai_queue()
{
    queue.head = queue.tail = NULL;
    queue.size = 0;

    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.cond, NULL);
}

void enqueue_ai_request(const char* text, void (*callback)(const char* result, void* arg), void* arg)
{
    Request* req = malloc(sizeof(Request));
    if (!req)
        return;

    req->text = strdup(text);
    if (!req->text)
    {
        free(req);
        return;
    }

    req->callback = callback;
    req->arg = arg;
    req->next = NULL;

    pthread_mutex_lock(&queue.mutex);

    if (queue.size >= AI_QUEUE_MAX)
    {
        pthread_mutex_unlock(&queue.mutex);
        free(req->text);
        free(req);
        return;
    }

    if (queue.tail)
    {
        queue.tail->next = req;
        queue.tail = req;
    }
    else
    {
        queue.head = queue.tail = req;
    }

    queue.size++;

    pthread_cond_signal(&queue.cond);
    pthread_mutex_unlock(&queue.mutex);
}

static Request* dequeue_ai_request()
{
    pthread_mutex_lock(&queue.mutex);
    while (queue.head == NULL)
    {
        pthread_cond_wait(&queue.cond, &queue.mutex);
    }

    Request* req = queue.head;
    queue.head = req->next;
    if (!queue.head)
        queue.tail = NULL;

    queue.size--;

    pthread_mutex_unlock(&queue.mutex);

    return req;
}

static void* worker_thread(void* arg)
{
    (void)arg;

    while (1)
    {
        Request* req = dequeue_ai_request();
        char* result = call_ai_text(req->text);
        if (req->callback)
            req->callback(result, req->arg);

        free(result);
        free(req->text);
        free(req);
    }

    return NULL;
}

void start_ai_worker()
{
    static int started = 0;
    if (started)
        return;
    started = 1;

    init_ai_queue();
    pthread_t tid;
    pthread_create(&tid, NULL, worker_thread, NULL);
    pthread_detach(tid);
}

void callback_ai_send_response(const char* result, void* arg)
{
    chttpx_response_t* res = (chttpx_response_t*)arg;
    char* safe = escape_json_string(result ? result : "");
    if (safe)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", safe);
        free(safe);
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"AI response error\"}");
    }
}

char* get_ollama_response(const char* json_str)
{
    cJSON* root = cJSON_Parse(json_str);
    if (!root)
        return NULL;

    cJSON* response = cJSON_GetObjectItem(root, "response");
    if (!cJSON_IsString(response))
    {
        cJSON_Delete(root);
        return NULL;
    }

    char* result = strdup(response->valuestring);

    cJSON_Delete(root);
    return result;
}

char* call_ai_text(const char* prompt)
{
    struct Memory chunk;

    chunk.response = malloc(1);
    if (!chunk.response)
        return NULL;

    chunk.response[0] = '\0';
    chunk.size = 0;

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        free(chunk.response);
        return NULL;
    }

    const char* ollama_phi_host = getenv("OLLAMA_PHI_HOST");
    if (!ollama_phi_host)
        ollama_phi_host = "http://localhost:11434";

    char url[256];
    snprintf(url, sizeof(url), "%s/api/generate", ollama_phi_host);

    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", "phi3:latest");
    cJSON_AddStringToObject(body, "prompt", prompt);
    cJSON_AddBoolToObject(body, "stream", 0);
    char* json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (!json)
    {
        curl_easy_cleanup(curl);
        free(chunk.response);
        return NULL;
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    CURLcode res = curl_easy_perform(curl);

    free(json);
    
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK)
    {
        logger_error("call_ai_text: curl error: %s", curl_easy_strerror(res));
        free(chunk.response);
        return NULL;
    }

    return chunk.response;
}
