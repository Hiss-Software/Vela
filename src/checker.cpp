#include "vela/checker.hpp"

#include <cstddef>

bool Checker::underscore(const std::string& n) {
    return !n.empty() && n[0] == '_';
}

bool Checker::reserved(const std::string& n) {
    return n == "print" || n == "str" || n == "len";
}

void Checker::push_scope() { scopes.emplace_back(); }
void Checker::pop_scope() { scopes.pop_back(); }

VarInfo* Checker::find_var(const std::string& n) {
    for (size_t k = scopes.size(); k-- > 0;)
        if (scopes[k].has(n)) return &scopes[k].get(n);
    return nullptr;
}

void Checker::check() {
    for (auto& f : prog.fns) {
        if (fns.count(f->name))
            fail("E0232", f->loc,
                 "function `" + f->name + "` is declared twice",
                 "Vela has no overloading: one name — one function");
        FnSig sig;
        for (auto& prm : f->params) sig.params.push_back(prm.type);
        sig.ret = f->ret;
        fns[f->name] = sig;
    }
    if (!fns.count("main"))
        fail("E0233", Loc{1, 1},
             "no entry point: declare `fn main() -> int`");
    for (auto& f : prog.fns) check_fn(*f);
}

void Checker::check_fn(FnDecl& f) {
    cur_ret = f.ret;
    cur_fn_name = f.name;
    scopes.clear();
    push_scope();
    for (auto& prm : f.params) {
        if (reserved(prm.name))
            fail("E0205", prm.loc,
                 "name `" + prm.name + "` is reserved by a builtin function",
                 "builtin functions: print, str, len");
        if (fns.count(prm.name))
            fail("E0204", prm.loc,
                 "name `" + prm.name + "` is already taken by a function",
                 "functions and variables do not share names in Vela");
        scopes.back().add(prm.name,
                          VarInfo{prm.type, true, false, prm.loc});
    }
    if (f.name == "main" && (!f.params.empty() || f.ret != Ty::Int))
        fail("E0231", f.loc,
             "the entry point must be `fn main() -> int` with no parameters",
             "the operating system expects an int as the exit code");
    check_block(*f.body);
    if (f.ret != Ty::Void && !always_returns(f.body.get()))
        fail("E0218", f.loc,
             "function `" + f.name + "` promises to return `" +
                 ty_name(f.ret) + "`, but there is a path without `return`",
             "Vela proves the return on every path: the last statement of "
             "the block (or both branches of `if`) must be a `return`");
    if (!lenient) {
        for (auto& [n, v] : scopes.back().vars) {
            if (!v.used && !underscore(n))
                fail("E0207", v.decl_loc,
                     "parameter `" + n + "` is declared but never used",
                     "extra parameters are garbage in the interface. Remove "
                     "it or rename it to `_" + n + "`");
        }
    }
    pop_scope();
}

bool Checker::always_returns(const Stmt* s) {
    if (auto* r = dynamic_cast<const ReturnStmt*>(s)) { (void)r; return true; }
    if (auto* b = dynamic_cast<const BlockStmt*>(s))
        return !b->stmts.empty() && always_returns(b->stmts.back().get());
    if (auto* i = dynamic_cast<const IfStmt*>(s))
        return i->else_b != nullptr && always_returns(i->then_b.get()) &&
               always_returns(i->else_b.get());
    return false;
}

void Checker::check_block(BlockStmt& b) {
    push_scope();
    bool unreachable = false;
    for (auto& st : b.stmts) {
        if (unreachable)
            fail("E0219", st->loc,
                 "code after `return` is unreachable",
                 "it will never run anyway — why is it here?");
        check_stmt(*st);
        if (always_returns(st.get())) unreachable = true;
    }
    if (!lenient) {
        for (auto& [n, v] : scopes.back().vars) {
            if (!v.used && !underscore(n))
                fail("E0206", v.decl_loc,
                     "variable `" + n + "` is declared but never used",
                     "a dead variable is a bug in the embryo. Delete it or "
                     "use it; or name it `_" + n + "`");
        }
    }
    pop_scope();
}

