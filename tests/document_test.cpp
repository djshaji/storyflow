#include "document.hpp"

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    auto* buffer = gtk_text_buffer_new(nullptr);
    gtk_text_buffer_create_tag(buffer, "bold", nullptr);
    gtk_text_buffer_create_tag(buffer, "italic", nullptr);
    const Document original = {
        {"\n  <Alice> & ", false, false},
        {"कहानी—雪", true, true},
        {"\n\nEnd.  ", false, true},
    };
    Document reopened;
    const auto html = to_html(original);
    const bool parsed = from_html(html, reopened);
    write_buffer(buffer, reopened);
    const bool roundtrip = to_html(read_buffer(buffer)) == html;
    g_object_unref(buffer);
    Document external;
    const bool imported = from_html("<p><b>Hello</b></p><p><i>world</i></p>", external);
    return parsed && roundtrip && imported && external.size() == 3 &&
                   external[0].bold && external[1].text == "\n" && external[2].italic
               ? 0 : 1;
}
