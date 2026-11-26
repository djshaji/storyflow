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

bool wait_for(const std::function<bool()>& done) {
    const auto deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND * 3;
    while (!done() && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(1000);
    }
    return done();
}

struct MockServer {
    int replies = 0;
    bool received_prompt = false;
};

void handle_sentence(SoupServer*, SoupServerMessage* message, const char*,
                     GHashTable*, gpointer data) {
    auto* mock = static_cast<MockServer*>(data);
    auto* bytes = soup_message_body_flatten(soup_server_message_get_request_body(message));
    gsize length = 0;
    const auto* raw = static_cast<const char*>(g_bytes_get_data(bytes, &length));
    mock->received_prompt = std::string_view(raw, length).find("The lamp was") != std::string::npos;
    g_bytes_unref(bytes);
    const char response[] = "{\"content\":\" dim. The second sentence.\"}";
    soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
    soup_server_message_set_response(message, "application/json", SOUP_MEMORY_COPY,
                                     response, sizeof(response) - 1);
    soup_server_message_pause(message);
    using Reply = std::pair<MockServer*, SoupServerMessage*>;
    g_timeout_add_full(G_PRIORITY_DEFAULT, 50, +[](gpointer payload) -> gboolean {
        auto* reply = static_cast<Reply*>(payload);
        ++reply->first->replies;
        soup_server_message_unpause(reply->second);
        return G_SOURCE_REMOVE;
    }, new Reply{mock, SOUP_SERVER_MESSAGE(g_object_ref(message))}, +[](gpointer payload) {
        auto* reply = static_cast<Reply*>(payload);
        g_object_unref(reply->second);
        delete reply;
    });
}

bool check_sentences(Editor& editor) {
    MockServer mock;
    auto* server = soup_server_new(nullptr, nullptr);
    auto* address = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    auto* socket = g_inet_socket_address_new(address, 0);
    g_object_unref(address);
    const bool listening = soup_server_listen(server, socket, static_cast<SoupServerListenOptions>(0),
                                             nullptr);
    g_object_unref(socket);
    if (!listening) {
        g_object_unref(server);
        return false;
    }
    auto* uris = soup_server_get_uris(server);
    editor.sentence_port = g_uri_get_port(static_cast<GUri*>(uris->data));
    g_slist_free_full(uris, +[](gpointer uri) { g_uri_unref(static_cast<GUri*>(uri)); });
    soup_server_add_handler(server, "/completion", handle_sentence, &mock, nullptr);
    auto end = editor.cursor();
    gtk_text_buffer_get_end_iter(editor.buffer, &end);
    gtk_text_buffer_place_cursor(editor.buffer, &end);
    type(editor, "\n\nThe lamp was");
    const auto before = html(editor);
    bool passed = press(editor, GDK_KEY_space, GDK_CONTROL_MASK) &&
                  wait_for([&] { return !editor.sentence_request; }) &&
                  mock.received_prompt && editor.suggestion == " dim." && html(editor) == before &&
                  press(editor, GDK_KEY_Tab) && html(editor).find("The lamp was dim.") != std::string::npos &&
                  press(editor, GDK_KEY_z, GDK_CONTROL_MASK) && html(editor) == before;

    for (int attempt = 0; passed && attempt < 3; ++attempt) {
        const auto previous_replies = mock.replies;
        mock.received_prompt = false;
        passed = press(editor, GDK_KEY_space, GDK_CONTROL_MASK) &&
                 wait_for([&] { return mock.received_prompt; });
        if (!passed)
            break;
        if (attempt == 0) {
            auto caret = editor.cursor();
            gtk_text_iter_backward_char(&caret);
            gtk_text_buffer_place_cursor(editor.buffer, &caret);
        } else if (attempt == 1) {
            type(editor, "x");
        } else {
            passed = press(editor, GDK_KEY_Escape);
        }
        passed = wait_for([&] { return mock.replies > previous_replies; }) &&
                 passed && !editor.sentence_request && editor.suggestion != " dim.";
        gtk_text_buffer_get_end_iter(editor.buffer, &end);
        gtk_text_buffer_place_cursor(editor.buffer, &end);
    }

    editor.cancel_sentence();
    soup_server_disconnect(server);
    g_object_unref(server);
    return passed;
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
    const bool passed = check_editor(*editor) && check_sentences(*editor);
    if (!passed)
        g_printerr("Editor completion interaction failed\n");
    gtk_widget_destroy(GTK_WIDGET(window));
    g_object_unref(app);
    return passed ? 0 : 1;
}
