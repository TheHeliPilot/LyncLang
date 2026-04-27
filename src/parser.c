//created by bucka on 2/9/2026.

#include "parser.h"
#include <ctype.h>

#define TOK_LOC(tok) ((SourceLocation){.line = (tok)->line, .column = (tok)->column, .filename = (tok)->filename})

// True if `tok` is a keyword whose textual name is a valid identifier
// (alphabetic first char). Used to allow keywords like `string`, `free` in
// positions that normally only accept VAR_T (module paths, extern fn names).
static bool tok_is_identifier_keyword(const Token* tok) {
    if (!tok || tok->value != NULL) return false;
    const char* n = token_type_name(tok->type);
    return n && (isalpha((unsigned char)n[0]) || n[0] == '_');
}

Pattern* parsePattern(Parser* p);

// Set by parseProgram on entry; nulled on exit. Used by helpers that need
// to push pending template instantiations (struct field types, var decl
// types, function call sites) without threading Program* through every
// signature.
static Program* g_current_program = NULL;

// If the current parser position is `IDENT < typearg... >` in a TYPE position
// (e.g. as a struct field type, var-decl type, return type), parse the type
// args, push a pending instantiation, and return the mangled name. Otherwise
// returns NULL. Caller has already consumed the IDENT — pass it as base_name
// + base_loc so we can record the use site.
//
// kind = 1 for struct templates (called from type-position parsers).
TypeArgList* parseTypeArgs(Parser* p);  // forward decl; full body below

static char* maybe_consume_type_args_as_struct(Parser* p, const char* base_name, SourceLocation base_loc) {
    if (peek(p, 0)->type != LESS_T) return NULL;
    if (!g_current_program) return NULL;  // parser ran without a program — punt
    TypeArgList* args = parseTypeArgs(p);
    char* mangled = tpl_mangle(base_name, args);
    tpl_push_pending(g_current_program, base_name, args, mangled, base_loc, /*kind=struct*/ 1);
    return mangled;
}

// Map a compound-assign token to the underlying arithmetic op.
// Returns the matching binary-op token, or EOF_T if `t` isn't compound.
static TokenType compound_op_base(TokenType t) {
    switch (t) {
        case PLUS_EQ_T:    return PLUS_T;
        case MINUS_EQ_T:   return MINUS_T;
        case STAR_EQ_T:    return STAR_T;
        case SLASH_EQ_T:   return SLASH_T;
        case PERCENT_EQ_T: return PERCENT_T;
        case PLUS_PLUS_T:  return PLUS_T;   // a++  ->  a = a + 1
        case MINUS_MINUS_T:return MINUS_T;  // a--  ->  a = a - 1
        default:           return EOF_T;
    }
}

static bool is_compound_assign_tok(TokenType t) {
    return compound_op_base(t) != EOF_T;
}

// Parse a single attribute argument: int / bool / string literal. Bare
// identifiers and arbitrary expressions are intentionally rejected for v1
// to keep plugin authoring simple — args are exactly literals.
static AttrArg parseAttrArg(Parser* p) {
    Token* t = consume(p);
    AttrArg a = {0};
    switch (t->type) {
        case INT_LIT_T:
            a.kind = ATTR_ARG_INT;
            a.int_val = *(int*)t->value;
            break;
        case BOOL_LIT_T:
            a.kind = ATTR_ARG_BOOL;
            a.int_val = *(int*)t->value;
            break;
        case STR_LIT_T:
            a.kind = ATTR_ARG_STRING;
            a.str_val = (char*)t->value;
            break;
        default:
            stage_fatal(STAGE_PARSER, TOK_LOC(t),
                        "attribute arguments must be int/bool/string literals, got %s",
                        token_type_name(t->type));
    }
    return a;
}

// `[name]` or `[name(arg, arg, ...)]`. Caller has confirmed peek == '['.
static Attribute* parseAttribute(Parser* p) {
    Token* lb = expect(p, L_BRACKET_T);
    Token* nameTok = expect(p, VAR_T);

    Attribute* a = malloc(sizeof(Attribute));
    a->name      = (char*)nameTok->value;
    a->loc       = TOK_LOC(lb);
    a->arg_count = 0;
    a->args      = NULL;

    if (peek(p, 0)->type == L_PAREN_T) {
        consume(p);
        int cap = 4;
        a->args = malloc(sizeof(AttrArg) * cap);
        while (peek(p, 0)->type != R_PAREN_T && peek(p, 0)->type != EOF_T) {
            if (a->arg_count > 0) expect(p, COMMA_T);
            if (a->arg_count >= cap) {
                cap *= 2;
                a->args = realloc(a->args, sizeof(AttrArg) * cap);
            }
            a->args[a->arg_count++] = parseAttrArg(p);
        }
        expect(p, R_PAREN_T);
    }
    expect(p, R_BRACKET_T);
    return a;
}

// Greedy-collect any leading `[...]` attributes. Returns NULL if none —
// callers attach to StructDecl/Func only when there's something to attach.
static AttributeList* parseAttributeList(Parser* p) {
    if (peek(p, 0)->type != L_BRACKET_T) return NULL;

    AttributeList* list = malloc(sizeof(AttributeList));
    list->capacity = 4;
    list->count    = 0;
    list->items    = malloc(sizeof(Attribute*) * list->capacity);

    while (peek(p, 0)->type == L_BRACKET_T) {
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            list->items = realloc(list->items, sizeof(Attribute*) * list->capacity);
        }
        list->items[list->count++] = parseAttribute(p);
    }
    return list;
}

// Parse a `<T1, T2, ...>` type-parameter list at a template decl site.
// Caller has confirmed peek(0) == LESS_T. Returns NULL if not present.
static TypeParamList* parseTypeParams(Parser* p) {
    if (peek(p, 0)->type != LESS_T) return NULL;
    consume(p);  // <
    TypeParamList* tp = malloc(sizeof(TypeParamList));
    tp->count = 0;
    int cap = 4;
    tp->names = malloc(sizeof(char*) * cap);
    while (peek(p, 0)->type != MORE_T && peek(p, 0)->type != EOF_T) {
        if (tp->count > 0) expect(p, COMMA_T);
        Token* nameTok = expect(p, VAR_T);
        if (tp->count >= cap) {
            cap *= 2;
            tp->names = realloc(tp->names, sizeof(char*) * cap);
        }
        tp->names[tp->count++] = (char*)nameTok->value;
    }
    expect(p, MORE_T);  // >
    return tp;
}

