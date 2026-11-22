#pragma once
// The Vela lexer. Tokens carry line and column (columns count code points).
#include "vela/common.hpp"

#include <string>
#include <unordered_map>
#include <vector>

enum class Tok {
    Eof, Ident, IntLit, StrLit,
    KwFn, KwLet, KwMut, KwIf, KwElse, KwWhile, KwReturn,
    KwTrue, KwFalse, KwInt, KwBool, KwStr, KwVoid,
    Plus, Minus, Star, Slash, Percent,
    EqEq, NotEq, Lt, Le, Gt, Ge,
    AndAnd, OrOr, Not, Assign, Arrow, Colon, Comma, Semi,
    LParen, RParen, LBrace, RBrace
};

// Human-readable token name, for error messages.
std::string tok_desc(Tok t);

struct Token {
    Tok t = Tok::Eof;
    std::string text;
    long long num = 0;
    Loc loc;
};

class Lexer {
public:
    explicit Lexer(std::string src) : src(std::move(src)) {}

    // Tokenizes the whole source. The vector always ends with an Eof token.
    std::vector<Token> run();

private:
    std::string src;
    size_t i = 0;
    Loc loc{1, 1};
    Tok prev = Tok::Eof;   // distinguishes unary minus from subtraction

    static bool is_ident_start(char c);
    static bool is_ident_char(char c);

    int peek(int off = 0) const;
    void adv();

    static bool unary_ctx(Tok p);
    Token lex_number(Loc l, bool negative);
    Token lex_string(Loc l);
    Token next();

    static const std::unordered_map<std::string, Tok>& keywords();
};
