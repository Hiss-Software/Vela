#include "vela/common.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

std::string g_source;
std::string g_path;

void fail(std::string code, Loc loc, std::string msg, std::string hint) {
    Diag d;
    d.code = std::move(code);
    d.path = g_path;
    d.line = loc.line;
    d.col = loc.col;
    d.msg = std::move(msg);
    d.hint = std::move(hint);
    throw VelaError(std::move(d));
}

void print_diag(const Diag& d) {
    std::fprintf(stderr, "error [%s]: %s:%d:%d — %s\n",
                 d.code.c_str(), d.path.c_str(), d.line, d.col, d.msg.c_str());

    std::string ln;
    {
        std::istringstream ls(g_source);
        for (int i = 1; i <= d.line; ++i) {
            if (!std::getline(ls, ln)) { ln.clear(); break; }
        }
    }
    while (!ln.empty() && (ln.back() == '\r' || ln.back() == '\n')) ln.pop_back();

    std::string num = std::to_string(d.line);
    std::string pad(4 - std::min<size_t>(num.size(), 4), ' ');
    std::fprintf(stderr, "%s%s | %s\n", pad.c_str(), num.c_str(), ln.c_str());

    // caret with UTF-8 awareness: columns count code points
    size_t bi = 0;
    int cp = 1;
    while (cp < d.col && bi < ln.size()) {
        unsigned char c = static_cast<unsigned char>(ln[bi]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        bi += len;
        ++cp;
    }
    std::string caret = pad + std::string(num.size(), ' ') + " | ";
    caret.append(bi, ' ');
    caret += '^';
    std::fprintf(stderr, "%s\n", caret.c_str());

    if (!d.hint.empty())
        std::fprintf(stderr, "  hint: %s\n", d.hint.c_str());
}
