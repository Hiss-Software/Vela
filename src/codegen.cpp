#include "vela/codegen.hpp"

#include <cstdint>
#include <cstdio>

// ============================================================================
//  Бэкенд Vela: собственная кодогенерация в ассемблер x86-64 (GNU as).
//
//  Соглашение о вызовах пользовательских функций Vela (своё, простое):
//    - все аргументы вызывающий кладёт на стек (первый — последним);
//    - вызываемый читает аргумент i по адресу 16+8*(n-1-i)(%rbp);
//    - вызывающий держит %rsp кратным 16 в точке call.
//  Рантайм-трюки v_rt_* мостят это соглашение с ABI платформы
//  (SysV: rdi/rsi/rdx; Win64: rcx/rdx/r8 + shadow space 32 байта).
//
//  ВАЖНО про форматы: .Lfmt_i — ЧИСТЫЙ "%lld" без переноса строки, им
//  пользуется конверсия str(int) через snprintf. Для print() существуют
//  отдельные .Lfmt_i_n и .Lfmt_s_n с '\n'. Один формат на два потребителя —
//  это баг: str(i) возвращал "0\n", и вывод рвал на три строки.
//
//  Семантика сравнений: в бинарном узле r10=lhs, rax=rhs. AT&T `cmpq A, B`
//  вычисляет B - A, поэтому упорядоченные сравнения используют
//  `cmpq %rax, %r10` (lhs - rhs) с естественными setcc.
// ============================================================================

#ifdef _WIN32
static const char* RODATA = ".section .rdata,\"dr\"";
#else
static const char* RODATA = ".section .rodata";
#endif

std::string CodegenX86::fresh_label(const char* hint) {
    return ".L" + std::string(hint) + "_" + std::to_string(++label_n);
}

void CodegenX86::push_scope() { scopes.emplace_back(); }
void CodegenX86::pop_scope() { scopes.pop_back(); }

int CodegenX86::lookup(const std::string& n) const {
    for (size_t k = scopes.size(); k-- > 0;) {
        auto it = scopes[k].find(n);
        if (it != scopes[k].end()) return it->second;
    }
    return 0;   // недостижимо: проверяльщик не пропускает необъявленные имена
}

int CodegenX86::count_slots(const Stmt* s) {
    int n = 0;
    if (auto* st = dynamic_cast<const LetStmt*>(s)) { (void)st; return 1; }
    if (auto* st = dynamic_cast<const BlockStmt*>(s)) {
        for (auto& c : st->stmts) n += count_slots(c.get());
        return n;
    }
    if (auto* st = dynamic_cast<const IfStmt*>(s)) {
        n += count_slots(st->then_b.get());
        if (st->else_b) n += count_slots(st->else_b.get());
        return n;
    }
    if (auto* st = dynamic_cast<const WhileStmt*>(s))
        return count_slots(st->body.get());
    return 0;
}

void CodegenX86::call_fn(const std::string& sym, const std::vector<Expr*>& args) {
    size_t n = args.size();
    // rsp в теле функции кратен 16 при чётном depth; нужно, чтобы и после
    // пуша аргументов он остался кратен 16 в точке call.
    int pad = ((depth + static_cast<int>(n)) % 2) ? 1 : 0;
    if (pad) out += "    subq $8, %rsp\n";
    for (Expr* a : args) {
        gen_expr(a);
        out += "    pushq %rax\n";
        ++depth;
    }
    out += "    call " + sym + "\n";
    out += "    addq $" + std::to_string(8 * (n + pad)) + ", %rsp\n";
    depth -= static_cast<int>(n);
}

