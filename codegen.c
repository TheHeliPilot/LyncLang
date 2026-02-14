//
// Created by bucka on 2/9/2026.
//

#include "codegen.h"

char* type_to_c_type(TokenType t) {
    switch (t) {
        case INT_KEYWORD_T: return "int";
        case BOOL_KEYWORD_T: return "bool";
        case CHAR_KEYWORD_T: return "char";
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

void emit_type(TokenType type, FILE* out) {
    switch (type) {
        case INT_KEYWORD_T: fprintf(out, "int"); break;
        case BOOL_KEYWORD_T: fprintf(out, "bool"); break;
        case STR_KEYWORD_T: fprintf(out, "char"); break;
        case CHAR_KEYWORD_T: fprintf(out, "char"); break;
        case VOID_KEYWORD_T: fprintf(out, "void"); break;
        default: fprintf(out, "void"); break;
    }
}

bool emit_pattern_condition(Pattern* pattern, Expr* matchVar, FILE* out, FuncSignToName* fstn) {
    switch (pattern->type) {
        case NULL_PATTERN:
            //for nullable pointers, check the pointer itself, not dereferenced value
            if (matchVar->type == VAR_E) {
                fprintf(out, "%s", matchVar->as.var.name);
            } else {
                emit_expr(matchVar, out, fstn);
            }
            fprintf(out, " == NULL");
            return false;
        case SOME_PATTERN:
            //for nullable pointers, check the pointer itself, not dereferenced value
            if (matchVar->type == VAR_E) {
                fprintf(out, "%s", matchVar->as.var.name);
            } else {
                emit_expr(matchVar, out, fstn);
            }
            fprintf(out, " != NULL");
            return false;
        case VALUE_PATTERN:
            emit_expr(matchVar, out, fstn);
            fprintf(out, " == ");
            emit_expr(pattern->as.value_expr, out, fstn);
            return false;
        case WILDCARD_PATTERN:
            return true;
    }
    return false;
}

char* get_mangled_name(FuncSign* sign) {
    static char buffer[512];
    char* ptr = buffer;

    stage_trace(STAGE_CODEGEN, "get_mangled_name entry: sign=%p", sign);

    if (sign == NULL) {
        strcpy(buffer, "NULL_SIGN");
        return buffer;
    }

    //read the name pointer without dereferencing the string yet
    char* name_ptr = sign->name;
    stage_trace(STAGE_CODEGEN, "name pointer value: %p", name_ptr);
    if (name_ptr == NULL) {
        strcpy(buffer, "NULL_NAME");
        return buffer;
    }
    stage_trace(STAGE_CODEGEN, "about to dereference name");
    stage_trace(STAGE_CODEGEN, "name is: %s", name_ptr);

    stage_trace(STAGE_CODEGEN, "name is: %s", sign->name);
    ptr += sprintf(ptr, "%s", sign->name);

    stage_trace(STAGE_CODEGEN, "adding return type");
    ptr += sprintf(ptr, "_%s", token_type_name(sign->retType));

    stage_trace(STAGE_CODEGEN, "adding %d parameters", sign->paramNum);
    for (int i = 0; i < sign->paramNum; i++) {
        stage_trace(STAGE_CODEGEN, "adding param %d", i);
        ptr += sprintf(ptr, "_%s", token_type_name(sign->parameters[i].type));
        if (sign->parameters[i].ownership != OWNERSHIP_NONE) {
            ptr += sprintf(ptr, "%s",
                           sign->parameters[i].ownership == OWNERSHIP_OWN ? "own" : "ref");
        }
    }

    stage_trace(STAGE_CODEGEN, "get_mangled_name done: %s", buffer);
    return buffer;
}

//hash-based version to keep names shorter
uint32_t hash_signature(FuncSign* sign) {
    uint32_t hash = 5381;

    //hash name
    for (char* s = sign->name; *s; s++) {
        hash = ((hash << 5) + hash) + *s;
    }

    //hash return type
    hash = ((hash << 5) + hash) + sign->retType;

    //hash parameters
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

    ptr += sprintf(ptr, "%s_", token_type_name(sign->retType));

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
    stage_trace(STAGE_CODEGEN, "emit_func: %s", f->signature->name);

    if(strcmp(f->signature->name, "main") == 0) fprintf(out, "int");
    else fprintf(out, "%s%s", type_to_c_type(f->signature->retType), f->signature->retOwnership != OWNERSHIP_NONE ? "*" : "");
    fprintf(out, " %s(", strcmp(f->signature->name, "main") == 0 ? "main" : get_func_name_from_sign(fstn, f->signature));

    for (int i = 0; i < f->signature->paramNum; ++i) {
        if(i > 0) fprintf(out, ", ");
        fprintf(out, "%s%s", type_to_c_type(f->signature->parameters[i].type), f->signature->parameters[i].ownership != OWNERSHIP_NONE ? "*" : "");
        fprintf(out, " %s", f->signature->parameters[i].name);
    }
    fprintf(out, ")\n");

    stage_trace(STAGE_CODEGEN, "emit_func: calling emit_stmt for body");
    emit_stmt(f->body, out, 0, fstn);
    stage_trace(STAGE_CODEGEN, "emit_func: done with %s", f->signature->name);
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

    char* mangled = get_mangled_name(f->signature);
    char* bufP = strdup(mangled);

    fstn->elements[fstn->count++] = (FuncSignToNameElement){.sign = f->signature, .name = bufP};

    if(fstn->count >= fstn->height) {
        fstn->height *= 2;
        fstn->elements = realloc(fstn->elements, sizeof(FuncSignToNameElement) * fstn->height);
    }

    fprintf(out, "%s%s", type_to_c_type(f->signature->retType), f->signature->retOwnership != OWNERSHIP_NONE ? "*" : "");
    fprintf(out, " %s(", mangled);

    for (int i = 0; i < f->signature->paramNum; ++i) {
        if(i > 0) fprintf(out, ", ");
        fprintf(out, "%s%s", type_to_c_type(f->signature->parameters[i].type), f->signature->parameters[i].ownership != OWNERSHIP_NONE ? "*" : "");
        fprintf(out, " %s", f->signature->parameters[i].name);
    }
    fprintf(out, ");\n");
}

void emit_expr(Expr* e, FILE* out, FuncSignToName* fstn) {
    if (e == NULL) return;

    stage_trace(STAGE_CODEGEN, "emit_expr: type=%d", e->type);

    switch (e->type) {
        case INT_LIT_E:
            fprintf(out, "%d", e->as.int_val);
            break;

        case BOOL_LIT_E:
            fprintf(out, e->as.bool_val ? "true" : "false");
            break;

        case STR_LIT_E: {
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

        case NULL_LIT_E: {
            fprintf(out, "NULL");
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
            //handle std.io read_* functions
            if (strcmp(e->as.func_call.name, "read_int") == 0) {
                fprintf(out, "read_int()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_str") == 0) {
                fprintf(out, "read_str()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_bool") == 0) {
                fprintf(out, "read_bool()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_char") == 0) {
                fprintf(out, "read_char()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_key") == 0) {
                fprintf(out, "read_key()");
                break;
            }

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

            stage_trace(STAGE_CODEGEN, "emitting regular function call: %s", e->as.func_call.name);

            //try to safely access resolved_sign
            FuncSign* rs = e->as.func_call.resolved_sign;
            stage_trace(STAGE_CODEGEN, "resolved_sign pointer: %p", rs);

            //dont try to dereference if it might be bad
            char* mangled_name = get_mangled_name(rs);
            stage_trace(STAGE_CODEGEN, "mangled name: %s", mangled_name);
            fprintf(out, "%s(", mangled_name);
            for (int i = 0; i < e->as.func_call.count; ++i) {
                stage_trace(STAGE_CODEGEN, "emitting parameter %d", i);
                if (i != 0) fprintf(out, ", ");
                emit_expr(e->as.func_call.params[i], out, fstn);
            }
            fprintf(out, ")");
            stage_trace(STAGE_CODEGEN, "done with function call: %s", e->as.func_call.name);
            break;
        }

        case FUNC_RET_E: {
            //if its a simple return, keep it on one line.
            //if its a match, use the temporary variable block.
            if (e->as.func_ret_expr->type == MATCH_E) {
                emit_indent(out, 0);
                fprintf(out, "{\n");
                emit_indent(out, 0 + 1);
                fprintf(out, "int _ret;\n");
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

        case SOME_E: {
            //for nullable pointers, dont dereference - check the pointer itself
            if (e->as.some.var->type == VAR_E) {
                fprintf(out, "%s != NULL", e->as.some.var->as.var.name);
            } else {
                fprintf(out, "(");
                emit_expr(e->as.some.var, out, fstn);
                fprintf(out, ") != NULL");
            }
            break;
        }

        case ALLOC_E: {
            break;
        }

        case ARRAY_DECL_E: {
            fprintf(out, "{");
            for (int i = 0; i < e->as.arr_decl.count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(e->as.arr_decl.values[i], out, fstn);
            }
            fprintf(out, "}");
            break;
        }

        case ARRAY_ACCESS_E: {
            fprintf(out, "%s[", e->as.array_access.arrayName);
            emit_expr(e->as.array_access.index, out, fstn);
            fprintf(out, "]");
            break;
        }
    }
}

void emit_assign_expr_to_var(Expr* e, const char* targetVar, Ownership o, FILE* out, int indent, FuncSignToName* fstn) {
    if (e->type == MATCH_E) {
        int defaultIdx = -1;
        bool firstCondition = true;

        //find wildcard
        for (int i = 0; i < e->as.match.branchCount; i++) {
            if (e->as.match.branches[i].pattern->type == WILDCARD_PATTERN) {
                defaultIdx = i;
                break;
            }
        }

        for (int i = 0; i < e->as.match.branchCount; i++) {
            if (i == defaultIdx) continue;  // handle wildcard at end

            MatchBranchExpr* branch = &e->as.match.branches[i];

            emit_indent(out, indent);
            if (firstCondition) {
                fprintf(out, "if (");
                firstCondition = false;
            } else {
                fprintf(out, "else if (");
            }

            emit_pattern_condition(branch->pattern, e->as.match.var, out, fstn);
            fprintf(out, ") {\n");

            //if SOME_PATTERN, declare binding variable
            if (branch->pattern->type == SOME_PATTERN) {
                emit_indent(out, indent + 1);
                emit_type(branch->analyzed_type, out);
                fprintf(out, "* %s = ", branch->pattern->as.binding_name);
                //emit just the variable name, not dereferenced
                if (e->as.match.var->type == VAR_E) {
                    fprintf(out, "%s", e->as.match.var->as.var.name);
                } else {
                    emit_expr(e->as.match.var, out, fstn);
                }
                fprintf(out, ";\n");
            }

            //handles nested matches or simple values
            emit_assign_expr_to_var(branch->caseRet, targetVar, o, out, indent + 1, fstn);

            emit_indent(out, indent);
            fprintf(out, "}\n");
        }

        if (defaultIdx != -1) {
            emit_indent(out, indent);
            fprintf(out, "else {\n");
            emit_assign_expr_to_var(e->as.match.branches[defaultIdx].caseRet, targetVar, o, out, indent + 1, fstn);
            emit_indent(out, indent);
            fprintf(out, "}\n");
        }
    } else if (e->type == ALLOC_E) {
        // Reassignment with alloc
        emit_indent(out, indent);
        fprintf(out, "%s = malloc(sizeof(%s));\n", targetVar, type_to_c_type(e->as.alloc.type));
        emit_indent(out, indent);
        fprintf(out, "*%s = ", targetVar);
        emit_expr(e->as.alloc.initialValue, out, fstn);
        fprintf(out, ";\n");
    } else {
        //base case: just a normal assignment
        emit_indent(out, indent);
        bool add_ampersand = (o != OWNERSHIP_NONE && e->type == VAR_E && e->as.var.ownership != OWNERSHIP_NONE);

        //dont dereference if expression is nullable (returns a pointer)
        bool needs_deref = (o != OWNERSHIP_NONE && e->analyzedType != NULL_LIT_T && !e->is_nullable && (e->type == VAR_E ? e->as.var.ownership == OWNERSHIP_NONE : true));

        fprintf(out, "%s%s = %s", needs_deref ? "*" : "", targetVar, add_ampersand ? "&" : "");
        emit_expr(e, out, fstn);
        fprintf(out, ";\n");
    }
}

//emit a statement (with indentation and newlines)
void emit_stmt(Stmt* s, FILE* out, int indent, FuncSignToName* fstn) {
    if (s == NULL) return;

    stage_trace(STAGE_CODEGEN, "emit_stmt: type=%d, indent=%d", s->type, indent);

    switch (s->type) {
        //inside emit_stmt switch case VAR_DECL_S:
        case VAR_DECL_S:
            if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_NONE) {
                //stack array
                emit_indent(out, indent);
                fprintf(out, "%s %s[",
                        type_to_c_type(s->as.var_decl.varType),
                        s->as.var_decl.name);
                emit_expr(s->as.var_decl.arraySize, out, fstn);
                fprintf(out, "]");

                //only emit initialization if present (VLAs can be uninitialized)
                if (s->as.var_decl.expr->type == ARRAY_DECL_E) {
                    fprintf(out, " = ");
                    emit_expr(s->as.var_decl.expr, out, fstn);
                }
                fprintf(out, ";\n");
            } else if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_OWN) {
                //heap array
                emit_indent(out, indent);
                fprintf(out, "%s* %s = malloc(sizeof(%s) * ",
                        type_to_c_type(s->as.var_decl.varType),
                        s->as.var_decl.name,
                        type_to_c_type(s->as.var_decl.varType));
                emit_expr(s->as.var_decl.arraySize, out, fstn);
                fprintf(out, ");\n");

                //todo: handle array initialization in alloc (Phase 6)
            } else if (s->as.var_decl.expr->type == ALLOC_E) {
                // Regular alloc (non-array)
                emit_indent(out, indent);
                fprintf(out, "%s", type_to_c_type(s->as.var_decl.varType));
                fprintf(out, " %s%s", s->as.var_decl.ownership != OWNERSHIP_NONE ? "*" : "", s->as.var_decl.name);
                fprintf(out, " = malloc(sizeof(%s));\n", type_to_c_type(s->as.var_decl.varType));

                emit_assign_expr_to_var(s->as.var_decl.expr->as.alloc.initialValue,
                                        s->as.var_decl.name,
                                        s->as.var_decl.ownership,
                                        out,
                                        indent, fstn);
            } else {
                //normal variable
                emit_indent(out, indent);
                fprintf(out, "%s", type_to_c_type(s->as.var_decl.varType));
                fprintf(out, " %s%s", s->as.var_decl.ownership != OWNERSHIP_NONE ? "*" : "", s->as.var_decl.name);
                fprintf(out, ";\n");
                emit_assign_expr_to_var(s->as.var_decl.expr,
                                        s->as.var_decl.name,
                                        s->as.var_decl.ownership,
                                        out,
                                        indent, fstn);
            }
            break;

        case ASSIGN_S:
            if (s->as.var_assign.isArray && s->as.var_assign.expr->type == ALLOC_E) {
                //array reallocation
                emit_indent(out, indent);
                fprintf(out, "%s = malloc(sizeof(%s) * ",
                        s->as.var_assign.name,
                        type_to_c_type(s->as.var_assign.expr->as.alloc.type));
                emit_expr(s->as.var_assign.expr->as.alloc.initialValue, out, fstn);
                fprintf(out, ");\n");
            } else {
                emit_assign_expr_to_var(s->as.var_assign.expr, s->as.var_assign.name, s->as.var_assign.ownership, out, indent, fstn);
            }
            break;

        case ARRAY_ELEM_ASSIGN_S:
            emit_indent(out, indent);
            fprintf(out, "%s[", s->as.array_elem_assign.arrayName);
            emit_expr(s->as.array_elem_assign.index, out, fstn);
            fprintf(out, "] = ");
            emit_expr(s->as.array_elem_assign.value, out, fstn);
            fprintf(out, ";\n");
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
            int wildcardIdx = -1;

            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                if (s->as.match_stmt.branches[i].pattern->type == WILDCARD_PATTERN) {
                    wildcardIdx = i;
                    break;
                }
            }

            bool firstCondition = true;
            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                if (i == wildcardIdx) continue;  // handle wildcard at end

                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];

                emit_indent(out, indent);
                if (firstCondition) {
                    fprintf(out, "if (");
                    firstCondition = false;
                } else {
                    fprintf(out, "else if (");
                }

                emit_pattern_condition(branch->pattern, s->as.match_stmt.var, out, fstn);
                fprintf(out, ") {\n");

                if (branch->pattern->type == SOME_PATTERN) {
                    emit_indent(out, indent + 1);
                    emit_type(branch->analyzed_type, out);
                    fprintf(out, "* %s = ", branch->pattern->as.binding_name);
                    //emit just the variable name, not dereferenced
                    if (s->as.match_stmt.var->type == VAR_E) {
                        fprintf(out, "%s", s->as.match_stmt.var->as.var.name);
                    } else {
                        emit_expr(s->as.match_stmt.var, out, fstn);
                    }
                    fprintf(out, ";\n");
                }

                for (int j = 0; j < branch->stmtCount; j++) {
                    emit_stmt(branch->stmts[j], out, indent + 1, fstn);
                }

                emit_indent(out, indent);
                fprintf(out, "}\n");
            }

            if (wildcardIdx != -1) {
                emit_indent(out, indent);
                fprintf(out, "else {\n");
                MatchBranchStmt* branch = &s->as.match_stmt.branches[wildcardIdx];
                for (int j = 0; j < branch->stmtCount; j++) {
                    emit_stmt(branch->stmts[j], out, indent + 1, fstn);
                }
                emit_indent(out, indent);
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
void generate_code(Program* prog, FILE* output) {
    stage_trace(STAGE_CODEGEN, "generate_code called with prog=%p, output=%p", prog, output);

    if (!prog) {
        stage_fatal(STAGE_CODEGEN, NO_LOC, "Program pointer is NULL");
    }
    if (!output) {
        stage_fatal(STAGE_CODEGEN, NO_LOC, "Output file pointer is NULL");
    }

    stage_trace(STAGE_CODEGEN, "prog->func_count=%d", prog->func_count);

    //emit C headers
    fprintf(output, "#include <stdio.h>\n");
    fprintf(output, "#include <stdlib.h>\n");
    fprintf(output, "#include <stdint.h>\n");
    fprintf(output, "#include <stdbool.h>\n");
    fprintf(output, "#include <string.h>\n");

    //add platform-specific headers for read_key if needed
    if (prog->imports && prog->imports->import_count > 0) {
        bool need_read_key = false;
        for (int i = 0; i < prog->imports->import_count; i++) {
            IncludeStmt* import = prog->imports->imports[i];
            if (import->type == IMPORT_ALL && strcmp(import->module_name, "std.io") == 0) {
                need_read_key = true;
                break;
            } else if (import->type == IMPORT_SPECIFIC && strcmp(import->function_name, "read_key") == 0) {
                need_read_key = true;
                break;
            }
        }
        if (need_read_key) {
            fprintf(output, "#ifdef _WIN32\n");
            fprintf(output, "#include <conio.h>\n");
            fprintf(output, "#else\n");
            fprintf(output, "#include <termios.h>\n");
            fprintf(output, "#include <unistd.h>\n");
            fprintf(output, "#endif\n");
        }
    }

    fprintf(output, "\n");

    stage_trace(STAGE_CODEGEN, "headers written, checking imports");

    //generate C helper functions based on imports
    if (prog->imports && prog->imports->import_count > 0) {
        fprintf(output, "// std.io helper functions\n");

        //check which functions are imported
        bool has_wildcard = false;
        bool need_read_int = false;
        bool need_read_str = false;
        bool need_read_bool = false;
        bool need_read_char = false;
        bool need_read_key = false;

        for (int i = 0; i < prog->imports->import_count; i++) {
            IncludeStmt* import = prog->imports->imports[i];
            if (import->type == IMPORT_ALL && strcmp(import->module_name, "std.io") == 0) {
                has_wildcard = true;
                break;
            } else if (import->type == IMPORT_SPECIFIC) {
                if (strcmp(import->function_name, "read_int") == 0) need_read_int = true;
                else if (strcmp(import->function_name, "read_str") == 0) need_read_str = true;
                else if (strcmp(import->function_name, "read_bool") == 0) need_read_bool = true;
                else if (strcmp(import->function_name, "read_char") == 0) need_read_char = true;
                else if (strcmp(import->function_name, "read_key") == 0) need_read_key = true;
            }
        }

        //generate read_int
        if (has_wildcard || need_read_int) {
            fprintf(output, "int* read_int() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    int* result = malloc(sizeof(int));\n");
            fprintf(output, "    *result = atoll(buffer);\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_str
        if (has_wildcard || need_read_str) {
            fprintf(output, "char** read_str() {\n");
            fprintf(output, "    char buffer[1024];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    //remove trailing newline\n");
            fprintf(output, "    size_t len = strlen(buffer);\n");
            fprintf(output, "    if (len > 0 && buffer[len-1] == '\\n') buffer[len-1] = '\\0';\n");
            fprintf(output, "    char** result = malloc(sizeof(char*));\n");
            fprintf(output, "#ifdef _WIN32\n");
            fprintf(output, "    *result = _strdup(buffer);\n");
            fprintf(output, "#else\n");
            fprintf(output, "    *result = strdup(buffer);\n");
            fprintf(output, "#endif\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_bool
        if (has_wildcard || need_read_bool) {
            fprintf(output, "bool* read_bool() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    bool* result = malloc(sizeof(bool));\n");
            fprintf(output, "    if (strncmp(buffer, \"true\", 4) == 0 || strncmp(buffer, \"1\", 1) == 0) {\n");
            fprintf(output, "        *result = true;\n");
            fprintf(output, "    } else if (strncmp(buffer, \"false\", 5) == 0 || strncmp(buffer, \"0\", 1) == 0) {\n");
            fprintf(output, "        *result = false;\n");
            fprintf(output, "    } else {\n");
            fprintf(output, "        free(result);\n");
            fprintf(output, "        return NULL;\n");
            fprintf(output, "    }\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_char
        if (has_wildcard || need_read_char) {
            fprintf(output, "char* read_char() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    if (buffer[0] == '\\0' || buffer[0] == '\\n') return NULL;\n");
            fprintf(output, "    char* result = malloc(sizeof(char));\n");
            fprintf(output, "    *result = buffer[0];\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_key (tbh no idea how this works but oh well, not all code needs to be mine :D)
        if (has_wildcard || need_read_key) {
            fprintf(output, "char* read_key() {\n");
            fprintf(output, "    char* result = malloc(sizeof(char));\n");
            fprintf(output, "#ifdef _WIN32\n");
            fprintf(output, "    *result = _getch();\n");
            fprintf(output, "#else\n");
            fprintf(output, "    struct termios oldt, newt;\n");
            fprintf(output, "    tcgetattr(STDIN_FILENO, &oldt);\n");
            fprintf(output, "    newt = oldt;\n");
            fprintf(output, "    newt.c_lflag &= ~(ICANON | ECHO);\n");
            fprintf(output, "    tcsetattr(STDIN_FILENO, TCSANOW, &newt);\n");
            fprintf(output, "    *result = getchar();\n");
            fprintf(output, "    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);\n");
            fprintf(output, "#endif\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }
    }

    stage_trace(STAGE_CODEGEN, "imports processed, allocating FuncSignToName");

    FuncSignToName* fstn = malloc(sizeof(FuncSignToName));
    fstn->count = 0;
    fstn->height = 2;
    fstn->elements = malloc(sizeof(FuncSignToNameElement) * fstn->height);

    FuncNameCounter* fnc = malloc(sizeof(FuncNameCounter));
    fnc->count = 0;
    fnc->height = 2;
    fnc->elements = malloc(sizeof(FuncSignToNameElement) * fnc->height);

    Func** program = prog->functions;
    int count = prog->func_count;

    stage_trace(STAGE_CODEGEN, "emitting %d function declarations", count);

    for (int i = 0; i < count; ++i) {
        stage_trace(STAGE_CODEGEN, "emitting decl for function %d", i);
        emit_func_decl(program[i], output, fnc, fstn);
    }

    stage_trace(STAGE_CODEGEN, "emitting %d function definitions", count);

    for (int i = 0; i < count; ++i) {
        stage_trace(STAGE_CODEGEN, "emitting function %d", i);
        emit_func(program[i], output, fstn);
    }

    stage_trace(STAGE_CODEGEN, "all functions emitted, cleaning up");

    free(fstn->elements);
    free(fstn);
    free(fnc->elements);
    free(fnc);
}