#include "lexer/lexer.hpp"

#include <cctype>
#include <utility>

namespace yukt {
namespace {

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_identifier_continue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_digit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

TokenKind keyword_kind(std::string_view text) {
    if (text == "create") {
        return TokenKind::KwCreate;
    }
    if (text == "say") {
        return TokenKind::KwSay;
    }
    if (text == "if") {
        return TokenKind::KwIf;
    }
    if (text == "otherwise") {
        return TokenKind::KwOtherwise;
    }
    if (text == "repeat") {
        return TokenKind::KwRepeat;
    }
    if (text == "in") {
        return TokenKind::KwIn;
    }
    if (text == "give") {
        return TokenKind::KwGive;
    }
    if (text == "true") {
        return TokenKind::KwTrue;
    }
    if (text == "false") {
        return TokenKind::KwFalse;
    }
    return TokenKind::Identifier;
}

}  // namespace

Lexer::Lexer(std::string_view source, DiagnosticEngine& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token token = next_token();
        const bool done = token.kind == TokenKind::EndOfFile;
        tokens.push_back(std::move(token));
        if (done) {
            break;
        }
    }
    return tokens;
}

Token Lexer::next_token() {
    Token token;
    if (!pending_.empty()) {
        token = std::move(pending_.front());
        pending_.pop_front();
    } else if (finished_) {
        token = make_token(TokenKind::EndOfFile, "", SourceLocation{line_, column_});
    } else if (at_line_start_) {
        token = handle_line_start();
    } else {
        token = scan_token();
    }

    if (token.kind != TokenKind::Newline && token.kind != TokenKind::EndOfFile &&
        token.kind != TokenKind::Indent && token.kind != TokenKind::Dedent) {
        emitted_newline_ = false;
    }
    return token;
}

Token Lexer::handle_line_start() {
    while (true) {
        if (is_at_end()) {
            return finish();
        }

        if (skip_blank_or_comment_line()) {
            continue;
        }

        SourceLocation tab_location{line_, column_};
        bool saw_tab = false;
        const int indent = measure_indent(tab_location, saw_tab);
        if (saw_tab) {
            diagnostics_.error(
                tab_location, "Invalid indentation",
                "Tabs are not allowed in YUKT source files.",
                "Replace tabs with spaces. Indentation must use spaces only.");
        }

        const int current = indents_.back();
        at_line_start_ = false;

        if (indent > current) {
            indents_.push_back(indent);
            return make_token(TokenKind::Indent, "", SourceLocation{line_, 1});
        }

        if (indent < current) {
            emit_dedents_to(indent);
            if (pending_.empty()) {
                return scan_token();
            }
            Token token = std::move(pending_.front());
            pending_.pop_front();
            return token;
        }

        return scan_token();
    }
}

Token Lexer::finish() {
    finished_ = true;
    const SourceLocation location{line_, column_};

    if (!emitted_newline_) {
        pending_.push_back(make_token(TokenKind::Newline, "\\n", location));
        emitted_newline_ = true;
    }

    while (indents_.size() > 1) {
        indents_.pop_back();
        pending_.push_back(make_token(TokenKind::Dedent, "", location));
    }

    pending_.push_back(make_token(TokenKind::EndOfFile, "", location));
    Token token = std::move(pending_.front());
    pending_.pop_front();
    return token;
}

Token Lexer::scan_token() {
    while (!is_at_end()) {
        const char c = peek();
        if (c == ' ') {
            advance();
            continue;
        }
        if (c == '\t') {
            diagnostics_.error(
                SourceLocation{line_, column_}, "Invalid character",
                "Tabs are not allowed in YUKT source files.",
                "Replace the tab with spaces.");
            advance();
            continue;
        }
        break;
    }

    if (is_at_end()) {
        return finish();
    }

    if (peek() == '#') {
        skip_line_comment();
        if (is_at_end()) {
            return finish();
        }
        if (peek() == '\n' || peek() == '\r') {
            return scan_token();
        }
    }

    const SourceLocation start{line_, column_};
    const char c = peek();

    if (c == '\r' || c == '\n') {
        if (c == '\r') {
            advance();
            if (peek() == '\n') {
                advance();
            }
        } else {
            advance();
        }
        at_line_start_ = true;
        emitted_newline_ = true;
        return make_token(TokenKind::Newline, "\\n", start);
    }

    if (is_identifier_start(c)) {
        return scan_identifier_or_keyword();
    }
    if (is_digit(c)) {
        return scan_number();
    }
    if (c == '"') {
        return scan_string();
    }

    advance();
    switch (c) {
        case '+':
            return make_token(TokenKind::Plus, "+", start);
        case '-':
            return make_token(TokenKind::Minus, "-", start);
        case '*':
            return make_token(TokenKind::Star, "*", start);
        case '/':
            return make_token(TokenKind::Slash, "/", start);
        case '%':
            return make_token(TokenKind::Percent, "%", start);
        case '(':
            return make_token(TokenKind::LParen, "(", start);
        case ')':
            return make_token(TokenKind::RParen, ")", start);
        case '[':
            return make_token(TokenKind::LBracket, "[", start);
        case ']':
            return make_token(TokenKind::RBracket, "]", start);
        case ',':
            return make_token(TokenKind::Comma, ",", start);
        case ':':
            return make_token(TokenKind::Colon, ":", start);
        case '=':
            if (match('=')) {
                return make_token(TokenKind::EqualEqual, "==", start);
            }
            return make_token(TokenKind::Equal, "=", start);
        case '!':
            if (match('=')) {
                return make_token(TokenKind::BangEqual, "!=", start);
            }
            diagnostics_.error(
                start, "Unexpected character",
                "Found `!`, which is not a valid YUKT token by itself.",
                "Use `!=` for inequality.");
            return scan_token();
        case '<':
            if (match('=')) {
                return make_token(TokenKind::LessEqual, "<=", start);
            }
            return make_token(TokenKind::Less, "<", start);
        case '>':
            if (match('=')) {
                return make_token(TokenKind::GreaterEqual, ">=", start);
            }
            return make_token(TokenKind::Greater, ">", start);
        case '.':
            if (match('.')) {
                return make_token(TokenKind::DotDot, "..", start);
            }
            diagnostics_.error(
                start, "Unexpected character",
                "Found `.`, which is not valid on its own.",
                "Use `..` to write a range, for example `1..10`.");
            return scan_token();
        default:
            diagnostics_.error(
                start, "Unexpected character",
                "Found `" + std::string(1, c) + "`, which is not valid in YUKT.",
                "Remove this character or check for a typo.");
            return scan_token();
    }
}

