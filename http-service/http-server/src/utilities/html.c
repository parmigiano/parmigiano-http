#include "utilities.h"

#include <libchttpx/libchttpx.h>

#include <stdio.h>
#include <string.h>

#define HTML_PRIVACY_URL "http://localhost:5173/license/privacy"
#define HTML_SUPPORT_TICKET_URL "http://localhost:5173/support/"

static const char* html_shell =
    "<body style=\"margin:0;padding:0;background:#ffffff;color:#222222;font-size:14px;line-height:1.45;\">"
    "<div style=\"max-width:640px;margin:0 auto;\">"
    "<div style=\"padding:20px 16px 8px;\">%s</div>"
    "<div style=\"padding:16px 20px;font-size:12px;color:#555555;\">"
    "<p style=\"margin:0 0 8px;\"><a href=\"" HTML_PRIVACY_URL "\" style=\"color:#1a5fb4;\">%s</a></p>"
    "<p style=\"margin:0;\">%s</p>"
    "</div>"
    "</div>"
    "</body>";

static const char* html_support_body =
    "<p style=\"margin:0 0 16px;\">%s</p>"
    "<p style=\"margin:0 0 16px;\"><a href=\"%s\" style=\"color:#1a5fb4;\">%s</a></p>"
    "<hr style=\"border:none;border-top:1px solid #cccccc;margin:16px 0;\">"
    "<p style=\"margin:0 0 8px;\"><b>%s</b> %s</p>"
    "<p style=\"margin:0 0 12px;\"><b>%s</b> %s</p>"
    "<div style=\"background:#d9d9d9;padding:8px 12px;font-weight:bold;\">%s</div>"
    "<div style=\"border:1px solid #dddddd;border-top:none;padding:12px 16px;white-space:pre-wrap;\">%s</div>";

static const char* html_support_answer_body =
    "<p style=\"margin:0 0 16px;\">%s</p>"
    "<div style=\"border:1px solid #dddddd;padding:12px 16px;white-space:pre-wrap;\">%s</div>"
    "<p style=\"margin:16px 0 0;\"><a href=\"%s\" style=\"color:#1a5fb4;\">%s</a></p>";

char* html_wrap(const char* title, const char* content, const char* terms, const char* language)
{
    if (!title || !content || !terms)
        return NULL;

    const char* copyright_line = cHTTPX_i18n_t("email.support.copyright", language ? language : "en");

    size_t size = strlen(html_shell) + strlen(title) + strlen(content) + strlen(terms) + strlen(copyright_line) + 1;
    char* page = malloc(size);
    if (!page)
        return NULL;

    int written = snprintf(page, size, html_shell, title, content, terms, copyright_line);
    if (written < 0 || (size_t)written >= size)
    {
        free(page);
        return NULL;
    }

    return page;
}

char* html_support_letter(const char* language, const char* subject, const char* ticket_id, const char* name, const char* description, const char* edit_token)
{
    if (!subject || !ticket_id || !name || !description || !edit_token)
        return NULL;

    const char* lang = language ? language : "en";

    char* subject_html = html_escape(subject);
    char* name_html = html_escape(name);
    char* description_html = html_escape(description);

    if (!subject_html || !name_html || !description_html)
    {
        free(subject_html);
        free(name_html);
        free(description_html);
        return NULL;
    }

    char stamp[64];
    char customer[2048];
    utc_timestamp(stamp, sizeof(stamp));
    snprintf(customer, sizeof(customer), cHTTPX_i18n_t("email.support.customer", lang), name_html, stamp);

    const char* received = cHTTPX_i18n_t("email.support.received", lang);
    const char* add_info = cHTTPX_i18n_t("email.support.add-info", lang);
    const char* subject_label = cHTTPX_i18n_t("email.support.subject-label", lang);
    const char* incident_label = cHTTPX_i18n_t("email.support.incident-label", lang);

    char ticket_url[192];
    snprintf(ticket_url, sizeof(ticket_url), "%s%s?token=%s", HTML_SUPPORT_TICKET_URL, ticket_id, edit_token);

    size_t size = strlen(html_support_body) + strlen(received) + strlen(ticket_url) + strlen(add_info) + strlen(subject_label) +
                  strlen(subject_html) + strlen(incident_label) + strlen(ticket_id) + strlen(customer) + strlen(description_html) + 1;

    char* content = malloc(size);
    if (!content)
    {
        free(subject_html);
        free(name_html);
        free(description_html);
        return NULL;
    }

    int written = snprintf(content, size, html_support_body, received, ticket_url, add_info, subject_label, subject_html, incident_label, ticket_id,
                           customer, description_html);

    free(subject_html);
    free(name_html);
    free(description_html);
    
    if (written < 0 || (size_t)written >= size)
    {
        free(content);
        return NULL;
    }

    char* page = html_wrap(cHTTPX_i18n_t("email.support.brand", lang), content, cHTTPX_i18n_t("email.support.terms", lang), lang);
    free(content);
    return page;
}

char* html_support_answer_letter(const char* language, const char* ticket_id, const char* answer, const char* edit_token)
{
    if (!ticket_id || !answer || !edit_token)
        return NULL;

    const char* lang = language ? language : "en";
    char* answer_html = html_escape(answer);
    if (!answer_html)
        return NULL;

    const char* intro = cHTTPX_i18n_t("email.support.answer-intro", lang);
    const char* add_info = cHTTPX_i18n_t("email.support.add-info", lang);

    char ticket_url[192];
    snprintf(ticket_url, sizeof(ticket_url), "%s%s?token=%s", HTML_SUPPORT_TICKET_URL, ticket_id, edit_token);

    size_t size = strlen(html_support_answer_body) + strlen(intro) + strlen(answer_html) + strlen(ticket_url) + strlen(add_info) + 1;
    char* content = malloc(size);
    if (!content)
    {
        free(answer_html);
        return NULL;
    }

    int written = snprintf(content, size, html_support_answer_body, intro, answer_html, ticket_url, add_info);
    free(answer_html);
    if (written < 0 || (size_t)written >= size)
    {
        free(content);
        return NULL;
    }

    char* page = html_wrap(cHTTPX_i18n_t("email.support.brand", lang), content, cHTTPX_i18n_t("email.support.terms", lang), lang);
    free(content);
    return page;
}
