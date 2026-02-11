//
// Created by bucka on 2/9/2026.
//

#include "parser.h"

// Helper to extract SourceLocation from Token
#define TOK_LOC(tok) ((SourceLocation){.line = (tok)->line, .column = (tok)->column, .filename = (tok)->filename})

Token* peek(Parser* parser, int offset) {
    if (parser->pos + offset >= parser->count) {
        // Use last token's location if available
        Token* lastTok = parser->pos > 0 ? &parser->tokens[parser->pos - 1] : &parser->tokens[0];
        stage_fatal(STAGE_PARSER, TOK_LOC(lastTok),
                    "peek beyond token stream (pos=%d)", parser->pos);
    }

    return &parser->tokens[parser->pos + offset];
}
Token* consume(Parser* parser) {
    Token* tok = &parser->tokens[parser->pos++];
    stage_trace(STAGE_PARSER,
                "consume %s (pos=%d)",
                token_type_name(tok->type), parser->pos - 1);
    return tok;
}
Token* expect(Parser* parser, TokenType type) {
    Token *tok = &parser->tokens[parser->pos++];

    if (tok->type != type) {
        stage_fatal(STAGE_PARSER, TOK_LOC(tok),
                    "expected %s but found %s at token index %d",
                    token_type_name(type),
                    token_type_name(tok->type),
                    parser->pos - 1);
    }

    return tok;
}

Func** parseFunctions(Parser* p, int* num) {
    Func** functions = malloc(sizeof(Func) * 2);
    int size = 2;
    int count = 0;

    // Use the parser's internal position, not a local loop index
    while (p->pos < p->count && peek(p, 0)->type == DEF_KEYWORD_T) {
        consume(p); // consume 'def'
        Token* name = expect(p, VAR_T);
        expect(p, L_PAREN_T);

        int pCount = 0;
        // Pass p, let it handle its own pos
        FuncParam* params = parseFuncParams(p, &pCount);

        expect(p, R_PAREN_T);
        expect(p, COLON_T);

        Token* ret = consume(p); // The return type keyword
        Stmt* body = parseBlock(p);

        functions[count++] = makeFunc(name->value, params, pCount, ret->type, body);

        if(count >= size) {
            size *= 2;
            functions = realloc(functions, sizeof(Func) * size);
        }
    }

    *num = count;
    return functions;
}

