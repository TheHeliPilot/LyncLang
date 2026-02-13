//
// Created by bucka on 2/12/2026.
//

#include "codegen.h"

static const char* get_asm_type(TokenType t) {
    switch (t) {
        case INT_KEYWORD_T: return "quad";
        case BOOL_KEYWORD_T: return "byte";
        default: return "byte";
    }
}

void generate_assembly(Func** program, int count, FILE* out) {
    //emit headers
    fprintf(out, "\t.section __TEXT,__text\n");
    fprintf(out, "\t.globl _main\n");

    for (int i = 0; i < count; i++) {
        Func* f = program[i];

        //function label
        if (strcmp(f->signature->name, "main") == 0) {
            fprintf(out, "_main:\n");
        } else {
            fprintf(out, "_%s:\n", f->signature->name);
        }

        fprintf(out, "\tpushq %%rbp\n");
        fprintf(out, "\tmovq %%rsp, %%rbp\n");

        fprintf(out, "\tsubq $32, %%rsp\n");

        // TODO: Actually generate code for the function body
        //haha dont think i will soon

        //default implementation - just return 0 for main
        if (strcmp(f->signature->name, "main") == 0) {
            fprintf(out, "\tmovl $0, %%eax\n");
        }

        fprintf(out, "\tleave\n");
        fprintf(out, "\tret\n\n");
    }
}