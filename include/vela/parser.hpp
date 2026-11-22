#pragma once
#include "vela/ast.hpp"
#include "vela/lexer.hpp"

#include <memory>
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> t) : toks(std::move(t)) {}

    std::unique_ptr<Program> run();

private:
    std::vector<Token> toks;
    size_t p = 0;

    const Token& peek() const;
    const Token& advance();
    bool is(Tok t) const;
    bool eat(Tok t);
    const Token& expect(Tok t, const char* what);

    Ty parse_type(bool allow_void);
    std::unique_ptr<BlockStmt> parse_block();
    std::unique_ptr<FnDecl> parse_fn();
    std::unique_ptr<Stmt> parse_stmt();

    std::unique_ptr<Expr> parse_expr();
    std::unique_ptr<Expr> parse_or();
    std::unique_ptr<Expr> parse_and();
    std::unique_ptr<Expr> parse_cmp();
    std::unique_ptr<Expr> parse_add();
    std::unique_ptr<Expr> parse_mul();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_primary();
};
