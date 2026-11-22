#pragma once
#include "vela/ast.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class CodegenX86 {
public:
    explicit CodegenX86(Program& p) : prog(p) {}

    // Returns the full text of the program's assembly file.
    std::string emit();

private:
    Program& prog;

    std::string out;      // assembly is emitted here
    std::vector<std::unordered_map<std::string, int>> scopes; // name -> offset from %rbp
    int label_n = 0;
    int slot_n = 0;
    int depth = 0;          // number of pending 8-byte pushes within an expression
    std::string ep_label;   // epilogue label of the current function

    std::unordered_map<std::string, std::string> str_labels; // text -> label
    std::vector<std::pair<std::string, std::string>> str_data;

    std::string fresh_label(const char* hint);
    void push_scope();
    void pop_scope();
    int lookup(const std::string& n) const;
    int count_slots(const Stmt* s);
    void call_fn(const std::string& sym, const std::vector<Expr*>& args);
    void gen_expr(Expr* e);
    void gen_stmt(Stmt* s);
    void gen_block(BlockStmt* b);
    void emit_fn(FnDecl& f);
    static std::string escape_gas(const std::string& s);
    std::string emit_thunks();
    std::string emit_rodata();
};