Expr* parseExpr(Parser* p) {
    Expr* e = parseAnd(p);
    while (peek(p, 0)->type == OR_T) {
        Token* op = consume(p);
        Expr* right = parseAnd(p);
        e = makeBinOp(TOK_LOC(op), e, op->type, right);
    }
    return e;
}
Expr* parseAnd(Parser* p) {
    Expr* e = parseComparison(p);
    while (peek(p, 0)->type == AND_T) {
        Token* op = consume(p);
        Expr* right = parseComparison(p);
        e = makeBinOp(TOK_LOC(op), e, op->type, right);
    }
    return e;
}
Expr* parseComparison(Parser* p) {
    Expr* e = parseAdd(p);
    TokenType t = peek(p, 0)->type;
    while (t == LESS_T || t == MORE_T || t == LESS_EQUALS_T ||
            t == MORE_EQUALS_T || t == DOUBLE_EQUALS_T || t == NOT_EQUALS_T) {
        Token* op = consume(p);
        Expr* right = parseAdd(p);
        e = makeBinOp(TOK_LOC(op), e, op->type, right);
        t = peek(p, 0)->type;
    }
    return e;
}
Expr* parseAdd(Parser* p) {
    Expr* e = parseTerm(p);
    TokenType t = peek(p, 0)->type;
    while (t == PLUS_T || t == MINUS_T) {
        Token* op = consume(p);
        Expr* right = parseTerm(p);
        e = makeBinOp(TOK_LOC(op), e, op->type, right);
        t = peek(p, 0)->type;
    }
    return e;
}
Expr* parseTerm(Parser* p) {
    Expr* e = parseFactor(p);
    TokenType t = peek(p, 0)->type;
    while (t == STAR_T || t == SLASH_T) {
        Token* op = consume(p);
        Expr* right = parseFactor(p);
        e = makeBinOp(TOK_LOC(op), e, op->type, right);
        t = peek(p, 0)->type;
    }
    return e;
}
Expr* parseFactor(Parser* p) {
    Token* tok = peek(p, 0);

    switch (tok->type) {
        case INT_LIT_T: {
            Token* t = consume(p);
            return makeIntLit(TOK_LOC(t), *(int*)t->value);
        }
        case BOOL_LIT_T: {
            Token* t = consume(p);
            return makeBoolLit(TOK_LOC(t), *(int*)t->value);
        }
        case VAR_T: {
            Token* t = consume(p);
            if (peek(p, 0)->type == L_PAREN_T) {
                expect(p, L_PAREN_T);
                Expr** args = malloc(sizeof(Expr*) * 2);
                int count = 0;
                int capacity = 2;
                while (peek(p, 0)->type != R_PAREN_T) {
                    if (count > 0) expect(p, COMMA_T);
                    args[count++] = parseExpr(p);
                    if (count >= capacity) {
                        capacity *= 2;
                        args = realloc(args, sizeof(Expr*) * capacity);
                    }
                }
                expect(p, R_PAREN_T);
                return makeFuncCall(TOK_LOC(t), t->value, args, count);
            }
            return makeVar(TOK_LOC(t), (char*)t->value);
        }
        case UNDERSCORE_T: {
            Token* t = consume(p);
            Expr* e = malloc(sizeof(Expr));
            e->type = VOID_E;
            e->loc = TOK_LOC(t);
            return e;
        }
        case MINUS_T: {
            Token* t = consume(p);
            return makeUnOp(TOK_LOC(t), MINUS_T, parseFactor(p));
        }
        case NEGATION_T: {
            Token* t = consume(p);
            return makeUnOp(TOK_LOC(t), NEGATION_T, parseFactor(p));
        }
        case L_PAREN_T: {
            consume(p);  // eat '('
            Expr* expr = parseExpr(p);
            expect(p, R_PAREN_T);  // eat ')'
            return expr;
        }
        case MATCH_T: {
            Token* matchTok = consume(p);
            Expr* target = parseExpr(p);
            expect(p, L_BRACE_T);

            MatchBranchExpr* branches = malloc(sizeof(MatchBranchExpr) * 2);
            int count = 0;
            int capacity = 2;
            while (peek(p, 0)->type != R_BRACE_T) {
                Expr* branchCond = parseExpr(p);
                expect(p, COLON_T);
                Expr* branchBody = parseExpr(p);
                expect(p, SEMICOLON_T);
                branches[count++] = (MatchBranchExpr){.caseExpr = branchCond, .caseRet = branchBody};

                if (count >= capacity) {
                    capacity *= 2;
                    branches = realloc(branches, sizeof(MatchBranchExpr) * capacity);
                }
            }

            expect(p, R_BRACE_T);
            // no semicolon here — caller handles it

            Expr* e = malloc(sizeof(Expr));
            e->type = MATCH_E;
            e->loc = TOK_LOC(matchTok);
            e->as.match.var = target;
            e->as.match.branches = branches;
            e->as.match.branchCount = count;
            return e;
        }
        case RETURN_T: {
            Token* retTok = consume(p);
            Expr* e = malloc(sizeof(Expr));
            e->type = FUNC_RET_E;
            e->loc = TOK_LOC(retTok);
            e->as.func_ret_expr = parseExpr(p);
            return e;
        }
        case ALLOC_T: {
            Token* allocTok = consume(p);
            Expr* e = parseExpr(p);
            Expr* al = malloc(sizeof(Expr));
            al->type = ALLOC_E;
            al->loc = TOK_LOC(allocTok);
            al->as.alloc.initialValue = e;
            return al;
        }

        default:
            stage_fatal(STAGE_PARSER, TOK_LOC(tok), "Unexpected token %s in expression", token_type_name(tok->type));
            return nullptr;
    }
}

