#include "vela/common.hpp"
#include "vela/driver.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const char* kVersion = "0.3.0";

void usage() {
    std::fprintf(stderr,
        "Vela %s — a strict compiler: I see the bug and I do not forgive it.\n"
        "usage:\n"
        "  vela check  program.vela [--lenient]\n"
        "  vela build  program.vela [-o name] [--lenient] [--as CMD] [--cc CMD] [--link F]\n"
        "  vela run    program.vela [--lenient] [-- args of the program]\n"
        "  vela --version\n"
        "flags:\n"
        "  --lenient   relax stylistic rules; types and memory stay strict\n"
        "  --as CMD    assembler for the generated code (default: as)\n"
        "  --cc CMD    linker: CRT + libc (default: gcc)\n"
        "  --link F    extra linker flags\n"
        "  --          everything after this goes to the run program\n",
        kVersion);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string a0 = argv[1];
    if (a0 == "--help" || a0 == "-h") { usage(); return 0; }
    if (a0 == "--version") { std::printf("Vela %s\n", kVersion); return 0; }

    Action action;
    if (a0 == "check") action = Action::Check;
    else if (a0 == "build") action = Action::Build;
    else if (a0 == "run") action = Action::Run;
    else { usage(); return 2; }

    Options opts;
    opts.action = action;
    bool parsing_run_args = false;
    for (int k = 2; k < argc; ++k) {
        std::string a = argv[k];
        if (parsing_run_args) {
            if (!opts.run_args.empty()) opts.run_args += " ";
            opts.run_args += a;
            continue;
        }
        if (a == "--") { parsing_run_args = true; continue; }
        if (a == "-o") {
            if (k + 1 >= argc) { usage(); return 2; }
            opts.output = argv[++k];
        }
        else if (a == "--lenient") opts.lenient = true;
        else if (a == "--as") {
            if (k + 1 >= argc) { usage(); return 2; }
            opts.as_cmd = argv[++k];
        }
        else if (a == "--cc") {
            if (k + 1 >= argc) { usage(); return 2; }
            opts.cc = argv[++k];
        }
        else if (a == "--link") {
            if (k + 1 >= argc) { usage(); return 2; }
            opts.link_flags = argv[++k];
        }
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a.empty()) { usage(); return 2; }
        else if (opts.input.empty()) opts.input = a;
        else { usage(); return 2; }
    }
    if (opts.input.empty()) { usage(); return 2; }

    std::ifstream in(opts.input, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "vela: failed to open file `%s`\n",
                     opts.input.c_str());
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    g_source = ss.str();
    g_path = fs::absolute(fs::path(opts.input)).lexically_normal().string();

    try {
        return driver_run(std::move(opts));
    } catch (const VelaError& e) {
        print_diag(e.d);
        std::fprintf(stderr,
            "\nstrict mode: showing the first error. Fix it and I will show "
            "the next one, if any remain.\n");
        return 1;
    }
}
