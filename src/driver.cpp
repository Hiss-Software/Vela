#include "vela/driver.hpp"

#include "vela/checker.hpp"
#include "vela/codegen.hpp"
#include "vela/common.hpp"
#include "vela/lexer.hpp"
#include "vela/parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

struct RunResult {
    int code = 0;
    std::string out;
};

std::string quote(const std::string& s) {
#ifdef _WIN32
    std::string q = "\"";
    for (char c : s) {
        if (c == '"') q += "\\\"";
        else q += c;
    }
    return q + "\"";
#else
    return "\"" + s + "\"";
#endif
}

// Runs an external stage, capturing its output. In the normal case the
// backend stays silent: any output from it is a Vela bug, and it must be
// shown in full — once.
RunResult run_capture(const std::string& cmd) {
    RunResult r;
    FILE* p = ::popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return RunResult{-1, "failed to launch the process"};
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) r.out.append(buf, n);
    int st = ::pclose(p);
#ifdef _WIN32
    r.code = st;
#else
    r.code = (st == -1) ? -1 : st >> 8;
#endif
    return r;
}

// The whole frontend: lexer -> parser -> checker. Strict mode is single:
// the output is either a valid program or the first error.
std::unique_ptr<Program> frontend(bool lenient) {
    Lexer lexer(g_source);
    Parser parser(lexer.run());
    auto prog = parser.run();
    Checker checker(*prog, lenient);
    checker.check();
    return prog;
}

void report_backend_bug(const char* stage, const std::string& cmd, int code,
                        const std::string& out, const fs::path& artifact) {
    std::fprintf(stderr,
                 "error [E9000]: %s — the external stage exited with code %d\n"
                 "  command: %s\n"
                 "This is a bug in the Vela compiler, not in your program: "
                 "a valid Vela program cannot cause a backend error.\n",
                 stage, code, cmd.c_str());
    if (!out.empty()) std::fprintf(stderr, "%s", out.c_str());
    std::fprintf(stderr,
                 "  stage artifact saved: %s\n"
                 "  hint: if the tool is simply not installed — install it; "
                 "otherwise keep this file and report the bug.\n",
                 artifact.string().c_str());
}

}  // namespace

int driver_run(Options opts) {
    auto prog = frontend(opts.lenient);

    if (opts.action == Action::Check) {
        std::fprintf(stderr, "done: no errors. Nothing to forgive.\n");
        return 0;
    }

    if (opts.output.empty()) {
#ifdef _WIN32
        opts.output = "a.exe";
#else
        opts.output = "a.out";
#endif
    }
    fs::path out = fs::path(opts.output);
#ifdef _WIN32
    if (!out.has_extension()) out.replace_extension(".exe");
#endif

    // All build artifacts go into a dedicated cache next to the result.
    fs::path out_dir = out.parent_path();
    if (out_dir.empty()) out_dir = fs::path(".");
    fs::path build_dir = out_dir / ".vela-build";
    std::error_code ec;
    fs::create_directories(build_dir, ec);

    std::string stem = out.filename().string();
    if (out.has_extension()) {
        size_t pos = stem.rfind(out.extension().string());
        if (pos != std::string::npos) stem.erase(pos);
    }
    fs::path sfile = build_dir / (stem + ".vela.s");
    fs::path objfile = build_dir / (stem + ".vela.o");

    // 1. Native code generation: Vela -> x86-64 assembly.
    CodegenX86 gen(*prog);
    {
        std::ofstream sf(sfile, std::ios::binary);
        sf << gen.emit();
    }

    // 2. Assembling into an object file.
    std::string as_cmd = opts.as_cmd + " " + quote(sfile.string()) + " -o " +
                         quote(objfile.string());
    RunResult sa = run_capture(as_cmd);
    if (sa.code != 0) {
        report_backend_bug("assembling stage", as_cmd, sa.code, sa.out, sfile);
        return 1;
    }

    // 3. Linking: object file + CRT + libc -> executable.
    std::string ld_cmd = opts.cc + " " + quote(objfile.string()) + " -o " +
                         quote(out.string());
    if (!opts.link_flags.empty()) ld_cmd += " " + opts.link_flags;
    RunResult ld = run_capture(ld_cmd);
    if (ld.code != 0) {
        report_backend_bug("linking stage", ld_cmd, ld.code, ld.out, objfile);
        return 1;
    }

    std::fprintf(stderr, "compiled: %s\n", out.string().c_str());

    if (opts.action == Action::Run) {
        std::string cmd = quote(out.string());
        if (!opts.run_args.empty()) cmd += " " + opts.run_args;
        int rc = std::system(cmd.c_str());
#ifdef _WIN32
        return rc;
#else
        return (rc == -1) ? 127 : rc >> 8;
#endif
    }
    return 0;
}