void Checker::check_stmt(Stmt& s) {
    if (auto* st = dynamic_cast<LetStmt*>(&s)) {
        if (reserved(st->name))
            fail("E0205", st->name_loc,
                 "name `" + st->name + "` is reserved by a builtin function",
                 "builtin functions: print, str, len");
        if (fns.count(st->name))
            fail("E0204", st->name_loc,
                 "name `" + st->name + "` is already taken by a function",
                 "functions and variables do not share names in Vela");
        if (!lenient) {
            for (size_t k = scopes.size(); k-- > 0;) {
                if (scopes[k].has(st->name))
                    fail("E0203", st->loc,
                         "variable `" + st->name +
                             "` shadows an outer declaration",
                         "shadowing is a way to lose a bug. Vela does not "
                         "forgive it: use another name or reassign via `=`");
            }
        }
        Ty init_ty = check_expr(st->init.get());
        if (init_ty != st->type)
            fail("E0209", st->loc,
                 "type mismatch: declared `" + ty_name(st->type) +
                     "`, initialized with a value of type `" +
                     ty_name(init_ty) + "`",
                 "there are no implicit conversions in Vela. Need a string — "
                 "wrap the number in str(...)");
        scopes.back().add(st->name,
                          VarInfo{st->type, st->is_mut, false, st->name_loc});
        return;
    }
    if (auto* st = dynamic_cast<AssignStmt*>(&s)) {
        VarInfo* v = find_var(st->name);
        if (!v)
            fail("E0201", st->name_loc,
                 "variable `" + st->name + "` is not declared",
                 "declare it with `let " + st->name + ": <type> = ...;`");
        if (!v->is_mut)
            fail("E0208", st->loc,
                 "variable `" + st->name + "` is declared without `mut`",
                 "assigning to an immutable value is something I do not "
                 "forgive. Want to change it — declare `let mut " +
                     st->name + ": " + ty_name(v->type) + " = ...`");
        Ty t = check_expr(st->value.get());
        if (t != v->type)
            fail("E0209", st->loc,
                 "type mismatch in assignment: variable `" + st->name +
                     "` has type `" + ty_name(v->type) + "`, but `" +
                     ty_name(t) + "` is assigned");
        return;
    }
    if (auto* st = dynamic_cast<IfStmt*>(&s)) {
        Ty c = check_expr(st->cond.get());
        if (c != Ty::Bool)
            fail("E0210", st->cond->loc,
                 "the `if` condition must be `bool`, not `" + ty_name(c) + "`",
                 "in Vela a number is not a truth value: write an explicit "
                 "comparison, e.g. `x != 0`");
        check_stmt(*st->then_b);
        if (st->else_b) check_stmt(*st->else_b);
        return;
    }
    if (auto* st = dynamic_cast<WhileStmt*>(&s)) {
        Ty c = check_expr(st->cond.get());
        if (c != Ty::Bool)
            fail("E0210", st->cond->loc,
                 "the `while` condition must be `bool`, not `" + ty_name(c) + "`",
                 "in Vela a number is not a truth value: write an explicit "
                 "comparison");
        check_stmt(*st->body);
        return;
    }
    if (auto* st = dynamic_cast<ReturnStmt*>(&s)) {
        if (cur_ret == Ty::Void) {
            if (st->has_value)
                fail("E0217", st->loc,
                     "function `" + cur_fn_name +
                         "` is declared without a return type — `return` with "
                         "a value is not allowed");
        } else {
            if (!st->has_value)
                fail("E0217", st->loc,
                     "function `" + cur_fn_name + "` returns `" +
                         ty_name(cur_ret) + "` — `return` must carry a value");
            Ty t = check_expr(st->value.get());
            if (t != cur_ret)
                fail("E0209", st->loc,
                     "`return` yields `" + ty_name(t) + "`, but function `" +
                         cur_fn_name + "` promised `" + ty_name(cur_ret) + "`");
        }
        return;
    }
    if (auto* st = dynamic_cast<ExprStmt*>(&s)) {
        check_expr(st->expr.get());
        if (auto* c = dynamic_cast<CallExpr*>(st->expr.get());
            c != nullptr && c->ty != Ty::Void) {
            if (!lenient)
                fail("E0220", st->loc,
                     "the result of the call to `" + c->name + "` is not used",
                     "store it: `let _: " + ty_name(c->ty) + " = " +
                         c->name + "(...)` — or make the function return void");
        }
        return;
    }
    if (auto* st = dynamic_cast<BlockStmt*>(&s)) {
        check_block(*st);
        return;
    }
}

Ty Checker::check_expr(Expr* e) {
    Ty t = check_expr_inner(e);
    e->ty = t;
    return t;
}