Func** parseProgram(Parser* p, int* length) {
    stage_debug(STAGE_PARSER, "parse program begin");

    int* funcCount = malloc(sizeof(int));
    *funcCount = 0;
    Func** funcs = parseFunctions(p, funcCount);

    //end of file
    expect(p, EOF_T);

    *length = *funcCount;
    free(funcCount);

    stage_debug(STAGE_PARSER, "parse program end");

    return funcs;
}
Stmt* parseStatement(Parser* p) {
    Token* t = peek(p, 0);

    stage_debug(STAGE_PARSER,
                "parse statement starting with %s",
                token_type_name(t->type));

    switch (t->type) {
        case VAR_T: {
            Stmt *s = malloc(sizeof(Stmt));
            if (peek(p, 1)->type == COLON_T) {
                Ownership o = OWNERSHIP_NONE;
                Token* varTok = consume(p);
                char *name = varTok->value;
                expect(p, COLON_T); //:

                if (peek(p, 0)->type == OWN_T) {
                    consume(p);
                    o = OWNERSHIP_OWN;
                } else if (peek(p, 0)->type == REF_T) {
                    consume(p);
                    o = OWNERSHIP_REF;
                }

                bool isNullable = false;
                if (peek(p, 0)->type == QUESTION_MARK_T) {
                    if(o == OWNERSHIP_NONE)
                        stage_fatal(STAGE_PARSER, TOK_LOC(peek(p, 0)), "Non-pointer nullable variable not allowed!");
                    consume(p);
                    isNullable = true;
                }

                TokenType varType = consume(p)->type;
                expect(p, EQUALS_T);
                Expr *e = parseExpr(p);
                expect(p, SEMICOLON_T);

                s->type = VAR_DECL_S;
                s->loc = TOK_LOC(varTok);
                s->as.var_decl.expr = e;
                s->as.var_decl.varType = varType;
                s->as.var_decl.name = name;
                s->as.var_decl.ownership = o;
                s->as.var_decl.isNullable = isNullable;
            } else if (peek(p, 1)->type == EQUALS_T) {
                Token* varTok = consume(p);
                char *name = varTok->value;
                expect(p, EQUALS_T);
                Expr *e = parseExpr(p);
                expect(p, SEMICOLON_T);

                s->type = ASSIGN_S;
                s->loc = TOK_LOC(varTok);
                s->as.var_assign.name = name;
                s->as.var_assign.expr = e;
                s->as.var_assign.ownership = OWNERSHIP_NONE;
            } else if (peek(p, 1)->type == L_PAREN_T) {
                Expr* e = parseExpr(p);
                expect(p, SEMICOLON_T);
                s->type = EXPR_STMT_S;
                s->loc = e->loc;  // Use expression's location
                s->as.expr_stmt = e;
            } else stage_fatal(STAGE_PARSER, TOK_LOC(peek(p, 1)), "Unexpected token after variable: %s", token_type_name(peek(p, 1)->type));
            return s;
        }
        case IF_T: {
            Stmt* s = malloc(sizeof(Stmt));
            Token* ifTok = consume(p);
            expect(p, L_PAREN_T);
            Expr* c = parseExpr(p);
            expect(p, R_PAREN_T);
            Stmt* te = parseBlock(p);

            Stmt* fe = nullptr;
            if (peek(p, 0)->type == ELSE_T) {
                consume(p);  // consume 'else'
                fe = parseBlock(p);
            }

            s->type = IF_S;
            s->loc = TOK_LOC(ifTok);
            s->as.if_stmt.cond = c;
            s->as.if_stmt.trueStmt = te;
            s->as.if_stmt.falseStmt = fe;
            return s;
        }
        case WHILE_T: {
            Stmt* s = malloc(sizeof(Stmt));
            Token* whileTok = consume(p);
            expect(p, L_PAREN_T);
            Expr* c = parseExpr(p);
            expect(p, R_PAREN_T);
            Stmt* b = parseBlock(p);

            s->type = WHILE_S;
            s->loc = TOK_LOC(whileTok);
            s->as.while_stmt.cond = c;
            s->as.while_stmt.body = b;
            return s;
        }
        case DO_T: {
            Stmt* s = malloc(sizeof(Stmt));
            Token* doTok = consume(p);
            Stmt* b = parseBlock(p);
            expect(p, L_PAREN_T);
            Expr* c = parseExpr(p);
            expect(p, R_PAREN_T);

            s->type = DO_WHILE_S;
            s->loc = TOK_LOC(doTok);
            s->as.do_while_stmt.cond = c;
            s->as.do_while_stmt.body = b;
            return s;
        }
        case FOR_T: {
            Stmt* s = malloc(sizeof(Stmt));
            Token* forTok = consume(p);
            expect(p, L_PAREN_T);
            char* name = consume(p)->value;
            expect(p, COLON_T);
            Expr* minE = parseExpr(p);
            expect(p, TO_T);
            Expr* maxE = parseExpr(p);
            expect(p, R_PAREN_T);
            Stmt* b = parseBlock(p);

            s->type = FOR_S;
            s->loc = TOK_LOC(forTok);
            s->as.for_stmt.varName = name;
            s->as.for_stmt.min = minE;
            s->as.for_stmt.max = maxE;
            s->as.for_stmt.body = b;
            return s;
        }
        case RETURN_T: {
            Stmt* s = malloc(sizeof(Stmt));
            Expr* e = parseExpr(p);
            expect(p, SEMICOLON_T);

            s->type = EXPR_STMT_S;
            s->loc = e->loc;  // Use expression's location
            s->as.expr_stmt = e;
            return s;
        }
        case MATCH_T: {
            Stmt* s = malloc(sizeof(Stmt));
            Token* matchTok = consume(p);
            Expr* var = parseExpr(p);
            expect(p, L_BRACE_T);

            int bCount = 0;
            int bSize = 2;
            MatchBranchStmt* branches = malloc(sizeof(MatchBranchStmt) * 2);

            while (peek(p, 0)->type != R_BRACE_T) {
                Expr* caseExpr = parseExpr(p);
                expect(p, COLON_T);
                expect(p, L_BRACE_T);

                int stmtCount = 0;
                int stmtsSize = 2;
                branches[bCount].stmts = malloc(sizeof(Stmt*) * 2);
                while (peek(p, 0)->type != R_BRACE_T) {
                    branches[bCount].stmts[stmtCount++] = parseStatement(p);

                    if(stmtCount >= stmtsSize) {
                        stmtsSize *= 2;
                        branches = realloc(branches, sizeof(MatchBranchStmt) * bSize);
                    }
                }
                expect(p, R_BRACE_T);

                branches[bCount].caseExpr = caseExpr;
                branches[bCount++].stmtCount = stmtCount;

                if (bCount >= bSize) {
                    bSize *= 2;
                    branches = realloc(branches, sizeof(MatchBranchStmt) * bSize);
                }
            }

            expect(p, R_BRACE_T);
            expect(p, SEMICOLON_T);

            s->type = MATCH_S;
            s->loc = TOK_LOC(matchTok);
            s->as.match_stmt.var = var;
            s->as.match_stmt.branches = branches;
            s->as.match_stmt.branchCount = bCount;
            return s;
        }
        case FREE_T: {
            Token* freeTok = consume(p);
            Token* var = expect(p, VAR_T);
            Stmt* s = malloc(sizeof(Stmt));
            s->type = FREE_S;
            s->loc = TOK_LOC(freeTok);
            s->as.free_stmt.varName = var->value;
            expect(p, SEMICOLON_T);
            return s;
        }
        default:
            stage_fatal(STAGE_PARSER, TOK_LOC(t), "Unexpected token %s at start of statement", token_type_name(t->type));
            return nullptr;
    }
}
Stmt* parseBlock(Parser* p) {
    Stmt** stmt = malloc(sizeof(Stmt*) * 2);
    int count = 0;
    int size = 2;
    Token* lbrace = expect(p, L_BRACE_T);
    while (peek(p, 0)->type != R_BRACE_T) {
        Stmt* s = parseStatement(p);
        stmt[count] = s;
        count++;
        if(count >= size)
        {
            stmt = realloc(stmt, sizeof(Stmt*) * size * 2);
            size *= 2;
        }
    }
    expect(p, R_BRACE_T);
    return makeBlock(TOK_LOC(lbrace), stmt, count);
}
FuncParam* parseFuncParams(Parser* p, int* count) {
    FuncParam* fps = malloc(sizeof(FuncParam));
    int counter = 0;
    while (peek(p, 0)->type != R_PAREN_T) {
        Token* t = consume(p);
        if(t->type != VAR_T) stage_fatal(STAGE_PARSER, TOK_LOC(t), "Expected %s in function parameter number %d, but got %s",
                                         token_type_name(VAR_T), counter, token_type_name(t->type));
        expect(p, COLON_T);
        Ownership o = OWNERSHIP_NONE;
        if(peek(p, 0)->type == OWN_T) {
            consume(p);
            o = OWNERSHIP_OWN;
        }
        else if(peek(p, 0)->type == REF_T) {
            consume(p);
            o = OWNERSHIP_REF;
        }
        Token* type = consume(p);
        FuncParam fp = (FuncParam){.type = type->type, .name = t->value, .ownership = o};
        fps = realloc(fps, sizeof(FuncParam) * ++counter);
        fps[counter - 1] = fp;
        if(peek(p, 0)->type != R_PAREN_T) expect(p, COMMA_T);
    }

    *count = counter;
    return fps;
}

