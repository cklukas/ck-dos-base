// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/dos_file.hpp"

#include "dosbase/devtools/binary.hpp"

#include <system_error>

namespace dosbase::devtools {
namespace fs = std::filesystem;
namespace {

fs::path dos_to_host(std::string_view name) {
    std::string text(name);
    for (char& character : text) {
        if (character == '\\') character = fs::path::preferred_separator;
    }
    return fs::path(text);
}

// Case-insensitive lookup of one path component inside `directory`. The
// directory is scanned even when the requested spelling already opens, so a
// case-insensitive host resolves to the same name a case-sensitive one does.
fs::path find_component(const fs::path& directory, const std::string& component) {
    const fs::path direct = directory / component;
    std::error_code error;
    const std::string wanted = dos_upper(component);
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        if (dos_upper(entry.path().filename().string()) == wanted) return entry.path();
    }
    return direct;
}

}  // namespace

std::string dos_upper(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return result;
}

fs::path resolve_dos_input(std::string_view name) {
    const fs::path requested = dos_to_host(name);
    fs::path resolved = requested.has_root_path() ? requested.root_path() : fs::path(".");
    for (const fs::path& part : requested.relative_path()) {
        if (part == "." || part == "..") {
            resolved /= part;
            continue;
        }
        resolved = find_component(resolved, part.string());
    }
    return resolved;
}

fs::path resolve_dos_output(std::string_view name) {
    const fs::path requested = dos_to_host(name);
    const fs::path parent = requested.has_parent_path()
                                ? resolve_dos_input(requested.parent_path().string())
                                : fs::path(".");
    return parent / dos_upper(requested.filename().string());
}

}  // namespace dosbase::devtools
