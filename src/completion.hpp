#pragma once

#include <map>
#include <string>
#include <string_view>

class WordCompleter {
public:
    explicit WordCompleter(std::string_view frequencies);
    std::string suffix(std::string_view prefix, std::string_view before,
                       std::string_view after) const;

private:
    std::map<std::string, unsigned> frequencies_;
};
