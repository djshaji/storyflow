#include "completion.hpp"
#include "document.hpp"
#include "word_data.hpp"

#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

bool continues_word(gunichar character) {
    return g_unichar_isalpha(character) || g_unichar_ismark(character);
}

struct Snapshot {
    Document document;
    int anchor;
    int cursor;
    std::uint64_t revision;
};

struct Editor {
    GtkWidget* window = nullptr;
    GtkWidget* view = nullptr;
    GtkTextBuffer* buffer = nullptr;
    GtkWidget* bold_button = nullptr;
    GtkWidget* italic_button = nullptr;
    std::string filename;
    Snapshot pending_snapshot;
    std::vector<Snapshot> undo_stack;
    std::vector<Snapshot> redo_stack;
    WordCompleter predictor{word_frequencies};
    std::string suggestion;
    std::uint64_t revision = 0;
    std::uint64_t saved_revision = 0;
    std::uint64_t next_revision = 0;
    bool restoring = false;
    bool in_action = false;
    bool updating_buttons = false;
    bool pending_format = false;
    bool pending_bold = false;
    bool pending_italic = false;
    int pending_offset = 0;

    void update_title() {
        gchar* basename = filename.empty() ? nullptr : g_filename_display_basename(filename.c_str());
        const std::string title = std::string(basename ? basename : "Untitled") +
                                  (revision != saved_revision ? " *" : "") + " — StoryFlow";
        gtk_window_set_title(GTK_WINDOW(window), title.c_str());
        g_free(basename);
    }

    GtkTextIter cursor() const {
        GtkTextIter position;
        gtk_text_buffer_get_iter_at_mark(buffer, &position, gtk_text_buffer_get_insert(buffer));
        return position;
    }

    Snapshot snapshot() const {
        GtkTextIter anchor;
        gtk_text_buffer_get_iter_at_mark(buffer, &anchor, gtk_text_buffer_get_selection_bound(buffer));
        const auto position = cursor();
        return {read_buffer(buffer), gtk_text_iter_get_offset(&anchor),
                gtk_text_iter_get_offset(&position), revision};
    }

    bool has_style(const GtkTextIter& position, const char* name) const {
        auto* tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
        return gtk_text_iter_has_tag(&position, tag);
    }

