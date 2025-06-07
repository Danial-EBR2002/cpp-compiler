#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//--------------------------------------------------- Defines
#define MAX_LINES     2048      //Maximum number of AST lines
#define MAX_LINE_LEN   512      //Maximum length of each AST line
#define MAX_SYMBOLS   1024      //Maximum symbols per scope
#define MAX_FUNCS      128      //Maximum functions

//--------------------------------------------------- Data Type
typedef enum { TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_VOID, TYPE_UNKNOWN } VarType; //Supported variable types

typedef struct {
    char    name[64];       //Symbol name
    VarType type;           //Symbol type
} Symbol;

typedef struct {
    Symbol symbols[MAX_SYMBOLS];  //Symbols in this scope
    int    count;                //Number of symbols
} Scope;

typedef struct {
    char    name[64];        //Function name
    VarType return_type;     //Declared return type
    Scope   scope;           //Local symbol scope
    int     has_return;      //Return statement flag
} Function;

typedef struct {
    int   indent;           //Indentation level (in 4-space units)
    char  text[MAX_LINE_LEN];  //AST line text
} ASTLine;

//--------------------------------------------------- Global Variables
static ASTLine   lines[MAX_LINES];   //Loaded AST lines
static int       line_count   = 0;   //Total AST lines
static int       current_line = 0;   //Index of current AST line
static Function  functions[MAX_FUNCS];
static int       func_count    = 0;  //Parsed functions count
static Function *current_function = NULL;  //Active function context

//--------------------------------------------------- Utility Functions
//Map type keyword string to VarType enum
static VarType string_to_type(const char *s) {
    if (strcmp(s, "int") == 0)   return TYPE_INT;
    if (strcmp(s, "float") == 0) return TYPE_FLOAT;
    if (strcmp(s, "bool") == 0)  return TYPE_BOOL;
    if (strcmp(s, "void") == 0)  return TYPE_VOID;
    return TYPE_UNKNOWN;
}


//Report a semantic error with line info and exit
static void semantic_error(int lineno, const char *msg) {
    fprintf(stderr, "Semantic Error [line %d]: %s\n", lineno, msg);
    exit(EXIT_FAILURE);
}

//--------------------------------------------------- Symbol Table Management
//Add symbol to scope with duplicate and overflow check
static void add_symbol(Scope *scope, const char *name, VarType type, int lineno) {
    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->symbols[i].name, name) == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Redeclaration of '%s'", name);
            semantic_error(lineno, buf);
        }
    }
    if (scope->count >= MAX_SYMBOLS) {
        semantic_error(lineno, "Symbol table overflow");
    }
    strncpy(scope->symbols[scope->count].name, name, sizeof(scope->symbols[0].name)-1);
    scope->symbols[scope->count].type = type;
    scope->count++;
}

//Lookup symbol type in scope, return TYPE_UNKNOWN if not found
static VarType lookup_symbol(Scope *scope, const char *name) {
    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->symbols[i].name, name) == 0) {
            return scope->symbols[i].type;
        }
    }
    return TYPE_UNKNOWN;
}

//--------------------------------------------------- AST Loading
//Load AST lines from file, set indent and strip newline
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
        buf[strcspn(buf, "\r\n")] = '\0';  //Remove newline
        lines[line_count].indent = indent;
        strncpy(lines[line_count].text, buf + spaces, MAX_LINE_LEN - 1);
        line_count++;
        if (line_count >= MAX_LINES) break;
    }
    fclose(fp);
}