void CodegenX86::gen_expr(Expr* e) {
    if (auto* x = dynamic_cast<IntLitExpr*>(e)) {
        if (x->value >= INT64_C(-2147483648) && x->value <= INT64_C(2147483647))
            out += "    movq $" + std::to_string(x->value) + ", %rax\n";
        else
            out += "    movabsq $" + std::to_string(x->value) + ", %rax\n";
        return;
    }
    if (auto* x = dynamic_cast<StrLitExpr*>(e)) {
        auto it = str_labels.find(x->value);
        if (it == str_labels.end()) {
            std::string lab = ".Lstr_" + std::to_string(str_labels.size());
            str_labels[x->value] = lab;
            str_data.emplace_back(lab, x->value);
            it = str_labels.find(x->value);
        }
        out += "    leaq " + it->second + "(%rip), %rax\n";
        return;
    }
    if (auto* x = dynamic_cast<BoolLitExpr*>(e)) {
        out += x->value ? "    movq $1, %rax\n" : "    movq $0, %rax\n";
        return;
    }
    if (auto* x = dynamic_cast<VarExpr*>(e)) {
        int off = lookup(x->name);
        out += "    movq " + std::to_string(off) + "(%rbp), %rax\n";
        return;
    }
    if (auto* x = dynamic_cast<CallExpr*>(e)) {
        if (x->b_str) {
            if (x->args[0]->ty == Ty::Int) {
                call_fn("v_rt_str_int", {x->args[0].get()});
                return;
            }
            if (x->args[0]->ty == Ty::Bool) {
                gen_expr(x->args[0].get());
                std::string lt = fresh_label("t"), le = fresh_label("e");
                out += "    testq %rax, %rax\n    jz " + lt + "\n";
                out += "    leaq .Ltrue(%rip), %rax\n    jmp " + le + "\n";
                out += lt + ":\n    leaq .Lfalse(%rip), %rax\n" + le + ":\n";
                return;
            }
            gen_expr(x->args[0].get());   // str(string) — тождество
            return;
        }
        if (x->b_len) {
            call_fn("v_rt_len", {x->args[0].get()});
            return;
        }
        std::vector<Expr*> raw;
        for (auto& a : x->args) raw.push_back(a.get());
        call_fn("v_" + x->name, raw);
        return;
    }
    if (auto* x = dynamic_cast<UnaryExpr*>(e)) {
        gen_expr(x->operand.get());
        if (x->op == Tok::Not) out += "    xorq $1, %rax\n";
        else out += "    negq %rax\n";
        return;
    }
    if (auto* x = dynamic_cast<BinaryExpr*>(e)) {
        // && и || — короткое замыкание через метки
        if (x->op == Tok::AndAnd || x->op == Tok::OrOr) {
            bool is_and = (x->op == Tok::AndAnd);
            std::string lshort = fresh_label(is_and ? "f" : "t");
            std::string lend = fresh_label("e");
            gen_expr(x->lhs.get());
            out += "    testq %rax, %rax\n    jz " + lshort + "\n";
            if (!is_and) {
                std::string l2 = fresh_label("t2");
                out += "    movq $1, %rax\n    jmp " + lend + "\n"
                       + lshort + ":\n";
                gen_expr(x->rhs.get());
                out += "    testq %rax, %rax\n    jz " + l2 + "\n"
                       "    movq $1, %rax\n    jmp " + lend + "\n"
                       + l2 + ":\n    xorq %rax, %rax\n" + lend + ":\n";
                return;
            }
            gen_expr(x->rhs.get());
            out += "    testq %rax, %rax\n    jz " + lshort + "\n"
                   "    movq $1, %rax\n    jmp " + lend + "\n"
                   + lshort + ":\n    xorq %rax, %rax\n" + lend + ":\n";
            return;
        }
        // строковые операции — через рантайм
        if (x->lhs->ty == Ty::Str) {
            if (x->op == Tok::Plus) {
                call_fn("v_rt_concat", {x->lhs.get(), x->rhs.get()});
                return;
            }
            if (x->op == Tok::EqEq || x->op == Tok::NotEq) {
                call_fn("v_rt_streq", {x->lhs.get(), x->rhs.get()});
                if (x->op == Tok::NotEq) out += "    xorq $1, %rax\n";
                return;
            }
        }
        // арифметика и сравнения: lhs -> %r10, rhs -> %rax
        gen_expr(x->lhs.get());
        out += "    pushq %rax\n";
        ++depth;
        gen_expr(x->rhs.get());
        out += "    popq %r10\n";
        --depth;
        switch (x->op) {
            case Tok::Plus:
                out += "    addq %r10, %rax\n";
                return;
            case Tok::Minus:
                out += "    subq %rax, %r10\n    movq %r10, %rax\n";
                return;
            case Tok::Star:
                out += "    imulq %r10, %rax\n";
                return;
            case Tok::Slash:
            case Tok::Percent: {
                // константный ноль ловит проверяльщик (E0214),
                // рантайм-ноль — паника v_rt_panic_div0
                std::string ldiv0 = fresh_label("div0");
                std::string lend = fresh_label("e");
                out += "    xchg %rax, %r10\n";   // rax=lhs, r10=rhs
                out += "    testq %r10, %r10\n    jz " + ldiv0 + "\n";
                out += "    cqto\n    idivq %r10\n";
                if (x->op == Tok::Percent) out += "    movq %rdx, %rax\n";
                out += "    jmp " + lend + "\n";
                out += ldiv0 + ":\n";
                // выравнивание: panic не вернётся, поэтому add не нужен
                if (depth % 2 != 0) out += "    subq $8, %rsp\n";
                out += "    call v_rt_panic_div0\n";
                out += lend + ":\n";
                return;
            }
            case Tok::EqEq:
                out += "    cmpq %r10, %rax\n    sete %al\n    movzbq %al, %rax\n";
                return;
            case Tok::NotEq:
                out += "    cmpq %r10, %rax\n    setne %al\n    movzbq %al, %rax\n";
                return;
            case Tok::Lt:
                out += "    cmpq %rax, %r10\n    setl %al\n    movzbq %al, %rax\n";
                return;
            case Tok::Le:
                out += "    cmpq %rax, %r10\n    setle %al\n    movzbq %al, %rax\n";
                return;
            case Tok::Gt:
                out += "    cmpq %rax, %r10\n    setg %al\n    movzbq %al, %rax\n";
                return;
            case Tok::Ge:
                out += "    cmpq %rax, %r10\n    setge %al\n    movzbq %al, %rax\n";
                return;
            default: break;
        }
    }
}

