#include "document.hpp"

#include <libxml/HTMLparser.h>

#include <climits>
#include <memory>

namespace {
struct FreeText {
    void operator()(gchar* text) const { g_free(text); }
};

void append(Document& document, const char* text, bool bold, bool italic) {
    if (!text || !*text)
        return;
    if (!document.empty() && document.back().bold == bold &&
        document.back().italic == italic) {
        document.back().text += text;
    } else {
        document.push_back({text, bold, italic});
    }
}

bool named(xmlNode* node, const char* name) {
    return xmlStrEqual(node->name, BAD_CAST name);
}

void read_nodes(xmlNode* node, Document& document, bool bold, bool italic) {
    for (; node; node = node->next) {
        if (node->type == XML_TEXT_NODE) {
            append(document, reinterpret_cast<const char*>(node->content), bold, italic);
            continue;
        }
        if (node->type != XML_ELEMENT_NODE || named(node, "script") ||
            named(node, "style") || named(node, "head"))
            continue;
        if (named(node, "br")) {
            append(document, "\n", bold, italic);
            continue;
        }
        const bool paragraph = named(node, "p") || named(node, "div");
        if (paragraph && !document.empty() && document.back().text.back() != '\n')
            append(document, "\n", false, false);
        read_nodes(node->children, document, bold || named(node, "strong") || named(node, "b"),
                   italic || named(node, "em") || named(node, "i"));
        if (paragraph && node->next && !document.empty() && document.back().text.back() != '\n')
            append(document, "\n", false, false);
    }
}
}

Document read_buffer(GtkTextBuffer* buffer) {
    Document document;
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    auto* tags = gtk_text_buffer_get_tag_table(buffer);
    auto* bold = gtk_text_tag_table_lookup(tags, "bold");
    auto* italic = gtk_text_tag_table_lookup(tags, "italic");
    while (!gtk_text_iter_is_end(&start)) {
        GtkTextIter end = start;
        if (!gtk_text_iter_forward_to_tag_toggle(&end, nullptr))
            gtk_text_buffer_get_end_iter(buffer, &end);
        std::unique_ptr<gchar, FreeText> text(gtk_text_buffer_get_text(buffer, &start, &end, TRUE));
        append(document, text.get(), gtk_text_iter_has_tag(&start, bold),
               gtk_text_iter_has_tag(&start, italic));
        start = end;
    }
    return document;
}

void write_buffer(GtkTextBuffer* buffer, const Document& document) {
    gtk_text_buffer_set_text(buffer, "", 0);
    for (const auto& run : document) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        const int offset = gtk_text_iter_get_offset(&end);
        gtk_text_buffer_insert(buffer, &end, run.text.c_str(), -1);
        GtkTextIter start;
        gtk_text_buffer_get_iter_at_offset(buffer, &start, offset);
        gtk_text_buffer_remove_all_tags(buffer, &start, &end);
        if (run.bold)
            gtk_text_buffer_apply_tag_by_name(buffer, "bold", &start, &end);
        if (run.italic)
            gtk_text_buffer_apply_tag_by_name(buffer, "italic", &start, &end);
    }
}

std::string to_html(const Document& document) {
    std::string html = "<!doctype html>\n<html><head><meta charset=\"utf-8\"></head>"
                       "<body><pre>";
    for (const auto& run : document) {
        if (run.bold)
            html += "<strong>";
        if (run.italic)
            html += "<em>";
        for (char character : run.text) {
            switch (character) {
            case '&': html += "&amp;"; break;
            case '<': html += "&lt;"; break;
            case '>': html += "&gt;"; break;
            case '\n': html += "<br>"; break;
            case '\r': html += "&#13;"; break;
            default: html += character; break;
            }
        }
        if (run.italic)
            html += "</em>";
        if (run.bold)
            html += "</strong>";
    }
    return html + "</pre></body></html>\n";
}

bool from_html(const std::string& html, Document& document) {
    if (html.size() > INT_MAX || html.find('\0') != std::string::npos ||
        !g_utf8_validate(html.data(), html.size(), nullptr))
        return false;
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> parsed(
        htmlReadMemory(html.data(), static_cast<int>(html.size()), nullptr, "UTF-8",
                       HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING),
        xmlFreeDoc);
    if (!parsed)
        return false;
    document.clear();
    read_nodes(xmlDocGetRootElement(parsed.get()), document, false, false);
    return true;
}
