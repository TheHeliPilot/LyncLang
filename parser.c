//
// Created by bucka on 2/9/2026.
//

#include "parser.h"

#define TOK_LOC(tok) ((SourceLocation){.line = (tok)->line, .column = (tok)->column, .filename = (tok)->filename})

Pattern* parsePattern(Parser* p);

Token* peek(Parser* parser, int offset) {
    if (parser->pos + offset >= parser->count) {
        //use last tokens location if available
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

    while (p->pos < p->count && peek(p, 0)->type == DEF_KEYWORD_T) {
        consume(p); // consume 'def'
        Token* name = expect(p, VAR_T);
        expect(p, L_PAREN_T);

        int pCount = 0;
        FuncParam* params = parseFuncParams(p, &pCount);

        expect(p, R_PAREN_T);
        expect(p, COLON_T);

        Token* ret = consume(p);
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
        case STR_LIT_T: {
            Token* t = consume(p);
            return makeStrLit(TOK_LOC(t), (char*)t->value);
        }
        case NULL_LIT_T: {
            Token* t = consume(p);
            return makeNullLit(TOK_LOC(t));
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
            e->is_nullable = false;
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
                Pattern* pattern = parsePattern(p);
                expect(p, COLON_T);
                Expr* branchBody = parseExpr(p);
                expect(p, SEMICOLON_T);
                branches[count++] = (MatchBranchExpr){.pattern = pattern, .caseRet = branchBody};

                if (count >= capacity) {
                    capacity *= 2;
                    branches = realloc(branches, sizeof(MatchBranchExpr) * capacity);
                }
            }

            expect(p, R_BRACE_T);

            Expr* e = malloc(sizeof(Expr));
            e->type = MATCH_E;
            e->loc = TOK_LOC(matchTok);
            e->is_nullable = false;  //will be determined by analyzer
            e->as.match.var = target;
            e->as.match.branches = branches;
            e->as.match.branchCount = count;
            return e;
        }
        case SOME_T: {
            Expr* e = malloc(sizeof(Expr));

            consume(p);
            expect(p, L_PAREN_T);
            Expr* v = parseExpr(p);
            expect(p, R_PAREN_T);

            e->type = SOME_E;
            e->loc = TOK_LOC(peek(p, -4));  //some token location
            e->is_nullable = false;
            e->as.match.var = v;
            return e;
        }
        case RETURN_T: {
            Token* retTok = consume(p);
            Expr* e = malloc(sizeof(Expr));
            e->type = FUNC_RET_E;
            e->loc = TOK_LOC(retTok);
            e->is_nullable = false;
            if(peek(p, 0)->type == SEMICOLON_T){
                Expr* ve = malloc(sizeof(Expr));
                ve->type = VOID_E;
                ve->loc = TOK_LOC(retTok);
                ve->is_nullable = false;
                e->as.func_ret_expr = ve;
            } else
                e->as.func_ret_expr = parseExpr(p);
            return e;
        }
        case ALLOC_T: {
            Token* allocTok = consume(p);
            Expr* e = parseExpr(p);
            Expr* al = malloc(sizeof(Expr));
            al->type = ALLOC_E;
            al->loc = TOK_LOC(allocTok);
            al->is_nullable = false;  //alloc always returns a pointer
            al->as.alloc.initialValue = e;
            return al;
        }

        default:
            stage_fatal(STAGE_PARSER, TOK_LOC(tok), "Unexpected token %s in expression", token_type_name(tok->type));
    }
}

