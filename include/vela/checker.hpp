#pragma once
// The Vela checker: strict. The first error is everything. No warnings.
#include "vela/ast.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct VarInfo {
    Ty type = Ty::Int;
    bool is_mut = false;
    bool used = false;
    Loc decl_loc;
};

struct Scope {
    std::vector<std::pair<std::string, VarInfo>> vars;
    std::unordered_map<std::string, size_t> idx;
    bool has(const std::string& n) const { return idx.count(n) != 0; }
    VarInfo& get(const std::string& n) { return vars[idx.at(n)].second; }
    void add(const std::string& n, VarInfo v) {
        idx[n] = vars.size();
        vars.emplace_back(n, std::move(v));
    }
};

struct FnSig {
    std::vector<Ty> params;
    Ty ret = Ty::Void;
};

class Checker {
public:
    Checker(Program& p, bool lenient) : prog(p), lenient(lenient) {}

    void check();

private:
    Program& prog;
    bool lenient;
    std::unordered_map<std::string, FnSig> fns;
    std::vector<Scope> scopes;
    Ty cur_ret = Ty::Void;
    std::string cur_fn_name;

    static bool underscore(const std::string& n);
    static bool reserved(const std::string& n);

    void push_scope();
    void pop_scope();
    VarInfo* find_var(const std::string& n);

    void check_fn(FnDecl& f);
    bool always_returns(const Stmt* s);
    void check_block(BlockStmt& b);
    void check_stmt(Stmt& s);
    Ty check_expr(Expr* e);
    Ty check_expr_inner(Expr* e);
};
