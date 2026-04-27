//created by bucka on 2/9/2026.

#ifndef LYNC_PARSER_H
#define LYNC_PARSER_H

#include "common.h"
#include "lexer.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Func Func;
typedef struct Func Func;
typedef struct ExternBlock ExternBlock;
typedef struct StructDecl StructDecl;
typedef struct Attribute Attribute;
typedef struct AttributeList AttributeList;

// One literal-typed attribute argument. For v1 we accept int/bool/string
// literals only — positional, no named args yet. Richer expressions come
// when we wire `comptime` evaluation; v1 is intentionally simple so plugin
// authors don't have to evaluate Lync expressions.
typedef enum {
    ATTR_ARG_INT,
    ATTR_ARG_BOOL,
    ATTR_ARG_STRING,
} AttrArgKind;

typedef struct {
    AttrArgKind kind;
    int    int_val;     // for INT, BOOL
    char*  str_val;     // for STRING
} AttrArg;

// `[name]`, `[name(arg1, arg2, ...)]`. Stored as metadata on StructDecl
// and Func — Lync core doesn't interpret the name; plugins do.
struct Attribute {
    char*           name;
    AttrArg*        args;
    int             arg_count;
    SourceLocation  loc;
};

struct AttributeList {
    Attribute** items;
    int         count;
    int         capacity;
};

typedef enum {
    OWNERSHIP_NONE,
    OWNERSHIP_OWN,
    OWNERSHIP_REF,
} Ownership;

typedef enum {
    IMPORT_ALL,
    IMPORT_SPECIFIC,
} ImportType;

typedef struct {
    char* module_name;
    ImportType type;
    char* function_name;
    SourceLocation loc;
} IncludeStmt;

typedef struct {
    IncludeStmt** imports;
    int import_count;
    int import_capacity;
} ImportList;

// Struct field. `type` carries the primitive (INT_KEYWORD_T, FLOAT_KEYWORD_T,
// ...) for built-ins. When the field's type is another struct, `type` is
// VAR_T and `type_name` is the struct's name. Same convention is used on
// var_decl + Symbol so the analyzer/codegen can route uniformly.
typedef struct {
    char*        name;
    TokenType    type;
    char*        type_name;     // non-NULL only when type == VAR_T
} StructField;

// ----------------------------------------------------------------------------
// Template support.
//
// Lync templates are C++-style: pure compile-time monomorphisation, no runtime
// cost, body duck-typed at instantiation. A function or struct decl that
// carries a non-NULL `type_params` is a template — the analyzer never sees the
// template directly. Instead, after parsing, an instantiation pass walks the
// "pending" queue (filled at parse time when the parser sees use sites like
// `Foo<int>` or `bar<int>(...)`), clones the template AST, substitutes type
// params, and registers the resulting concrete decl under a mangled name
// (`Foo__int`, `bar__int`).
// ----------------------------------------------------------------------------
typedef struct {
    char** names;     // e.g. ["T", "U"]
    int    count;
} TypeParamList;

// One concrete type argument used at a template use site.
typedef struct {
    TokenType type;        // INT_KEYWORD_T, FLOAT_KEYWORD_T, ..., or VAR_T
    char*     type_name;   // when type == VAR_T (a struct name)
} TypeArg;

typedef struct {
    TypeArg* args;
    int      count;
} TypeArgList;

// Pending template instantiation request, queued at parse time and drained
// after parseProgram completes.
typedef struct {
    char*           template_name;   // unmangled, e.g. "List"
    TypeArgList*    type_args;       // owned
    char*           mangled_name;    // owned, e.g. "List__int"
    SourceLocation  use_loc;         // for "instantiated from <here>" notes
    int             kind;            // 0 = function, 1 = struct
} PendingInstantiation;

struct StructDecl {
    char*            name;
    StructField*     fields;
    int              field_count;
    AttributeList*   attrs;          // attached `[...]` attributes (may be NULL)
    SourceLocation   loc;
    TypeParamList*   type_params;    // NULL = concrete struct; non-NULL = template
};

typedef struct {
    ImportList* imports;
    ExternBlock** externBlocks;
    int ext_block_count;
    StructDecl** structs;       // user-defined struct types (concrete + monomorphised)
    int struct_count;
    Func** functions;           // concrete + monomorphised functions
    int func_count;

    // Template support (see comment above TypeParamList).
    PendingInstantiation** pending;     // queued at parse time
    int pending_count;
    int pending_capacity;
    char** instantiated_names;          // mangled names already realized (memo)
    int    instantiated_count;
    int    instantiated_capacity;
} Program;

typedef struct {
    TokenType type;
    char* name;
    char* type_name;            // non-NULL when type == VAR_T (struct type)
    struct FuncSign* fn_sig;    // non-NULL when type == FN_T (function pointer)
    Ownership ownership;
    bool isNullable;
    bool isConst;
} FuncParam;

