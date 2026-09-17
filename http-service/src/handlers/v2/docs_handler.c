#include "handlers.h"

void swagger_json_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResFile(cHTTPX_StatusOK, cHTTPX_CTYPE_JSON, "docs/swagger/swagger_v2.json");
    return;
}

void swagger_gui_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    const char* base_addr = getenv("SERVER_BASE_ADDR");
    if (!base_addr || base_addr[0] == '\0')
        base_addr = "http://localhost:8080/api";

    char url[256];
    snprintf(url, sizeof(url), "%s/v2", base_addr);

    char swagger_html[8192];
    snprintf(swagger_html, sizeof(swagger_html),
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
             "  window.ui = SwaggerUIBundle({\n"
             "    url: '%s/doc.api/swagger/json',\n"
             "    dom_id: '#swagger-ui'\n"
             "  });\n"
             "};\n"
             "</script>\n"
             "</body>\n"
             "</html>\n",
             url);

    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "%s", swagger_html);
    return;
}
