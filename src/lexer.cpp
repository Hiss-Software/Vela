#include "vela/lexer.hpp"

#include <cctype>
#include <climits>
#include <cstdlib>

std::vector<Token> Lexer::run() {
    std::vector<Token> out;
    while (true) {
        Token t = next();
        out.push_back(t);
        if (t.t == Tok::Eof) break;
    }
    return out;
}

bool Lexer::is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string tok_desc(Tok t) {
    switch (t) {
        case Tok::Eof:     return "end of file";
        case Tok::Ident:   return "identifier";
        case Tok::IntLit:  return "number";
        case Tok::StrLit:  return "string";
        case Tok::KwFn:    return "`fn`";
        case Tok::KwLet:   return "`let`";
        case Tok::KwMut:   return "`mut`";
        case Tok::KwIf:    return "`if`";
        case Tok::KwElse:  return "`else`";
        case Tok::KwWhile: return "`while`";
        case Tok::KwReturn:return "`return`";
        case Tok::KwTrue:  case Tok::KwFalse: return "`true`/`false`";
        case Tok::KwInt:   return "`int`";
        case Tok::KwBool:  return "`bool`";
        case Tok::KwStr:   return "`string`";
        case Tok::KwVoid:  return "`void`";
        case Tok::Plus:    return "`+`";
        case Tok::Minus:   return "`-`";
        case Tok::Star:    return "`*`";
        case Tok::Slash:   return "`/`";
        case Tok::Percent: return "`%`";
        case Tok::EqEq:    return "`==`";
        case Tok::NotEq:   return "`!=`";
        case Tok::Lt:      return "`<`";
        case Tok::Le:      return "`<=`";
        case Tok::Gt:      return "`>`";
        case Tok::Ge:      return "`>=`";
        case Tok::AndAnd:  return "`&&`";
        case Tok::OrOr:    return "`||`";
        case Tok::Not:     return "`!`";
        case Tok::Assign:  return "`=`";
        case Tok::Arrow:   return "`->`";
        case Tok::Colon:   return "`:`";
        case Tok::Comma:   return "`,`";
        case Tok::Semi:    return "`;`";
        case Tok::LParen:  return "`(`";
        case Tok::RParen:  return "`)`";
        case Tok::LBrace:  return "`{`";
        case Tok::RBrace:  return "`}`";
    }
    return "?";
}

int Lexer::peek(int off) const {
    return i + off < src.size()
               ? static_cast<int>(static_cast<unsigned char>(src[i + off]))
               : -1;
}

void Lexer::adv() {
    if (i >= src.size()) return;
    if (src[i] == '\n') { loc.line++; loc.col = 1; }
    else {
        unsigned char c = static_cast<unsigned char>(src[i]);
        if ((c & 0xC0) != 0x80) loc.col++;   // columns in code points
    }
    i++;
}

// minus in unary position? then "-123" is a single literal
bool Lexer::unary_ctx(Tok p) {
    switch (p) {
        case Tok::Ident: case Tok::IntLit: case Tok::StrLit:
        case Tok::KwTrue: case Tok::KwFalse:
        case Tok::RParen: case Tok::RBrace:
            return false;
        default:
            return true;
    }
}

Token Lexer::lex_number(Loc l, bool negative) {
    std::string digs;
    while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) {
        digs += src[i];
        adv();
    }
    size_t nz = digs.find_first_not_of('0');
    std::string d = (nz == std::string::npos) ? "0" : digs.substr(nz);
    const std::string max_pos = "9223372036854775807";
    const std::string max_neg = "9223372036854775808";
    const std::string& bound = negative ? max_neg : max_pos;
    if (d.size() > bound.size() ||
        (d.size() == bound.size() && d > bound)) {
        fail("E0003", l,
             "integer literal `" + digs + "` does not fit in int",
             "int in Vela is a signed 64-bit number: "
             "from -9223372036854775808 to 9223372036854775807. "
             "Overflow is a bug, and I do not forgive bugs.");
    }
    long long v;
    if (negative && d == max_neg) v = LLONG_MIN;
    else if (negative) v = -strtoll(digs.c_str(), nullptr, 10);
    else v = strtoll(digs.c_str(), nullptr, 10);
    prev = Tok::IntLit;
    return Token{Tok::IntLit, digs, v, l};
}

