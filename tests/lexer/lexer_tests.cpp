#include "diagnostics/diagnostics.hpp"
#include "lexer/lexer.hpp"
#include "test_harness.hpp"

#include <string>
#include <vector>

namespace {

std::vector<yukt::TokenKind> kinds_of(const std::vector<yukt::Token>& tokens) {
    std::vector<yukt::TokenKind> kinds;
    kinds.reserve(tokens.size());
    for (const auto& token : tokens) {
        kinds.push_back(token.kind);
    }
    return kinds;
}

struct Lexed {
    std::vector<yukt::Token> tokens;
    yukt::DiagnosticEngine diagnostics;
};

Lexed lex(const std::string& source, const std::string& name = "<test>") {
    yukt::DiagnosticEngine diagnostics(name, source);
    yukt::Lexer lexer(source, diagnostics);
    auto tokens = lexer.tokenize();
    return Lexed{std::move(tokens), std::move(diagnostics)};
}

bool contains_kind(const std::vector<yukt::Token>& tokens, yukt::TokenKind kind) {
    for (const auto& token : tokens) {
        if (token.kind == kind) {
            return true;
        }
    }
    return false;
}

const yukt::Token* first_kind(const std::vector<yukt::Token>& tokens, yukt::TokenKind kind) {
    for (const auto& token : tokens) {
        if (token.kind == kind) {
            return &token;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    {
        const auto result = lex("create say if otherwise repeat in give true false use share as record name\n");
        REQUIRE(!result.diagnostics.has_errors());
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwCreate));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwSay));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwIf));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwOtherwise));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwRepeat));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwIn));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwGive));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwTrue));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwFalse));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwUse));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwShare));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwAs));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::KwRecord));
        const auto* ident = first_kind(result.tokens, yukt::TokenKind::Identifier);
        REQUIRE(ident != nullptr);
        REQUIRE(ident != nullptr && ident->lexeme == "name");
    }

    {
        const auto result = lex("42 3.14 1..5\n");
        REQUIRE(!result.diagnostics.has_errors());
        REQUIRE(result.tokens.size() >= 5);
        REQUIRE(result.tokens[0].kind == yukt::TokenKind::Integer);
        REQUIRE(result.tokens[0].lexeme == "42");
        REQUIRE(result.tokens[1].kind == yukt::TokenKind::Float);
        REQUIRE(result.tokens[1].lexeme == "3.14");
        REQUIRE(result.tokens[2].kind == yukt::TokenKind::Integer);
        REQUIRE(result.tokens[2].lexeme == "1");
        REQUIRE(result.tokens[3].kind == yukt::TokenKind::DotDot);
        REQUIRE(result.tokens[4].kind == yukt::TokenKind::Integer);
        REQUIRE(result.tokens[4].lexeme == "5");
    }

    {
        const auto result = lex("say \"Hello,\\nWorld\"\n");
        REQUIRE(!result.diagnostics.has_errors());
        const auto* string = first_kind(result.tokens, yukt::TokenKind::String);
        REQUIRE(string != nullptr);
        REQUIRE(string != nullptr && string->lexeme == "Hello,\nWorld");
    }

    {
        const auto result = lex("a + b - c * d / e % f == g != h < i <= j > k >= l\n");
        REQUIRE(!result.diagnostics.has_errors());
        const auto kinds = kinds_of(result.tokens);
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Plus));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Minus));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Star));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Slash));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Percent));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::EqualEqual));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::BangEqual));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Less));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::LessEqual));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Greater));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::GreaterEqual));
        (void)kinds;
    }

    {
        const auto result = lex("say 1 # this is ignored\nsay 2\n");
        REQUIRE(!result.diagnostics.has_errors());
        int say_count = 0;
        for (const auto& token : result.tokens) {
            if (token.kind == yukt::TokenKind::KwSay) {
                ++say_count;
            }
            REQUIRE(token.lexeme.find("ignored") == std::string::npos);
        }
        REQUIRE(say_count == 2);
    }

    {
        const auto result = lex("create main:\n    say 1\n");
        REQUIRE(!result.diagnostics.has_errors());
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Indent));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Dedent));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Newline));
    }

    {
        const auto result = lex(
            "create main:\n"
            "    repeat i in 1..2:\n"
            "        repeat j in 1..2:\n"
            "            say i\n"
            "    say 0\n");
        REQUIRE(!result.diagnostics.has_errors());
        int indents = 0;
        int dedents = 0;
        for (const auto& token : result.tokens) {
            if (token.kind == yukt::TokenKind::Indent) {
                ++indents;
            }
            if (token.kind == yukt::TokenKind::Dedent) {
                ++dedents;
            }
        }
        REQUIRE(indents == 3);
        REQUIRE(dedents == 3);
    }

    {
        const auto result = lex(
            "create main:\n"
            "    say 1\n"
            "  say 2\n");
        REQUIRE(result.diagnostics.has_errors());
        REQUIRE(!result.diagnostics.diagnostics().empty());
        REQUIRE(result.diagnostics.diagnostics().front().title == "Inconsistent indentation");
    }

    {
        const auto result = lex("say \"unterminated\n");
        REQUIRE(result.diagnostics.has_errors());
        REQUIRE(result.diagnostics.diagnostics().front().title == "Unterminated string");
    }

    {
        const auto result = lex("create main:\n\tsay 1\n");
        REQUIRE(result.diagnostics.has_errors());
        REQUIRE(result.diagnostics.diagnostics().front().title == "Invalid indentation");
    }

    {
        const auto result = lex("@\n");
        REQUIRE(result.diagnostics.has_errors());
        REQUIRE(result.diagnostics.diagnostics().front().title == "Unexpected character");
    }

    {
        const auto result = lex("value = 10\n");
        REQUIRE(!result.diagnostics.has_errors());
        REQUIRE(result.tokens[0].location.line == 1);
        REQUIRE(result.tokens[0].location.column == 1);
        const auto* equal = first_kind(result.tokens, yukt::TokenKind::Equal);
        REQUIRE(equal != nullptr);
        REQUIRE(equal != nullptr && equal->location.column == 7);
    }

    {
        const auto result = lex("math.add(1)\n");
        REQUIRE(!result.diagnostics.has_errors());
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Dot));
        REQUIRE(contains_kind(result.tokens, yukt::TokenKind::Dot) &&
                first_kind(result.tokens, yukt::TokenKind::Dot)->lexeme == ".");
    }

    return yukt::test::summary("lexer");
}
