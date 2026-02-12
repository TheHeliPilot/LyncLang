//
// Created by bucka on 2/9/2026.
//

#include "codegen.h"

char* type_to_c_type(TokenType t) {
    switch (t) {
        case INT_KEYWORD_T: return "int64_t";
        case BOOL_KEYWORD_T: return "bool";
        case VOID_KEYWORD_T: return "void";
        default: return "-???-";
    }
}

//emit indentation (2 spaces per level)
void emit_indent(FILE* out, int level) {
    for (int i = 0; i < level; i++) {
        fprintf(out, "  ");
    }
}

char* get_mangled_name(FuncSign* sign) {
    static char buffer[512];
    char* ptr = buffer;

    // Start with base name
    ptr += sprintf(ptr, "%s", sign->name);

    // Add return type
    ptr += sprintf(ptr, "_%s", token_type_name(sign->retType));

    // Add parameter types
    for (int i = 0; i < sign->paramNum; i++) {
        ptr += sprintf(ptr, "_%s", token_type_name(sign->parameters[i].type));
        if (sign->parameters[i].ownership != OWNERSHIP_NONE) {
            ptr += sprintf(ptr, "%s",
                           sign->parameters[i].ownership == OWNERSHIP_OWN ? "own" : "ref");
        }
    }

    return buffer;
}

// Hash-based version to keep names shorter
uint32_t hash_signature(FuncSign* sign) {
    uint32_t hash = 5381;

    // Hash name
    for (char* s = sign->name; *s; s++) {
        hash = ((hash << 5) + hash) + *s;
    }

    // Hash return type
    hash = ((hash << 5) + hash) + sign->retType;

    // Hash parameters
    for (int i = 0; i < sign->paramNum; i++) {
        hash = ((hash << 5) + hash) + sign->parameters[i].type;
        hash = ((hash << 5) + hash) + sign->parameters[i].ownership;
    }

    return hash;
}

char* get_mangled_name_short(FuncSign* sign) {
    static char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s_%x", sign->name, hash_signature(sign));
    return buffer;
}

char* get_func_name_from_sign(FuncSignToName* fstn, FuncSign* sign) {
    for (int i = 0; i < fstn->count; ++i) {
        if(check_func_sign(fstn->elements[i].sign, sign))
            return fstn->elements[i].name;
    }
    return "--NO_GOOD_SIGN--";
}

char* get_type_signature(FuncSign* sign) {
    static char buffer[256];
    char* ptr = buffer;

    //start with return type
    ptr += sprintf(ptr, "%s_", token_type_name(sign->retType));

    //add parameter types
    for (int i = 0; i < sign->paramNum; i++) {
        if (i > 0) ptr += sprintf(ptr, "_");
        ptr += sprintf(ptr, "%s", token_type_name(sign->parameters[i].type));
        if (sign->parameters[i].ownership != OWNERSHIP_NONE) {
            ptr += sprintf(ptr, "%s",
                           sign->parameters[i].ownership == OWNERSHIP_OWN ? "own" : "ref");
        }
    }

    return buffer;
}

void emit_func(Func* f, FILE* out, FuncSignToName* fstn) {
    if(strcmp(f->signature->name, "main") == 0) fprintf(out, "int");
    else fprintf(out, "%s", type_to_c_type(f->signature->retType));
    fprintf(out, " %s(", strcmp(f->signature->name, "main") == 0 ? "main" : get_func_name_from_sign(fstn, f->signature));

    for (int i = 0; i < f->signature->paramNum; ++i) {
        if(i > 0) fprintf(out, ", ");
        fprintf(out, "%s%s", type_to_c_type(f->signature->parameters[i].type), f->signature->parameters[i].ownership != OWNERSHIP_NONE ? "*" : "");
        fprintf(out, " %s", f->signature->parameters[i].name);
    }
    fprintf(out, ")\n");

    emit_stmt(f->body, out, 0, fstn);
}

