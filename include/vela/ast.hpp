#pragma once
// The Vela AST. Nodes carry their location; the checker fills in Expr::ty.
#include "vela/common.hpp"
#include "vela/lexer.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class Ty { Int, Bool, Str, Void };

inline std::string ty_name(Ty t) {
    switch (t) {
        case Ty::Int:  return "int";
        case Ty::Bool: return "bool";
        case Ty::Str:  return "string";
        case Ty::Void: return "void";
    }
    return "?";
}

// ---- expressions ----

struct Expr {
    Loc loc;
    Ty ty = Ty::Void;   // filled in by the checker
    virtual ~Expr() = default;
};
struct IntLitExpr  : Expr { long long value = 0; };
struct StrLitExpr  : Expr { std::string value; };
struct BoolLitExpr : Expr { bool value = false; };
struct VarExpr     : Expr { std::string name; };
struct CallExpr    : Expr {
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
    bool b_print = false, b_str = false, b_len = false;
};
struct UnaryExpr   : Expr { Tok op = Tok::Not; std::unique_ptr<Expr> operand; };
struct BinaryExpr  : Expr {
    Tok op = Tok::Plus;
    std::unique_ptr<Expr> lhs, rhs;
};

// ---- statements ----

struct Stmt {
    Loc loc;
    virtual ~Stmt() = default;
};
struct LetStmt    : Stmt {
    std::string name; Loc name_loc;
    Ty type = Ty::Int; bool is_mut = false;
    std::unique_ptr<Expr> init;
};
struct AssignStmt : Stmt {
    std::string name; Loc name_loc;
    std::unique_ptr<Expr> value;
};
struct IfStmt     : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> then_b;
    std::unique_ptr<Stmt> else_b;   // may be nullptr
};
struct WhileStmt  : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
};
struct ReturnStmt : Stmt { std::unique_ptr<Expr> value; bool has_value = false; };
struct ExprStmt   : Stmt { std::unique_ptr<Expr> expr; };
struct BlockStmt  : Stmt { std::vector<std::unique_ptr<Stmt>> stmts; };

struct Param { std::string name; Ty type = Ty::Int; Loc loc; };

struct FnDecl {
    std::string name;
    Loc loc;
    std::vector<Param> params;
    Ty ret = Ty::Void;
    std::unique_ptr<BlockStmt> body;
};

struct Program { std::vector<std::unique_ptr<FnDecl>> fns; };

template <class T, class... A>
inline std::unique_ptr<T> mk(Loc loc, A&&... a) {
    auto p = std::make_unique<T>(std::forward<A>(a)...);
    p->loc = loc;
    return p;
}
