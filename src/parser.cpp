#include "vela/parser.hpp"

std::unique_ptr<Program> Parser::run() {
    auto prog = std::make_unique<Program>();
    while (!is(Tok::Eof)) prog->fns.push_back(parse_fn());
    return prog;
}

const Token& Parser::peek() const { return toks[p]; }
const Token& Parser::advance() { return toks[p++]; }
bool Parser::is(Tok t) const { return toks[p].t == t; }
bool Parser::eat(Tok t) {
    if (is(t)) { ++p; return true; }
    return false;
}

const Token& Parser::expect(Tok t, const char* what) {
    if (!is(t)) {
        fail("E0101", toks[p].loc,
             "expected " + std::string(what) + ", found `" +
                 tok_desc(toks[p].t) + "`");
    }
    return advance();
}

Ty Parser::parse_type(bool allow_void) {
    if (is(Tok::KwInt))  { advance(); return Ty::Int; }
    if (is(Tok::KwBool)) { advance(); return Ty::Bool; }
    if (is(Tok::KwStr))  { advance(); return Ty::Str; }
    if (is(Tok::KwVoid)) {
        if (!allow_void)
            fail("E0103", peek().loc,
                 "type `void` cannot be used for variables and parameters",
                 "void exists only as a function return type");
        advance();
        return Ty::Void;
    }
    fail("E0101", peek().loc,
         "expected a type (`int`, `bool`, `string` or `void`), found `" +
             tok_desc(peek().t) + "`");
}

std::unique_ptr<BlockStmt> Parser::parse_block() {
    Loc l = peek().loc;
    expect(Tok::LBrace, "`{`");
    auto b = mk<BlockStmt>(l);
    while (!is(Tok::RBrace)) {
        if (is(Tok::Eof))
            fail("E0101", peek().loc,
                 "unexpected end of file — unclosed `{` block");
        b->stmts.push_back(parse_stmt());
    }
    expect(Tok::RBrace, "`}`");
    return b;
}

std::unique_ptr<FnDecl> Parser::parse_fn() {
    Loc l = peek().loc;
    expect(Tok::KwFn, "`fn`");
    Token name = expect(Tok::Ident, "a function name");
    auto f = std::make_unique<FnDecl>();
    f->name = name.text;
    f->loc = l;
    expect(Tok::LParen, "`(`");
    if (!is(Tok::RParen)) {
        while (true) {
            Token pn = expect(Tok::Ident, "a parameter name");
            expect(Tok::Colon, "`:`");
            Ty t = parse_type(false);
            f->params.push_back(Param{pn.text, t, pn.loc});
            if (!eat(Tok::Comma)) break;
        }
    }
    expect(Tok::RParen, "`)`");
    if (eat(Tok::Arrow)) f->ret = parse_type(true);
    else f->ret = Ty::Void;
    f->body = parse_block();
    return f;
}

std::unique_ptr<Stmt> Parser::parse_stmt() {
    Loc l = peek().loc;
    if (is(Tok::KwLet)) {
        advance();
        bool is_mut = eat(Tok::KwMut);
        Token name = expect(Tok::Ident, "a variable name");
        expect(Tok::Colon, "`:`");
        Ty ty = parse_type(false);
        if (is(Tok::Semi))
            fail("E0102", peek().loc,
                 "variable `" + name.text + "` must be initialized",
                 "uninitialized variables are a source of bugs, and Vela "
                 "does not forgive bugs. Write `let " + name.text + ": " +
                 ty_name(ty) + " = <value>;`");
        expect(Tok::Assign, "`=`");
        auto init = parse_expr();
        expect(Tok::Semi, "`;`");
        auto st = mk<LetStmt>(l);
        st->name = name.text;
        st->name_loc = name.loc;
        st->type = ty;
        st->is_mut = is_mut;
        st->init = std::move(init);
        return st;
    }
    if (is(Tok::KwIf)) {
        advance();
        expect(Tok::LParen, "`(`");
        auto cond = parse_expr();
        expect(Tok::RParen, "`)`");
        auto st = mk<IfStmt>(l);
        st->cond = std::move(cond);
        st->then_b = parse_block();
        if (eat(Tok::KwElse)) {
            if (is(Tok::KwIf)) {
                auto inner = parse_stmt();
                auto blk = mk<BlockStmt>(inner->loc);
                blk->stmts.push_back(std::move(inner));
                st->else_b = std::move(blk);
            } else {
                st->else_b = parse_block();
            }
        }
        return st;
    }
    if (is(Tok::KwWhile)) {
        advance();
        expect(Tok::LParen, "`(`");
        auto cond = parse_expr();
        expect(Tok::RParen, "`)`");
        auto st = mk<WhileStmt>(l);
        st->cond = std::move(cond);
        st->body = parse_block();
        return st;
    }
    if (is(Tok::KwReturn)) {
        advance();
        auto st = mk<ReturnStmt>(l);
        if (is(Tok::Semi)) { advance(); return st; }
        st->has_value = true;
        st->value = parse_expr();
        expect(Tok::Semi, "`;`");
        return st;
    }
    if (is(Tok::LBrace)) return parse_block();
    if (is(Tok::Ident) && toks[p + 1].t == Tok::Assign) {
        Token name = advance();
        advance();  // =
        auto value = parse_expr();
        expect(Tok::Semi, "`;`");
        auto st = mk<AssignStmt>(l);
        st->name = name.text;
        st->name_loc = name.loc;
        st->value = std::move(value);
        return st;
    }
    // expression statement (e.g. a print call)
    auto expr = parse_expr();
    expect(Tok::Semi, "`;`");
    auto st = mk<ExprStmt>(l);
    st->expr = std::move(expr);
    return st;
}