void CodegenX86::gen_stmt(Stmt* s) {
    if (auto* st = dynamic_cast<LetStmt*>(s)) {
        gen_expr(st->init.get());
        int off = -(++slot_n) * 8;
        scopes.back()[st->name] = off;
        out += "    movq %rax, " + std::to_string(off) + "(%rbp)\n";
        return;
    }
    if (auto* st = dynamic_cast<AssignStmt*>(s)) {
        gen_expr(st->value.get());
        int off = lookup(st->name);
        out += "    movq %rax, " + std::to_string(off) + "(%rbp)\n";
        return;
    }
    if (auto* st = dynamic_cast<IfStmt*>(s)) {
        std::string lelse = fresh_label("else"), lend = fresh_label("endif");
        gen_expr(st->cond.get());
        out += "    testq %rax, %rax\n    jz " + lelse + "\n";
        gen_block(dynamic_cast<BlockStmt*>(st->then_b.get()));
        if (st->else_b) {
            out += "    jmp " + lend + "\n" + lelse + ":\n";
            gen_block(dynamic_cast<BlockStmt*>(st->else_b.get()));
            out += lend + ":\n";
        } else {
            out += lelse + ":\n";   // ложь -> падаем на конец if
        }
        return;
    }
    if (auto* st = dynamic_cast<WhileStmt*>(s)) {
        std::string lstart = fresh_label("while"), lend = fresh_label("wend");
        out += lstart + ":\n";
        gen_expr(st->cond.get());
        out += "    testq %rax, %rax\n    jz " + lend + "\n";
        gen_block(dynamic_cast<BlockStmt*>(st->body.get()));
        out += "    jmp " + lstart + "\n" + lend + ":\n";
        return;
    }
    if (auto* st = dynamic_cast<ReturnStmt*>(s)) {
        if (st->has_value) gen_expr(st->value.get());
        out += "    jmp " + ep_label + "\n";
        return;
    }
    if (auto* st = dynamic_cast<ExprStmt*>(s)) {
        if (auto* c = dynamic_cast<CallExpr*>(st->expr.get());
            c != nullptr && c->b_print) {
            Expr* arg = c->args[0].get();
            Ty at = arg->ty;
            // Аргумент вычисляется всегда; bool дополнительно превращаем
            // в указатель на "true"/"false".
            gen_expr(arg);
            if (at == Ty::Bool) {
                std::string lt = fresh_label("t"), le = fresh_label("e");
                out += "    testq %rax, %rax\n    jz " + lt + "\n";
                out += "    leaq .Ltrue(%rip), %rax\n    jmp " + le + "\n";
                out += lt + ":\n    leaq .Lfalse(%rip), %rax\n" + le + ":\n";
                at = Ty::Str;
            }
#ifdef _WIN32
            // shadow space 32 байта + запас; rsp в теле кратен 16
            out += "    subq $48, %rsp\n";
            out += "    movq %rax, %rdx\n";
            out += (at == Ty::Int)
                       ? "    leaq .Lfmt_i_n(%rip), %rcx\n"
                       : "    leaq .Lfmt_s_n(%rip), %rcx\n";
            // printf — varargs: по ABI Win64 в %al должно лежать число
            // используемых XMM-регистров. У нас всегда 0; если об этом не
            // сказать, printf сохранит XMM в наш стек — падение без вывода.
            out += "    xorl %eax, %eax\n    call printf\n";
            // fflush(NULL): вывод сразу, а не в буфер — если программа
            // упадёт позже, уже напечатанное не пропадёт вместе с буфером.
            out += "    xorl %ecx, %ecx\n    call fflush\n";
            out += "    addq $48, %rsp\n";
#else
            out += "    movq %rax, %rsi\n";
            out += (at == Ty::Int)
                       ? "    leaq .Lfmt_i_n(%rip), %rdi\n"
                       : "    leaq .Lfmt_s_n(%rip), %rdi\n";
            out += "    xorl %eax, %eax\n    call printf\n";
            out += "    xorl %edi, %edi\n    call fflush\n";
#endif
            return;
        }
        gen_expr(st->expr.get());   // результат выбрасывается
        return;
    }
    if (auto* st = dynamic_cast<BlockStmt*>(s)) {
        gen_block(st);
        return;
    }
}