    void sync_buttons() {
        GtkTextIter position = cursor();
        GtkTextIter end;
        const bool selection = gtk_text_buffer_get_selection_bounds(buffer, &position, &end);
        if (!selection)
            gtk_text_iter_backward_char(&position);
        updating_buttons = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bold_button),
            pending_format ? pending_bold : has_style(position, "bold"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(italic_button),
            pending_format ? pending_italic : has_style(position, "italic"));
        updating_buttons = false;
    }

    void set_suggestion(std::string value) {
        suggestion = std::move(value);
        gtk_widget_queue_draw(view);
    }

    void predict() {
        const GtkTextIter end = cursor();
        if (gtk_text_buffer_get_has_selection(buffer) ||
            continues_word(gtk_text_iter_get_char(&end))) {
            set_suggestion({});
            return;
        }

        GtkTextIter start = end;
        GtkTextIter previous = start;
        while (gtk_text_iter_backward_char(&previous) &&
               continues_word(gtk_text_iter_get_char(&previous)))
            start = previous;
        if (gtk_text_iter_get_offset(&end) - gtk_text_iter_get_offset(&start) < 3) {
            set_suggestion({});
            return;
        }

        GtkTextIter document_start, document_end;
        gtk_text_buffer_get_bounds(buffer, &document_start, &document_end);
        gchar* prefix = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
        gchar* before = gtk_text_buffer_get_text(buffer, &document_start, &start, TRUE);
        gchar* after = gtk_text_buffer_get_text(buffer, &end, &document_end, TRUE);
        set_suggestion(predictor.suffix(prefix, before, after));
        g_free(prefix);
        g_free(before);
        g_free(after);
    }

    void remember(std::vector<Snapshot>& stack, Snapshot state) {
        if (stack.size() == 64)
            stack.erase(stack.begin());
        stack.push_back(std::move(state));
    }

    void begin_action() {
        if (!restoring) {
            in_action = true;
            pending_snapshot = snapshot();
        }
    }

    void end_action() {
        if (restoring)
            return;
        in_action = false;
        const auto position = cursor();
        pending_offset = gtk_text_iter_get_offset(&position);
        remember(undo_stack, std::move(pending_snapshot));
        redo_stack.clear();
        revision = ++next_revision;
        update_title();
        sync_buttons();
        predict();
    }

    void restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to) {
        if (from.empty())
            return;
        remember(to, snapshot());
        Snapshot state = std::move(from.back());
        from.pop_back();
        restoring = true;
        pending_format = false;
        write_buffer(buffer, state.document);
        GtkTextIter anchor, position;
        gtk_text_buffer_get_iter_at_offset(buffer, &anchor, state.anchor);
        gtk_text_buffer_get_iter_at_offset(buffer, &position, state.cursor);
        gtk_text_buffer_select_range(buffer, &position, &anchor);
        restoring = false;
        revision = state.revision;
        set_suggestion({});
        sync_buttons();
        update_title();
    }

    void toggle_style(const char* name) {
        if (updating_buttons)
            return;
        GtkTextIter start, end;
        if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
            if (!pending_format) {
                const auto position = cursor();
                pending_offset = gtk_text_iter_get_offset(&position);
                pending_format = true;
            }
            pending_bold = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bold_button));
            pending_italic = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(italic_button));
        } else {
            GtkTextIter scan = start;
            while (gtk_text_iter_compare(&scan, &end) < 0 && has_style(scan, name))
                gtk_text_iter_forward_char(&scan);
            gtk_text_buffer_begin_user_action(buffer);
            if (gtk_text_iter_equal(&scan, &end))
                gtk_text_buffer_remove_tag_by_name(buffer, name, &start, &end);
            else
                gtk_text_buffer_apply_tag_by_name(buffer, name, &start, &end);
            gtk_text_buffer_end_user_action(buffer);
        }
        gtk_widget_grab_focus(view);
    }

    void style_insert(GtkTextIter* end, const gchar* text, gint bytes) {
        if (restoring || !pending_format)
            return;
        const int offset = gtk_text_iter_get_offset(end);
        GtkTextIter start;
        gtk_text_buffer_get_iter_at_offset(buffer, &start, offset - g_utf8_strlen(text, bytes));
        for (const auto& style : {std::pair{"bold", pending_bold}, std::pair{"italic", pending_italic}}) {
            if (style.second)
                gtk_text_buffer_apply_tag_by_name(buffer, style.first, &start, end);
            else
                gtk_text_buffer_remove_tag_by_name(buffer, style.first, &start, end);
        }
        pending_offset = offset;
    }

    void accept() {
        const auto accepted = suggestion;
        const auto position = cursor();
        GtkTextIter previous = position;
        gtk_text_iter_backward_char(&previous);
        const bool bold = has_style(previous, "bold");
        const bool italic = has_style(previous, "italic");
        const int offset = gtk_text_iter_get_offset(&position);

        gtk_text_buffer_begin_user_action(buffer);
        GtkTextIter end = position;
        gtk_text_buffer_insert(buffer, &end, accepted.c_str(), -1);
        GtkTextIter start;
        gtk_text_buffer_get_iter_at_offset(buffer, &start, offset);
        gtk_text_buffer_remove_tag_by_name(buffer, "bold", &start, &end);
        gtk_text_buffer_remove_tag_by_name(buffer, "italic", &start, &end);
        if (bold)
            gtk_text_buffer_apply_tag_by_name(buffer, "bold", &start, &end);
        if (italic)
            gtk_text_buffer_apply_tag_by_name(buffer, "italic", &start, &end);
        gtk_text_buffer_end_user_action(buffer);
        set_suggestion({});
    }

    void show_error(const char* heading, const char* detail) {
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", heading);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    std::string choose_file(GtkFileChooserAction action) {
        const bool saving = action == GTK_FILE_CHOOSER_ACTION_SAVE;
        auto* dialog = gtk_file_chooser_dialog_new(saving ? "Save Story" : "Open Story",
            GTK_WINDOW(window), action, "_Cancel", GTK_RESPONSE_CANCEL,
            saving ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        auto* chooser = GTK_FILE_CHOOSER(dialog);
        auto* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "HTML documents");
        gtk_file_filter_add_pattern(filter, "*.html");
        gtk_file_filter_add_pattern(filter, "*.htm");
        gtk_file_chooser_add_filter(chooser, filter);
        if (saving) {
            gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
            if (filename.empty())
                gtk_file_chooser_set_current_name(chooser, "Untitled.html");
            else
                gtk_file_chooser_set_filename(chooser, filename.c_str());
        }
        std::string chosen;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            gchar* path = gtk_file_chooser_get_filename(chooser);
            if (path)
                chosen = path;
            g_free(path);
        }
        gtk_widget_destroy(dialog);
        return chosen;
    }

    bool save(bool save_as = false) {
        const auto path = save_as || filename.empty() ? choose_file(GTK_FILE_CHOOSER_ACTION_SAVE) : filename;
        if (path.empty())
            return false;
        const auto html = to_html(read_buffer(buffer));
        GError* error = nullptr;
        auto* file = g_file_new_for_path(path.c_str());
        const bool saved = g_file_replace_contents(file, html.data(), html.size(), nullptr, FALSE,
            G_FILE_CREATE_NONE, nullptr, nullptr, &error);
        g_object_unref(file);
        if (!saved) {
            show_error("Could not save the story", error->message);
            g_error_free(error);
            return false;
        }
        filename = path;
        saved_revision = revision;
        update_title();
        return true;
    }

    bool can_discard() {
        if (revision == saved_revision)
            return true;
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Save changes to this story?");
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Discard", GTK_RESPONSE_REJECT,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return response == GTK_RESPONSE_REJECT || (response == GTK_RESPONSE_ACCEPT && save());
    }

    void open() {
        if (!can_discard())
            return;
        const auto path = choose_file(GTK_FILE_CHOOSER_ACTION_OPEN);
        if (path.empty())
            return;
        gchar* text = nullptr;
        gsize length = 0;
        GError* error = nullptr;
        if (!g_file_get_contents(path.c_str(), &text, &length, &error)) {
            show_error("Could not open the story", error->message);
            g_error_free(error);
            return;
        }
        Document document;
        const bool parsed = from_html(std::string(text, length), document);
        g_free(text);
        if (!parsed) {
            show_error("Could not open the story", "Choose a UTF-8 HTML document.");
            return;
        }
        restoring = true;
        pending_format = false;
        write_buffer(buffer, document);
        auto position = cursor();
        gtk_text_buffer_get_start_iter(buffer, &position);
        gtk_text_buffer_place_cursor(buffer, &position);
        restoring = false;
        undo_stack.clear();
        redo_stack.clear();
        filename = path;
        revision = saved_revision = ++next_revision;
        set_suggestion({});
        sync_buttons();
        update_title();
    }

    gboolean key_press(GdkEventKey* event) {
        const auto modifiers = event->state & gtk_accelerator_get_default_mod_mask();
        if (modifiers == GDK_CONTROL_MASK || modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
            const bool shift = modifiers & GDK_SHIFT_MASK;
            switch (gdk_keyval_to_lower(event->keyval)) {
            case GDK_KEY_o: if (!shift) { open(); return TRUE; } break;
            case GDK_KEY_s: save(shift); return TRUE;
            case GDK_KEY_b: if (!shift) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bold_button),
                !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bold_button))); return TRUE; } break;
            case GDK_KEY_i: if (!shift) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(italic_button),
                !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(italic_button))); return TRUE; } break;
            case GDK_KEY_z:
                if (shift)
                    restore(redo_stack, undo_stack);
                else
                    restore(undo_stack, redo_stack);
                return TRUE;
            case GDK_KEY_y: if (!shift) { restore(redo_stack, undo_stack); return TRUE; } break;
            }
        }
        if (!suggestion.empty()) {
            if (modifiers == 0 && event->keyval == GDK_KEY_Escape) {
                set_suggestion({});
                return TRUE;
            }
            if (modifiers == 0 && event->keyval == GDK_KEY_Tab) {
                accept();
                return TRUE;
            }
            if (modifiers == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) {
                accept();
                return TRUE;
            }
        }
        return FALSE;
    }

    void draw_suggestion(cairo_t* cr) {
        if (suggestion.empty() || !gtk_widget_has_focus(view))
            return;
        auto position = cursor();
        auto* text_view = GTK_TEXT_VIEW(view);
        GdkRectangle caret, visible;
        gtk_text_view_get_cursor_locations(text_view, &position, &caret, nullptr);
        gtk_text_view_get_visible_rect(text_view, &visible);
        int x, y, clip_x, clip_y;
        gtk_text_view_buffer_to_window_coords(text_view, GTK_TEXT_WINDOW_WIDGET,
                                               caret.x, caret.y, &x, &y);
        gtk_text_view_buffer_to_window_coords(text_view, GTK_TEXT_WINDOW_WIDGET,
                                               visible.x, visible.y, &clip_x, &clip_y);
        auto* layout = gtk_widget_create_pango_layout(view, suggestion.c_str());
        auto* style = gtk_widget_get_style_context(view);
        GdkRGBA color;
        gtk_style_context_get_color(style, gtk_widget_get_state_flags(view), &color);
        cairo_save(cr);
        cairo_rectangle(cr, clip_x, clip_y, visible.width, visible.height);
        cairo_clip(cr);
        int width, height;
        pango_layout_get_pixel_size(layout, &width, &height);
        if ((!gtk_text_iter_ends_line(&position) && !gtk_text_iter_is_end(&position)) ||
            x + width > clip_x + visible.width) {
            constexpr int padding = 4;
            x = std::clamp(x, clip_x + padding,
                           std::max(clip_x + padding, clip_x + visible.width - width - padding));
            y = y - height - 2 * padding >= clip_y ? y - height - padding : y + caret.height + padding;
            gtk_style_context_save(style);
            gtk_style_context_add_class(style, GTK_STYLE_CLASS_TOOLTIP);
            gtk_render_background(style, cr, x - padding, y - padding,
                                  width + 2 * padding, height + 2 * padding);
            gtk_render_frame(style, cr, x - padding, y - padding,
                             width + 2 * padding, height + 2 * padding);
            gtk_style_context_get_color(style, gtk_widget_get_state_flags(view), &color);
            gtk_style_context_restore(style);
            cairo_set_source_rgba(cr, color.red, color.green, color.blue, 1);
        } else {
            cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.45);
        }
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout);
        cairo_restore(cr);
        g_object_unref(layout);
    }
};

