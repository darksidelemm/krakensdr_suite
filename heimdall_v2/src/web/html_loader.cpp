#include "html_loader.hpp"
#include "../core/config.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <string_view>

static std::string index_html_content;

bool load_html_file(std::string& content) {
    try {
        if (!std::filesystem::exists("index.html")) return false;
        
        std::ifstream file("index.html");
        content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        
        // JSON array of the runtime serial list (--serials aware) for the UI's
        // channel labels.
        std::string serials_json = "[";
        for (size_t i = 0; i < expected_serials.size(); ++i) {
            if (i) serials_json += ',';
            serials_json += '"' + expected_serials[i] + '"';
        }
        serials_json += "]";

        // Modern replacement using std::format pattern
        const std::map<std::string_view, std::string> replacements = {
            {"{{NUM_DEVICES}}", std::to_string(NUM_DEVICES)},
            {"{{CORRELATION_SIZE}}", std::to_string(CORRELATION_SIZE)},
            {"{{REF_CHANNEL}}", std::to_string(REF_CHANNEL)},
            {"{{SERIALS}}", serials_json}
        };
        
        // Single-pass replacement
        for (const auto& [from, to] : replacements) {
            for (size_t pos = 0; (pos = content.find(from, pos)) != std::string::npos; pos += to.length()) {
                content.replace(pos, from.length(), to);
            }
        }
        
        index_html_content = content;
        std::cout << "Loaded index.html (" << content.size() << " bytes)" << std::endl;
        return true;
    } catch (...) {
        return false;
    }
}

std::string& get_html_content() {
    return index_html_content;
}