// Parse a `<int, float, MyStruct>` type-argument list at a template use site.
// Caller has confirmed peek(0) == LESS_T. Returns the parsed args.
// Non-static so the type-position helpers above can call it.
TypeArgList* parseTypeArgs(Parser* p) {
    expect(p, LESS_T);
    TypeArgList* ta = malloc(sizeof(TypeArgList));
    ta->count = 0;
    int cap = 4;
    ta->args = malloc(sizeof(TypeArg) * cap);
    while (peek(p, 0)->type != MORE_T && peek(p, 0)->type != EOF_T) {
        if (ta->count > 0) expect(p, COMMA_T);
        Token* t = consume(p);
        if (ta->count >= cap) {
            cap *= 2;
            ta->args = realloc(ta->args, sizeof(TypeArg) * cap);
        }
        TypeArg* a = &ta->args[ta->count++];
        a->type = t->type;
        a->type_name = (t->type == VAR_T) ? (char*)t->value : NULL;
    }
    expect(p, MORE_T);
    return ta;
}

// Heuristic: is the current position the start of a type-arg list?
// Specifically: peek(0) == LESS_T followed by a token that is a type
// (primitive keyword or VAR_T) followed by either COMMA_T or MORE_T,
// and (after closing >) a follow-up token consistent with a use site.
//
// This is what lets us disambiguate `Foo<int>` (template) from `Foo < int`
// (less-than). We only call this when we're in expression position and
// have just seen an IDENT.
//
// `follow_paren_required`: when true, require the closing `>` to be followed
// by `(` — used in expression position to avoid grabbing comparison chains.
static bool looks_like_type_args(Parser* p, bool follow_paren_required) {
    if (peek(p, 0)->type != LESS_T) return false;
    int j = 1;
    int depth = 1;
    int safety = 0;
    while (depth > 0 && safety++ < 32) {
        Token* t = peek(p, j);
        if (t->type == EOF_T) return false;
        switch (t->type) {
            case INT_KEYWORD_T: case BOOL_KEYWORD_T: case STR_KEYWORD_T:
            case CHAR_KEYWORD_T: case FLOAT_KEYWORD_T: case DOUBLE_KEYWORD_T:
            case VOID_KEYWORD_T: case PTR_KEYWORD_T: case VAR_T:
            case COMMA_T:
                j++;
                break;
            case LESS_T:
                depth++; j++; break;
            case MORE_T:
                depth--; j++; break;
            default:
                return false;  // anything else means it's not a type-arg list
        }
    }
    if (depth != 0) return false;
    if (!follow_paren_required) return true;
    return peek(p, j)->type == L_PAREN_T;
}

// Parse exactly one `def name<TParams>(...): T { ... }` function. Extracted
// from parseFunctions so the top-level loop can attach attributes per-function.
// Caller has already confirmed peek == DEF_KEYWORD_T.
static Func* parseSingleFunction(Parser* p) {
    consume(p);                                  // def
    Token* name = expect(p, VAR_T);

    // Optional <T1, T2, ...> turns this into a function template.
    TypeParamList* type_params = NULL;
    if (peek(p, 0)->type == LESS_T) {
        type_params = parseTypeParams(p);
    }

    expect(p, L_PAREN_T);

    int pCount = 0;
    FuncParam* params = parseFuncParams(p, &pCount);

    expect(p, R_PAREN_T);
    expect(p, COLON_T);

    Ownership o = OWNERSHIP_NONE;
    Token* retOwn = peek(p, 0);
    if (retOwn->type == OWN_T)      { consume(p); o = OWNERSHIP_OWN; }
    else if (retOwn->type == REF_T) { consume(p); o = OWNERSHIP_REF; }

    Token* ret = consume(p);
    TokenType retType = ret->type;
    char* retTypeName = (ret->type == VAR_T) ? (char*)ret->value : NULL;
    // Templated return type: `def list_new<T>(): List<T> { ... }` -> List__T.
    if (retType == VAR_T && peek(p, 0)->type == LESS_T) {
        char* mangled = maybe_consume_type_args_as_struct(p, retTypeName, TOK_LOC(ret));
        if (mangled) retTypeName = mangled;
    }
    Stmt* body = parseBlock(p);

    Func* f = makeFunc(name->value, params, pCount, retType, o, body);
    if (retTypeName) f->signature->retTypeName = retTypeName;
    f->type_params = type_params;     // NULL when not a template
    return f;
}

// Parse a function-pointer type starting at the `fn` token:
//   fn(T1, T2, ...): ReturnType
// Returns an anonymous FuncSign (name == NULL). Used in type position
// only — variable type, function param type, extern decl param type.
FuncSign* parseFnType(Parser* p) {
    expect(p, FN_T);
    expect(p, L_PAREN_T);

    FuncSign* sig = malloc(sizeof(FuncSign));
    sig->name         = NULL;
    sig->paramNum     = 0;
    sig->parameters   = NULL;
    sig->retType      = VOID_KEYWORD_T;
    sig->retTypeName  = NULL;
    sig->retOwnership = OWNERSHIP_NONE;
    sig->isExtern     = false;
    sig->retNullable  = false;

    int cap = 4;
    sig->parameters = malloc(sizeof(FuncParam) * cap);

    while (peek(p, 0)->type != R_PAREN_T && peek(p, 0)->type != EOF_T) {
        if (sig->paramNum > 0) expect(p, COMMA_T);

        // Each fn-type param is positional only — just a type, no name.
        // We synthesise a dummy name "_" so the existing FuncParam code
        // (which assumes name presence) keeps working.
        FuncParam fp = {0};
        fp.name = "_";

        Token* tt = consume(p);
        if (tt->type == FN_T) {
            // nested fn types in fn types — backtrack one and recurse
            // (rare but valid: callbacks-of-callbacks).
            p->pos--;
            fp.type    = FN_T;
            fp.fn_sig  = parseFnType(p);
            fp.type_name = NULL;
        } else {
            fp.type      = tt->type;
            fp.type_name = (tt->type == VAR_T) ? (char*)tt->value : NULL;
            fp.fn_sig    = NULL;
        }

        if (sig->paramNum >= cap) {
            cap *= 2;
            sig->parameters = realloc(sig->parameters, sizeof(FuncParam) * cap);
        }
        sig->parameters[sig->paramNum++] = fp;
    }
    expect(p, R_PAREN_T);

    // Return type. Optional: omitted means void.
    if (peek(p, 0)->type == COLON_T) {
        consume(p);
        Token* rt = consume(p);
        sig->retType     = rt->type;
        sig->retTypeName = (rt->type == VAR_T) ? (char*)rt->value : NULL;
    }
    return sig;
}