void CodegenX86::gen_block(BlockStmt* b) {
    push_scope();
    for (auto& st : b->stmts) gen_stmt(st.get());
    pop_scope();
}

void CodegenX86::emit_fn(FnDecl& f) {
    int slots = count_slots(f.body.get());
    int frame = (slots * 8 + 15) / 16 * 16;   // размер кадра кратен 16

    slot_n = 0;   // нумерация локальных слотов — строго внутри одной функции
    ep_label = "v_" + f.name + "__ep";
    out += "v_" + f.name + ":\n";
    out += "    pushq %rbp\n    movq %rsp, %rbp\n";
    if (frame) out += "    subq $" + std::to_string(frame) + ", %rsp\n";

    push_scope();
    int n = static_cast<int>(f.params.size());
    for (int k = 0; k < n; ++k)
        scopes.back()[f.params[static_cast<size_t>(k)].name] =
            16 + 8 * (n - 1 - k);
    for (auto& st : f.body->stmts) gen_stmt(st.get());
    pop_scope();

    out += ep_label + ":\n";
    out += "    movq %rbp, %rsp\n    popq %rbp\n    ret\n\n";
}

std::string CodegenX86::escape_gas(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        switch (c) {
            case '\\': r += "\\\\"; break;
            case '"':  r += "\\\""; break;
            case '\n': r += "\\n";  break;
            case '\t': r += "\\t";  break;
            case '\r': r += "\\r";  break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\%03o", static_cast<unsigned>(c));
                    r += buf;
                } else {
                    r += static_cast<char>(c);
                }
        }
    }
    return r;
}