Ty Checker::check_expr_inner(Expr* e) {
    if (auto* x = dynamic_cast<IntLitExpr*>(e))  { (void)x; return Ty::Int; }
    if (auto* x = dynamic_cast<StrLitExpr*>(e))  { (void)x; return Ty::Str; }
    if (auto* x = dynamic_cast<BoolLitExpr*>(e)) { (void)x; return Ty::Bool; }
    if (auto* x = dynamic_cast<VarExpr*>(e)) {
        if (VarInfo* v = find_var(x->name)) {
            v->used = true;
            return v->type;
        }
        if (fns.count(x->name))
            fail("E0204", e->loc,
                 "`" + x->name + "` is a function, not a variable",
                 "call it: " + x->name + "(...)");
        fail("E0201", e->loc,
             "variable `" + x->name + "` is not declared",
             "declare it with `let " + x->name + ": <type> = ...;` before "
             "this point");
    }
    if (auto* x = dynamic_cast<CallExpr*>(e)) {
        if (x->name == "print") {
            x->b_print = true;
            if (x->args.size() != 1)
                fail("E0216", x->loc,
                     "`print` takes exactly 1 argument, got " +
                         std::to_string(x->args.size()));
            Ty a = check_expr(x->args[0].get());
            if (a == Ty::Void)
                fail("E0209", x->args[0]->loc,
                     "the argument of `print` cannot be `void`");
            return Ty::Void;
        }
        if (x->name == "str") {
            x->b_str = true;
            if (x->args.size() != 1)
                fail("E0216", x->loc,
                     "`str` takes exactly 1 argument, got " +
                         std::to_string(x->args.size()));
            Ty a = check_expr(x->args[0].get());
            if (a != Ty::Int && a != Ty::Bool && a != Ty::Str)
                fail("E0209", x->args[0]->loc,
                     "`str` does not accept `" + ty_name(a) + "`");
            return Ty::Str;
        }
        if (x->name == "len") {
            x->b_len = true;
            if (x->args.size() != 1)
                fail("E0216", x->loc,
                     "`len` takes exactly 1 argument, got " +
                         std::to_string(x->args.size()));
            Ty a = check_expr(x->args[0].get());
            if (a != Ty::Str)
                fail("E0209", x->args[0]->loc,
                     "`len` takes only `string`, not `" + ty_name(a) + "`");
            return Ty::Int;
        }
        auto it = fns.find(x->name);
        if (it == fns.end())
            fail("E0215", x->loc,
                 "unknown function `" + x->name + "`",
                 "check the name and the declaration order");
        if (x->args.size() != it->second.params.size())
            fail("E0216", x->loc,
                 "function `" + x->name + "` takes " +
                     std::to_string(it->second.params.size()) +
                     " argument(s), got " +
                     std::to_string(x->args.size()));
        for (size_t k = 0; k < x->args.size(); ++k) {
            Ty at = check_expr(x->args[k].get());
            Ty pt = it->second.params[k];
            if (at != pt)
                fail("E0209", x->args[k]->loc,
                     "argument " + std::to_string(k + 1) + " of function `" +
                         x->name + "`: expected `" + ty_name(pt) +
                         "`, got `" + ty_name(at) + "`",
                     "there are no implicit conversions in Vela");
        }
        return it->second.ret;
    }
    if (auto* x = dynamic_cast<UnaryExpr*>(e)) {
        Ty t = check_expr(x->operand.get());
        if (x->op == Tok::Not) {
            if (t != Ty::Bool)
                fail("E0210", x->loc,
                     "`!` works only on `bool`, not on `" + ty_name(t) + "`");
            return Ty::Bool;
        }
        // Minus
        if (t != Ty::Int)
            fail("E0211", x->loc,
                 "unary minus works only on `int`, not on `" +
                     ty_name(t) + "`");
        return Ty::Int;
    }
    if (auto* x = dynamic_cast<BinaryExpr*>(e)) {
        Ty lt = check_expr(x->lhs.get());
        Ty rt = check_expr(x->rhs.get());
        switch (x->op) {
            case Tok::Plus:
                if (lt == Ty::Int && rt == Ty::Int) return Ty::Int;
                if (lt == Ty::Str && rt == Ty::Str) return Ty::Str;
                if (lt == Ty::Str || rt == Ty::Str)
                    fail("E0212", e->loc,
                         "only a string can be concatenated with a string",
                         "convert the non-string operand explicitly: str(...)");
                fail("E0211", e->loc,
                     "`+` is defined for int and string, not for `" +
                         ty_name(lt) + "` and `" + ty_name(rt) + "`");
            case Tok::Minus: case Tok::Star: case Tok::Slash: case Tok::Percent:
                if (lt != Ty::Int || rt != Ty::Int)
                    fail("E0211", e->loc,
                         "`" + tok_desc(x->op) + "` works only on `int`, not on `" +
                             ty_name(lt) + "` and `" + ty_name(rt) + "`");
                if (x->op == Tok::Slash || x->op == Tok::Percent) {
                    if (auto* z = dynamic_cast<IntLitExpr*>(x->rhs.get());
                        z != nullptr && z->value == 0)
                        fail("E0214", x->rhs->loc,
                             "division by a constant zero",
                             "I can see this before the program even runs — "
                             "and I do not forgive it");
                }
                return Ty::Int;
            case Tok::EqEq: case Tok::NotEq:
                if (lt != rt)
                    fail("E0209", e->loc,
                         "cannot compare `" + ty_name(lt) + "` and `" +
                             ty_name(rt) + "` — these are different types",
                         "there are no implicit conversions in comparisons "
                         "either");
                return Ty::Bool;
            case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge:
                if (lt == Ty::Str || rt == Ty::Str)
                    fail("E0213", e->loc,
                         "strings are compared only with `==` and `!=`",
                         "define your own ordering rules for strings");
                if (lt != Ty::Int || rt != Ty::Int)
                    fail("E0211", e->loc,
                         "`" + tok_desc(x->op) + "` is defined only for `int`, "
                         "not for `" + ty_name(lt) + "` and `" + ty_name(rt) + "`");
                return Ty::Bool;
            case Tok::AndAnd: case Tok::OrOr:
                if (lt != Ty::Bool || rt != Ty::Bool)
                    fail("E0210", e->loc,
                         "`" + tok_desc(x->op) + "` works only on `bool`, not on `" +
                             ty_name(lt) + "` and `" + ty_name(rt) + "`",
                         "in Vela a number is not a truth value");
                return Ty::Bool;
            default: break;
        }
    }
    fail("E0101", e->loc, "internal error: unknown expression");
}