// Parse a struct declaration: `Name: struct { f1: T1, f2: T2 }`. The
// caller has confirmed the token sequence VAR_T COLON_T STRUCT_T but
// hasn't consumed any of them yet. Field separator is comma; trailing
// comma is allowed. Field types are either primitive type-keyword tokens
// or VAR_T (a previously-declared struct's name).
StructDecl* parseStructDecl(Parser* p) {
    Token* nameTok = expect(p, VAR_T);

    // Optional <T1, T2, ...> turns this into a struct template.
    TypeParamList* type_params = NULL;
    if (peek(p, 0)->type == LESS_T) {
        type_params = parseTypeParams(p);
    }

    expect(p, COLON_T);
    expect(p, STRUCT_T);
    expect(p, L_BRACE_T);

    StructDecl* decl = malloc(sizeof(StructDecl));
    decl->name        = (char*)nameTok->value;
    decl->loc         = TOK_LOC(nameTok);
    decl->field_count = 0;
    decl->attrs       = NULL;       // overwritten by parseProgram if `[..]` precedes
    decl->type_params = type_params;

    int cap = 4;
    decl->fields = malloc(sizeof(StructField) * cap);

    while (peek(p, 0)->type != R_BRACE_T && peek(p, 0)->type != EOF_T) {
        if (decl->field_count > 0) {
            // Lenient separator: prefer comma but accept semicolon too.
            if (peek(p, 0)->type == COMMA_T) consume(p);
            else if (peek(p, 0)->type == SEMICOLON_T) consume(p);
            else break;     // missing separator — let the R_BRACE expect catch it
        }
        if (peek(p, 0)->type == R_BRACE_T) break;   // trailing comma

        Token* fname = expect(p, VAR_T);
        expect(p, COLON_T);
        Token* ftype = consume(p);

        if (decl->field_count >= cap) {
            cap *= 2;
            decl->fields = realloc(decl->fields, sizeof(StructField) * cap);
        }
        StructField* f = &decl->fields[decl->field_count++];
        f->name      = (char*)fname->value;
        f->type      = ftype->type;
        // VAR_T type carries a name (the struct's name); other types are
        // primitives where the C type is derivable from TokenType alone.
        f->type_name = (ftype->type == VAR_T) ? (char*)ftype->value : NULL;
        // Templated field type: `f: List<int>` mangles to `List__int`. This
        // also queues the instantiation so the drain pass realizes it.
        if (ftype->type == VAR_T && peek(p, 0)->type == LESS_T) {
            char* mangled = maybe_consume_type_args_as_struct(p, (char*)ftype->value, TOK_LOC(ftype));
            if (mangled) f->type_name = mangled;
        }
    }
    expect(p, R_BRACE_T);
    return decl;
}

Token* peek(Parser* parser, int offset) {
    //use last tokens location if available
    if (parser->pos + offset >= parser->count) {
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
        consume(p);
        Token* name = expect(p, VAR_T);
        expect(p, L_PAREN_T);

        int pCount = 0;
        FuncParam* params = parseFuncParams(p, &pCount);

        expect(p, R_PAREN_T);
        expect(p, COLON_T);

        Ownership o = OWNERSHIP_NONE;
        Token* retOwn = peek(p, 0);
        if (retOwn->type == OWN_T)
        {
            consume(p);
            o = OWNERSHIP_OWN;
        } else if (retOwn->type == REF_T)
        {
            consume(p);
            o = OWNERSHIP_REF;
        }

        Token* ret = consume(p);
        Stmt* body = parseBlock(p);

        functions[count++] = makeFunc(name->value, params, pCount, ret->type, o, body);

        if(count >= size) {
            size *= 2;
            functions = realloc(functions, sizeof(Func) * size);
        }
    }

    *num = count;
    return functions;
}

