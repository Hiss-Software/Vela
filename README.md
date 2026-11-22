# Vela

A strict systems programming language with a self-contained native backend:
**Vela → x86-64 assembly → machine code**. No intermediate C, no LLVM.
The compiler is a single C++17 project; the backend emits assembly text and
assembles it with the platform toolchain (`as` + linker).

```
program.vela ──lexer──▶ tokens ──parser──▶ AST ──checker──▶ typed AST
      ──codegen──▶ x86-64 assembly (.s) ──as──▶ object (.o) ──cc──▶ executable
```

## Why "strict"

Vela is a language that sees bugs and does not forgive them. No warnings:
every diagnostic is fatal, and the compiler shows **one** error at a time —
fix it, and the next one appears, if any remain. Types never convert
implicitly, uninitialized variables do not exist, dead code does not compile.

What the checker rejects (non-exhaustive):

- **E0203** — variable shadowing: redeclaring a name in an inner scope
- **E0206/E0207** — unused variable / unused parameter (rename to `_name` or delete)
- **E0208** — assignment to a non-`mut` variable
- **E0209** — type mismatch, including `print(void)` and `str` on wrong types
- **E0210** — non-`bool` condition in `if`/`while` (a number is not a truth value)
- **E0214** — division by constant zero, caught before the program runs
- **E0215/E0216** — unknown function / wrong argument count
- **E0217/E0218** — `return` discipline: proven return on every path
- **E0219** — unreachable code after `return`
- **E0220** — discarded non-void call result
- **E0231/E0232/E0233** — entry point rules: `fn main() -> int` exists, is unique, takes no parameters

## Language

```vela
fn fib(n: int) -> int {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}

fn main() -> int {
    let mut i: int = 0;
    while (i <= 10) {
        print(str(i) + ": " + str(fib(i)));
        i = i + 1;
    }
    return 0;
}
```

Types: `int` (64-bit signed), `bool`, `string` (NUL-terminated), `void`.
Builtins: `print(expr)`, `str(int|bool|string)`, `len(string)`.
Operators: `+ - * / %`, comparisons, `&&`/`||` with short-circuit evaluation,
unary `!` and `-`. String concatenation is `+`, string equality is `==`/`!=`;
ordering comparisons on strings are rejected by design.

Run-time errors are honest: dividing by zero at run time prints
`runtime error: division by zero` and exits with code 101 — not an OS crash.

## Building the compiler

Prerequisites: CMake ≥ 3.16, a C++17 compiler, Ninja. On Windows the tested
environment is MSYS2 UCRT64 (GCC).

```bash
cmake -S . -B build -G Ninja
ninja -C build
```

The binary lands in `build/vela.exe` (Windows) or `build/vela`.

## Using the compiler

```bash
vela check  program.vela            # parse and check only
vela build  program.vela -o fib     # produce an executable
vela run    program.vela -- 1 2 3   # check, build, then run with arguments
```

Flags:

| Flag | Meaning |
| --- | --- |
| `--lenient` | Relax stylistic rules (unused/shadowing); types stay strict |
| `--as CMD` | Assembler for the generated code (default: `as`) |
| `--cc CMD` | Linker: CRT + libc (default: `gcc`) |
| `--link F` | Extra linker flags |
| `--` | Everything after it goes to the run program |

Build artifacts are cached next to the output, in `.vela-build/`:
`<name>.vela.s` (assembly, readable) and `<name>.vela.o` (object file).
Keep them when reporting a backend bug — they are the evidence.

## Diagnostics format

One error, compact and self-explanatory:

```text
error [E0208]: /path/to/main.vela:7:9 — variable `i` is declared without `mut`
   7 |         i = i + 1;
     |         ^
  hint: assigning to an immutable value is something I do not forgive.
  Want to change it — declare `let mut i: int = ...`
```

The caret counts columns in code points, so it points at the right character
even in multi-byte text.

## Repository layout

```text
include/vela/   common.hpp  ast.hpp  lexer.hpp  parser.hpp  checker.hpp  codegen.hpp  driver.hpp
src/            lexer.cpp  parser.cpp  checker.cpp  codegen.cpp  driver.cpp  main.cpp  diag.cpp
tests/          fib.vela and other smoke tests
```

## Error-code convention

`E0001–E0999` — lexer; `E01xx` — parser; `E02xx` — checker; `E9xxx` —
backend/environment bugs (e.g. E9000: an external stage failed on a valid
program — this is always a Vela bug, never a user error).

## Roadmap

1. `extern fn` declarations — call libc/WinAPI directly from Vela
2. Arrays and structs — indexing, memory layout, ownership
3. Own COFF64/ELF64 object emitter — drop the external `as` and linker entirely

## License

TBD.
