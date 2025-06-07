#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//--------------------------------------------------- Defines
#define MAX_LINES     2048      // Maximum number of AST lines
#define MAX_LINE_LEN   512      // Maximum length of each AST line

//--------------------------------------------------- AST Line Structure
typedef struct {
    int   indent;             // Indentation level (in 4-space units)
    char  text[MAX_LINE_LEN]; // AST line text
} ASTLine;

//--------------------------------------------------- Globals
static ASTLine lines[MAX_LINES];
static int     line_count = 0;
static int     current_line = 0;
static int     temp_counter = 0;
static int     label_counter = 0;

//--------------------------------------------------- Utility: generate new temp and label
static char *new_temp() {
    char buf[32];
    snprintf(buf, sizeof(buf), "t%d", temp_counter++);
    return strdup(buf);
}

static char *new_label() {
    char buf[32];
    snprintf(buf, sizeof(buf), "L%d", label_counter++);
    return strdup(buf);
}

//--------------------------------------------------- Load AST
static void load_ast(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Error opening ast.txt");
        exit(EXIT_FAILURE);
    }
    char buf[MAX_LINE_LEN];
    while (fgets(buf, sizeof(buf), fp)) {
        int spaces = 0;
        while (buf[spaces] == ' ') spaces++;
        int indent = spaces / 4;
        buf[strcspn(buf, "\r\n")] = '\0';
        lines[line_count].indent = indent;
        strncpy(lines[line_count].text, buf + spaces, MAX_LINE_LEN - 1);
        line_count++;
        if (line_count >= MAX_LINES) break;
    }
    fclose(fp);
}

//--------------------------------------------------- Code Generation
// Returns operand name for use in expressions
static char *gen_node(int indent);

static void gen_block(int indent) {
    while (current_line < line_count && lines[current_line].indent >= indent) {
        gen_node(indent);
    }
}

static char *gen_node(int indent) {
    if (current_line >= line_count || lines[current_line].indent < indent) return NULL;
    ASTLine *ln = &lines[current_line];
    char *result = NULL;

    // FunctionDefinition: name
    if (strncmp(ln->text, "FunctionDefinition:", 19) == 0) {
        char name[64]; sscanf(ln->text + 19, "%s", name);
        printf("func %s:\n", name);
        current_line++;
        // skip parameters
        if (current_line < line_count && lines[current_line].indent == indent+1)
            gen_node(indent+1);
        // body
        if (current_line < line_count && strstr(lines[current_line].text, "Body:"))
            gen_block(indent+1);
        printf("endfunc\n\n");
        return NULL;
    }

    // Body:
    if (strcmp(ln->text, "Body:") == 0) {
        current_line++;
        gen_block(indent+1);
        return NULL;
    }

    // VarDeclGroup: skip declarations
    if (strncmp(ln->text, "VarDeclGroup:", 13) == 0) {
        current_line++;
        while (current_line < line_count && lines[current_line].indent > indent)
            current_line++;
        return NULL;
    }

    // VarDecl: skip
    if (strncmp(ln->text, "VarDecl:", 8) == 0) {
        current_line++;
        if (current_line < line_count && lines[current_line].indent > indent)
            gen_node(indent+1);
        return NULL;
    }

    // Assign: name =
    if (strncmp(ln->text, "Assign:", 7) == 0) {
        char var[64]; sscanf(ln->text + 7, "%s", var);
        current_line++;
        char *r = gen_node(indent+1);
        printf("%s = %s\n", var, r);
        free(r);
        return NULL;
    }

    // Return:
    if (strncmp(ln->text, "Return", 6) == 0) {
        current_line++;
        char *r = gen_node(indent+1);
        if (r) {
            printf("return %s\n", r);
            free(r);
        } else {
            printf("return\n");
        }
        return NULL;
    }

    // If:
    if (strncmp(ln->text, "If:", 3) == 0) {
        current_line++;
        char *cond = gen_node(indent+1);
        char *Lelse = new_label();
        char *Lend  = new_label();
        printf("ifFalse %s goto %s\n", cond, Lelse);
        free(cond);
        // then
        if (current_line < line_count && strcmp(lines[current_line].text, "Body:") == 0)
            gen_block(indent+1);
        printf("goto %s\n", Lend);
        printf("%s:\n", Lelse);
        // else
        if (current_line < line_count && strncmp(lines[current_line].text, "Else:",5)==0) {
            current_line++;
            if (current_line < line_count && strcmp(lines[current_line].text, "Body:") == 0)
                gen_block(indent+2);
        }
        printf("%s:\n", Lend);
        free(Lelse); free(Lend);
        return NULL;
    }

    // For:
    if (strncmp(ln->text, "For:", 4) == 0) {
        current_line++;
        // init
        gen_node(indent+1);
        char *Lstart = new_label();
        char *Lend   = new_label();
        printf("%s:\n", Lstart);
        // cond
        char *cond = gen_node(indent+1);
        printf("ifFalse %s goto %s\n", cond, Lend);
        free(cond);
        // body
        if (current_line < line_count && strcmp(lines[current_line].text, "Body:") == 0)
            gen_block(indent+1);
        // increment
        gen_node(indent+1);
        printf("goto %s\n", Lstart);
        printf("%s:\n", Lend);
        free(Lstart); free(Lend);
        return NULL;
    }

    // While:
    if (strncmp(ln->text, "While:", 6) == 0) {
        current_line++;
        char *Lstart = new_label();
        char *Lend   = new_label();
        printf("%s:\n", Lstart);
        char *cond = gen_node(indent+1);
        printf("ifFalse %s goto %s\n", cond, Lend);
        free(cond);
        if (current_line < line_count && strcmp(lines[current_line].text, "Body:")==0)
            gen_block(indent+1);
        printf("goto %s\n", Lstart);
        printf("%s:\n", Lend);
        free(Lstart); free(Lend);
        return NULL;
    }

    // BinOp(op)
    if (strncmp(ln->text, "BinOp(", 6) == 0) {
        char op[8]; sscanf(ln->text + 6, "%[^)]", op);
        current_line++;
        char *l = gen_node(indent+1);
        char *r = gen_node(indent+1);
        char *t = new_temp();
        printf("%s = %s %s %s\n", t, l, op, r);
        free(l); free(r);
        return t;
    }

    // Number(
    if (strncmp(ln->text, "Number(", 7) == 0) {
        char val[64]; sscanf(ln->text + 7, "%[^)]", val);
        current_line++;
        return strdup(val);
    }

    // Var(
    if (strncmp(ln->text, "Var(", 4) == 0) {
        char name[64]; sscanf(ln->text + 4, "%[^)]", name);
        current_line++;
        return strdup(name);
    }

    // Cast(type)
    if (strncmp(ln->text, "Cast(", 5) == 0) {
        current_line++;
        return gen_node(indent+1);
    }

    // Default skip
    current_line++;
    return NULL;
}

//--------------------------------------------------- main
int main() {
    load_ast("ast.txt");
    current_line = 0;
    while (current_line < line_count) {
        gen_node(0);
    }
    return 0;
}