// construction helpers
Expr* makeIntLit(SourceLocation loc, int val) {
    Expr* e = malloc(sizeof(Expr));
    e->type = INT_LIT_E;
    e->loc = loc;
    e->as.int_val = val;
    return e;
}
Expr* makeBoolLit(SourceLocation loc, bool val) {
    Expr* e = malloc(sizeof(Expr));
    e->type = BOOL_LIT_E;
    e->loc = loc;
    e->as.bool_val = val;
    return e;
}
Expr* makeVar(SourceLocation loc, char* name) {
    Expr* e = malloc(sizeof(Expr));
    e->type = VAR_E;
    e->loc = loc;
    e->as.var.name = name;
    e->as.var.ownership = OWNERSHIP_NONE;
    return e;
}
Expr* makeUnOp(SourceLocation loc, TokenType t, Expr* expr) {
    Expr* e = malloc(sizeof(Expr));
    e->type = UN_OP_E;
    e->loc = loc;
    e->as.un_op.expr = expr;
    e->as.un_op.op = t;
    return e;
}
Expr* makeBinOp(SourceLocation loc, Expr* el, TokenType t, Expr* er) {
    Expr* e = malloc(sizeof(Expr));
    e->type = BIN_OP_E;
    e->loc = loc;
    e->as.bin_op.op = t;
    e->as.bin_op.exprL = el;
    e->as.bin_op.exprR = er;
    return e;
}
Expr* makeFuncCall(SourceLocation loc, char* n, Expr** params, int paramC) {
    Expr* e = malloc(sizeof(Expr));
    e->type = FUNC_CALL_E;
    e->loc = loc;
    e->as.func_call.name = n;
    e->as.func_call.params = params;
    e->as.func_call.count = paramC;
    return e;
}

