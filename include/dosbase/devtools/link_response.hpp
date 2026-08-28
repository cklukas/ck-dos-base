// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace dosbase::devtools {

// LINK 3.x/5.x's response-file grammar: the `.LNK` files the
// tree's own makefiles pass as `link @foo.lnk`. Four positional fields --
// object files, the run (.EXE) file, the list (.MAP) file, and libraries --
// separated by `,` or by a bare newline (a newline only ends a field when
// the line does not continue it), with `/switch` tokens free to appear
// anywhere and `;` ending the response early, taking every remaining field
// as empty. The object and library fields are themselves lists, whose
// items may be joined by `+`, by whitespace, or both at once -- COMMAND.LNK
// uses space-separated names on some lines and `+`-continued line breaks
// between them, SYS.LNK joins three names with bare `+` on one line, and
// both forms coexist in the same real files, not just in the spec.
struct LinkResponse {
    std::vector<std::string> objects;    // as written, no extension implied
    std::string exe_name;                // empty if the field was omitted
    std::string map_name;                // empty if the field was omitted
    std::vector<std::string> libraries;  // as written; no fixture in the
                                          // tree exercises a non-empty one
    std::vector<std::string> switches;   // `/word` tokens, in the order seen
};

[[nodiscard]] LinkResponse parse_link_response(std::string_view text);

// LINK's own default when the exe field is empty: the first object's name
// with a `.EXE` extension. Verified against a real capture -- `link
// @tree.lnk` with an empty exe field produced `TREE.EXE`. Returns the
// response's own exe_name unchanged when it is not empty.
[[nodiscard]] std::string default_exe_name(const LinkResponse& response);

}  // namespace dosbase::devtools