typedef struct FuncSign FuncSign;
struct FuncSign {
    char* name;                  // NULL for anonymous fn-type sigs
    FuncParam* parameters;
    int paramNum;
    TokenType retType;
    char* retTypeName;           // non-NULL when retType == VAR_T (struct return)
    Ownership retOwnership;
    bool isExtern;
    bool retNullable;            // when true, callers must treat the result as nullable
};

struct Func {
    FuncSign*      signature;
    Stmt*          body;
    AttributeList* attrs;            // attached `[...]` attributes (may be NULL)
    TypeParamList* type_params;      // NULL = concrete function; non-NULL = template
};

struct ExternBlock {
    char* header;
    FuncSign** signs;
    int count;
    int capacity;
};

typedef enum {
    NULL_PATTERN,      //null
    SOME_PATTERN,      //some(binding_name)
    WILDCARD_PATTERN,  //_
    VALUE_PATTERN,     //any expression (for non-nullable matches)
} PatternType;

typedef struct Pattern {
    PatternType type;
    SourceLocation loc;
    union {
        char* binding_name;  //for SOME_PATTERN
        Expr* value_expr;    //for VALUE_PATTERN
    } as;
} Pattern;

typedef struct {
    Pattern* pattern;
    Expr* caseRet;
    TokenType analyzed_type; //filled by analyzer
} MatchBranchExpr;

typedef struct {
    Pattern* pattern;
    Stmt** stmts;
    int stmtCount;
    TokenType analyzed_type;    //filled by analyzer
} MatchBranchStmt;

typedef enum {
    //literals
    INT_LIT_E, BOOL_LIT_E, STR_LIT_E, CHAR_LIT_E, FLOAT_LIT_E, NULL_LIT_E,

    //vars
    VAR_E, ARRAY_ACCESS_E,

    //funcs
    FUNC_CALL_E, FUNC_RET_E,

    //other
    MATCH_E, VOID_E, ARRAY_DECL_E,

    //mem
    ALLOC_E, ALLOC_ARR_E,

    //match
    SOME_E,

    //operations
    UN_OP_E, BIN_OP_E,

    //structs
    STRUCT_LIT_E,    //Name { field: value, ... }
    FIELD_ACCESS_E,  //expr.field
} ExprType;

struct Expr {
    ExprType type;
    SourceLocation loc;
    TokenType analyzedType;  //filled in by analyzer
    char* analyzed_type_name; //filled in by analyzer when analyzedType == VAR_T (struct)
    FuncSign* analyzed_fn_sig; //filled in by analyzer when expression is a function reference
    bool is_nullable;        //filled in by analyzer for nullable return types

    union {
        int int_val;

        int bool_val;

        float float_val;
        double double_val;

        char char_val;

        char* str_val;

        struct {
            char* name;
            Ownership ownership;
            bool isConst;
        } var;

        struct {
            char* arrayName;
            Expr* index;
        } array_access;

        struct {
            TokenType op;
            struct Expr* expr;
        } un_op;

        struct {
            struct Expr* exprL;
            TokenType op;
            struct Expr* exprR;
        } bin_op;

        struct {
            char* name;
            Expr** params;
            int count;
            FuncSign* resolved_sign;
        } func_call;

        struct {
            Expr** values;
            int count;
            TokenType resolvedType;
        } arr_decl;

        Expr* func_ret_expr;

        struct {
            Expr* initialValue;
            TokenType type;
            bool isArray;
        } alloc;

        struct {
            Expr* var;
            MatchBranchExpr* branches;
            int branchCount;
        } match;

        struct {
            Expr* var;
        } some;

        // Struct literal: `Name { f1: v1, f2: v2, ... }`. Field order in
        // the source doesn't have to match struct decl order — codegen
        // emits designated initializers by name so it's robust to that.
        struct {
            char*  type_name;        // e.g. "Position"
            char** field_names;      // length = field_count
            Expr** field_values;
            int    field_count;
        } struct_lit;

        // Field access: `target.field_name`. Chains naturally (a.b.c parses
        // as ((a.b).c)). Codegen handles the C `.` directly.
        struct {
            Expr* target;
            char* field_name;
            // Resolved by analyzer:
            TokenType field_type;
            char*     field_type_name;   // for nested struct fields
        } field_access;
    } as;
};

typedef enum {
    VAR_DECL_S,         //x: int = 5;
    ASSIGN_S,           //x = 5;
    ARRAY_ELEM_ASSIGN_S, //arr[i] = value;
    FIELD_ASSIGN_S,      //target.field = value;
    IF_S,               //if cond { } else { }
    WHILE_S,            //while cond { }
    DO_WHILE_S,         //do { } while cond
    FOR_S,              //for (var: min to max) { }
    BLOCK_S,            //{ stmt; stmt; stmt; }
    MATCH_S,
    FREE_S,
    EXPR_STMT_S,        //expression as statement
} StmtType;