ExternBlock* parseExternBlock(Parser* p) {
    consume(p);
    expect(p, LESS_T);
    
    //parse header name (e.g. "math.h") - could be VAR_T, DOT_T, etc.
    //for simplicity, lets just consume until MORE_T and build a string
    char header[256] = {0};
    while(peek(p, 0)->type != MORE_T && peek(p, 0)->type != EOF_T) {
        Token* t = consume(p);
        //rudimentary reconstruction of header name
        if(strlen(header) > 0 && t->type != DOT_T) strcat(header, ""); //simple concat
        //actually for "sys/types.h" etc we might need better logic, but for "math.h"
        //we likely get VAR_T DOT_T VAR_T
        //lets just append the token text if possible, but we dont have text easily for all tokens
        //for now support simple "math.h"
        if(t->type == VAR_T) strcat(header, (char*)t->value);
        else if(t->type == DOT_T) strcat(header, ".");
        else strcat(header, token_type_name(t->type)); //fallback
    }
    expect(p, MORE_T);
    expect(p, L_BRACE_T);

    ExternBlock* block = malloc(sizeof(ExternBlock));
    block->header = strdup(header);
    block->capacity = 4;
    block->count = 0;
    block->signs = malloc(sizeof(FuncSign*) * block->capacity);

    while(peek(p, 0)->type != R_BRACE_T && peek(p, 0)->type != EOF_T) {
        if(peek(p, 0)->type == DEF_KEYWORD_T) {
             consume(p);
             // Extern fn names usually parse as VAR_T, but some libc names
             // collide with Lync keywords (e.g. `free`). Accept any
             // identifier-shaped keyword here too.
             Token* nameTok = peek(p, 0);
             char* name = NULL;
             if (nameTok->type == VAR_T) {
                 name = (char*)consume(p)->value;
             } else if (tok_is_identifier_keyword(nameTok)) {
                 name = (char*)token_type_name(nameTok->type);
                 consume(p);
             } else {
                 nameTok = expect(p, VAR_T); // produce the standard error
                 name = (char*)nameTok->value;
             }
             
             expect(p, L_PAREN_T);
             int paramCount = 0;
             FuncParam* params = parseFuncParams(p, &paramCount); //reuse existing param parser
             expect(p, R_PAREN_T);
             
             expect(p, COLON_T);
             //return type parsing - similar to parseFunc
             TokenType retType = VOID_KEYWORD_T; //default?
             Ownership retOwn = OWNERSHIP_NONE;
             bool retNullable = false;

             Token* typeTok = peek(p, 0);
             if(typeTok->type == OWN_T || typeTok->type == REF_T) {
                 retOwn = (typeTok->type == OWN_T) ? OWNERSHIP_OWN : OWNERSHIP_REF;
                 consume(p);
             }

             //primitive types or void
             if(peek(p, 0)->type == VOID_KEYWORD_T) {
                 consume(p);
                 retType = VOID_KEYWORD_T;
             } else {
                 Token* retTok = consume(p);
                 retType = retTok->type; //assumption: its a type keyword
             }

             //postfix nullable marker: `: ptr?` (and also `: own ptr?`)
             if (peek(p, 0)->type == QUESTION_MARK_T) {
                 consume(p);
                 retNullable = true;
             }

             expect(p, SEMICOLON_T);

             FuncSign* sign = malloc(sizeof(FuncSign));
             sign->name = name;
             sign->parameters = params;
             sign->paramNum = paramCount;
             sign->retType = retType;
             sign->retTypeName = NULL;
             sign->retOwnership = retOwn;
             sign->isExtern = true; //iMPORTANT
             sign->retNullable = retNullable;

             if(block->count >= block->capacity) {
                 block->capacity *= 2;
                 block->signs = realloc(block->signs, sizeof(FuncSign*) * block->capacity);
             }
             block->signs[block->count++] = sign;
        } else {
            //error or skip? 
            consume(p); 
        }
    }
    expect(p, R_BRACE_T);
    return block;
}