Stmt* makeVarDecl(SourceLocation loc, char* n, TokenType t, Expr* e) {
    Stmt* s = malloc(sizeof(Stmt));
    s->type = VAR_DECL_S;
    s->loc = loc;
    s->as.var_decl.name = n;
    s->as.var_decl.varType = t;
    s->as.var_decl.expr = e;
    return s;
}
Stmt* makeAssign(SourceLocation loc, char* n, Expr* e) {
    Stmt* s = malloc(sizeof(Stmt));
    s->type = ASSIGN_S;
    s->loc = loc;
    s->as.var_assign.name = n;
    s->as.var_assign.expr = e;
    return s;
}
Stmt* makeIf(SourceLocation loc, Expr* c, Stmt* t, Stmt* f) {
    Stmt* s = malloc(sizeof(Stmt));
    s->type = IF_S;
    s->loc = loc;
    s->as.if_stmt.cond = c;
    s->as.if_stmt.trueStmt = t;
    s->as.if_stmt.falseStmt = f;
    return s;
}
Stmt* makeWhile(SourceLocation loc, Expr* c, Stmt* b){
    Stmt* s = malloc(sizeof(Stmt));
    s->type = WHILE_S;
    s->loc = loc;
    s->as.while_stmt.cond = c;
    s->as.while_stmt.body = b;
    return s;
}
Stmt* makeBlock(SourceLocation loc, Stmt** stmts, int c) {
    Stmt* s = malloc(sizeof(Stmt));
    s->type = BLOCK_S;
    s->loc = loc;
    s->as.block_stmt.stmts = stmts;
    s->as.block_stmt.count = c;
    return s;
}
Stmt* makeExprStmt(SourceLocation loc, Expr* e) {
    Stmt* s = malloc(sizeof(Stmt));
    s->type = EXPR_STMT_S;
    s->loc = loc;
    s->as.expr_stmt = e;
    return s;
}

Func* makeFunc(char* name, FuncParam* params, int paramCount, TokenType ret, Stmt* body) {
    Func* f = malloc(sizeof(Func));
    f->body = body;
    f->signature = malloc(sizeof(FuncSign));
    f->signature->name = name;
    f->signature->parameters = params;
    f->signature->paramNum = paramCount;
    f->signature->retType = ret;
    return f;
}
bool checkFuncSign(FuncSign* a, FuncSign* b) {
    if(a->paramNum != b->paramNum)
        return false;

    for (int i = 0; i < a->paramNum; ++i) {
        if(a->parameters[i].type != b->parameters[i].type)
            return false;
    }

    return (a->retType == b->retType && a->name == b->name);
}

