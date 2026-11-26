#define STORYFLOW_EDITOR_TEST
#include "../src/main.cpp"

namespace {
bool press(Editor& editor, guint key, guint modifiers = 0) {
    GdkEventKey event{};
    event.type = GDK_KEY_PRESS;
    event.keyval = key;
    event.state = modifiers;
    gboolean handled = FALSE;
    g_signal_emit_by_name(editor.view, "key-press-event", &event, &handled);
    return handled;
}

void type(Editor& editor, const char* text) {
    gtk_text_buffer_begin_user_action(editor.buffer);
    gtk_text_buffer_insert_at_cursor(editor.buffer, text, -1);
    gtk_text_buffer_end_user_action(editor.buffer);
}

std::string html(Editor& editor) {
    return to_html(read_buffer(editor.buffer));
}

bool check_editor(Editor& editor) {
    type(editor, "whis");
    const auto before = html(editor);
    if (editor.suggestion != "pered" || before.find("whispered") != std::string::npos ||
        !press(editor, GDK_KEY_Escape) || !editor.suggestion.empty() || html(editor) != before)
        return false;

    type(editor, "p");
    if (editor.suggestion != "ered" || !press(editor, GDK_KEY_Tab) ||
        html(editor).find("whispered") == std::string::npos ||
        !press(editor, GDK_KEY_z, GDK_CONTROL_MASK) ||
        html(editor).find("whisp</pre>") == std::string::npos)
        return false;

    type(editor, "\nMirehaven. Mirehaven.\n went home.");
    GtkTextIter position;
    gtk_text_buffer_get_iter_at_line(editor.buffer, &position, 2);
    gtk_text_buffer_place_cursor(editor.buffer, &position);
    type(editor, "Mir");
    if (editor.suggestion != "ehaven" || !press(editor, GDK_KEY_Right, GDK_MOD1_MASK) ||
        html(editor).find("Mirehaven went home.") == std::string::npos)
        return false;

    type(editor, "\nMir");
    if (editor.suggestion.empty())
        return false;
    position = editor.cursor();
    gtk_text_iter_backward_char(&position);
    gtk_text_buffer_place_cursor(editor.buffer, &position);
    if (!editor.suggestion.empty())
        return false;

    gtk_text_buffer_get_end_iter(editor.buffer, &position);
    gtk_text_buffer_place_cursor(editor.buffer, &position);
    type(editor, "\nMir");
    if (editor.suggestion.empty())
        return false;
    auto anchor = editor.cursor();
    position = anchor;
    gtk_text_iter_backward_char(&anchor);
    gtk_text_buffer_select_range(editor.buffer, &position, &anchor);
    if (!editor.suggestion.empty())
        return false;

    gtk_text_buffer_get_end_iter(editor.buffer, &position);
    gtk_text_buffer_place_cursor(editor.buffer, &position);
    type(editor, "\nhe shook ");
    const auto phrase_before = html(editor);
    if (editor.suggestion != "his head" ||
        !press(editor, GDK_KEY_Right, GDK_MOD1_MASK) ||
        editor.suggestion != " head" ||
        html(editor).find("he shook his</pre>") == std::string::npos ||
        !press(editor, GDK_KEY_Tab) ||
        html(editor).find("he shook his head</pre>") == std::string::npos ||
        !press(editor, GDK_KEY_z, GDK_CONTROL_MASK) ||
        html(editor).find("he shook his</pre>") == std::string::npos ||
        !press(editor, GDK_KEY_z, GDK_CONTROL_MASK) || html(editor) != phrase_before)
        return false;

    type(editor, " ");
    if (editor.suggestion != "his head" || !press(editor, GDK_KEY_Tab) ||
        html(editor).find("he shook  his head</pre>") == std::string::npos ||
        !press(editor, GDK_KEY_z, GDK_CONTROL_MASK))
        return false;

    type(editor, ".");
    type(editor, " ");
    return editor.suggestion.empty();
}
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    const WordCompleter predictor{"whisper 3\nwhispered 12\nwhistle 8\n"};
    if (!predictor.suffix("wh", "", "").empty() ||
        predictor.suffix("Whis", "", "") != "pered" ||
        predictor.suffix("WHIS", "", "") != "PERED" ||
        predictor.suffix("whis", "whistle whistle", "whispered") != "tle" ||
        predictor.suffix("Élo", "Élodie Élodie", "") != "die" ||
        predictor.suffix("Mir", "", "Mirehaven") != "ehaven") {
        g_printerr("Word ranking or case handling failed\n");
        return 1;
    }

    const PhraseCompleter phrases{"look\there\nwe look\taway\nlook away\tfrom\naway from\thome\nfrom home\tagain\n"};
    if (phrases.continuation("We", "look") != "away from home" ||
        phrases.continuation("they", "look") != "here" ||
        !phrases.continuation("we", "unknown").empty()) {
        g_printerr("Phrase backoff or length limit failed\n");
        return 1;
    }

    auto* app = gtk_application_new(nullptr, G_APPLICATION_NON_UNIQUE);
    if (!g_application_register(G_APPLICATION(app), nullptr, nullptr)) {
        g_object_unref(app);
        return 1;
    }
    activate(app, nullptr);
    auto* window = GTK_WINDOW(gtk_application_get_windows(app)->data);
    auto* editor = static_cast<Editor*>(g_object_get_data(G_OBJECT(window), "editor"));
    const bool passed = check_editor(*editor);
    if (!passed)
        g_printerr("Editor completion interaction failed\n");
    gtk_widget_destroy(GTK_WIDGET(window));
    g_object_unref(app);
    return passed ? 0 : 1;
}