// extern decls reuse parseFuncParams above (which now handles fn-types),
// so they get function-pointer parameter support for free.
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
        case CHAR_LIT_T: {
            Token* t = consume(p);
            Expr* e = malloc(sizeof(Expr));
            e->type = CHAR_LIT_E;
            e->loc = TOK_LOC(t);
            e->as.char_val = (char)*(int*)t->value;
            e->is_nullable = false;
            e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
            return e;
        }
        case STR_LIT_T: {
            Token* t = consume(p);
            return makeStrLit(TOK_LOC(t), (char*)t->value);
        }
        case NULL_LIT_T: {
            Token* t = consume(p);
            return makeNullLit(TOK_LOC(t));
        }
        case FLOAT_LIT_T: {
            Token* t = consume(p);
            char* str = (char*)t->value;
            Expr* e = malloc(sizeof(Expr));
            e->type = FLOAT_LIT_E;
            e->loc = TOK_LOC(t);
            e->is_nullable = false;
            e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
            e->as.double_val = strtod(str, NULL);
            //set analyzedType hint for analyzer: f suffix = float, else double
            size_t len = strlen(str);
            if (len > 0 && (str[len-1] == 'f' || str[len-1] == 'F')) {
                e->analyzedType = FLOAT_KEYWORD_T;
            } else {
                e->analyzedType = DOUBLE_KEYWORD_T;
            }
            return e;
        }
        case VAR_T: {
            Token* t = consume(p);
            Expr* base;
            // Template call: `name<int>(args)`. Only treated as a template
            // when the `<...>` shape clearly leads to `(` — otherwise it's
            // a less-than comparison and we fall through to the normal
            // primary handler below.
            char* call_name = (char*)t->value;
            if (peek(p, 0)->type == LESS_T && looks_like_type_args(p, /*follow_paren_required=*/true)) {
                TypeArgList* args_list = parseTypeArgs(p);
                char* mangled = tpl_mangle(call_name, args_list);
                if (g_current_program) {
                    tpl_push_pending(g_current_program, call_name, args_list,
                                     mangled, TOK_LOC(t), /*kind=func*/ 0);
                }
                call_name = mangled;
            }
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
                base = makeFuncCall(TOK_LOC(t), call_name, args, count);
            } else if (peek(p, 0)->type == L_BRACKET_T) {
                consume(p);
                Expr* e = parseExpr(p);
                expect(p, R_BRACKET_T);
                base = makeArrAccess(TOK_LOC(t), t->value, e);
            } else {
                base = makeVar(TOK_LOC(t), (char*)t->value);
            }
            // Chain `.IDENT` after the primary. Two flavours per step:
            //   - `.IDENT(args)`  -> UFCS rewrite to `IDENT(base, args)`
            //                         (Universal Function Call Syntax: lets
            //                         users write `e.HasX()` for `HasX(e)`.
            //                         Trade-off: struct-fn-pointer-field
            //                         calls go through the same path; for
            //                         those, the analyzer decides if there's
            //                         a free function with this name first
            //                         and falls back to field-call if not.
            //                         v1 prefers UFCS unconditionally.)
            //   - `.IDENT`         -> regular field access node.
            while (peek(p, 0)->type == DOT_T) {
                consume(p);
                Token* fieldTok = expect(p, VAR_T);

                // UFCS template call: `expr.name<int>(args)` -> name__int(expr, args).
                char* ufcs_name = (char*)fieldTok->value;
                if (peek(p, 0)->type == LESS_T && looks_like_type_args(p, true)) {
                    TypeArgList* args_list = parseTypeArgs(p);
                    char* mangled = tpl_mangle(ufcs_name, args_list);
                    if (g_current_program) {
                        tpl_push_pending(g_current_program, ufcs_name, args_list,
                                         mangled, TOK_LOC(fieldTok), 0);
                    }
                    ufcs_name = mangled;
                }

                if (peek(p, 0)->type == L_PAREN_T) {
                    // UFCS call: re-route as a regular FUNC_CALL with `base`
                    // injected as the first argument.
                    expect(p, L_PAREN_T);
                    Expr** args = malloc(sizeof(Expr*) * 4);
                    int count    = 1;
                    int capacity = 4;
                    args[0] = base;            // implicit self
                    while (peek(p, 0)->type != R_PAREN_T) {
                        if (count > 1) expect(p, COMMA_T);
                        args[count++] = parseExpr(p);
                        if (count >= capacity) {
                            capacity *= 2;
                            args = realloc(args, sizeof(Expr*) * capacity);
                        }
                    }
                    expect(p, R_PAREN_T);
                    base = makeFuncCall(TOK_LOC(fieldTok), ufcs_name, args, count);
                    continue;
                }

                Expr* fa = malloc(sizeof(Expr));
                fa->type = FIELD_ACCESS_E;
                fa->loc  = TOK_LOC(fieldTok);
                fa->is_nullable = false;
                fa->analyzed_type_name = NULL;
                fa->analyzed_fn_sig = NULL;
                fa->as.field_access.target          = base;
                fa->as.field_access.field_name      = (char*)fieldTok->value;
                fa->as.field_access.field_type      = VOID_KEYWORD_T;
                fa->as.field_access.field_type_name = NULL;
                base = fa;
            }
            return base;
        }
        case UNDERSCORE_T: {
            Token* t = consume(p);
            Expr* e = malloc(sizeof(Expr));
            e->type = VOID_E;
            e->loc = TOK_LOC(t);
            e->is_nullable = false;
            e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
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
            consume(p);
            Expr* expr = parseExpr(p);
            expect(p, R_PAREN_T);
            return expr;
        }
        case L_BRACE_T: {
            Token* t = consume(p);
            Expr** exprs = malloc(sizeof(Expr*) * 2);
            int count = 0;
            int height = 2;
            while (peek(p, 0)->type != R_BRACE_T) {
                Expr* e = parseExpr(p);
                exprs[count++] = e;
                if(peek(p, 0)->type == COMMA_T)
                    consume(p);

                if(count >= height){
                    height *= 2;
                    exprs = realloc(exprs, sizeof(Expr*) * height);
                }
            }
            consume(p);
            return makeArrDecl(TOK_LOC(t), exprs, count);
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
            e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
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
            e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
            e->as.match.var = v;
            return e;
        }
        case RETURN_T: {
            Token* retTok = consume(p);
            Expr* e = malloc(sizeof(Expr));
            e->type = FUNC_RET_E;
            e->loc = TOK_LOC(retTok);
            e->is_nullable = false;
            e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
            if(peek(p, 0)->type == SEMICOLON_T){
                Expr* ve = malloc(sizeof(Expr));
                ve->type = VOID_E;
                ve->loc = TOK_LOC(retTok);
                ve->is_nullable = false;
                ve->analyzed_type_name = NULL;
                ve->analyzed_fn_sig = NULL;
                e->as.func_ret_expr = ve;
            } else
                e->as.func_ret_expr = parseExpr(p);
            return e;
        }
        case ALLOC_T: {
            Token* allocTok = consume(p);

            bool isArr = false;
            Expr* arrSizeExpr = NULL;
            TokenType allocType = VOID_KEYWORD_T; //default if not specified

            if(peek(p, 0)->type == L_BRACKET_T){
                isArr = true;
                consume(p);
                arrSizeExpr = parseExpr(p);
                expect(p, R_BRACKET_T);
                
                //captured allocated type
                Token* typeTok = consume(p); 
                allocType = typeTok->type;
            }

            Expr* e = isArr ? arrSizeExpr : parseExpr(p);

            Expr* al = malloc(sizeof(Expr));
            al->type = ALLOC_E;
            al->loc = TOK_LOC(allocTok);
            al->is_nullable = false;  //alloc always returns a pointer
            al->analyzed_type_name = NULL;
            al->analyzed_fn_sig = NULL;
            al->as.alloc.initialValue = e;
            al->as.alloc.isArray = isArr;
            al->as.alloc.type = allocType; //store the type
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

    // A module-path component is normally an identifier (VAR_T). But some
    // stdlib module names collide with keywords ("string", "free", ...) and
    // must still be usable in a path. Accept those keyword tokens too,
    // turning them back into their textual name via token_type_name().
    Token* first = peek(p, 0);
    if (first->type == VAR_T) {
        parts[part_count++] = consume(p)->value;
    } else if (tok_is_identifier_keyword(first)) {
        parts[part_count++] = (char*)token_type_name(first->type);
        consume(p);
    } else {
        expect(p, VAR_T); // produce the standard error
    }

    while (peek(p, 0)->type == DOT_T) {
        consume(p);

        Token* nxt = peek(p, 0);
        if (nxt->type == STAR_T) {
            //wildcard: everything so far is the module
            consume(p);

            //build module name from all parts
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
        } else if (nxt->type == VAR_T) {
            if (part_count >= part_capacity) {
                part_capacity *= 2;
                parts = realloc(parts, sizeof(char*) * part_capacity);
            }
            parts[part_count++] = consume(p)->value;
        } else if (tok_is_identifier_keyword(nxt)) {
            // Keyword as path component (e.g. `std.string.*`).
            if (part_count >= part_capacity) {
                part_capacity *= 2;
                parts = realloc(parts, sizeof(char*) * part_capacity);
            }
            parts[part_count++] = (char*)token_type_name(nxt->type);
            consume(p);
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

    prog->ext_block_count = 0;
    int ext_cap = 4;
    prog->externBlocks = malloc(sizeof(ExternBlock*) * ext_cap);

    prog->struct_count = 0;
    int struct_cap = 4;
    prog->structs = malloc(sizeof(StructDecl*) * struct_cap);

    int func_cap = 4;
    prog->func_count = 0;
    prog->functions  = malloc(sizeof(Func*) * func_cap);

    // Template support: pending instantiations queued by the type/call
    // parsers; drained after the main parse loop finishes.
    prog->pending = NULL;
    prog->pending_count = 0;
    prog->pending_capacity = 0;
    prog->instantiated_names = NULL;
    prog->instantiated_count = 0;
    prog->instantiated_capacity = 0;
    g_current_program = prog;

    // Single top-level loop handles every decl kind. Attributes are
    // collected eagerly before each decl and attached only to those that
    // accept them (struct, def). For others (include, extern), passing
    // attributes is silently ignored — could become an error later.
    while (peek(p, 0)->type != EOF_T) {
        AttributeList* pending_attrs =
            (peek(p, 0)->type == L_BRACKET_T) ? parseAttributeList(p) : NULL;

        if (peek(p, 0)->type == INCLUDE_T) {
            if (prog->imports->import_count >= prog->imports->import_capacity) {
                prog->imports->import_capacity *= 2;
                prog->imports->imports = realloc(prog->imports->imports,
                                                 sizeof(IncludeStmt *) * prog->imports->import_capacity);
            }
            prog->imports->imports[prog->imports->import_count++] = parseIncludeStmt(p);
            // pending_attrs ignored — attributes don't make sense on include
        } else if (peek(p, 0)->type == EXTERN_T) {
            if (prog->ext_block_count >= ext_cap) {
                ext_cap *= 2;
                prog->externBlocks = realloc(prog->externBlocks, sizeof(ExternBlock*) * ext_cap);
            }
            prog->externBlocks[prog->ext_block_count++] = parseExternBlock(p);
        } else if (peek(p, 0)->type == VAR_T
                   && (
                        // Plain struct decl: `Name : struct { ... }`
                        (peek(p, 1)->type == COLON_T && peek(p, 2)->type == STRUCT_T)
                        // Templated struct decl: `Name<T,...> : struct { ... }`.
                        // We probe to find the matching `>` followed by `: struct`.
                        || (peek(p, 1)->type == LESS_T &&
                            ({ int j = 2; int depth = 1; int safe = 0;
                               while (depth > 0 && safe++ < 32) {
                                   TokenType tt = peek(p, j)->type;
                                   if (tt == EOF_T) break;
                                   if (tt == LESS_T) depth++;
                                   else if (tt == MORE_T) depth--;
                                   j++;
                               }
                               (depth == 0
                                && peek(p, j)->type == COLON_T
                                && peek(p, j+1)->type == STRUCT_T); }))
                      )) {
            // `Name: struct { ... }` — unique 3-token shape at top level.
            if (prog->struct_count >= struct_cap) {
                struct_cap *= 2;
                prog->structs = realloc(prog->structs, sizeof(StructDecl*) * struct_cap);
            }
            StructDecl* sd = parseStructDecl(p);
            sd->attrs = pending_attrs;
            prog->structs[prog->struct_count++] = sd;
        } else if (peek(p, 0)->type == DEF_KEYWORD_T) {
            if (prog->func_count >= func_cap) {
                func_cap *= 2;
                prog->functions = realloc(prog->functions, sizeof(Func*) * func_cap);
            }
            Func* fn = parseSingleFunction(p);
            fn->attrs = pending_attrs;
            prog->functions[prog->func_count++] = fn;
        } else {
            stage_fatal(STAGE_PARSER, TOK_LOC(peek(p, 0)),
                        "unexpected token at top level: %s",
                        token_type_name(peek(p, 0)->type));
        }
    }

    //end of file
    expect(p, EOF_T);

    // Realize all template instantiations queued during the main parse. The
    // drain may itself enqueue more (a template body that uses Foo<T>); the
    // drain loop iterates until empty. We use the non-strict mode here:
    // templates from `include`d files (notably the stdlib) are merged AFTER
    // parseProgram returns, so unresolved pendings stay queued for a later
    // strict drain that runs once all merging is complete.
    tpl_drain_pending(prog, false);

    g_current_program = NULL;
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

    bool isConst = false;
    switch (t->type) {
        case CONST_T: {
            consume(p);
            isConst = true;
            //continues to var as it will be var
        }
        case VAR_T: {
            Stmt *s = malloc(sizeof(Stmt));
            if (peek(p, 1)->type == COLON_T) {
                Ownership o = OWNERSHIP_NONE;
                Token* varTok = consume(p);
                char *name = varTok->value;
                expect(p, COLON_T);

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

                bool isArray = false;
                Expr* arrSize = NULL;
                if(peek(p, 0)->type == L_BRACKET_T){
                    isArray = true;
                    consume(p);
                    arrSize = parseExpr(p);
                    expect(p, R_BRACKET_T);
                }

                //parse element ownership (for [N] own int)
                Ownership elemOwnership = OWNERSHIP_NONE;
                if(isArray && peek(p, 0)->type == OWN_T) {
                    consume(p);
                    elemOwnership = OWNERSHIP_OWN;
                } else if(isArray && peek(p, 0)->type == REF_T) {
                    consume(p);
                    elemOwnership = OWNERSHIP_REF;
                }

                // Type can be a primitive keyword OR a VAR_T identifier
                // (struct type name) OR a fn(...) function-pointer type.
                // The analyzer checks the name resolves to a registered
                // struct (for VAR_T) or that fn-typed init expressions
                // match the declared signature.
                TokenType varType = VOID_KEYWORD_T;
                char*     typeName = NULL;
                FuncSign* fnSig    = NULL;
                if (peek(p, 0)->type == FN_T) {
                    varType = FN_T;
                    fnSig   = parseFnType(p);
                } else {
                    Token* typeTok = consume(p);
                    varType = typeTok->type;
                    typeName = (varType == VAR_T) ? (char*)typeTok->value : NULL;
                    // Templated var-decl type: `nums: List<int>` -> mangled.
                    if (varType == VAR_T && peek(p, 0)->type == LESS_T) {
                        char* mangled = maybe_consume_type_args_as_struct(p, typeName, TOK_LOC(typeTok));
                        if (mangled) typeName = mangled;
                    }
                }

                Expr *e = NULL;
                if (peek(p, 0)->type == EQUALS_T) {
                    consume(p);
                    e = parseExpr(p);
                } else if (isArray) {
                    //array without initializer - create VOID expression as placeholder
                    e = malloc(sizeof(Expr));
                    e->type = VOID_E;
                    e->analyzed_type_name = NULL;
                    e->analyzed_fn_sig = NULL;
                    e->loc = TOK_LOC(peek(p, 0));
                    e->is_nullable = false;
                } else if (varType == VAR_T) {
                    // Struct types default-init to all zeros — no `= ...`
                    // required. Lets users write `p: Point;` then assign
                    // fields one by one without inventing literal syntax.
                    e = malloc(sizeof(Expr));
                    e->type = VOID_E;
                    e->analyzed_type_name = NULL;
                    e->analyzed_fn_sig = NULL;
                    e->loc = TOK_LOC(peek(p, 0));
                    e->is_nullable = false;
                } else if (varType == FN_T) {
                    stage_fatal(STAGE_PARSER, TOK_LOC(peek(p, 0)),
                                "function-pointer variable '%s' requires an initializer", name);
                } else {
                    //non-array primitive variables still must have an initializer
                    stage_fatal(STAGE_PARSER, TOK_LOC(peek(p, 0)),
                                "variable declaration requires initializer (expected '=')");
                }
                expect(p, SEMICOLON_T);

                s->type = VAR_DECL_S;
                s->loc = TOK_LOC(varTok);
                s->as.var_decl.expr = e;
                s->as.var_decl.varType = varType;
                s->as.var_decl.typeName = typeName;
                s->as.var_decl.fnSig = fnSig;
                s->as.var_decl.name = name;
                s->as.var_decl.ownership = o;
                s->as.var_decl.elementOwnership = elemOwnership;
                s->as.var_decl.isNullable = isNullable;
                s->as.var_decl.isConst = isConst;
                s->as.var_decl.isArray = isArray;
                s->as.var_decl.arraySize = arrSize;
            } else if (peek(p, 1)->type == L_BRACKET_T) {
                //array element assignment: arr[i] = value
                //also: arr[i] += value, arr[i]++, arr[i]--, etc.
                Token* arrayTok = consume(p);
                char* arrayName = arrayTok->value;
                consume(p);
                Expr* index = parseExpr(p);
                expect(p, R_BRACKET_T);

                Expr* value = NULL;
                Token* opTok = peek(p, 0);
                if (opTok->type == EQUALS_T) {
                    consume(p);
                    value = parseExpr(p);
                } else if (is_compound_assign_tok(opTok->type)) {
                    consume(p);
                    SourceLocation oloc = TOK_LOC(opTok);
                    TokenType base = compound_op_base(opTok->type);
                    Expr* rhs;
                    if (opTok->type == PLUS_PLUS_T || opTok->type == MINUS_MINUS_T) {
                        rhs = makeIntLit(oloc, 1);
                    } else {
                        rhs = parseExpr(p);
                    }
                    Expr* lhs_read = makeArrAccess(TOK_LOC(arrayTok), arrayName, index);
                    value = makeBinOp(oloc, lhs_read, base, rhs);
                } else {
                    stage_fatal(STAGE_PARSER, TOK_LOC(opTok),
                        "expected '=' or compound assign after array index, got %s",
                        token_type_name(opTok->type));
                }
                expect(p, SEMICOLON_T);

                s->type = ARRAY_ELEM_ASSIGN_S;
                s->loc = TOK_LOC(arrayTok);
                s->as.array_elem_assign.arrayName = arrayName;
                s->as.array_elem_assign.index = index;
                s->as.array_elem_assign.value = value;
            } else if (peek(p, 1)->type == DOT_T) {
                // Field assignment: target.field [.field2 ...] = value;
                // Parse the LHS as a normal expression — it'll come back
                // as a chain of FIELD_ACCESS_E rooted at the original Var.
                // The trailing `=` distinguishes assignment from a plain
                // expression statement; if no `=`, fall through to the
                // generic expression-statement branch below.
                Token* startTok = peek(p, 0);
                Expr* lhs = parseExpr(p);

                if (peek(p, 0)->type == EQUALS_T ||
                        is_compound_assign_tok(peek(p, 0)->type)) {
                    if (lhs->type != FIELD_ACCESS_E) {
                        stage_fatal(STAGE_PARSER, lhs->loc,
                                    "invalid assignment target (expected field access)");
                    }
                    Token* opTok = consume(p);  // = or += etc.
                    Expr* value;
                    if (opTok->type == EQUALS_T) {
                        value = parseExpr(p);
                    } else {
                        SourceLocation oloc = TOK_LOC(opTok);
                        TokenType base = compound_op_base(opTok->type);
                        Expr* rhs;
                        if (opTok->type == PLUS_PLUS_T || opTok->type == MINUS_MINUS_T) {
                            rhs = makeIntLit(oloc, 1);
                        } else {
                            rhs = parseExpr(p);
                        }
                        value = makeBinOp(oloc, lhs, base, rhs);
                    }
                    expect(p, SEMICOLON_T);

                    s->type = FIELD_ASSIGN_S;
                    s->loc = TOK_LOC(startTok);
                    s->as.field_assign.target     = lhs->as.field_access.target;
                    s->as.field_assign.field_name = lhs->as.field_access.field_name;
                    s->as.field_assign.value      = value;
                } else {
                    // Bare expression like `p.x;` — keep as expression stmt.
                    expect(p, SEMICOLON_T);
                    s->type = EXPR_STMT_S;
                    s->loc = lhs->loc;
                    s->as.expr_stmt = lhs;
                }
            } else if (peek(p, 1)->type == EQUALS_T ||
                       is_compound_assign_tok(peek(p, 1)->type)) {
                // Plain assignment + compound assigns (a = ..., a += ..., a++)
                // all desugar into ASSIGN_S. The compound forms wrap the rhs
                // in a BIN_OP so codegen / analyzer don't need to know about
                // them at all.
                Token* varTok = consume(p);
                char* name = varTok->value;
                Token* opTok = consume(p);  // = or += etc.

                Expr* e;
                if (opTok->type == EQUALS_T) {
                    e = parseExpr(p);
                } else {
                    SourceLocation oloc = TOK_LOC(opTok);
                    TokenType base = compound_op_base(opTok->type);
                    Expr* rhs;
                    if (opTok->type == PLUS_PLUS_T || opTok->type == MINUS_MINUS_T) {
                        rhs = makeIntLit(oloc, 1);
                    } else {
                        rhs = parseExpr(p);
                    }
                    e = makeBinOp(oloc, makeVar(TOK_LOC(varTok), name), base, rhs);
                }
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
                s->loc = e->loc;  //use expressions location
                s->as.expr_stmt = e;
            } else if (peek(p, 1)->type == R_BRACKET_T); //for = T[expr];
            else stage_fatal(STAGE_PARSER, TOK_LOC(peek(p, 1)), "Unexpected token after variable: %s", token_type_name(peek(p, 1)->type));
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
                consume(p);
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
            //parse as expression statement
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
        bool isConst = false;
        if(t->type == CONST_T) {
            isConst = true;
            t = consume(p);  //now get the actual parameter name
        }

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

        FuncParam fp;
        memset(&fp, 0, sizeof(fp));
        fp.name      = t->value;
        fp.ownership = o;
        fp.isNullable = isNullable;
        fp.isConst   = isConst;

        if (peek(p, 0)->type == FN_T) {
            fp.type   = FN_T;
            fp.fn_sig = parseFnType(p);
        } else {
            Token* type = consume(p);
            fp.type      = type->type;
            fp.type_name = (type->type == VAR_T) ? (char*)type->value : NULL;
            // Templated parameter type: `l: ref List<int>` mangles List__int.
            if (type->type == VAR_T && peek(p, 0)->type == LESS_T) {
                char* mangled = maybe_consume_type_args_as_struct(p, fp.type_name, TOK_LOC(type));
                if (mangled) fp.type_name = mangled;
            }
        }

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
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.int_val = val;
    return e;
}
Expr* makeBoolLit(SourceLocation loc, bool val) {
    Expr* e = malloc(sizeof(Expr));
    e->type = BOOL_LIT_E;
    e->loc = loc;
    e->is_nullable = false;
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.bool_val = val;
    return e;
}
Expr* makeStrLit(SourceLocation loc, char* val) {
    Expr* e = malloc(sizeof(Expr));
    e->type = STR_LIT_E;
    e->loc = loc;
    e->is_nullable = false;
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.str_val = val;
    return e;
}
Expr* makeNullLit(SourceLocation loc) {
    Expr* e = malloc(sizeof(Expr));
    e->type = NULL_LIT_E;
    e->loc = loc;
    e->is_nullable = true;  //null is always nullable
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    return e;
}
Expr* makeVar(SourceLocation loc, char* name) {
    Expr* e = malloc(sizeof(Expr));
    e->type = VAR_E;
    e->loc = loc;
    e->is_nullable = false;  //will be determined by analyzer
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.var.name = name;
    e->as.var.ownership = OWNERSHIP_NONE;
    e->as.var.isConst = false;
    return e;
}
Expr* makeArrAccess(SourceLocation loc, char* name, Expr* index) {
    Expr* e = malloc(sizeof(Expr));
    e->type = ARRAY_ACCESS_E;
    e->loc = loc;
    e->is_nullable = false;
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.array_access.arrayName = name;
    e->as.array_access.index = index;
    return e;
}
Expr* makeArrDecl(SourceLocation loc, Expr** exprs, int count) {
    Expr* e = malloc(sizeof(Expr));
    e->type = ARRAY_DECL_E;
    e->loc = loc;
    e->is_nullable = false;
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.arr_decl.values = exprs;
    e->as.arr_decl.count = count;
    return e;
}
Expr* makeUnOp(SourceLocation loc, TokenType t, Expr* expr) {
    Expr* e = malloc(sizeof(Expr));
    e->type = UN_OP_E;
    e->loc = loc;
    e->is_nullable = false;
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.un_op.expr = expr;
    e->as.un_op.op = t;
    return e;
}
Expr* makeBinOp(SourceLocation loc, Expr* el, TokenType t, Expr* er) {
    Expr* e = malloc(sizeof(Expr));
    e->type = BIN_OP_E;
    e->loc = loc;
    e->is_nullable = false;
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
    e->as.bin_op.op = t;
    e->as.bin_op.exprL = el;
    e->as.bin_op.exprR = er;
    return e;
}
Expr* makeFuncCall(SourceLocation loc, char* n, Expr** params, int paramC) {
    Expr* e = malloc(sizeof(Expr));
    e->type = FUNC_CALL_E;
    e->loc = loc;
    e->is_nullable = false;  //will be determined by analyzer for read_* functions
    e->analyzed_type_name = NULL;
    e->analyzed_fn_sig = NULL;
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

Func* makeFunc(char* name, FuncParam* params, int paramCount, TokenType ret, Ownership retOwnership, Stmt* body) {
    Func* f = malloc(sizeof(Func));
    f->body = body;
    f->attrs = NULL;                // attached later by parseProgram if any
    f->signature = malloc(sizeof(FuncSign));
    f->signature->name = name;
    f->signature->parameters = params;
    f->signature->paramNum = paramCount;
    f->signature->retType = ret;
    f->signature->retTypeName = NULL;   // struct-return support is a future turn
    f->signature->retOwnership = retOwnership;
    f->signature->isExtern = false;   // user functions are never extern (was uninit)
    f->signature->retNullable = false;
    f->type_params = NULL;
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

//aST printing functions (only active in trace mode, output to stderr)
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
        case ALLOC_E:
            fprintf(stderr, "Alloc:\n");
            print_expr(e->as.alloc.initialValue, depth + 1);
            break;
        case ARRAY_DECL_E:
            fprintf(stderr, "ArrayDecl: [%d elements]\n", e->as.arr_decl.count);
            break;
        case ARRAY_ACCESS_E:
            fprintf(stderr, "ArrayAccess: %s[...]\n", e->as.array_access.arrayName);
            break;
        default:
            fprintf(stderr, "Expr (type=%d)\n", e->type);
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

            //only print else if it exists
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