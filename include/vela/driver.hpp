#pragma once
// The Vela driver: orchestrates build stages — checking, code generation,
// assembling, linking.
#include <string>

enum class Action { Check, Build, Run };

struct Options {
    Action action = Action::Check;
    std::string input;        // the main .vela file
    std::string output;       // empty -> a.exe (Windows) / a.out
    bool lenient = false;     // relax stylistic rules
    std::string as_cmd = "as";   // assembler for the generated code
    std::string cc = "gcc";      // linker (CRT + libc)
    std::string link_flags;   // extra linker flags
    std::string run_args;     // arguments of the run program (after --)
};

// Runs the requested action; returns the process exit code.
int driver_run(Options opts);
