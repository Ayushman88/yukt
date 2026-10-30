#pragma once

#include "diagnostics/diagnostics.hpp"
#include "lexer/token.hpp"

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace yukt {

class Lexer {
public:
    Lexer(std::string_view source, DiagnosticEngine& diagnostics);

    std::vector<Token> tokenize();

private:
    Token next_token();
    Token handle_line_start();
    Token finish();
    Token scan_token();
    Token scan_identifier_or_keyword();
    Token scan_number();
    Token scan_string();
    Token make_token(TokenKind kind, std::string lexeme, SourceLocation location) const;

    void skip_line_comment();
    bool skip_blank_or_comment_line();
    int measure_indent(SourceLocation& tab_location, bool& saw_tab);
    void emit_dedents_to(int indent);

    char peek(std::size_t offset = 0) const;
    char advance();
    bool match(char expected);
    bool is_at_end() const;

    std::string_view source_;
    DiagnosticEngine& diagnostics_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
    bool at_line_start_ = true;
    bool emitted_newline_ = true;
    bool finished_ = false;
    std::vector<int> indents_{0};
    std::deque<Token> pending_;
};

}  // namespace yukt