void activate(GtkApplication* app, gpointer) {
    if (auto* existing = gtk_application_get_active_window(app)) {
        gtk_window_present(existing);
        return;
    }
    auto* editor = new Editor;
    editor->window = gtk_application_window_new(app);
    g_object_set_data_full(G_OBJECT(editor->window), "editor", editor,
                           [](gpointer data) { delete static_cast<Editor*>(data); });
    gtk_window_set_default_size(GTK_WINDOW(editor->window), 850, 650);

    auto* header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "StoryFlow");
    gtk_window_set_titlebar(GTK_WINDOW(editor->window), header);

    auto* open = gtk_button_new_with_label("Open");
    auto* save = gtk_button_new_with_label("Save");
    auto* save_as = gtk_button_new_with_label("Save As");
    for (auto* button : {open, save, save_as})
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), button);
    editor->bold_button = gtk_toggle_button_new_with_label("Bold");
    editor->italic_button = gtk_toggle_button_new_with_label("Italic");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), editor->italic_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), editor->bold_button);

    auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    editor->view = gtk_text_view_new();
    auto* view = GTK_TEXT_VIEW(editor->view);
    gtk_text_view_set_wrap_mode(view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(view, 32);
    gtk_text_view_set_right_margin(view, 32);
    gtk_text_view_set_top_margin(view, 24);
    gtk_text_view_set_bottom_margin(view, 24);
    editor->buffer = gtk_text_view_get_buffer(view);
    gtk_text_buffer_create_tag(editor->buffer, "bold", "weight", PANGO_WEIGHT_BOLD, nullptr);
    gtk_text_buffer_create_tag(editor->buffer, "italic", "style", PANGO_STYLE_ITALIC, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), editor->view);
    gtk_container_add(GTK_CONTAINER(editor->window), scroll);

    g_signal_connect(open, "clicked", G_CALLBACK(+[](GtkButton*, Editor* e) { e->open(); }), editor);
    g_signal_connect(save, "clicked", G_CALLBACK(+[](GtkButton*, Editor* e) { e->save(); }), editor);
    g_signal_connect(save_as, "clicked", G_CALLBACK(+[](GtkButton*, Editor* e) { e->save(true); }), editor);
    g_signal_connect(editor->bold_button, "toggled",
        G_CALLBACK(+[](GtkToggleButton*, Editor* e) { e->toggle_style("bold"); }), editor);
    g_signal_connect(editor->italic_button, "toggled",
        G_CALLBACK(+[](GtkToggleButton*, Editor* e) { e->toggle_style("italic"); }), editor);
    g_signal_connect(editor->buffer, "begin-user-action",
        G_CALLBACK(+[](GtkTextBuffer*, Editor* e) { e->begin_action(); }), editor);
    g_signal_connect(editor->buffer, "end-user-action",
        G_CALLBACK(+[](GtkTextBuffer*, Editor* e) { e->end_action(); }), editor);
    g_signal_connect_after(editor->buffer, "insert-text",
        G_CALLBACK(+[](GtkTextBuffer*, GtkTextIter* end, gchar* text, gint bytes, Editor* e) {
            e->style_insert(end, text, bytes);
        }), editor);
    g_signal_connect(editor->buffer, "mark-set",
        G_CALLBACK(+[](GtkTextBuffer* buffer, GtkTextIter*, GtkTextMark* mark, Editor* e) {
            if (e->restoring ||
                (mark != gtk_text_buffer_get_insert(buffer) &&
                 mark != gtk_text_buffer_get_selection_bound(buffer)))
                return;
            const auto position = e->cursor();
            if (!e->in_action && e->pending_format &&
                (gtk_text_iter_get_offset(&position) != e->pending_offset ||
                 gtk_text_buffer_get_has_selection(buffer)))
                e->pending_format = false;
            e->set_suggestion({});
            e->sync_buttons();
        }), editor);
    g_signal_connect(editor->view, "key-press-event",
        G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, Editor* e) -> gboolean {
            return e->key_press(event);
        }), editor);
    g_signal_connect_after(editor->view, "draw",
        G_CALLBACK(+[](GtkWidget*, cairo_t* cr, Editor* e) -> gboolean {
            e->draw_suggestion(cr);
            return FALSE;
        }), editor);
    g_signal_connect(editor->window, "delete-event",
        G_CALLBACK(+[](GtkWidget*, GdkEvent*, Editor* e) -> gboolean {
            return !e->can_discard();
        }), editor);

    editor->update_title();
    gtk_widget_show_all(editor->window);
    gtk_widget_grab_focus(editor->view);
}

}

#ifndef STORYFLOW_EDITOR_TEST
int main(int argc, char** argv) {
    auto* app = gtk_application_new("io.github.djshaji.StoryFlow", static_cast<GApplicationFlags>(0));
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    const int result = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return result;
}

#endif