IncludeStmt* parseIncludeStmt(Parser* p) {
    IncludeStmt* stmt = malloc(sizeof(IncludeStmt));
    Token* usingTok = expect(p, INCLUDE_T);
    stmt->loc = TOK_LOC(usingTok);

    //parse: std.io.* or std.io.read_int
    //module is everything before the last dot, last part is * or function name

    char** parts = malloc(sizeof(char*) * 10);
    int part_count = 0;
    int part_capacity = 10;

    // Collect all identifiers
    parts[part_count++] = expect(p, VAR_T)->value;

    while (peek(p, 0)->type == DOT_T) {
        consume(p);  // consume .

        if (peek(p, 0)->type == STAR_T) {
            //wildcard: everything so far is the module
            consume(p);

            // Build module name from all parts
            char* module = parts[0];
            for (int i = 1; i < part_count; i++) {
                char* new_module = malloc(strlen(module) + strlen(parts[i]) + 2);
                sprintf(new_module, "%s.%s", module, parts[i]);
                module = new_module;
            }

            stmt->module_name = module;
            stmt->type = IMPORT_ALL;
            stmt->function_name = NULL;
            free(parts);
            expect(p, SEMICOLON_T);
            return stmt;
        } else if (peek(p, 0)->type == VAR_T) {
            if (part_count >= part_capacity) {
                part_capacity *= 2;
                parts = realloc(parts, sizeof(char*) * part_capacity);
            }
            parts[part_count++] = consume(p)->value;
        } else {
            stage_fatal(STAGE_PARSER, stmt->loc, "expected identifier or '*' after '.'");
        }
    }

    //specific import: last part is function, rest is module
    if (part_count < 2) {
        stage_fatal(STAGE_PARSER, stmt->loc,
            "invalid import: expected 'module.function' or 'module.*' (e.g., 'std.io.read_int')");
    }

    //build module from all but last part
    char* module = parts[0];
    for (int i = 1; i < part_count - 1; i++) {
        char* new_module = malloc(strlen(module) + strlen(parts[i]) + 2);
        sprintf(new_module, "%s.%s", module, parts[i]);
        module = new_module;
    }

    stmt->module_name = module;
    stmt->type = IMPORT_SPECIFIC;
    stmt->function_name = parts[part_count - 1];

    free(parts);
    expect(p, SEMICOLON_T);
    return stmt;
}

Program* parseProgram(Parser* p) {
    stage_trace(STAGE_PARSER, "parse program begin");

    Program* prog = malloc(sizeof(Program));
    prog->imports = malloc(sizeof(ImportList));
    prog->imports->import_capacity = 10;
    prog->imports->imports = malloc(sizeof(IncludeStmt*) * prog->imports->import_capacity);
    prog->imports->import_count = 0;

    while (peek(p, 0)->type == INCLUDE_T) {
        if (prog->imports->import_count >= prog->imports->import_capacity) {
            prog->imports->import_capacity *= 2;
            prog->imports->imports = realloc(prog->imports->imports,
                sizeof(IncludeStmt*) * prog->imports->import_capacity);
        }
        prog->imports->imports[prog->imports->import_count++] = parseIncludeStmt(p);
    }

    int funcCount = 0;
    prog->functions = parseFunctions(p, &funcCount);
    prog->func_count = funcCount;

    //end of file
    expect(p, EOF_T);

    stage_trace(STAGE_PARSER, "parse program end");

    return prog;
}

Pattern* parsePattern(Parser* p) {
    Pattern* pattern = malloc(sizeof(Pattern));
    Token* tok = peek(p, 0);
    pattern->loc = TOK_LOC(tok);

    switch (tok->type) {
        case NULL_LIT_T:
            consume(p);
            pattern->type = NULL_PATTERN;
            break;
        case UNDERSCORE_T:
            consume(p);
            pattern->type = WILDCARD_PATTERN;
            break;
        case SOME_T: {
            consume(p);
            expect(p, L_PAREN_T);
            Token* bindingTok = expect(p, VAR_T);
            expect(p, R_PAREN_T);
            pattern->type = SOME_PATTERN;
            pattern->as.binding_name = bindingTok->value;
            break;
        }
        default:
            pattern->type = VALUE_PATTERN;
            pattern->as.value_expr = parseExpr(p);
            break;
    }

    return pattern;
}

