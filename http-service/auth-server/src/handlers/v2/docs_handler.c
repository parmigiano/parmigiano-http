#include "handlers.h"

#include <stdio.h>
#include <stdlib.h>

void swagger_json_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResFile(cHTTPX_StatusOK, cHTTPX_CTYPE_JSON, "docs/swagger/swagger_v2.json");
    return;
}

void swagger_gui_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* base_addr = getenv("SERVER_BASE_ADDR");
    if (!base_addr || base_addr[0] == '\0')
        base_addr = "http://localhost:8081/api";

    char url[256];
    snprintf(url, sizeof(url), "%s/v2", base_addr);

    char swagger_html[16384];
    int written = snprintf(swagger_html, sizeof(swagger_html),
                           "<!DOCTYPE html>\n"
                           "<html lang=\"en\">\n"
                           "<head>\n"
                           "  <meta charset=\"utf-8\" />\n"
                           "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
                           "  <meta name=\"description\" content=\"SwaggerUI\" />\n"
                           "  <title>SwaggerUI</title>\n"
                           "  <link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui.css\" />\n"
                           "</head>\n"
                           "<body>\n"
                           "<div id=\"swagger-ui\"></div>\n"
                           "<script src=\"https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui-bundle.js\" crossorigin></script>\n"
                           "<script>\n"
                           "window.onload = () => {\n"
                           "  const storageKey = (name) => 'parmigiano.' + name;\n"
                           "  const readBody = (res) => {\n"
                           "    if (!res) return null;\n"
                           "    if (typeof res.text === 'string' && res.text.charAt(0) === '{') return JSON.parse(res.text);\n"
                           "    if (typeof res.data === 'string' && res.data.charAt(0) === '{') return JSON.parse(res.data);\n"
                           "    if (res.data && typeof res.data === 'object') return res.data;\n"
                           "    return null;\n"
                           "  };\n"
                           "  window.ui = SwaggerUIBundle({\n"
                           "    url: '%s/doc.api/swagger/json',\n"
                           "    dom_id: '#swagger-ui',\n"
                           "    responseInterceptor: (res) => {\n"
                           "      try {\n"
                           "        const body = readBody(res);\n"
                           "        if (body && typeof body.registration_token === 'string' && body.registration_token)\n"
                           "          sessionStorage.setItem(storageKey('registration_token'), body.registration_token);\n"
                           "        if (body && typeof body.login_token === 'string' && body.login_token)\n"
                           "          sessionStorage.setItem(storageKey('login_token'), body.login_token);\n"
                           "      } catch (e) {}\n"
                           "      return res;\n"
                           "    },\n"
                           "    requestInterceptor: (req) => {\n"
                           "      try {\n"
                           "        if (!req || !req.url || req.body == null) return req;\n"
                           "        const original = req.body;\n"
                           "        const data = typeof original === 'string' ? JSON.parse(original) : original;\n"
                           "        if (!data || typeof data !== 'object') return req;\n"
                           "        const fill = (path, field) => {\n"
                           "          if (req.url.indexOf(path) < 0 || data[field]) return;\n"
                           "          const saved = sessionStorage.getItem(storageKey(field));\n"
                           "          if (saved) data[field] = saved;\n"
                           "        };\n"
                           "        fill('/auth/create', 'registration_token');\n"
                           "        fill('/auth/login', 'login_token');\n"
                           "        if (typeof original === 'string') req.body = JSON.stringify(data);\n"
                           "      } catch (e) {}\n"
                           "      return req;\n"
                           "    }\n"
                           "  });\n"
                           "};\n"
                           "</script>\n"
                           "</body>\n"
                           "</html>\n",
                           url);

    if (written < 0 || (size_t)written >= sizeof(swagger_html))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return;
    }

    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "%s", swagger_html);
    return;
}