// AST printing functions
void print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
}

void print_expr(Expr* e, int depth);
void print_stmt(Stmt* s, int depth);

void print_expr(Expr* e, int depth) {
    if (e == NULL) {
        print_indent(depth);
        printf("NULL\n");
        return;
    }

    print_indent(depth);
    switch (e->type) {
        case INT_LIT_E:
            printf("IntLit: %d\n", e->as.int_val);
            break;
        case BOOL_LIT_E:
            printf("BoolLit: %s\n", e->as.bool_val ? "true" : "false");
            break;
        case VAR_E:
            printf("Var: %s\n", e->as.var);
            break;
        case UN_OP_E:
            printf("UnaryOp: %s\n", token_type_name(e->as.un_op.op));
            print_expr(e->as.un_op.expr, depth + 1);
            break;
        case BIN_OP_E:
            printf("BinaryOp: %s\n", token_type_name(e->as.bin_op.op));
            print_indent(depth);
            printf("Left:\n");
            print_expr(e->as.bin_op.exprL, depth + 1);
            print_indent(depth);
            printf("Right:\n");
            print_expr(e->as.bin_op.exprR, depth + 1);
            break;
    }
}

void print_stmt(Stmt* s, int depth) {
    if (s == NULL) {
        print_indent(depth);
        printf("NULL\n");
        return;
    }

    print_indent(depth);
    switch (s->type) {
        case VAR_DECL_S:
            printf("VarDecl: %s : %s\n", s->as.var_decl.name,
                   token_type_name(s->as.var_decl.varType));
            print_indent(depth);
            printf("Init:\n");
            print_expr(s->as.var_decl.expr, depth + 1);
            break;

        case ASSIGN_S:
            printf("Assign: %s\n", s->as.var_assign.name);
            print_indent(depth);
            printf("Value:\n");
            print_expr(s->as.var_assign.expr, depth + 1);
            break;

        case IF_S:
            printf("If:\n");
            print_indent(depth);
            printf("Condition:\n");
            print_expr(s->as.if_stmt.cond, depth + 1);
            print_indent(depth);
            printf("Then:\n");
            print_stmt(s->as.if_stmt.trueStmt, depth + 1);

            // Only print else if it exists
            if (s->as.if_stmt.falseStmt != NULL) {
                print_indent(depth);
                printf("Else:\n");
                print_stmt(s->as.if_stmt.falseStmt, depth + 1);
            }
            break;

        case WHILE_S:
            printf("While:\n");
            print_indent(depth);
            printf("Condition:\n");
            print_expr(s->as.while_stmt.cond, depth + 1);
            print_indent(depth);
            printf("Body:\n");
            print_stmt(s->as.while_stmt.body, depth + 1);
            break;

        case DO_WHILE_S:
            printf("DoWhile:\n");
            print_indent(depth);
            printf("Body:\n");
            print_stmt(s->as.do_while_stmt.body, depth + 1);
            print_indent(depth);
            printf("Condition:\n");
            print_expr(s->as.do_while_stmt.cond, depth + 1);
            break;

        case FOR_S:
            printf("For: %s\n", s->as.for_stmt.varName);
            print_indent(depth);
            printf("Min:\n");
            print_expr(s->as.for_stmt.min, depth + 1);
            print_indent(depth);
            printf("Max:\n");
            print_expr(s->as.for_stmt.max, depth + 1);
            print_indent(depth);
            printf("Body:\n");
            print_stmt(s->as.for_stmt.body, depth + 1);
            break;

        case BLOCK_S:
            printf("Block (%d statements):\n", s->as.block_stmt.count);
            for (int i = 0; i < s->as.block_stmt.count; i++) {
                print_stmt(s->as.block_stmt.stmts[i], depth + 1);
            }
            break;

        case EXPR_STMT_S:
            printf("ExprStmt:\n");
            print_expr(s->as.expr_stmt, depth + 1);
            break;
    }
}

void print_ast(Stmt** program, int count) {
    printf("\n=== AST ===\n");
    for (int i = 0; i < count; i++) {
        printf("Top-level statement %d:\n", i);
        print_stmt(program[i], 1);
    }
    printf("===========\n");
}