//--------------------------------------------------- AST Parsing and Semantic Analysis
//Parse AST node at expected indent level and check semantics
static VarType parse_node(int expected_indent) {
    if (current_line >= line_count || lines[current_line].indent < expected_indent)
        return TYPE_UNKNOWN;
    if (current_line >= line_count) return TYPE_UNKNOWN;
    ASTLine *ln = &lines[current_line];
    if (ln->indent != expected_indent) return TYPE_UNKNOWN;
    char *txt = ln->text;
    printf(">> Line %d | indent=%d | text='%s'\n", current_line, ln->indent, txt);

    VarType result = TYPE_UNKNOWN;

    if (strncmp(txt, "FunctionDefinition:", 19) == 0) {
        char fname[64];
        sscanf(txt + 19, "%s", fname);
        Function *fn = &functions[func_count++];
        strncpy(fn->name, fname, sizeof(fn->name)-1);
        fn->return_type = TYPE_INT;  //Default return type
        fn->scope.count  = 0;
        fn->has_return   = 0;
        current_function = fn;
        current_line++;
      // First child: Parameters
        VarType dummy = parse_node(expected_indent + 1);  // We'll handle "Parameters:" here

        // Second child: Body
        parse_node(expected_indent + 1);
        return TYPE_VOID;
    }

    if (strncmp(txt, "Body:", 5) == 0) {
        current_line++;
        while (current_line < line_count && lines[current_line].indent > expected_indent) {
            parse_node(expected_indent + 1);
        }
        return TYPE_VOID;
    }

    if (strncmp(txt, "VarDecl:", 8) == 0) {
        char typestr[16], name[64];
        sscanf(txt + 8, "%s %s", typestr, name);
        VarType vt = string_to_type(typestr);

        if (current_function) {
            add_symbol(&current_function->scope, name, vt, current_line);
        } else {
            static Scope global_scope = { .count = 0 };
            add_symbol(&global_scope, name, vt, current_line);
        }

        current_line++;
        if (lines[current_line].indent == expected_indent + 1) {
            parse_node(expected_indent + 1);
        }
        return TYPE_VOID;
    }


    if (strncmp(txt, "Assign:", 7) == 0) {
        char name[64];
        sscanf(txt + 7, "%s", name);
        if (!current_function) semantic_error(0, "Assignment outside function");
        VarType lhs = lookup_symbol(&current_function->scope, name);
        if (lhs == TYPE_UNKNOWN) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Use of undeclared '%s'", name);
            semantic_error(0, buf);
        }
        current_line++;
        VarType rhs = parse_node(expected_indent + 1);
        if (rhs != lhs) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Type mismatch in assignment to '%s'", name);
            semantic_error(0, buf);
        }
        return TYPE_VOID;
    }

    if (strncmp(txt, "If:", 3) == 0) {
        current_line++;  // Skip "If:"
        VarType cond_type = parse_node(expected_indent + 1);
        if (cond_type != TYPE_BOOL) {
            semantic_error(current_line, "Condition of 'if' must be boolean");
        }

        // then‐body
        if (current_line < line_count && lines[current_line].indent == expected_indent + 1 &&
            strncmp(lines[current_line].text, "Body:", 5) == 0) {
            parse_node(expected_indent + 1);
        }

        if (current_line < line_count && lines[current_line].indent == expected_indent + 1 &&
            strncmp(lines[current_line].text, "Else:", 5) == 0) {
            current_line++;  // Skip "Else:"
            if (current_line < line_count && lines[current_line].indent == expected_indent + 2 &&
                strncmp(lines[current_line].text, "Body:", 5) == 0) {
                parse_node(expected_indent + 2);
            }
        }

        return TYPE_VOID;
    }



    if (strncmp(txt, "Return:", 7) == 0) {
        current_function->has_return = 1;
        char *rest = txt + 7;
        while (*rest == ' ') rest++;

        VarType rt;

        if (*rest == '\0') {
            current_line++;
            rt = parse_node(expected_indent + 1);
        } else {
            char temp[64];
            sscanf(rest, "%s", temp);
            if (isdigit(temp[0]))
                rt = TYPE_INT;
            else
                rt = lookup_symbol(&current_function->scope, temp);
            current_line++;
        }

        if (current_function->return_type == TYPE_INT && strcmp(current_function->name, "main") != 0) {
            current_function->return_type = rt;
        }

        if (rt != current_function->return_type) {
            semantic_error(current_line, "Return type mismatch");
        }

        return TYPE_VOID;
    }

    if (strncmp(txt, "For:", 4) == 0) {
        current_line++;  // Skip "For:"

        // 1) init statement
        parse_node(expected_indent + 1);

        // 2) condition expression
        VarType cond = parse_node(expected_indent + 1);
        if (cond != TYPE_BOOL) {
            semantic_error(current_line, "Condition of 'for' must be boolean");
        }

        // 3) increment statement
        parse_node(expected_indent + 1);

        // 4) body of the loop
        if (current_line < line_count
            && lines[current_line].indent == expected_indent + 1
            && strncmp(lines[current_line].text, "Body:", 5) == 0) {
            parse_node(expected_indent + 1);
        }

        return TYPE_VOID;
    }

    if (strncmp(txt, "While:", 6) == 0) {
        current_line++;  // Skip "While:"

        VarType cond = parse_node(expected_indent + 1);
        if (cond != TYPE_BOOL) {
            semantic_error(current_line, "Condition of 'while' must be boolean");
        }

        if (current_line < line_count
            && lines[current_line].indent == expected_indent + 1
            && strncmp(lines[current_line].text, "Body:", 5) == 0) {
            parse_node(expected_indent + 1);
        }

        return TYPE_VOID;
    }

    if (strncmp(txt, "BinOp(", 6) == 0) {
        char op[8];
        sscanf(txt + 6, "%[^)]", op);
        current_line++;
        VarType left  = parse_node(expected_indent + 1);
        VarType right = parse_node(expected_indent + 1);

        if (left != right) semantic_error(current_line, "Type mismatch in binary operation");

        if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<") == 0  || strcmp(op, ">") == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
            strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
            return TYPE_BOOL;
        }

        return left;
    }


    if (strncmp(txt, "Number(", 7) == 0) {
        current_line++;
        return TYPE_INT;           //Integer literal
    }

    if (strncmp(txt, "Var(", 4) == 0) {
        char varname[64];
        sscanf(txt + 4, "%[^)]", varname);
        VarType vt = lookup_symbol(&current_function->scope, varname);
        if (vt == TYPE_UNKNOWN) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Use of undeclared '%s'", varname);
            semantic_error(0, buf);
        }
        current_line++;
        return vt;
    }
    if (strncmp(txt, "Cast(", 5) == 0) {
        char typestr[16];
        sscanf(txt + 5, "%[^)]", typestr);
        VarType cast_type = string_to_type(typestr);
        current_line++;
        VarType inner = parse_node(expected_indent + 1);
        return cast_type;
    }
    if (strncmp(txt, "Parameters:", 11) == 0) {
        current_line++;  // Skip "Parameters:"
        while (current_line < line_count && lines[current_line].indent > expected_indent) {
            char *subtxt = lines[current_line].text;

            if (strncmp(subtxt, "Param:", 6) == 0) {
                char param_type[16], param_name[64];
                if (sscanf(subtxt + 6, "%s %s", param_type, param_name) == 2) {
                    VarType vt = string_to_type(param_type);
                    add_symbol(&current_function->scope, param_name, vt, current_line);
                }
            }

            else if (strncmp(subtxt, "VarDecl:", 8) == 0) {
                char param_type[16], param_name[64];
                if (sscanf(subtxt + 8, "%s %s", param_type, param_name) == 2) {
                    VarType vt = string_to_type(param_type);
                    add_symbol(&current_function->scope, param_name, vt, current_line);
                }
            }

            current_line++;
        }
        return TYPE_VOID;
    }

    if (strncmp(txt, "Assign:", 7) == 0) {
        char varname[64];
        if (sscanf(txt + 7, "%s =", varname) != 1) {
            semantic_error(current_line, "Malformed assignment");
            current_line++;
            return TYPE_UNKNOWN;
        }

        VarType lhs_type = lookup_symbol(&current_function->scope, varname);
        if (lhs_type == TYPE_UNKNOWN) {
            semantic_error(current_line, "Assignment to undeclared variable");
        }

        current_line++;
        VarType rhs_type = parse_node(expected_indent + 1);

        if (lhs_type != rhs_type) {
            semantic_error(current_line, "Type mismatch in assignment");
        }

        return TYPE_VOID;
    }




    if (strncmp(txt, "VarDeclGroup:", 13) == 0) {
        current_line++;
        while (current_line < line_count && lines[current_line].indent == expected_indent + 1) {
            parse_node(expected_indent + 1);
        }
        return TYPE_VOID;
    }

    for (int fi = 0; fi < func_count; fi++) {
        if (strcmp(txt, functions[fi].name) == 0) {
            VarType ret_type = functions[fi].return_type;
            current_line++;

            while (current_line < line_count
                   && lines[current_line].indent > expected_indent) {
                parse_node(expected_indent + 1);
            }

            return ret_type;
        }
    }

    current_line++;  //Skip unhandled node
    return TYPE_UNKNOWN;
}

//--------------------------------------------------- main
int main() {
    load_ast("ast.txt");        //Load AST from file
    current_line = 0;
    while (current_line < line_count) {
        parse_node(0);
    }
    for (int i = 0; i < func_count; i++) {
        if (functions[i].return_type != TYPE_VOID && !functions[i].has_return) {
            fprintf(stderr, "Semantic Error: function '%s' missing return\n", functions[i].name);
            return EXIT_FAILURE;
        }
    }
    printf("Semantic Analysis: Successful\n");
    return EXIT_SUCCESS;
}