Stmt* parseStatement(Parser* p) {
    Token* t = peek(p, 0);

    stage_trace(STAGE_PARSER,
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
            expect(p, WHILE_T);
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
        case MATCH_T: {
            Stmt* s = malloc(sizeof(Stmt));
            Token* matchTok = consume(p);
            Expr* var = parseExpr(p);
            expect(p, L_BRACE_T);

            int bCount = 0;
            int bSize = 2;
            MatchBranchStmt* branches = malloc(sizeof(MatchBranchStmt) * 2);

            while (peek(p, 0)->type != R_BRACE_T) {
                Pattern* pattern = parsePattern(p);
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

                branches[bCount].pattern = pattern;
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
        case RETURN_T: {
            // Parse as expression statement
            Expr* e = parseExpr(p);
            expect(p, SEMICOLON_T);

            Stmt* s = malloc(sizeof(Stmt));
            s->type = EXPR_STMT_S;
            s->loc = e->loc;
            s->as.expr_stmt = e;
            return s;
        }
        default:
            stage_fatal(STAGE_PARSER, TOK_LOC(t), "Unexpected token %s at start of statement", token_type_name(t->type));
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

    if (peek(p, 0)->type == R_PAREN_T) {
        *count = 0;
        return fps;
    }

    while (peek(p, 0)->type != R_PAREN_T) {
        if (counter > 0) {
            expect(p, COMMA_T);
        }

        Token* t = consume(p);
        if(t->type != VAR_T) {
            stage_fatal(STAGE_PARSER, TOK_LOC(t),
                        "Expected identifier in function parameter number %d, but got %s",
                        counter + 1, token_type_name(t->type));
        }

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

        bool isNullable = false;
        if(peek(p, 0)->type == QUESTION_MARK_T) {
            consume(p);
            isNullable = true;
        }

        Token* type = consume(p);
        FuncParam fp = (FuncParam){.type = type->type, .name = t->value, .ownership = o, .isNullable = isNullable};
        fps = realloc(fps, sizeof(FuncParam) * (counter + 1));
        fps[counter] = fp;
        counter++;
    }

    *count = counter;
    return fps;
}

//construction helpers
Expr* makeIntLit(SourceLocation loc, int val) {
    Expr* e = malloc(sizeof(Expr));
    e->type = INT_LIT_E;
    e->loc = loc;
    e->is_nullable = false;
    e->as.int_val = val;
    return e;
}
Expr* makeBoolLit(SourceLocation loc, bool val) {
    Expr* e = malloc(sizeof(Expr));
    e->type = BOOL_LIT_E;
    e->loc = loc;
    e->is_nullable = false;
    e->as.bool_val = val;
    return e;
}
Expr* makeStrLit(SourceLocation loc, char* val) {
    Expr* e = malloc(sizeof(Expr));
    e->type = STR_LIT_E;
    e->loc = loc;
    e->is_nullable = false;
    e->as.str_val = val;
    return e;
}
Expr* makeNullLit(SourceLocation loc) {
    Expr* e = malloc(sizeof(Expr));
    e->type = NULL_LIT_E;
    e->loc = loc;
    e->is_nullable = true;  // null is always nullable
    return e;
}
Expr* makeVar(SourceLocation loc, char* name) {
    Expr* e = malloc(sizeof(Expr));
    e->type = VAR_E;
    e->loc = loc;
    e->is_nullable = false;  // will be determined by analyzer
    e->as.var.name = name;
    e->as.var.ownership = OWNERSHIP_NONE;
    return e;
}
Expr* makeUnOp(SourceLocation loc, TokenType t, Expr* expr) {
    Expr* e = malloc(sizeof(Expr));
    e->type = UN_OP_E;
    e->loc = loc;
    e->is_nullable = false;
    e->as.un_op.expr = expr;
    e->as.un_op.op = t;
    return e;
}
Expr* makeBinOp(SourceLocation loc, Expr* el, TokenType t, Expr* er) {
    Expr* e = malloc(sizeof(Expr));
    e->type = BIN_OP_E;
    e->loc = loc;
    e->is_nullable = false;
    e->as.bin_op.op = t;
    e->as.bin_op.exprL = el;
    e->as.bin_op.exprR = er;
    return e;
}
Expr* makeFuncCall(SourceLocation loc, char* n, Expr** params, int paramC) {
    Expr* e = malloc(sizeof(Expr));
    e->type = FUNC_CALL_E;
    e->loc = loc;
    e->is_nullable = false;  // will be determined by analyzer for read_* functions
    e->as.func_call.name = n;
    e->as.func_call.params = params;
    e->as.func_call.count = paramC;
    e->as.func_call.resolved_sign = nullptr;
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
bool check_func_sign(FuncSign* a, FuncSign* b) {
    if(a->paramNum != b->paramNum)
        return false;

    for (int i = 0; i < a->paramNum; ++i) {
        if(a->parameters[i].type != b->parameters[i].type)
            return false;
    }

    if (a->retType != b->retType) return false;
    return (a->retType == b->retType && strcmp(a->name, b->name) == 0);
}

bool check_func_sign_unwrapped(FuncSign* a, char* name, int paramNum, Expr** parameters) {
    if(a->paramNum != paramNum) { return false; }

    for (int i = 0; i < a->paramNum; ++i) {
        if(a->parameters[i].type != parameters[i]->analyzedType) { return false; }
    }

    return strcmp(a->name, name) == 0;
}

//AST printing functions (only active in trace mode, output to stderr)
void print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        fprintf(stderr, "  ");
    }
}

void print_expr(Expr* e, int depth);
void print_stmt(Stmt* s, int depth);

void print_expr(Expr* e, int depth) {
    if (e == NULL) {
        print_indent(depth);
        fprintf(stderr, "NULL\n");
        return;
    }

    print_indent(depth);
    switch (e->type) {
        case INT_LIT_E:
            fprintf(stderr, "IntLit: %d\n", e->as.int_val);
            break;
        case BOOL_LIT_E:
            fprintf(stderr, "BoolLit: %s\n", e->as.bool_val ? "true" : "false");
            break;
        case VAR_E:
            fprintf(stderr, "Var: %s\n", e->as.var.name);
            break;
        case UN_OP_E:
            fprintf(stderr, "UnaryOp: %s\n", token_type_name(e->as.un_op.op));
            print_expr(e->as.un_op.expr, depth + 1);
            break;
        case BIN_OP_E:
            fprintf(stderr, "BinaryOp: %s\n", token_type_name(e->as.bin_op.op));
            print_indent(depth);
            fprintf(stderr, "Left:\n");
            print_expr(e->as.bin_op.exprL, depth + 1);
            print_indent(depth);
            fprintf(stderr, "Right:\n");
            print_expr(e->as.bin_op.exprR, depth + 1);
            break;
    }
}

void print_stmt(Stmt* s, int depth) {
    if (s == NULL) {
        print_indent(depth);
        fprintf(stderr, "NULL\n");
        return;
    }

    print_indent(depth);
    switch (s->type) {
        case VAR_DECL_S:
            fprintf(stderr, "VarDecl: %s : %s\n", s->as.var_decl.name,
                   token_type_name(s->as.var_decl.varType));
            print_indent(depth);
            fprintf(stderr, "Init:\n");
            print_expr(s->as.var_decl.expr, depth + 1);
            break;

        case ASSIGN_S:
            fprintf(stderr, "Assign: %s\n", s->as.var_assign.name);
            print_indent(depth);
            fprintf(stderr, "Value:\n");
            print_expr(s->as.var_assign.expr, depth + 1);
            break;

        case IF_S:
            fprintf(stderr, "If:\n");
            print_indent(depth);
            fprintf(stderr, "Condition:\n");
            print_expr(s->as.if_stmt.cond, depth + 1);
            print_indent(depth);
            fprintf(stderr, "Then:\n");
            print_stmt(s->as.if_stmt.trueStmt, depth + 1);

            // Only print else if it exists
            if (s->as.if_stmt.falseStmt != NULL) {
                print_indent(depth);
                fprintf(stderr, "Else:\n");
                print_stmt(s->as.if_stmt.falseStmt, depth + 1);
            }
            break;

        case WHILE_S:
            fprintf(stderr, "While:\n");
            print_indent(depth);
            fprintf(stderr, "Condition:\n");
            print_expr(s->as.while_stmt.cond, depth + 1);
            print_indent(depth);
            fprintf(stderr, "Body:\n");
            print_stmt(s->as.while_stmt.body, depth + 1);
            break;

        case DO_WHILE_S:
            fprintf(stderr, "DoWhile:\n");
            print_indent(depth);
            fprintf(stderr, "Body:\n");
            print_stmt(s->as.do_while_stmt.body, depth + 1);
            print_indent(depth);
            fprintf(stderr, "Condition:\n");
            print_expr(s->as.do_while_stmt.cond, depth + 1);
            break;

        case FOR_S:
            fprintf(stderr, "For: %s\n", s->as.for_stmt.varName);
            print_indent(depth);
            fprintf(stderr, "Min:\n");
            print_expr(s->as.for_stmt.min, depth + 1);
            print_indent(depth);
            fprintf(stderr, "Max:\n");
            print_expr(s->as.for_stmt.max, depth + 1);
            print_indent(depth);
            fprintf(stderr, "Body:\n");
            print_stmt(s->as.for_stmt.body, depth + 1);
            break;

        case BLOCK_S:
            fprintf(stderr, "Block (%d statements):\n", s->as.block_stmt.count);
            for (int i = 0; i < s->as.block_stmt.count; i++) {
                print_stmt(s->as.block_stmt.stmts[i], depth + 1);
            }
            break;

        case EXPR_STMT_S:
            fprintf(stderr, "ExprStmt:\n");
            print_expr(s->as.expr_stmt, depth + 1);
            break;
    }
}

void print_ast(Func** program, int count) {
    if (!g_trace_mode) return;
    fprintf(stderr, "\n=== AST (%d functions) ===\n", count);
    for (int i = 0; i < count; i++) {
        fprintf(stderr, "Function: %s\n", program[i]->signature->name);
        print_stmt(program[i]->body, 1);
    }
    fprintf(stderr, "===========\n");
}