// Рантайм-трюки: мост между соглашением Vela (аргументы на стеке) и ABI
// платформы. Аргумент i лежит по адресу 16+8*(n-1-i)(%rbp), поэтому при
// двух аргументах первый — в 24(%rbp), второй — в 16(%rbp).
//
// Для печати используются .Lfmt_i_n / .Lfmt_s_n (с '\n'); для конверсии
// str(int) — чистый .Lfmt_i. Смешивать нельзя: это уже сломало вывод.
std::string CodegenX86::emit_thunks() {
    std::string o;

#ifdef _WIN32
    o += R"ASM(# ---- рантайм Vela (Win64 ABI) ----
# void v_rt_print_int(long long v): printf("%lld\n", v)
v_rt_print_int:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movq 16(%rbp), %rdx
    leaq .Lfmt_i_n(%rip), %rcx
    xorl %eax, %eax
    call printf
    xorl %ecx, %ecx
    call fflush
    movq %rbp, %rsp
    popq %rbp
    ret

# void v_rt_print_str(const char* s)
v_rt_print_str:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movq 16(%rbp), %rdx
    leaq .Lfmt_s_n(%rip), %rcx
    xorl %eax, %eax
    call printf
    xorl %ecx, %ecx
    call fflush
    movq %rbp, %rsp
    popq %rbp
    ret

# const char* v_rt_str_int(long long v): malloc(32) + snprintf
# snprintf(char* buf, size_t size, const char* fmt, ...):
#   rcx=buf  rdx=size  r8=fmt  r9=value
# Формат .Lfmt_i — БЕЗ '\n': str() не добавляет перенос строки.
v_rt_str_int:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movq $32, %rcx
    call malloc
    movq %rax, 32(%rsp)
    movq 32(%rsp), %rcx
    movq $32, %rdx
    leaq .Lfmt_i(%rip), %r8
    movq 16(%rbp), %r9
    xorl %eax, %eax
    call snprintf
    movq 32(%rsp), %rax
    movq %rbp, %rsp
    popq %rbp
    ret

# long long v_rt_len(const char* s): strlen
v_rt_len:
    pushq %rbp
    movq %rsp, %rbp
    subq $32, %rsp
    movq 16(%rbp), %rcx
    call strlen
    movq %rbp, %rsp
    popq %rbp
    ret

# const char* v_rt_concat(const char* a, const char* b)
# a — первый аргумент: 24(%rbp); b — второй: 16(%rbp)
v_rt_concat:
    pushq %rbp
    movq %rsp, %rbp
    subq $64, %rsp
    movq 24(%rbp), %rcx
    call strlen
    movq %rax, 32(%rsp)        # la
    movq 16(%rbp), %rcx
    call strlen
    movq %rax, 40(%rsp)        # lb
    movq 32(%rsp), %rax
    addq 40(%rsp), %rax
    incq %rax                  # la + lb + 1
    movq %rax, %rcx
    call malloc
    movq %rax, 48(%rsp)        # dst
    movq %rax, %rcx
    movq 24(%rbp), %rdx
    movq 32(%rsp), %r8
    call memcpy
    movq 48(%rsp), %rcx
    addq 32(%rsp), %rcx        # dst + la
    movq 16(%rbp), %rdx
    movq 40(%rsp), %r8
    incq %r8                   # lb + 1
    call memcpy
    movq 48(%rsp), %rax
    movq %rbp, %rsp
    popq %rbp
    ret

# long long v_rt_streq(const char* a, const char* b)
v_rt_streq:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movq 24(%rbp), %rcx
    movq 16(%rbp), %rdx
    call strcmp
    testq %rax, %rax
    sete %al
    movzbq %al, %rax
    movq %rbp, %rsp
    popq %rbp
    ret
)ASM";
#else
    o += R"ASM(# ---- рантайм Vela (SysV ABI) ----
# void v_rt_print_int(long long v): printf("%lld\n", v)
v_rt_print_int:
    pushq %rbp
    movq %rsp, %rbp
    movq 16(%rbp), %rsi
    leaq .Lfmt_i_n(%rip), %rdi
    xorl %eax, %eax
    call printf
    xorl %edi, %edi
    call fflush
    movq %rbp, %rsp
    popq %rbp
    ret

# void v_rt_print_str(const char* s)
v_rt_print_str:
    pushq %rbp
    movq %rsp, %rbp
    movq 16(%rbp), %rsi
    leaq .Lfmt_s_n(%rip), %rdi
    xorl %eax, %eax
    call printf
    xorl %edi, %edi
    call fflush
    movq %rbp, %rsp
    popq %rbp
    ret

# const char* v_rt_str_int(long long v): malloc(32) + snprintf
# snprintf(char* buf, size_t size, const char* fmt, ...):
#   rdi=buf  rsi=size  rdx=fmt  rcx=value
# Формат .Lfmt_i — БЕЗ '\n': str() не добавляет перенос строки.
v_rt_str_int:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    movq $32, %rdi
    call malloc
    movq %rax, -8(%rbp)
    movq -8(%rbp), %rdi
    movq $32, %rsi
    leaq .Lfmt_i(%rip), %rdx
    movq 16(%rbp), %rcx
    xorl %eax, %eax
    call snprintf
    movq -8(%rbp), %rax
    movq %rbp, %rsp
    popq %rbp
    ret