void emit_func_decl(Func* f, FILE* out, FuncNameCounter* fnc, FuncSignToName* fstn) {
    if(strcmp(f->signature->name, "main") == 0) return;

    int funcNum = -1;
    char* origName = f->signature->name;

    for (int i = 0; i < fnc->count; ++i) {
        if (strcmp(fnc->elements[i].name, origName) == 0) {
            funcNum = ++fnc->elements[i].count;
            if(fnc->count >= fnc->height) {
                fnc->height *= 2;
                fnc->elements = realloc(fnc->elements, sizeof(FuncNameCounterElement) * fnc->height);
            }
        }
    }
    if(funcNum == -1) {
        fnc->elements[fnc->count++] = (FuncNameCounterElement){.count = 0, .name = origName};
        if(fnc->count >= fnc->height) {
            fnc->height *= 2;
            fnc->elements = realloc(fnc->elements, sizeof(FuncNameCounterElement) * fnc->height);
        }
        funcNum = 0;
    }

    char buf[sizeof(origName)+ 1 + 32 + 1]; // Make sure this is large enough for a 64-bit int + sign
    snprintf(buf, sizeof(buf), "%s_%s", origName, get_type_signature(f->signature));

    char* bufP = strdup(buf);

    fstn->elements[fstn->count++] = (FuncSignToNameElement){.sign = f->signature, .name = bufP};

    if(fstn->count >= fstn->height) {
        fstn->height *= 2;
        fstn->elements = realloc(fstn->elements, sizeof(FuncSignToNameElement) * fstn->height);
    }
    
    fprintf(out, "%s", type_to_c_type(f->signature->retType));
    fprintf(out, " %s(", buf);

    for (int i = 0; i < f->signature->paramNum; ++i) {
        if(i > 0) fprintf(out, ", ");
        fprintf(out, "%s%s", type_to_c_type(f->signature->parameters[i].type), f->signature->parameters[i].ownership != OWNERSHIP_NONE ? "*" : "");
        fprintf(out, " %s", f->signature->parameters[i].name);
    }
    fprintf(out, ");\n");
}

