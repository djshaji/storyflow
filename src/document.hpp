#pragma once

#include <gtk/gtk.h>

#include <string>
#include <vector>

struct StyledRun {
    std::string text;
    bool bold;
    bool italic;
};

using Document = std::vector<StyledRun>;

Document read_buffer(GtkTextBuffer* buffer);
void write_buffer(GtkTextBuffer* buffer, const Document& document);
std::string to_html(const Document& document);
bool from_html(const std::string& html, Document& document);
