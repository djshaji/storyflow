#pragma once

#include <gio/gio.h>
#include <libsoup/soup.h>

#include <functional>
#include <string>

class SentenceClient {
public:
    using Callback = std::function<void(std::string, std::string)>;

    SentenceClient();
    ~SentenceClient();
    SentenceClient(const SentenceClient&) = delete;
    SentenceClient& operator=(const SentenceClient&) = delete;

    void request(unsigned port, const std::string& prompt, GCancellable* cancellable,
                 Callback callback);

private:
    SoupSession* session_;
};