struct Stmt {
    StmtType type;
    SourceLocation loc;

    union {
        struct {
            char* name;
            TokenType varType;
            char* typeName;             // non-NULL when varType == VAR_T (struct type)
            FuncSign* fnSig;            // non-NULL when varType == FN_T (function pointer)
            Ownership ownership;
            Ownership elementOwnership; //ownership of each element (for [N] own int)
            bool isNullable;
            bool isConst;
            bool isArray;
            Expr* arraySize;
            Expr* expr;
        } var_decl;

        struct {
            char* name;
            Expr* expr;
            Ownership ownership;
            bool isArray;
            int arraySize;
        } var_assign;

        struct {
            Expr* cond;
            Stmt* trueStmt;
            Stmt* falseStmt;
        } if_stmt;

        struct {
            Expr* cond;
            Stmt* body;
        } while_stmt;

        struct {
            Expr* cond;
            Stmt* body;
        } do_while_stmt;

        struct {
            char* varName;
            Expr* min;
            Expr* max;
            Stmt* body;
        } for_stmt;

        struct {
            Stmt** stmts;
            int count;
        } block_stmt;

        struct {
            Expr* var;
            MatchBranchStmt* branches;
            int branchCount;
        } match_stmt;

        struct {
            char* varName;
            bool isArrayOfOwned;  //set by analyzer: array has element ownership
            int arraySize;        //set by analyzer: number of elements to free
        } free_stmt;

        struct {
            char* arrayName;
            Expr* index;
            Expr* value;
        } array_elem_assign;

        // target.field = value;   target is any expression (chained access ok)
        struct {
            Expr* target;
            char* field_name;
            Expr* value;
        } field_assign;

        Expr* expr_stmt;

    } as;
};

typedef struct {
    Token* tokens;
    int count;
    int size;
    int pos;
} Parser;

Token* peek(Parser*, int);
Token* consume(Parser*);
Token* expect(Parser*, TokenType);

Expr* makeIntLit(SourceLocation, int);
Expr* makeBoolLit(SourceLocation, bool);
Expr* makeStrLit(SourceLocation, char*);
Expr* makeNullLit(SourceLocation);
Expr* makeVar(SourceLocation, char*);
Expr* makeArrAccess(SourceLocation, char*, Expr*);
Expr* makeFuncCall(SourceLocation, char*, Expr**, int);
Expr* makeArrDecl(SourceLocation, Expr**, int);
Expr* makeUnOp(SourceLocation, TokenType, Expr*);
Expr* makeBinOp(SourceLocation, Expr*, TokenType, Expr*);

Stmt* makeVarDecl(SourceLocation, char*, TokenType, Expr*);
Stmt* makeAssign(SourceLocation, char*, Expr*);
Stmt* makeIf(SourceLocation, Expr*, Stmt*, Stmt*);
Stmt* makeWhile(SourceLocation, Expr*, Stmt*);
Stmt* makeBlock(SourceLocation, Stmt**, int);
Stmt* makeExprStmt(SourceLocation, Expr*);

Func* makeFunc(char*, FuncParam*, int, TokenType, Ownership, Stmt*);
bool check_func_sign(FuncSign *a, FuncSign *b);
bool check_func_sign_unwrapped(FuncSign* a, char* name, int paramNum, Expr** parameters);

Program* parseProgram(Parser*);

// ---- template.c surface ---------------------------------------------------
char* tpl_mangle(const char* base_name, const TypeArgList* args);
bool  tpl_already_instantiated(const Program* p, const char* mangled);
void  tpl_push_pending(Program* p, const char* template_name,
                       TypeArgList* args, const char* mangled,
                       SourceLocation loc, int kind);
// Realize queued template instantiations. With strict=true, missing
// templates produce a parse error and the pending entry is dropped. With
// strict=false, unresolved pendings are kept so a later strict drain (after
// stdlib / file merging) can retry them.
void  tpl_drain_pending(Program* p, bool strict);
Stmt* parseStatement(Parser*);
Stmt* parseBlock(Parser*);

Expr* parseExpr(Parser*);
Expr* parseAnd(Parser*);
Expr* parseComparison(Parser*);
Expr* parseAdd(Parser*);
Expr* parseTerm(Parser*);
Expr* parseFactor(Parser*);
Func** parseFunctions(Parser* p, int*);
FuncParam* parseFuncParams(Parser*, int*);

void print_ast(Func**, int);

#endif //lYNC_PARSER_H
