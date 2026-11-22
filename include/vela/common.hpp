#pragma once
// Common Vela entities: source location, diagnostics, fatal errors.
#include <stdexcept>
#include <string>

struct Loc {
    int line = 1;
    int col = 1;
};

struct Diag {
    std::string code;   // error code, e.g. E0206
    std::string path;   // full path to the source file
    int line = 1;
    int col = 1;
    std::string msg;
    std::string hint;
};

// Any error in Vela is fatal: build a Diag and bail out. No error spam.
class VelaError : public std::runtime_error {
public:
    explicit VelaError(Diag d) : std::runtime_error(d.msg), d(std::move(d)) {}
    Diag d;
};

// The source text and its full path: needed for diagnostics printing
// (the source line with the caret).
extern std::string g_source;
extern std::string g_path;

// The only way to report an error: fatal, immediately.
[[noreturn]] void fail(std::string code, Loc loc, std::string msg,
                       std::string hint = "");

// One compact error block: full path, line, caret, hint.
void print_diag(const Diag& d);