//emit an expression (no newlines, just the code)
void emit_expr(Expr* e, FILE* out, FuncSignToName* fstn) {
    if (e == NULL) return;

    switch (e->type) {
        case INT_LIT_E:
            fprintf(out, "%d", e->as.int_val);
            break;

        case BOOL_LIT_E:
            fprintf(out, e->as.bool_val ? "true" : "false");
            break;

        case STR_LIT_E: {
            // Re-escape the string for C output
            fprintf(out, "\"");
            for (char* s = e->as.str_val; *s != '\0'; s++) {
                switch (*s) {
                    case '\n': fprintf(out, "\\n"); break;
                    case '\t': fprintf(out, "\\t"); break;
                    case '\r': fprintf(out, "\\r"); break;
                    case '\\': fprintf(out, "\\\\"); break;
                    case '"': fprintf(out, "\\\""); break;
                    default: fprintf(out, "%c", *s); break;
                }
            }
            fprintf(out, "\"");
            break;
        }

        case VAR_E:
            fprintf(out, "%s%s", e->as.var.ownership != OWNERSHIP_NONE ? "*" : "", e->as.var.name);
            break;

        case UN_OP_E: {
            fprintf(out, "(");
            if (e->as.un_op.op == MINUS_T) {
                fprintf(out, "-");
            } else if (e->as.un_op.op == NEGATION_T) {
                fprintf(out, "!");
            }
            emit_expr(e->as.un_op.expr, out, fstn);
            fprintf(out, ")");
            break;
        }

        case BIN_OP_E: {
            //emit: (left op right)
            fprintf(out, "(");
            emit_expr(e->as.bin_op.exprL, out, fstn);

            switch (e->as.bin_op.op) {
                case PLUS_T: fprintf(out, " + "); break;
                case MINUS_T: fprintf(out, " - "); break;
                case STAR_T: fprintf(out, " * "); break;
                case SLASH_T: fprintf(out, " / "); break;
                case DOUBLE_EQUALS_T: fprintf(out, " == "); break;
                case NOT_EQUALS_T: fprintf(out, " != "); break;
                case LESS_T: fprintf(out, " < "); break;
                case MORE_T: fprintf(out, " > "); break;
                case LESS_EQUALS_T: fprintf(out, " <= "); break;
                case MORE_EQUALS_T: fprintf(out, " >= "); break;
                case AND_T: fprintf(out, " && "); break;
                case OR_T: fprintf(out, " || "); break;
                default: fprintf(out, " ??? "); break;
            }

            emit_expr(e->as.bin_op.exprR, out, fstn);
            fprintf(out, ")");
            break;
        }

        case FUNC_CALL_E: {
            //built in string
            if(strcmp(e->as.func_call.name, "print") == 0) {

                fprintf(out, "printf(\"");

                for (int i = 0; i < e->as.func_call.count; ++i) {
                    Expr* p = e->as.func_call.params[i];

                    if (p->analyzedType == INT_KEYWORD_T) {
                        fprintf(out, "%%d");
                    } else if (p->analyzedType == BOOL_KEYWORD_T) {
                        fprintf(out, "%%s");
                    } else if (p->analyzedType == STR_KEYWORD_T) {
                        fprintf(out, "%%s");
                    }

                    if (i < e->as.func_call.count - 1) {
                        fprintf(out, " ");
                    }
                }
                fprintf(out, "\\n\"");

                for (int i = 0; i < e->as.func_call.count; ++i) {
                    Expr* p = e->as.func_call.params[i];
                    fprintf(out, ", ");

                    if (p->analyzedType == BOOL_KEYWORD_T) {
                        fprintf(out, "(");
                        emit_expr(p, out, fstn);
                        fprintf(out, " ? \"true\" : \"false\")");
                    } else {
                        emit_expr(p, out, fstn);
                    }
                }

                fprintf(out, ")");
                return;
            }

            //regular function call
            if (e->as.func_call.resolved_sign == NULL) {
                fprintf(out, "/* ERROR: unresolved function %s */", e->as.func_call.name);
                break;
            }

            char* mangled_name = get_mangled_name(e->as.func_call.resolved_sign);
            fprintf(out, "%s(", mangled_name);
            for (int i = 0; i < e->as.func_call.count; ++i) {
                if (i != 0) fprintf(out, ", ");
                emit_expr(e->as.func_call.params[i], out, fstn);
            }
            fprintf(out, ")");
            break;
        }

        case FUNC_RET_E: {
            // If it's a simple return, keep it on one line.
            // If it's a match, use the temporary variable block.
            if (e->as.func_ret_expr->type == MATCH_E) {
                emit_indent(out, 0);
                fprintf(out, "{\n");
                emit_indent(out, 0 + 1);
                fprintf(out, "int64_t _ret;\n");
                emit_assign_expr_to_var(e->as.func_ret_expr, "_ret", OWNERSHIP_NONE, out, 0 + 1, fstn);
                emit_indent(out, 0 + 1);
                fprintf(out, "return _ret;\n");
                emit_indent(out, 0);
                fprintf(out, "}\n");
            } else if (e->as.func_ret_expr->type == VOID_E) {
                emit_indent(out, 0);
                fprintf(out, "return");
            } else {
                emit_indent(out, 0);
                fprintf(out, "return ");
                emit_expr(e->as.func_ret_expr, out, fstn);
                fprintf(out, "");
            }
            break;
        }

        case ALLOC_E: {
            break;
        }
    }
}

