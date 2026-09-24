#include "utilities.h"

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include <cjson/cJSON.h>

char* translate(const char* text, const char* source, const char* target)
{
    CURL* curl;
    CURLcode res;

    struct Memory chunk;
    chunk.response = malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if (!curl)
    {
        free(chunk.response);
        return NULL;
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char json_data[512];
    snprintf(json_data, sizeof(json_data), "{\"q\":\"%s\",\"source\":\"%s\",\"target\":\"%s\",\"format\":\"text\"}", escape_json_string(text), source,
             target);

    const char* libretranslate_url = getenv("LIBRETRANSLATE_URL");
    if (!libretranslate_url)
        libretranslate_url = "http://localhost:5000";

    char url[256];
    snprintf(url, sizeof(url), "%s/translate", libretranslate_url);

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK)
    {
        logger_error("translate: CURL error: %s", curl_easy_strerror(res));
        fprintf(stderr, "CURL error: %s\n", curl_easy_strerror(res));
        free(chunk.response);
        return NULL;
    }

    return chunk.response;
}

char* detect_lang(const char* text)
{
    CURL* curl;
    CURLcode res;

    struct Memory chunk;
    chunk.response = malloc(1);
    chunk.response[0] = '\0';
    chunk.size = 0;

    curl = curl_easy_init();
    if (!curl)
    {
        free(chunk.response);
        return NULL;
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char json_data[512];
    snprintf(json_data, sizeof(json_data), "{\"q\":\"%s\"}", text);

    const char* libretranslate_url = getenv("LIBRETRANSLATE_URL");
    if (!libretranslate_url)
        libretranslate_url = "http://localhost:5000";

    char url[256];
    snprintf(url, sizeof(url), "%s/detect", libretranslate_url);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK)
    {
        free(chunk.response);
        return NULL;
    }

    char* pos = strstr(chunk.response, "\"language\":\"");
    if (!pos)
    {
        free(chunk.response);
        return NULL;
    }

    pos += strlen("\"language\":\"");

    char* lang = malloc(3);
    strncpy(lang, pos, 2);
    lang[2] = '\0';

    free(chunk.response);
    return lang;
}

char* get_translated_text(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root)
        return strdup("");

    cJSON* item = cJSON_GetObjectItem(root, "translatedText");
    if (!cJSON_IsString(item))
    {
        cJSON_Delete(root);
        return strdup("");
    }

    char* result = strdup(item->valuestring);
    cJSON_Delete(root);
    return result;
}