Token Lexer::scan_identifier_or_keyword() {
    const SourceLocation start{line_, column_};
    const std::size_t begin = pos_;
    advance();
    while (!is_at_end() && is_identifier_continue(peek())) {
        advance();
    }
    const auto text = std::string(source_.substr(begin, pos_ - begin));
    return make_token(keyword_kind(text), text, start);
}

Token Lexer::scan_number() {
    const SourceLocation start{line_, column_};
    const std::size_t begin = pos_;

    while (!is_at_end() && is_digit(peek())) {
        advance();
    }

    bool is_float = false;
    if (peek() == '.' && peek(1) != '.' && is_digit(peek(1))) {
        is_float = true;
        advance();
        while (!is_at_end() && is_digit(peek())) {
            advance();
        }
    }

    return make_token(is_float ? TokenKind::Float : TokenKind::Integer,
                      std::string(source_.substr(begin, pos_ - begin)), start);
}

Token Lexer::scan_string() {
    const SourceLocation start{line_, column_};
    advance();  // opening quote

    std::string value;
    while (!is_at_end()) {
        const char c = peek();
        if (c == '"') {
            advance();
            return make_token(TokenKind::String, std::move(value), start);
        }
        if (c == '\n' || c == '\r') {
            diagnostics_.error(
                start, "Unterminated string",
                "This string reaches a newline before its closing quote.",
                "Close the string with `\"` on the same line.");
            return make_token(TokenKind::String, std::move(value), start);
        }
        if (c == '\\') {
            advance();
            if (is_at_end()) {
                break;
            }
            const char escaped = advance();
            switch (escaped) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case '"':
                    value.push_back('"');
                    break;
                default:
                    diagnostics_.error(
                        SourceLocation{line_, column_ > 1 ? column_ - 1 : 1}, "Invalid escape sequence",
                        "Unknown string escape `\\" + std::string(1, escaped) + "`.",
                        "Supported escapes are `\\\\`, `\\\"`, `\\n`, and `\\t`.");
                    value.push_back(escaped);
                    break;
            }
            continue;
        }
        value.push_back(advance());
    }

    diagnostics_.error(
        start, "Unterminated string",
        "This string never reaches a closing quote.",
        "Close the string with `\"`.");
    return make_token(TokenKind::String, std::move(value), start);
}

Token Lexer::make_token(TokenKind kind, std::string lexeme, SourceLocation location) const {
    Token token;
    token.kind = kind;
    token.lexeme = std::move(lexeme);
    token.location = location;
    return token;
}

void Lexer::skip_line_comment() {
    while (!is_at_end() && peek() != '\n' && peek() != '\r') {
        advance();
    }
}

bool Lexer::skip_blank_or_comment_line() {
    std::size_t look = pos_;
    while (look < source_.size() && source_[look] == ' ') {
        ++look;
    }
    if (look < source_.size() && source_[look] == '\t') {
        return false;
    }
    if (look >= source_.size()) {
        while (pos_ < source_.size()) {
            advance();
        }
        return false;
    }

    const char c = source_[look];
    if (c == '#' || c == '\n' || c == '\r') {
        while (!is_at_end() && peek() != '\n' && peek() != '\r') {
            advance();
        }
        if (peek() == '\r') {
            advance();
            if (peek() == '\n') {
                advance();
            }
        } else if (peek() == '\n') {
            advance();
        }
        at_line_start_ = true;
        return true;
    }
    return false;
}

int Lexer::measure_indent(SourceLocation& tab_location, bool& saw_tab) {
    int indent = 0;
    while (!is_at_end()) {
        const char c = peek();
        if (c == ' ') {
            advance();
            ++indent;
            continue;
        }
        if (c == '\t') {
            if (!saw_tab) {
                tab_location = SourceLocation{line_, column_};
                saw_tab = true;
            }
            advance();
            indent += 4;
            continue;
        }
        break;
    }
    return indent;
}

void Lexer::emit_dedents_to(int indent) {
    while (indents_.size() > 1 && indents_.back() > indent) {
        indents_.pop_back();
        pending_.push_back(make_token(TokenKind::Dedent, "", SourceLocation{line_, 1}));
    }

    if (indents_.back() != indent) {
        diagnostics_.error(
            SourceLocation{line_, 1}, "Inconsistent indentation",
            "This line is indented " + std::to_string(indent) +
                " spaces, which does not match any open block.",
            "Indent to " + std::to_string(indents_.back()) +
                " spaces, or align with a previous indentation level.");
        indents_.back() = indent;
    }
}

char Lexer::peek(std::size_t offset) const {
    if (pos_ + offset >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + offset];
}

char Lexer::advance() {
    const char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() != expected) {
        return false;
    }
    advance();
    return true;
}

bool Lexer::is_at_end() const {
    return pos_ >= source_.size();
}

}  // namespace yukt