void emit_assign_expr_to_var(Expr* e, const char* targetVar, Ownership o, FILE* out, int indent, FuncSignToName* fstn) {
    if (e->type == MATCH_E) {
        int defaultIdx = -1;

        for (int i = 0; i < e->as.match.branchCount; i++) {
            MatchBranchExpr branch = e->as.match.branches[i];

            if (branch.caseExpr->type == VOID_E) {
                defaultIdx = i; // Save the wildcard for the final 'else'
                continue;
            }

            emit_indent(out, indent);
            if (i == 0) fprintf(out, "if (");
            else fprintf(out, "else if (");

            emit_expr(e->as.match.var, out, fstn);
            fprintf(out, " == ");
            emit_expr(branch.caseExpr, out, fstn);
            fprintf(out, ") {\n");

            // Recurse: handles nested matches or simple values
            emit_assign_expr_to_var(branch.caseRet, targetVar, o, out, indent + 1, fstn);

            emit_indent(out, indent);
            fprintf(out, "}\n");
        }

        // Output the wildcard as a final 'else'
        if (defaultIdx != -1) {
            emit_indent(out, indent);
            fprintf(out, "else {\n");
            emit_assign_expr_to_var(e->as.match.branches[defaultIdx].caseRet, targetVar, o, out, indent + 1, fstn);
            emit_indent(out, indent);
            fprintf(out, "}\n");
        }
    } else {
        // Base case: just a normal assignment
        emit_indent(out, indent);
        fprintf(out, "%s%s = ", o != OWNERSHIP_NONE ? "*" : "", targetVar);
        emit_expr(e, out, fstn);
        fprintf(out, ";\n");
    }
}