# long long v_rt_len(const char* s): strlen
v_rt_len:
    pushq %rbp
    movq %rsp, %rbp
    movq 16(%rbp), %rdi
    call strlen
    movq %rbp, %rsp
    popq %rbp
    ret

# const char* v_rt_concat(const char* a, const char* b)
# a — первый аргумент: 24(%rbp); b — второй: 16(%rbp)
v_rt_concat:
    pushq %rbp
    movq %rsp, %rbp
    subq $32, %rsp
    movq 24(%rbp), %rdi
    call strlen
    movq %rax, -8(%rbp)        # la
    movq 16(%rbp), %rdi
    call strlen
    movq %rax, -16(%rbp)       # lb
    movq -8(%rbp), %rax
    addq -16(%rbp), %rax
    incq %rax                  # la + lb + 1
    movq %rax, %rdi
    call malloc
    movq %rax, -24(%rbp)       # dst
    movq %rax, %rdi
    movq 24(%rbp), %rsi
    movq -8(%rbp), %rdx
    call memcpy
    movq -24(%rbp), %rdi
    addq -8(%rbp), %rdi        # dst + la
    movq 16(%rbp), %rsi
    movq -16(%rbp), %rdx
    incq %rdx                  # lb + 1
    call memcpy
    movq -24(%rbp), %rax
    movq %rbp, %rsp
    popq %rbp
    ret

# long long v_rt_streq(const char* a, const char* b)
v_rt_streq:
    pushq %rbp
    movq %rsp, %rbp
    movq 24(%rbp), %rdi
    movq 16(%rbp), %rsi
    call strcmp
    testq %rax, %rax
    sete %al
    movzbq %al, %rax
    movq %rbp, %rsp
    popq %rbp
    ret
)ASM";
#endif

    // Деление на ноль во время выполнения: честное сообщение и выход.
    // Сообщение в ASCII: консоль Windows может быть не в UTF-8.
    o += R"ASM(
# [[noreturn]] v_rt_panic_div0()
v_rt_panic_div0:
    pushq %rbp
    movq %rsp, %rbp
)ASM";
#ifdef _WIN32
    o += R"ASM(
    subq $48, %rsp
    leaq .Lerr_div0(%rip), %rcx
    xorl %eax, %eax
    call printf
    xorl %ecx, %ecx
    call fflush
    movq $101, %rcx
    call exit
)ASM";
#else
    o += R"ASM(
    leaq .Lerr_div0(%rip), %rdi
    xorl %eax, %eax
    call printf
    xorl %edi, %edi
    call fflush
    movq $101, %rdi
    call exit
)ASM";
#endif
    o += R"ASM(
    movq %rbp, %rsp
    popq %rbp
    ret

)ASM";
    return o;
}

std::string CodegenX86::emit_rodata() {
    std::string o = std::string(RODATA) + "\n";
    // ЧИСТЫЕ форматы для конверсии str(int) — без переноса строки.
    o += ".Lfmt_i:\n    .string \"%lld\"\n";
    // Форматы для print() — с переносом строки.
    o += ".Lfmt_i_n:\n    .string \"%lld\\n\"\n";
    o += ".Lfmt_s_n:\n    .string \"%s\\n\"\n";
    o += ".Lerr_div0:\n    .string \"runtime error: division by zero\\n\"\n";
    o += ".Ltrue:\n    .string \"true\"\n";
    o += ".Lfalse:\n    .string \"false\"\n";
    for (auto& [label, text] : str_data) {
        o += label + ":\n    .string \"" + escape_gas(text) + "\"\n";
    }
    return o;
}

std::string CodegenX86::emit() {
    out.clear();
    label_n = 0;
    str_labels.clear();
    str_data.clear();

    out += "\t.text\n";
    out += emit_thunks();

    for (auto& f : prog.fns) emit_fn(*f);

    // Обёртка точки входа: CRT зовёт main по ABI платформы со стеком,
    // выровненным на 16; наше соглашение совпадает с этим требованием.
    out += "\t.globl main\nmain:\n";
    out += "    pushq %rbp\n    movq %rsp, %rbp\n";
    out += "    call v_main\n";
    out += "    movq %rbp, %rsp\n    popq %rbp\n    ret\n\n";

    out += emit_rodata();
#ifndef _WIN32
    out += ".section .note.GNU-stack,\"\",@progbits\n";
#endif
    return out;
}
