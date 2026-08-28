// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dosbase::devtools {

// The DOS 4 message toolchain: BUILDIDX, NOSRVBLD, and BUILDMSG,
// measured against the six invocations of the captured reference build.
// One database feeds every tool: USA-MS.MSG holds a level line (4 hex
// digits), then per-component sections. A section header is
// `NAME     oooo cccc` -- the name padded to eight columns, the byte offset
// of the header line in lowercase hex (four decimal digits could not span
// the 54K file), and the message count in zero-padded DECIMAL. A message
// line is `nnnn U ffff <expression>`; continuation lines start with a tab,
// which every consumer preserves verbatim in its output. The skeleton
// directives (`:use`, `:def`) resolve text exclusively through this
// database -- inline text in a `:def` is documentation, proved by MSBIO
// 12/16/21 where skeleton and database disagree and the reference outputs
// carry the database's words.

struct MsgMessage {
    int number = 0;
    char usage = 'U';
    std::string flags;               // the third field, verbatim
    std::vector<std::string> lines;  // expression, then continuations with their tab
};

struct MsgSection {
    std::string name;
    std::vector<MsgMessage> messages;
};

struct MsgDatabase {
    std::string level;  // 4 lowercase hex digits
    std::vector<MsgSection> sections;

    [[nodiscard]] const MsgMessage* find(std::string_view component, int number) const;
};

// Parses the database; accepts LF or CRLF line endings (the repair tree
// stores the file with bare LF; the seed reads either). Throws FormatError
// naming the first line it cannot classify.
[[nodiscard]] MsgDatabase parse_msg(std::string_view raw);

// BUILDIDX rewrites the database -- CRLF endings, the level incremented by
// one (hex), every section header recomputed with its offset in the
// rewritten file -- and emits the index: the level line plus the section
// header lines, verbatim.
struct BuildIdxResult {
    std::string message_file;
    std::string index_file;
};
[[nodiscard]] BuildIdxResult buildidx(std::string_view msg_raw);

// NOSRVBLD renders flat `LABEL DB    <expression>` include files, one per
// `:class` in skeleton order, named `<base>.CL<class>`. `:use n COMP LABEL`
// resolves COMP n; `:def n LABEL ...` resolves <base> n and ignores the
// rest of the line. Returns (class id, file bytes) in declaration order.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> nosrvbld(
    std::string_view skl_raw, std::string_view base_name, const MsgDatabase& msg);

// BUILDMSG renders the message-server include per class plus the `.CTL`
// count file. Classes 1 and 2 are the transient pair: PUBLIC
// `$M_MSGSERV_<n>`, class byte 001H/002H, an implicit 0FFFFH sentinel
// ("Extended Error %1" / "Parse Error %1"), and a lowercase `Endp`. Every
// other class is resident: PUBLIC `$M_CLS_<i>` numbered in declaration
// order across the resident classes, class byte 0FFH, uppercase `ENDP`, no
// implicit sentinel. A three-argument `:use id COMPn` appends `,CR,LF`
// when the resolved text does not already end in CR,LF; `:use -1 COMPn`
// renders as an ordinary entry with id 0FFFFH whose failed lookup falls
// back to the class's builtin text (and gains `,CR,LF` by the same rule).
// `$M_NUM_CLS` in the `.CTL` is the resident-class count.
struct BuildMsgResult {
    std::string util;
    std::vector<std::pair<std::string, std::string>> files;  // ("CL1".."CTL", bytes)
};
[[nodiscard]] BuildMsgResult buildmsg(std::string_view skl_raw, const MsgDatabase& msg);

}  // namespace dosbase::devtools