// Emit a statement (with indentation and newlines)
void emit_stmt(Stmt* s, FILE* out, int indent, FuncSignToName* fstn) {
    if (s == NULL) return;

    switch (s->type) {
        // Inside emit_stmt switch case VAR_DECL_S:
        case VAR_DECL_S:
            emit_indent(out, indent);
            fprintf(out, "%s", type_to_c_type(s->as.var_decl.varType));
            fprintf(out, " %s%s", s->as.var_decl.ownership != OWNERSHIP_NONE ? "*" : "", s->as.var_decl.name);

            // Check if it's an allocation
            if (s->as.var_decl.expr->type == ALLOC_E) {
                // Emit: int64_t *x = malloc(sizeof(int64_t));
                fprintf(out, " = malloc(sizeof(%s));\n", type_to_c_type(s->as.var_decl.varType));

                // Now assign the initial value: *x = initialValue;
                emit_assign_expr_to_var(s->as.var_decl.expr->as.alloc.initialValue,
                                        s->as.var_decl.name,
                                        s->as.var_decl.ownership,
                                        out,
                                        indent, fstn);
            } else {
                // Normal declaration
                fprintf(out, ";\n");
                emit_assign_expr_to_var(s->as.var_decl.expr,
                                        s->as.var_decl.name,
                                        s->as.var_decl.ownership,
                                        out,
                                        indent, fstn);
            }
            break;

        case ASSIGN_S:
            emit_assign_expr_to_var(s->as.var_assign.expr, s->as.var_assign.name, s->as.var_assign.ownership, out, indent, fstn);
            break;

        case IF_S:
            emit_indent(out, indent);
            fprintf(out, "if (");
            emit_expr(s->as.if_stmt.cond, out, fstn);
            fprintf(out, ") ");

            //true
            if (s->as.if_stmt.trueStmt->type == BLOCK_S) {
                emit_stmt(s->as.if_stmt.trueStmt, out, indent, fstn);
            } else {
                fprintf(out, "{\n");
                emit_stmt(s->as.if_stmt.trueStmt, out, indent + 1, fstn);
                emit_indent(out, indent);
                fprintf(out, "}");
            }

            //false
            if (s->as.if_stmt.falseStmt != NULL) {
                fprintf(out, " else ");
                if (s->as.if_stmt.falseStmt->type == BLOCK_S) {
                    emit_stmt(s->as.if_stmt.falseStmt, out, indent, fstn);
                } else {
                    fprintf(out, "{\n");
                    emit_stmt(s->as.if_stmt.falseStmt, out, indent + 1, fstn);
                    emit_indent(out, indent);
                    fprintf(out, "}");
                }
            }
            fprintf(out, "\n");
            break;

        case WHILE_S:
            emit_indent(out, indent);
            fprintf(out, "while (");
            emit_expr(s->as.while_stmt.cond, out, fstn);
            fprintf(out, ") ");
            emit_stmt(s->as.while_stmt.body, out, indent, fstn);
            break;

        case DO_WHILE_S:
            emit_indent(out, indent);
            fprintf(out, "do ");
            emit_stmt(s->as.do_while_stmt.body, out, indent, fstn);
            emit_indent(out, indent);
            fprintf(out, "while (");
            emit_expr(s->as.do_while_stmt.cond, out, fstn);
            fprintf(out, ");\n");
            break;

        case FOR_S:
            emit_indent(out, indent);
            fprintf(out, "for (int %s = ", s->as.for_stmt.varName);
            emit_expr(s->as.for_stmt.min, out, fstn);
            fprintf(out, "; %s <= ", s->as.for_stmt.varName);
            emit_expr(s->as.for_stmt.max, out, fstn);
            fprintf(out, "; %s++) ", s->as.for_stmt.varName);
            emit_stmt(s->as.for_stmt.body, out, indent, fstn);
            break;

        case BLOCK_S:
            emit_indent(out, indent);
            fprintf(out, "{\n");
            for (int i = 0; i < s->as.block_stmt.count; i++) {
                emit_stmt(s->as.block_stmt.stmts[i], out, indent + 1, fstn);
            }
            emit_indent(out, indent);
            fprintf(out, "}\n");
            break;

        case EXPR_STMT_S:
            emit_indent(out, indent);
            emit_expr(s->as.expr_stmt, out, fstn);
            fprintf(out, ";\n");
            break;

        case MATCH_S: {
            for (int i = 0; i < s->as.match_stmt.branchCount; ++i) {
                MatchBranchStmt b = s->as.match_stmt.branches[i];

                if(i == 0) {
                    fprintf(out, "if (");
                    emit_expr(b.caseExpr, out, fstn);
                    fprintf(out, ") {\n");
                } else if ( b.caseExpr->type == VOID_E) {
                    fprintf(out, "else {\n");
                } else {
                    fprintf(out, "else if (");
                    emit_expr(b.caseExpr, out, fstn);
                    fprintf(out, ") {\n");
                }

                for (int j = 0; j < b.stmtCount; ++j) {
                    emit_stmt(b.stmts[j], out, indent, fstn);
                }

                fprintf(out, "}\n");
            }
            break;
        }

        case FREE_S: {
            emit_indent(out, indent);
            fprintf(out, "free(%s);\n", s->as.free_stmt.varName);
            break;
        }
    }
}

//main codegen entry point
void generate_code(Func** program, int count, FILE* output) {
    //emit C headers
    fprintf(output, "#include <stdio.h>\n");
    fprintf(output, "#include <stdlib.h>\n");
    fprintf(output, "#include <stdint.h>\n");
    fprintf(output, "#include <stdbool.h>\n\n");

    FuncSignToName* fstn = malloc(sizeof(FuncSignToName));
    fstn->count = 0;
    fstn->height = 2;
    fstn->elements = malloc(sizeof(FuncSignToNameElement) * fstn->height);

    FuncNameCounter* fnc = malloc(sizeof(FuncNameCounter));
    fnc->count = 0;
    fnc->height = 2;
    fnc->elements = malloc(sizeof(FuncSignToNameElement) * fnc->height);

    for (int i = 0; i < count; ++i) {
        emit_func_decl(program[i], output, fnc, fstn);
    }

    for (int i = 0; i < count; ++i) {
        emit_func(program[i], output, fstn);
    }

    free(fstn->elements);
    free(fstn);
    free(fnc->elements);
    free(fnc);
}