#include "httpx.h"
#include "utilities.h"

#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libchttpx/libchttpx.h>

int main(void)
{
    srand(time(NULL));

    curl_global_init(CURL_GLOBAL_DEFAULT);

    env_init(".env");

    const char* i18n_locate = getenv("I18N_LOCATE");
    if (!i18n_locate || i18n_locate[0] == '\0')
    {
        fprintf(stderr, "I18N_LOCATE is missing or empty (expected ./src/infra/locale)\n");
        return 1;
    }

    cHTTPX_i18n(i18n_locate);

    http_init();
    return 0;
}