std::unique_ptr<Expr> Parser::parse_expr() { return parse_or(); }

std::unique_ptr<Expr> Parser::parse_or() {
    auto lhs = parse_and();
    while (is(Tok::OrOr)) {
        Loc l = advance().loc;
        auto e = mk<BinaryExpr>(l);
        e->op = Tok::OrOr;
        e->lhs = std::move(lhs);
        e->rhs = parse_and();
        lhs = std::move(e);
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_and() {
    auto lhs = parse_cmp();
    while (is(Tok::AndAnd)) {
        Loc l = advance().loc;
        auto e = mk<BinaryExpr>(l);
        e->op = Tok::AndAnd;
        e->lhs = std::move(lhs);
        e->rhs = parse_cmp();
        lhs = std::move(e);
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_cmp() {
    auto lhs = parse_add();
    while (is(Tok::EqEq) || is(Tok::NotEq) || is(Tok::Lt) ||
           is(Tok::Le) || is(Tok::Gt) || is(Tok::Ge)) {
        Loc l = advance().loc;
        Tok op = toks[p - 1].t;
        auto e = mk<BinaryExpr>(l);
        e->op = op;
        e->lhs = std::move(lhs);
        e->rhs = parse_add();
        lhs = std::move(e);
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_add() {
    auto lhs = parse_mul();
    while (is(Tok::Plus) || is(Tok::Minus)) {
        Loc l = advance().loc;
        Tok op = toks[p - 1].t;
        auto e = mk<BinaryExpr>(l);
        e->op = op;
        e->lhs = std::move(lhs);
        e->rhs = parse_mul();
        lhs = std::move(e);
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_mul() {
    auto lhs = parse_unary();
    while (is(Tok::Star) || is(Tok::Slash) || is(Tok::Percent)) {
        Loc l = advance().loc;
        Tok op = toks[p - 1].t;
        auto e = mk<BinaryExpr>(l);
        e->op = op;
        e->lhs = std::move(lhs);
        e->rhs = parse_unary();
        lhs = std::move(e);
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (is(Tok::Not) || is(Tok::Minus)) {
        Loc l = advance().loc;
        Tok op = toks[p - 1].t;
        auto e = mk<UnaryExpr>(l);
        e->op = op;
        e->operand = parse_unary();
        return e;
    }
    return parse_primary();
}

std::unique_ptr<Expr> Parser::parse_primary() {
    Loc l = peek().loc;
    if (is(Tok::IntLit)) {
        auto e = mk<IntLitExpr>(l);
        e->value = advance().num;
        return e;
    }
    if (is(Tok::StrLit)) {
        auto e = mk<StrLitExpr>(l);
        e->value = advance().text;
        return e;
    }
    if (is(Tok::KwTrue) || is(Tok::KwFalse)) {
        auto e = mk<BoolLitExpr>(l);
        e->value = advance().t == Tok::KwTrue;
        return e;
    }
    if (is(Tok::LParen)) {
        advance();
        auto e = parse_expr();
        expect(Tok::RParen, "`)`");
        return e;
    }
    if (is(Tok::Ident)) {
        Token name = advance();
        if (is(Tok::LParen)) {
            advance();
            auto e = mk<CallExpr>(l);
            e->name = name.text;
            if (!is(Tok::RParen)) {
                while (true) {
                    e->args.push_back(parse_expr());
                    if (!eat(Tok::Comma)) break;
                }
            }
            expect(Tok::RParen, "`)`");
            return e;
        }
        auto e = mk<VarExpr>(l);
        e->name = name.text;
        return e;
    }
    fail("E0101", l, "expected an expression, found `" +
                         tok_desc(peek().t) + "`");
}