Token Lexer::lex_string(Loc l) {
    adv();  // opening quote
    std::string s;
    while (true) {
        if (i >= src.size() || src[i] == '\n')
            fail("E0002", l, "string literal is not closed",
                 "a string cannot span lines; use \\n");
        char c = src[i];
        if (c == '"') { adv(); break; }
        if (c == '\\') {
            adv();
            if (i >= src.size())
                fail("E0002", l, "string literal is not closed");
            char e = src[i];
            switch (e) {
                case 'n':  s += '\n'; break;
                case 't':  s += '\t'; break;
                case 'r':  s += '\r'; break;
                case '\\': s += '\\'; break;
                case '"':  s += '"';  break;
                default:
                    fail("E0004", loc,
                         std::string("unknown escape sequence `\\") +
                             e + "`",
                         "available: \\n  \\t  \\r  \\\"  \\\\");
            }
            adv();
            continue;
        }
        s += c;
        adv();
    }
    prev = Tok::StrLit;
    return Token{Tok::StrLit, s, 0, l};
}

Token Lexer::next() {
    // skip whitespace and // comments
    while (true) {
        if (i >= src.size()) break;
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { adv(); continue; }
        if (c == '/' && peek(1) == '/') {
            while (i < src.size() && src[i] != '\n') adv();
            continue;
        }
        break;
    }

    Loc l = loc;
    if (i >= src.size()) return Token{Tok::Eof, "", 0, l};

    char c = src[i];

    if (c == '-' && peek(1) >= '0' && peek(1) <= '9' && unary_ctx(prev)) {
        adv();  // minus
        return lex_number(l, true);
    }
    if (std::isdigit(static_cast<unsigned char>(c)))
        return lex_number(l, false);
    if (c == '"') return lex_string(l);
    if (is_ident_start(c)) {
        std::string s;
        while (i < src.size() && is_ident_char(src[i])) { s += src[i]; adv(); }
        auto it = keywords().find(s);
        Tok t = (it != keywords().end()) ? it->second : Tok::Ident;
        prev = t;
        return Token{t, s, 0, l};
    }

    auto two = [&](const char* s, Tok t) -> bool {
        if (i + 1 < src.size() && src[i] == s[0] && src[i + 1] == s[1]) {
            adv(); adv();
            prev = t;
            return true;
        }
        return false;
    };
    auto one = [&](Tok t) -> Token {
        adv();
        prev = t;
        return Token{t, std::string(1, c), 0, l};
    };

    if (two("->", Tok::Arrow)) return Token{Tok::Arrow, "->", 0, l};
    if (two("==", Tok::EqEq))  return Token{Tok::EqEq, "==", 0, l};
    if (two("!=", Tok::NotEq)) return Token{Tok::NotEq, "!=", 0, l};
    if (two("<=", Tok::Le))    return Token{Tok::Le, "<=", 0, l};
    if (two(">=", Tok::Ge))    return Token{Tok::Ge, ">=", 0, l};
    if (two("&&", Tok::AndAnd))return Token{Tok::AndAnd, "&&", 0, l};
    if (two("||", Tok::OrOr))  return Token{Tok::OrOr, "||", 0, l};

    switch (c) {
        case '+': return one(Tok::Plus);
        case '-': return one(Tok::Minus);
        case '*': return one(Tok::Star);
        case '/': return one(Tok::Slash);
        case '%': return one(Tok::Percent);
        case '<': return one(Tok::Lt);
        case '>': return one(Tok::Gt);
        case '!': return one(Tok::Not);
        case '=': return one(Tok::Assign);
        case ':': return one(Tok::Colon);
        case ',': return one(Tok::Comma);
        case ';': return one(Tok::Semi);
        case '(': return one(Tok::LParen);
        case ')': return one(Tok::RParen);
        case '{': return one(Tok::LBrace);
        case '}': return one(Tok::RBrace);
        default: break;
    }

    std::string shown(1, c);
    fail("E0001", l, "unknown character `" + shown + "`",
         "Vela does not know this character. Check your keyboard layout "
         "and syntax.");
}

const std::unordered_map<std::string, Tok>& Lexer::keywords() {
    static const std::unordered_map<std::string, Tok> kw = {
        {"fn", Tok::KwFn}, {"let", Tok::KwLet}, {"mut", Tok::KwMut},
        {"if", Tok::KwIf}, {"else", Tok::KwElse}, {"while", Tok::KwWhile},
        {"return", Tok::KwReturn}, {"true", Tok::KwTrue},
        {"false", Tok::KwFalse}, {"int", Tok::KwInt},
        {"bool", Tok::KwBool}, {"string", Tok::KwStr}, {"void", Tok::KwVoid},
    };
    return kw;
}
