#include "sentence.hpp"

#include <json-glib/json-glib.h>

#include <memory>
#include <string_view>
#include <utility>

namespace {
struct Request {
    SoupMessage* message;
    SentenceClient::Callback callback;

    ~Request() { g_object_unref(message); }
};

std::string first_sentence(std::string_view text) {
    if (text.empty() || !g_utf8_validate(text.data(), text.size(), nullptr))
        return {};
    text = text.substr(0, text.find_first_of("\r\n"));
    for (auto position = text.find_first_of(".!?"); position != text.npos;
         position = text.find_first_of(".!?", position + 1)) {
        auto end = text.find_first_not_of(".!?", position);
        if (end == text.npos)
            return std::string(text);
        while (end < text.size()) {
            const auto character = g_utf8_get_char(text.data() + end);
            if (character != '"' && character != '\'' && character != ')' &&
                character != 0x2019 && character != 0x201d)
                break;
            end = g_utf8_next_char(text.data() + end) - text.data();
        }
        if (end == text.size() || g_unichar_isspace(g_utf8_get_char(text.data() + end)))
            return std::string(text.substr(0, end));
    }
    return {};
}

void finished(GObject* source, GAsyncResult* result, gpointer data) {
    std::unique_ptr<Request> request(static_cast<Request*>(data));
    GError* error = nullptr;
    auto* bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    if (!bytes) {
        const bool cancelled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        g_clear_error(&error);
        if (!cancelled)
            request->callback({}, "Could not reach the local llama.cpp server");
        return;
    }
    if (soup_message_get_status(request->message) != SOUP_STATUS_OK) {
        const auto status = soup_message_get_status(request->message);
        g_bytes_unref(bytes);
        request->callback({}, "Local server returned HTTP " + std::to_string(status));
        return;
    }

    gsize size = 0;
    const auto* body = static_cast<const char*>(g_bytes_get_data(bytes, &size));
    auto* parser = json_parser_new();
    std::string sentence;
    bool valid = size > 0 && size <= 65536 &&
                 json_parser_load_from_data(parser, body, size, nullptr);
    if (valid) {
        auto* root = json_parser_get_root(parser);
        auto* object = JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : nullptr;
        auto* content = object ? json_object_get_member(object, "content") : nullptr;
        valid = content && JSON_NODE_HOLDS_VALUE(content) &&
                json_node_get_value_type(content) == G_TYPE_STRING;
        if (valid)
            sentence = first_sentence(json_node_get_string(content));
    }
    g_object_unref(parser);
    g_bytes_unref(bytes);
    if (!valid)
        request->callback({}, "Invalid reply from the local server");
    else if (sentence.empty())
        request->callback({}, "No complete sentence returned");
    else
        request->callback(std::move(sentence), {});
}
}

SentenceClient::SentenceClient() : session_(soup_session_new()) {
    auto* resolver = g_simple_proxy_resolver_new(nullptr, nullptr);
    soup_session_set_proxy_resolver(session_, resolver);
    soup_session_set_timeout(session_, 20);
    g_object_unref(resolver);
}

SentenceClient::~SentenceClient() {
    soup_session_abort(session_);
    g_object_unref(session_);
}

void SentenceClient::request(unsigned port, const std::string& prompt, GCancellable* cancellable,
                             Callback callback) {
    const auto url = "http://127.0.0.1:" + std::to_string(port) + "/completion";
    auto* message = soup_message_new("POST", url.c_str());
    soup_message_add_flags(message, SOUP_MESSAGE_NO_REDIRECT);

    auto* json = json_object_new();
    json_object_set_string_member(json, "prompt", prompt.c_str());
    json_object_set_int_member(json, "n_predict", 64);
    json_object_set_boolean_member(json, "stream", FALSE);
    auto* stop = json_array_new();
    json_array_add_string_element(stop, "\n");
    json_object_set_array_member(json, "stop", stop);
    auto* root = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(root, json);
    auto* serialized = json_to_string(root, FALSE);
    auto* body = g_bytes_new_take(serialized, std::char_traits<char>::length(serialized));
    json_node_free(root);
    soup_message_set_request_body_from_bytes(message, "application/json", body);
    g_bytes_unref(body);

    auto* request = new Request{message, std::move(callback)};
    soup_session_send_and_read_async(session_, message, G_PRIORITY_DEFAULT, cancellable,
                                     finished, request);
}
