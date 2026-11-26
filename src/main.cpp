#include "completion.hpp"
#include "document.hpp"
#include "sentence.hpp"
#include "profiles.hpp"
#include "project.hpp"
#include "word_data.hpp"

#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool continues_word(gunichar character) {
    return g_unichar_isalpha(character) || g_unichar_ismark(character);
}

constexpr std::array<const char*, 4> writing_profiles{
    "Fiction", "Literary fiction", "Academic", "Personal"
};

std::string journal_path() {
    gchar* directory = g_build_filename(g_get_user_data_dir(), "storyflow", nullptr);
    g_mkdir_with_parents(directory, 0700);
    gchar* filename = g_build_filename(directory, "profiles.sqlite3", nullptr);
    std::string path(filename);
    g_free(filename);
    g_free(directory);
    return path;
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
    GtkHeaderBar* header = nullptr;
    GtkTextBuffer* buffer = nullptr;
    GtkWidget* bold_button = nullptr;
    GtkWidget* italic_button = nullptr;
    StoryProject project;
    Vocabulary project_names;
    ProfileStore profiles{journal_path()};
    std::string active_profile;
    Vocabulary profile_words;
    Transitions profile_phrases;
    std::string filename;
    Snapshot pending_snapshot;
    std::vector<Snapshot> undo_stack;
    std::vector<Snapshot> redo_stack;
    WordCompleter predictor{word_frequencies};
    PhraseCompleter phrases{phrase_transitions};
    std::string suggestion;
    SentenceClient sentence_client;
    GCancellable* sentence_request = nullptr;
    std::shared_ptr<int> lifetime = std::make_shared<int>(0);
    std::uint64_t sentence_generation = 0;
    guint sentence_key = GDK_KEY_space;
    GdkModifierType sentence_modifiers = GDK_CONTROL_MASK;
    unsigned sentence_port = 8080;
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

    Editor() {
        if (const char* shortcut = g_getenv("STORYFLOW_SENTENCE_SHORTCUT")) {
            guint key = 0;
            GdkModifierType modifiers{};
            gtk_accelerator_parse(shortcut, &key, &modifiers);
            if (key && (modifiers & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) &&
                gtk_accelerator_valid(key, modifiers)) {
                sentence_key = key;
                sentence_modifiers = modifiers;
            }
        }
        if (const char* value = g_getenv("STORYFLOW_LLAMA_PORT")) {
            guint64 port = 0;
            if (g_ascii_string_to_unsigned(value, 10, 1, 65535, &port, nullptr))
                sentence_port = static_cast<unsigned>(port);
        }
    }

    ~Editor() {
        lifetime.reset();
        if (sentence_request)
            g_cancellable_cancel(sentence_request);
        g_clear_object(&sentence_request);
    }

    void cancel_sentence() {
        ++sentence_generation;
        if (sentence_request)
            g_cancellable_cancel(sentence_request);
        g_clear_object(&sentence_request);
        gtk_header_bar_set_subtitle(header, nullptr);
    }

    void request_sentence() {
        cancel_sentence();
        set_suggestion({});
        auto end = cursor();
        if (gtk_text_buffer_get_has_selection(buffer) || !gtk_text_iter_ends_line(&end)) {
            gtk_header_bar_set_subtitle(header, "Place the caret at the end of a line");
            return;
        }
        auto start = end;
        gtk_text_iter_backward_chars(&start, 4096);
        gchar* raw = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
        std::string prompt(raw);
        g_free(raw);
        const auto current_paragraph = prompt.rfind("\n\n");
        if (current_paragraph != std::string::npos && current_paragraph > 0) {
            const auto earlier_paragraph = prompt.rfind("\n\n", current_paragraph - 1);
            if (earlier_paragraph != std::string::npos)
                prompt.erase(0, earlier_paragraph + 2);
        }
        if (prompt.find_last_not_of(" \t\r\n") == std::string::npos) {
            gtk_header_bar_set_subtitle(header, "Write some text first");
            return;
        }
        auto previous = end;
        const bool after_space = gtk_text_iter_backward_char(&previous) &&
                                 g_unichar_isspace(gtk_text_iter_get_char(&previous));
        sentence_request = g_cancellable_new();
        const auto generation = sentence_generation;
        gtk_header_bar_set_subtitle(header, "Generating sentence…");
        sentence_client.request(sentence_port, project.sentence_prompt(prompt), sentence_request,
            [this, alive = std::weak_ptr<int>(lifetime), generation, after_space]
            (std::string sentence, const std::string& error) {
                if (alive.expired() || generation != sentence_generation)
                    return;
                g_clear_object(&sentence_request);
                gtk_header_bar_set_subtitle(header, error.empty() ? nullptr : error.c_str());
                if (after_space && !sentence.empty())
                    sentence.erase(0, sentence.find_first_not_of(" \t"));
                set_suggestion(std::move(sentence));
            });
    }

    void load_profile() {
        if (active_profile.empty()) {
            profile_words.clear();
            profile_phrases.clear();
        } else if (!profiles.load(active_profile, profile_words, profile_phrases)) {
            gtk_header_bar_set_subtitle(header, "Could not load writing profile");
        }
    }

    void change_profile(const char* profile) {
        cancel_sentence();
        set_suggestion({});
        active_profile = profile ? profile : "";
        load_profile();
        if (!profiles.select(active_profile))
            gtk_header_bar_set_subtitle(header, "Could not save profile selection");
        gtk_widget_grab_focus(view);
    }

    void learn_saved_document(const std::string& path, const Document& document) {
        if (active_profile.empty())
            return;
        std::string text;
        for (const auto& run : document)
            text += run.text;
        if (profiles.learn(active_profile, path, writing_statistics(text)))
            load_profile();
        else
            gtk_header_bar_set_subtitle(header, "Story saved, but profile learning failed");
    }

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

    static GtkTextIter word_start(GtkTextIter end) {
        GtkTextIter previous = end;
        while (gtk_text_iter_backward_char(&previous) &&
               continues_word(gtk_text_iter_get_char(&previous)))
            end = previous;
        return end;
    }

    std::string phrase_at(GtkTextIter end) const {
        auto read_previous = [this](GtkTextIter& position) {
            const auto boundary = position;
            GtkTextIter previous = position;
            while (gtk_text_iter_backward_char(&previous) &&
                   gtk_text_iter_get_char(&previous) == ' ')
                position = previous;
            if (gtk_text_iter_equal(&position, &boundary) ||
                !gtk_text_iter_backward_char(&position) ||
                !continues_word(gtk_text_iter_get_char(&position)))
                return std::string{};
            gtk_text_iter_forward_char(&position);
            const auto start = word_start(position);
            gchar* text = gtk_text_buffer_get_text(buffer, &start, &position, TRUE);
            std::string word(text);
            g_free(text);
            position = start;
            return word;
        };
        auto recent = read_previous(end);
        if (recent.empty())
            return {};
        auto older = read_previous(end);
        return phrases.continuation(std::move(older), std::move(recent), profile_phrases);
    }

    void predict() {
        const GtkTextIter end = cursor();
        if (gtk_text_buffer_get_has_selection(buffer) ||
            continues_word(gtk_text_iter_get_char(&end))) {
            set_suggestion({});
            return;
        }

        auto previous = end;
        if (gtk_text_iter_backward_char(&previous) && gtk_text_iter_get_char(&previous) == ' ') {
            set_suggestion(phrase_at(end));
            return;
        }

        const auto start = word_start(end);
        if (gtk_text_iter_get_offset(&end) - gtk_text_iter_get_offset(&start) < 3) {
            set_suggestion({});
            return;
        }

        GtkTextIter document_start, document_end;
        gtk_text_buffer_get_bounds(buffer, &document_start, &document_end);
        gchar* prefix = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
        gchar* before = gtk_text_buffer_get_text(buffer, &document_start, &start, TRUE);
        gchar* after = gtk_text_buffer_get_text(buffer, &end, &document_end, TRUE);
        static const WordCompleter project_completer{""};
        auto name = project_completer.suffix(prefix, "", "", project_names);
        set_suggestion(name.empty() ? predictor.suffix(prefix, before, after, profile_words)
                                    : std::move(name));
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
            cancel_sentence();
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
        cancel_sentence();
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

    void accept(bool word_only) {
        auto accepted = suggestion;
        std::string remainder;
        if (word_only) {
            const auto boundary = accepted.find(' ', accepted.find_first_not_of(' '));
            if (boundary != std::string::npos) {
                remainder = accepted.substr(boundary);
                accepted.erase(boundary);
            }
        }
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
        set_suggestion(std::move(remainder));
    }

    void show_error(const char* heading, const char* detail) {
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", heading);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    std::string choose_file(GtkFileChooserAction action, bool project_file = false) {
        const bool saving = action == GTK_FILE_CHOOSER_ACTION_SAVE;
        auto* dialog = gtk_file_chooser_dialog_new(
            project_file ? (saving ? "Save Project" : "Open Project")
                         : (saving ? "Save Story" : "Open Story"),
            GTK_WINDOW(window), action, "_Cancel", GTK_RESPONSE_CANCEL,
            saving ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        auto* chooser = GTK_FILE_CHOOSER(dialog);
        auto* filter = gtk_file_filter_new();
        if (project_file) {
            gtk_file_filter_set_name(filter, "StoryFlow projects");
            gtk_file_filter_add_pattern(filter, "*.storyflow");
        } else {
            gtk_file_filter_set_name(filter, "HTML documents");
            gtk_file_filter_add_pattern(filter, "*.html");
            gtk_file_filter_add_pattern(filter, "*.htm");
        }
        gtk_file_chooser_add_filter(chooser, filter);
        if (saving) {
            gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
            if (project_file)
                gtk_file_chooser_set_current_name(chooser, "Untitled.storyflow");
            else if (filename.empty())
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
        const auto document = read_buffer(buffer);
        const auto html = to_html(document);
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
        learn_saved_document(path, document);
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
        cancel_sentence();
        if (!can_discard())
            return;
        const auto path = choose_file(GTK_FILE_CHOOSER_ACTION_OPEN);
        if (!path.empty() && open_document(path)) {
            const auto canonical = project.chapter_path(path);
            const bool listed = std::any_of(project.chapters.begin(), project.chapters.end(),
                [&](const auto& chapter) { return project.chapter_path(chapter) == canonical; });
            if (!listed)
                set_project({});
        }
    }

    bool open_document(const std::string& path) {
        gchar* text = nullptr;
        gsize length = 0;
        GError* error = nullptr;
        if (!g_file_get_contents(path.c_str(), &text, &length, &error)) {
            show_error("Could not open the story", error->message);
            g_error_free(error);
            return false;
        }
        Document document;
        const bool parsed = from_html(std::string(text, length), document);
        g_free(text);
        if (!parsed) {
            show_error("Could not open the story", "Choose a UTF-8 HTML document.");
            return false;
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
        return true;
    }

    void set_project(StoryProject next) {
        cancel_sentence();
        set_suggestion({});
        project = std::move(next);
        project_names = project.names();
        gchar* title = project.path.empty() ? nullptr : g_filename_display_basename(project.path.c_str());
        gtk_header_bar_set_title(header, title ? title : "StoryFlow");
        g_free(title);
    }

    void open_project() {
        const auto path = choose_file(GTK_FILE_CHOOSER_ACTION_OPEN, true);
        if (path.empty())
            return;
        StoryProject next;
        GError* error = nullptr;
        if (!StoryProject::read(path, next, &error)) {
            show_error("Could not open the project", error->message);
            g_error_free(error);
            return;
        }
        if (!can_discard())
            return;
        if (!next.chapters.empty()) {
            if (!open_document(next.chapter_path(next.chapters.front())))
                return;
        } else {
            clear_document();
        }
        set_project(std::move(next));
    }

    void clear_document() {
        restoring = true;
        pending_format = false;
        write_buffer(buffer, {});
        restoring = false;
        filename.clear();
        undo_stack.clear();
        redo_stack.clear();
        revision = saved_revision = ++next_revision;
        sync_buttons();
        update_title();
    }

    void choose_chapter() {
        if (project.chapters.empty()) {
            show_error("No chapters in this project", "Add HTML chapter paths in Project → Edit Project.");
            return;
        }
        auto* dialog = gtk_dialog_new_with_buttons("Open Chapter", GTK_WINDOW(window),
            GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        auto* chapters = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), GTK_WIDGET(chapters));
        int selected = 0;
        for (std::size_t i = 0; i < project.chapters.size(); ++i) {
            const auto& chapter = project.chapters[i];
            gtk_combo_box_text_append_text(chapters, chapter.c_str());
            if (project.chapter_path(chapter) == filename)
                selected = static_cast<int>(i);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(chapters), selected);
        gtk_widget_show_all(dialog);
        const bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
        const auto index = gtk_combo_box_get_active(GTK_COMBO_BOX(chapters));
        gtk_widget_destroy(dialog);
        if (accepted && index >= 0 && can_discard()) {
            cancel_sentence();
            open_document(project.chapter_path(project.chapters[index]));
        }
    }

    void edit_project(bool create) {
        auto next = create ? StoryProject{} : project;
        if (next.path.empty()) {
            next.path = choose_file(GTK_FILE_CHOOSER_ACTION_SAVE, true);
            if (next.path.empty())
                return;
        }
        auto* dialog = gtk_dialog_new_with_buttons("Story Project", GTK_WINDOW(window),
            GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 540);
        auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content), 12);
        gtk_box_set_spacing(GTK_BOX(content), 8);
        const auto section = [&](const char* label, const std::string& text) {
            auto* caption = gtk_label_new(label);
            gtk_widget_set_halign(caption, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(content), caption, FALSE, FALSE, 0);
            auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
            auto* field = gtk_text_view_new();
            auto* field_view = GTK_TEXT_VIEW(field);
            gtk_text_view_set_wrap_mode(field_view, GTK_WRAP_WORD_CHAR);
            auto* field_buffer = gtk_text_view_get_buffer(field_view);
            gtk_text_buffer_set_text(field_buffer, text.c_str(), -1);
            gtk_container_add(GTK_CONTAINER(scroll), field);
            gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
            return field_buffer;
        };
        std::string chapter_list;
        for (const auto& chapter : next.chapters)
            chapter_list += chapter + "\n";
        if (create && !filename.empty())
            chapter_list = filename + "\n";
        auto* chapters = section("Chapter paths, one HTML file per line in reading order", chapter_list);
        auto* add = gtk_button_new_with_label("Add HTML Chapter…");
        gtk_widget_set_halign(add, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(content), add, FALSE, FALSE, 0);
        g_object_set_data(G_OBJECT(add), "chapters", chapters);
        g_signal_connect(add, "clicked", G_CALLBACK(+[](GtkButton* button, Editor* editor) {
            const auto path = editor->choose_file(GTK_FILE_CHOOSER_ACTION_OPEN);
            if (path.empty())
                return;
            auto* field = GTK_TEXT_BUFFER(g_object_get_data(G_OBJECT(button), "chapters"));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(field, &end);
            if (!gtk_text_iter_starts_line(&end))
                gtk_text_buffer_insert(field, &end, "\n", -1);
            gtk_text_buffer_insert(field, &end, (path + "\n").c_str(), -1);
        }), this);
        auto* cast = section("Characters, one per line (Name | Alias: notes)", next.characters);
        auto* location = section("Scene location", next.location);
        auto* summary = section("Scene summary", next.summary);
        const auto text = [](GtkTextBuffer* field) {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(field, &start, &end);
            gchar* value = gtk_text_buffer_get_text(field, &start, &end, TRUE);
            std::string result(value);
            g_free(value);
            return result;
        };
        gtk_widget_show_all(dialog);
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            next.chapters.clear();
            std::istringstream paths(text(chapters));
            std::string chapter;
            while (std::getline(paths, chapter)) {
                if (!chapter.empty())
                    next.chapters.push_back(chapter);
            }
            next.characters = text(cast);
            next.location = text(location);
            next.summary = text(summary);
            GError* error = nullptr;
            if (next.save(&error)) {
                set_project(std::move(next));
                break;
            }
            show_error("Could not save the project", error->message);
            g_error_free(error);
        }
        gtk_widget_destroy(dialog);
    }

    gboolean key_press(GdkEventKey* event) {
        const auto modifiers = event->state & gtk_accelerator_get_default_mod_mask();
        if (modifiers == sentence_modifiers && gdk_keyval_to_lower(event->keyval) == sentence_key) {
            request_sentence();
            return TRUE;
        }
        if (modifiers == 0 && event->keyval == GDK_KEY_Escape && sentence_request) {
            cancel_sentence();
            return TRUE;
        }
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
                accept(false);
                return TRUE;
            }
            if (modifiers == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) {
                accept(true);
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
    editor->header = GTK_HEADER_BAR(header);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "StoryFlow");
    gtk_window_set_titlebar(GTK_WINDOW(editor->window), header);

    auto* open = gtk_button_new_with_label("Open");
    auto* save = gtk_button_new_with_label("Save");
    auto* save_as = gtk_button_new_with_label("Save As");
    for (auto* button : {open, save, save_as})
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), button);
    auto* project_menu = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(project_menu), "Project");
    auto* items = gtk_menu_new();
    auto add_project_item = [&](const char* label, GCallback action) {
        auto* item = gtk_menu_item_new_with_label(label);
        gtk_menu_shell_append(GTK_MENU_SHELL(items), item);
        g_signal_connect(item, "activate", action, editor);
    };
    add_project_item("New Project", G_CALLBACK(+[](GtkMenuItem*, Editor* e) { e->edit_project(true); }));
    add_project_item("Open Project", G_CALLBACK(+[](GtkMenuItem*, Editor* e) { e->open_project(); }));
    add_project_item("Open Chapter", G_CALLBACK(+[](GtkMenuItem*, Editor* e) { e->choose_chapter(); }));
    add_project_item("Edit Project", G_CALLBACK(+[](GtkMenuItem*, Editor* e) {
        e->edit_project(e->project.path.empty());
    }));
    add_project_item("Close Project", G_CALLBACK(+[](GtkMenuItem*, Editor* e) { e->set_project({}); }));
    gtk_widget_show_all(items);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(project_menu), items);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), project_menu);
    editor->bold_button = gtk_toggle_button_new_with_label("Bold");
    editor->italic_button = gtk_toggle_button_new_with_label("Italic");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), editor->italic_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), editor->bold_button);

    auto* profiles = gtk_combo_box_text_new();
    auto* profile_combo = GTK_COMBO_BOX_TEXT(profiles);
    gtk_widget_set_tooltip_text(profiles, "Writing profile for vocabulary and phrase learning");
    gtk_combo_box_text_append(profile_combo, "", "Learning off");
    for (const auto* name : writing_profiles)
        gtk_combo_box_text_append(profile_combo, name, name);
    const auto selected = editor->profiles.selected();
    if (gtk_combo_box_set_active_id(GTK_COMBO_BOX(profiles), selected.c_str()))
        editor->active_profile = selected;
    else
        gtk_combo_box_set_active(GTK_COMBO_BOX(profiles), 0);
    editor->load_profile();
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), profiles);
    g_signal_connect(profiles, "changed",
        G_CALLBACK(+[](GtkComboBox* combo, Editor* e) {
            e->change_profile(gtk_combo_box_get_active_id(combo));
        }), editor);

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
            e->cancel_sentence();
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

    g_signal_connect(editor->window, "destroy",
        G_CALLBACK(+[](GtkWidget*, Editor* e) {
            e->lifetime.reset();
            if (e->sentence_request)
                g_cancellable_cancel(e->sentence_request);
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
