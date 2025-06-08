#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//--------------------------------------------------- Defines

#define MAX_TOKENS 4096

//--------------------------------------------------- Data Types

typedef enum {
    TOK_KEYWORD,
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,
    TOK_OPERATOR,
    TOK_PUNCTUATION,
    TOK_EOF_TOKEN
} TokenType;

//Structure of a token
typedef struct {
    TokenType type;
    char lexeme[256];
    int line;
} Token;

//Array of tokens
static Token tokens[MAX_TOKENS];
static int token_count = 0;

//Current token index during parsing
static int current_token_index = 0;

//Convert strings of TYPE (e.g., "KEYWORD") to TokenType
static TokenType token_type_from_string(const char *str) {
    if (strcmp(str, "KEYWORD") == 0)       return TOK_KEYWORD;
    if (strcmp(str, "IDENTIFIER") == 0)    return TOK_IDENTIFIER;
    if (strcmp(str, "INT_LITERAL") == 0)   return TOK_INT_LITERAL;
    if (strcmp(str, "FLOAT_LITERAL") == 0) return TOK_FLOAT_LITERAL;
    if (strcmp(str, "OPERATOR") == 0)      return TOK_OPERATOR;
    if (strcmp(str, "PUNCTUATION") == 0)   return TOK_PUNCTUATION;
    if (strcmp(str, "EOF") == 0)           return TOK_EOF_TOKEN;
    return TOK_EOF_TOKEN;
}

//--------------------------------------------------- Token Loading

// Each line must follow exactly this format: [line:<number>] <TYPE> "<lexeme>"
static void load_tokens(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Error opening tokens.txt");
        exit(EXIT_FAILURE);
    }

    char linebuf[512];
    token_count = 0;

    while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
        if (token_count >= MAX_TOKENS) {
            fprintf(stderr, "Error: too many tokens (>%d)\n", MAX_TOKENS);
            exit(EXIT_FAILURE);
        }

        int line_num = 0;
        char type_str[32] = {0};
        char lexeme_str[256] = {0};

        /* Extract line number from between "[line:" and "]" */
        char *p = strstr(linebuf, "[line:");
        if (p) {
            p += strlen("[line:");
            line_num = atoi(p);
        } else {
            fprintf(stderr, "Error parsing line number: %s", linebuf);
            exit(EXIT_FAILURE);
        }

        /* Find ']' and move past it to reach TYPE */
        char *r = strchr(linebuf, ']');
        if (!r) {
            fprintf(stderr, "Error parsing token type: %s", linebuf);
            exit(EXIT_FAILURE);
        }
        r++;
        while (*r == ' ' || *r == '\t') r++;

        /* Read TYPE until first space, tab, null, or '"' */
        int ti = 0;
        while (*r != ' ' && *r != '\t' && *r != '\0' && *r != '"') {
            if (ti < (int)sizeof(type_str) - 1) {
                type_str[ti++] = *r;
            }
            r++;
        }
        type_str[ti] = '\0';

        /* Find the string inside quotes (lexeme) */
        char *q1 = strchr(linebuf, '"');
        if (!q1) {
            fprintf(stderr, "Error parsing lexeme (no opening quote): %s", linebuf);
            exit(EXIT_FAILURE);
        }
        char *q2 = strchr(q1 + 1, '"');
        if (!q2) {
            fprintf(stderr, "Error parsing lexeme (no closing quote): %s", linebuf);
            exit(EXIT_FAILURE);
        }

        int len = (int)(q2 - (q1 + 1));
        if (len > 0 && len < (int)sizeof(lexeme_str)) {
            strncpy(lexeme_str, q1 + 1, len);
            lexeme_str[len] = '\0';
        } else {
            lexeme_str[0] = '\0';
        }

        /* Fill the Token structure */
        tokens[token_count].type = token_type_from_string(type_str);
        strncpy(tokens[token_count].lexeme, lexeme_str, sizeof(tokens[token_count].lexeme) - 1);
        tokens[token_count].lexeme[sizeof(tokens[token_count].lexeme) - 1] = '\0';
        tokens[token_count].line = line_num;

        token_count++;

        /* Stop if we reach EOF token */
        if (tokens[token_count - 1].type == TOK_EOF_TOKEN) {
            break;
        }
    }

    fclose(fp);
}

//--------------------------------------------------- AST Structures


// AST node types
typedef enum {
    NODE_PROGRAM,
    NODE_FUNCTION_DEF,
    NODE_PARAM_LIST,
    NODE_BODY,
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_RETURN,
    NODE_BINOP,
    NODE_NUMBER,
    NODE_VAR,
    NODE_IF,
    NODE_ELSE,
    NODE_WHILE,
    NODE_FOR,
} NodeKind;

//Forward declaration of ASTNode
typedef struct ASTNode ASTNode;

// Dynamic array to hold children of each node
typedef struct {
    ASTNode **items;
    int       count;
    int       capacity;
} NodeList;

// Main structure of an AST node
struct ASTNode {
    NodeKind   kind;
    char       text[64];    /* e.g., function name, variable name, or operator */
    NodeList   children;    /* list of children */
};

// Initialize a NodeList
static void node_list_init(NodeList *list) {
    list->count = 0;
    list->capacity = 4;
    list->items = (ASTNode **)malloc(sizeof(ASTNode *) * list->capacity);
    if (!list->items) {
        fprintf(stderr, "Error: malloc failed in node_list_init\n");
        exit(EXIT_FAILURE);
    }
}

// Append a node to the children list
static void node_list_append(NodeList *list, ASTNode *child) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        ASTNode **new_items = (ASTNode **)realloc(list->items, sizeof(ASTNode *) * list->capacity);
        if (!new_items) {
            fprintf(stderr, "Error: realloc failed in node_list_append\n");
            exit(EXIT_FAILURE);
        }
        list->items = new_items;
    }
    list->items[list->count++] = child;
}

// Create a new node with specified kind and text
static ASTNode *ast_new_node(NodeKind kind, const char *text) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: malloc failed in ast_new_node\n");
        exit(EXIT_FAILURE);
    }
    node->kind = kind;
    if (text) {
        strncpy(node->text, text, sizeof(node->text) - 1);
        node->text[sizeof(node->text) - 1] = '\0';
    } else {
        node->text[0] = '\0';
    }
    node_list_init(&node->children);
    return node;
}

//--------------------------------------------------- Token Consumption & Peek APIs

// Current token
static Token *peek_token() {
    if (current_token_index < token_count) {
        return &tokens[current_token_index];
    }
    return NULL;
}

// Peek at a token with an offset (without consuming)
static Token *peek_token_offset(int i) {
    int idx = current_token_index + i;
    if (idx < token_count) {
        return &tokens[idx];
    }
    return NULL;
}

// Consume the current token and move to the next
static Token *advance_token() {
    if (current_token_index < token_count) {
        return &tokens[current_token_index++];
    }
    return NULL;
}

//If the current token has the specified type and lexeme, consume it and return 1; otherwise return 0.
static int match_token(TokenType type, const char *lexeme) {
    Token *t = peek_token();
    if (!t) return 0;
    if (t->type == type && strcmp(t->lexeme, lexeme) == 0) {
        advance_token();
        return 1;
    }
    return 0;
}

//If the current token does not match the specified type and lexeme, print an error and exit.
static void expect_token(TokenType type, const char *lex) {
    Token *t = peek_token();
    if (!t || t->type != type || strcmp(t->lexeme, lex) != 0) {
        if (t) {
            fprintf(stderr, "Syntax Error [line %d]: expected '%s', got '%s'\n",
                    t->line, lex, t->lexeme);
        } else {
            fprintf(stderr, "Syntax Error: unexpected end of input, expected '%s'\n", lex);
        }
        exit(EXIT_FAILURE);
    }
    advance_token();
}

// Ensure the current token is a KEYWORD with the lexeme equal to kw
static void expect_keyword(const char *kw) {
    Token *t = peek_token();
    if (!t || t->type != TOK_KEYWORD || strcmp(t->lexeme, kw) != 0) {
        if (t) {
            fprintf(stderr, "Syntax Error [line %d]: expected keyword '%s', got '%s'\n",
                    t->line, kw, t->lexeme);
        } else {
            fprintf(stderr, "Syntax Error: unexpected end of input, expected keyword '%s'\n", kw);
        }
        exit(EXIT_FAILURE);
    }
    advance_token();
}

//--------------------------------------------------- Parser Function Declarations

static ASTNode *parse_program();
static ASTNode *parse_function_def();
static ASTNode *parse_param_list();
static ASTNode *parse_body();
static ASTNode *parse_var_decl();
static ASTNode *parse_statement();
static ASTNode *parse_assignment();
static ASTNode *parse_return_stmt();
static ASTNode *parse_logical_or();
static ASTNode *parse_logical_and();
static ASTNode *parse_expression() {
    return parse_logical_or();
}
static ASTNode *parse_term();
static ASTNode *parse_factor();
static ASTNode *parse_if_statement();
static ASTNode *parse_while_statement();
static ASTNode *parse_for_statement();
static ASTNode *parse_add_sub();
static ASTNode *parse_block_statement();
static ASTNode *parse_assignment_inline();
static ASTNode *parse_function_call();

//--------------------------------------------------- Parsing Function Implementations

// program := { function_def | var_decl }
static ASTNode *parse_program() {
    ASTNode *program_node = ast_new_node(NODE_PROGRAM, NULL);

    while (peek_token() && peek_token()->type != TOK_EOF_TOKEN) {
        Token *t = peek_token();

        if (t->type == TOK_KEYWORD &&
            (strcmp(t->lexeme, "int") == 0 || strcmp(t->lexeme, "float") == 0 || strcmp(t->lexeme, "void") == 0)) {

            Token *t1 = peek_token_offset(1);
            Token *t2 = peek_token_offset(2);
            if (t1 && t1->type == TOK_IDENTIFIER &&
                t2 && t2->type == TOK_PUNCTUATION && strcmp(t2->lexeme, "(") == 0) {
                ASTNode *fn = parse_function_def();
                node_list_append(&program_node->children, fn);
                continue;
            }
        }

        if (t->type == TOK_KEYWORD &&
            (strcmp(t->lexeme, "int") == 0 || strcmp(t->lexeme, "float") == 0)) {
            ASTNode *decl = parse_var_decl();
            node_list_append(&program_node->children, decl);
            continue;
        }

        fprintf(stderr, "Syntax Error [line %d]: unexpected token '%s' at global scope\n",
                t->line, t->lexeme);
        exit(EXIT_FAILURE);
    }

    return program_node;
}


// function_def := type identifier '(' param_list ')' '{' body '}'
static ASTNode *parse_function_def() {
//------------------------------ Return type: int | float | void
    Token *t = peek_token();
    if (t->type != TOK_KEYWORD ||
        (strcmp(t->lexeme, "int") != 0 &&
         strcmp(t->lexeme, "float") != 0 &&
         strcmp(t->lexeme, "void") != 0)) {
        fprintf(stderr, "Syntax Error [line %d]: expected function return type, got '%s'\n",
                t->line, t->lexeme);
        exit(EXIT_FAILURE);
    }
    advance_token();  // consume return type

//------------------------------ Function name
    t = peek_token();
    if (!t || t->type != TOK_IDENTIFIER) {
        if (t) {
            fprintf(stderr, "Syntax Error [line %d]: expected function name, got '%s'\n",
                    t->line, t->lexeme);
        } else {
            fprintf(stderr, "Syntax Error: unexpected end of input, expected function name\n");
        }
        exit(EXIT_FAILURE);
    }
    ASTNode *fn_node = ast_new_node(NODE_FUNCTION_DEF, t->lexeme);
    advance_token();  /* consume identifier */

//------------------------------ '('
    expect_token(TOK_PUNCTUATION, "(");

//------------------------------ Parameter list
    ASTNode *params = parse_param_list();
    node_list_append(&fn_node->children, params);

//------------------------------ ')'
    expect_token(TOK_PUNCTUATION, ")");

//------------------------------ '{'
    expect_token(TOK_PUNCTUATION, "{");

//------------------------------ Body
    ASTNode *body = parse_body();
    node_list_append(&fn_node->children, body);

//------------------------------ '}'
    expect_token(TOK_PUNCTUATION, "}");

    return fn_node;
}

// param_list := ')' | empty lists are supported
static ASTNode *parse_param_list() {
    ASTNode *params = ast_new_node(NODE_PARAM_LIST, "Parameters:");
    char type_buf[16] = {0};
    char name_buf[32] = {0};

    while (1) {
        Token *t = peek_token();
        if (!t) break;

        // End of param list
        if (t->type == TOK_PUNCTUATION && strcmp(t->lexeme, ")") == 0)
            break;

        // Type (int, float)
        if (t->type == TOK_KEYWORD && (strcmp(t->lexeme, "int") == 0 || strcmp(t->lexeme, "float") == 0)) {
            strncpy(type_buf, t->lexeme, sizeof(type_buf)-1);
            advance_token();
        } else {
            fprintf(stderr, "Syntax Error [line %d]: expected type in parameter, got '%s'\n", t->line, t->lexeme);
            exit(EXIT_FAILURE);
        }

        // Identifier
        t = peek_token();
        if (!t || t->type != TOK_IDENTIFIER) {
            fprintf(stderr, "Syntax Error [line %d]: expected identifier in parameter, got '%s'\n", t ? t->line : -1, t ? t->lexeme : "NULL");
            exit(EXIT_FAILURE);
        }
        strncpy(name_buf, t->lexeme, sizeof(name_buf)-1);
        advance_token();

        // Optional brackets for array param
        int is_array = 0;
        if (peek_token() && peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, "[") == 0) {
            advance_token();
            expect_token(TOK_PUNCTUATION, "]");
            is_array = 1;
        }

        char desc[128];
        snprintf(desc, sizeof(desc), "Param: %s %s%s", type_buf, name_buf, is_array ? "[]" : "");
        ASTNode *param = ast_new_node(NODE_VAR_DECL, desc);
        node_list_append(&params->children, param);

        // Comma or end
        if (peek_token() && peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, ",") == 0) {
            advance_token();
        } else {
            break;
        }
    }
    return params;
}


// body := { var_decl | statement }
static ASTNode *parse_body() {
    ASTNode *body_node = ast_new_node(NODE_BODY, "Body:");

    while (peek_token()) {
        Token *t = peek_token();
        // If '}' appears, the body is complete
        if (t->type == TOK_PUNCTUATION && strcmp(t->lexeme, "}") == 0) {
            break;
        }
        // If KEYWORD of type int|float, it's a var_decl
        if (t->type == TOK_KEYWORD &&
            (strcmp(t->lexeme, "int") == 0 || strcmp(t->lexeme, "float") == 0)) {
            ASTNode *var_decl = parse_var_decl();
            node_list_append(&body_node->children, var_decl);
        } else {
            ASTNode *stmt = parse_statement();
            node_list_append(&body_node->children, stmt);
        }
    }

    return body_node;
}

// var_decl := type identifier [= expression] {',' identifier [= expression]} ';'
static ASTNode *parse_var_decl() {
    Token *t = peek_token();
    char type_text[16] = {0};
    if (t->type != TOK_KEYWORD || (strcmp(t->lexeme, "int") != 0 && strcmp(t->lexeme, "float") != 0)) {
        fprintf(stderr, "Syntax Error [line %d]: expected type in declaration, got '%s'\n", t->line, t->lexeme);
        exit(EXIT_FAILURE);
    }
    strncpy(type_text, t->lexeme, sizeof(type_text) - 1);
    advance_token();

    ASTNode *decl = ast_new_node(NODE_VAR_DECL, "VarDeclGroup:");

    while (1) {
        // identifier
        t = peek_token();
        if (!t || t->type != TOK_IDENTIFIER) {
            fprintf(stderr, "Syntax Error [line %d]: expected identifier in declaration, got '%s'\n",
                    t ? t->line : -1, t ? t->lexeme : "NULL");
            exit(EXIT_FAILURE);
        }
        char var_name[64];
        strncpy(var_name, t->lexeme, sizeof(var_name) - 1);
        advance_token();

        // check for optional '=' initializer
        ASTNode *var_node = NULL;
        if (peek_token() && peek_token()->type == TOK_OPERATOR && strcmp(peek_token()->lexeme, "=") == 0) {
            advance_token();  // consume '='
            ASTNode *rhs = parse_expression();

            char buf[128];
            snprintf(buf, sizeof(buf), "VarDecl: %s %s =", type_text, var_name);
            var_node = ast_new_node(NODE_VAR_DECL, buf);
            node_list_append(&var_node->children, rhs);
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "VarDecl: %s %s", type_text, var_name);
            var_node = ast_new_node(NODE_VAR_DECL, buf);
        }

        node_list_append(&decl->children, var_node);

        // check for ',' or end with ';'
        if (peek_token() && peek_token()->type == TOK_PUNCTUATION) {
            if (strcmp(peek_token()->lexeme, ",") == 0) {
                advance_token();  // consume ',' and continue
            } else if (strcmp(peek_token()->lexeme, ";") == 0) {
                advance_token();  // consume ';' and break
                break;
            } else {
                fprintf(stderr, "Syntax Error [line %d]: expected ',' or ';', got '%s'\n",
                        peek_token()->line, peek_token()->lexeme);
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Syntax Error [line %d]: expected ',' or ';'\n",
                    peek_token() ? peek_token()->line : -1);
            exit(EXIT_FAILURE);
        }
    }

    return decl;
}

// Parses assignment but WITHOUT consuming the ending ';'
static ASTNode *parse_assignment_inline() {
    Token *t = peek_token();
    char var_name[64];
    if (t->type == TOK_IDENTIFIER) {
        strncpy(var_name, t->lexeme, sizeof(var_name)-1);
        advance_token();
    } else {
        fprintf(stderr, "Syntax Error [line %d]: expected identifier in assignment, got '%s'\n", t->line, t->lexeme);
        exit(EXIT_FAILURE);
    }

    expect_token(TOK_OPERATOR, "=");

    char buf[128];
    snprintf(buf, sizeof(buf), "Assign: %s =", var_name);
    ASTNode *assign_node = ast_new_node(NODE_ASSIGN, buf);

    ASTNode *expr = parse_expression();
    node_list_append(&assign_node->children, expr);

    return assign_node;
}


// block := '{' { var_decl | statement } '}'
static ASTNode *parse_block_statement() {
    expect_token(TOK_PUNCTUATION, "{");
    ASTNode *body_node = ast_new_node(NODE_BODY, "Body:");

    while (peek_token() && !(peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, "}") == 0)) {
        Token *t = peek_token();

        // variable declaration
        if (t->type == TOK_KEYWORD &&
            (strcmp(t->lexeme, "int") == 0 || strcmp(t->lexeme, "float") == 0)) {
            ASTNode *decl = parse_var_decl();
            node_list_append(&body_node->children, decl);
        } else {
            ASTNode *stmt = parse_statement();
            node_list_append(&body_node->children, stmt);
        }
    }

    expect_token(TOK_PUNCTUATION, "}");
    return body_node;
}


// statement := assignment | return_stmt | if_stmt | while_stmt | for_stmt | block
static ASTNode *parse_statement() {
    Token *t = peek_token();

    // Block statement: { ... }
    if (t->type == TOK_PUNCTUATION && strcmp(t->lexeme, "{") == 0) {
        return parse_block_statement();
    }

    // Assignment
    if (t->type == TOK_IDENTIFIER && peek_token_offset(1) &&
        peek_token_offset(1)->type == TOK_OPERATOR &&
        strcmp(peek_token_offset(1)->lexeme, "=") == 0) {
        return parse_assignment();
    }

    // Return
    if (t->type == TOK_KEYWORD && strcmp(t->lexeme, "return") == 0) {
        return parse_return_stmt();
    }

    // if
    if (t->type == TOK_KEYWORD && strcmp(t->lexeme, "if") == 0) {
        return parse_if_statement();
    }

    // while
    if (t->type == TOK_KEYWORD && strcmp(t->lexeme, "while") == 0) {
        return parse_while_statement();
    }

    // for
    if (t->type == TOK_KEYWORD && strcmp(t->lexeme, "for") == 0) {
        return parse_for_statement();
    }

    fprintf(stderr, "Syntax Error [line %d]: unexpected token '%s' in statement\n", t->line, t->lexeme);
    exit(EXIT_FAILURE);
}


// assignment := identifier '=' expression ';'
static ASTNode *parse_assignment() {
//------------------------------ identifier
    Token *t = peek_token();
    char var_name[64];
    if (t->type == TOK_IDENTIFIER) {
        strncpy(var_name, t->lexeme, sizeof(var_name)-1);
        advance_token();
    } else {
        fprintf(stderr, "Syntax Error [line %d]: expected identifier in assignment, got '%s'\n",
                t->line, t->lexeme);
        exit(EXIT_FAILURE);
    }

//------------------------------ '='
    expect_token(TOK_OPERATOR, "=");

    // Create Assign node with text "Assign: <var> ="
    char buf[128];
    snprintf(buf, sizeof(buf), "Assign: %s =", var_name);
    ASTNode *assign_node = ast_new_node(NODE_ASSIGN, buf);

//------------------------------ expression
    ASTNode *expr = parse_expression();
    node_list_append(&assign_node->children, expr);

//------------------------------ ';'
    expect_token(TOK_PUNCTUATION, ";");

    return assign_node;
}

// return_stmt := 'return' expression ';'
static ASTNode *parse_return_stmt() {
    expect_keyword("return");  // Consume 'return'

//------------------------------ expression
    ASTNode *expr = parse_expression();

//------------------------------ ';'
    expect_token(TOK_PUNCTUATION, ";");

    // Create Return node. If expr is a Number or Var, include its text.
    ASTNode *return_node;
    if (expr->kind == NODE_NUMBER || expr->kind == NODE_VAR) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Return: %s", expr->text);
        return_node = ast_new_node(NODE_RETURN, buf);
        free(expr->children.items);
        free(expr);
    } else {
        return_node = ast_new_node(NODE_RETURN, "Return:");
        node_list_append(&return_node->children, expr);
    }
    return return_node;
}

// if_stmt := "if" "(" expression ")" statement [ "else" statement ]
static ASTNode *parse_if_statement() {
    expect_keyword("if");
    expect_token(TOK_PUNCTUATION, "(");

    ASTNode *condition = parse_expression();

    expect_token(TOK_PUNCTUATION, ")");

    ASTNode *if_node = ast_new_node(NODE_IF, "If:");
    node_list_append(&if_node->children, condition);

    ASTNode *then_stmt = parse_statement();
    node_list_append(&if_node->children, then_stmt);

    Token *t = peek_token();
    if (t && t->type == TOK_KEYWORD && strcmp(t->lexeme, "else") == 0) {
        advance_token(); // consume 'else'

        Token *next = peek_token();
        if (next && next->type == TOK_KEYWORD && strcmp(next->lexeme, "if") == 0) {
            ASTNode *else_if_node = parse_statement();
            node_list_append(&if_node->children, else_if_node);
        } else {
            ASTNode *else_body = parse_body();
            ASTNode *else_node = ast_new_node(NODE_ELSE, "Else:");
            node_list_append(&else_node->children, else_body);
            node_list_append(&if_node->children, else_node);
        }
    }


    return if_node;
}
// while_stmt := "while" "(" expression ")" statement
static ASTNode *parse_while_statement() {
    expect_keyword("while");
    expect_token(TOK_PUNCTUATION, "(");
    ASTNode *cond = parse_expression();
    expect_token(TOK_PUNCTUATION, ")");

    ASTNode *while_node = ast_new_node(NODE_WHILE, "While:");
    node_list_append(&while_node->children, cond);

    ASTNode *body = parse_statement();
    node_list_append(&while_node->children, body);

    return while_node;
}
// for_stmt := "for" "(" [assignment] ";" [expression] ";" [assignment] ")" statement
static ASTNode *parse_for_statement() {
    expect_keyword("for");
    expect_token(TOK_PUNCTUATION, "(");

    ASTNode *for_node = ast_new_node(NODE_FOR, "For:");

    // Init
    if (!(peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, ";") == 0)) {
        ASTNode *init = parse_assignment_inline();
        node_list_append(&for_node->children, init);
    }
    expect_token(TOK_PUNCTUATION, ";");

    // Condition
    if (!(peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, ";") == 0)) {
        ASTNode *cond = parse_expression();
        node_list_append(&for_node->children, cond);
    }
    expect_token(TOK_PUNCTUATION, ";");

    // Increment
    if (!(peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, ")") == 0)) {
        ASTNode *inc = parse_assignment_inline();
        node_list_append(&for_node->children, inc);
    }
    expect_token(TOK_PUNCTUATION, ")");

    ASTNode *body = parse_statement();
    node_list_append(&for_node->children, body);

    return for_node;
}

static ASTNode *parse_add_sub() {
    ASTNode *node = parse_term();
    while (peek_token() && peek_token()->type == TOK_OPERATOR &&
           (strcmp(peek_token()->lexeme, "+") == 0 || strcmp(peek_token()->lexeme, "-") == 0)) {
        Token *op = peek_token();
        char op_text[16]; snprintf(op_text, sizeof(op_text), "BinOp(%s)", op->lexeme);
        advance_token();
        ASTNode *new_node = ast_new_node(NODE_BINOP, op_text);
        node_list_append(&new_node->children, node);
        node_list_append(&new_node->children, parse_term());
        node = new_node;
    }
    return node;
}


// comparison := term { (== | != | < | > | <= | >=) term }
static ASTNode *parse_comparison() {
    ASTNode *node = parse_add_sub();
    while (peek_token() && peek_token()->type == TOK_OPERATOR &&
           (strcmp(peek_token()->lexeme, "==") == 0 || strcmp(peek_token()->lexeme, "!=") == 0 ||
            strcmp(peek_token()->lexeme, "<") == 0 || strcmp(peek_token()->lexeme, ">") == 0 ||
            strcmp(peek_token()->lexeme, "<=") == 0 || strcmp(peek_token()->lexeme, ">=") == 0)) {
        Token *op = peek_token();
        char op_text[16]; snprintf(op_text, sizeof(op_text), "BinOp(%s)", op->lexeme);
        advance_token();
        ASTNode *new_node = ast_new_node(NODE_BINOP, op_text);
        node_list_append(&new_node->children, node);
        node_list_append(&new_node->children, parse_add_sub());
        node = new_node;
    }
    return node;
}


// logical_or := logical_and { "||" logical_and }
static ASTNode *parse_logical_or() {
    ASTNode *node = parse_logical_and();
    while (peek_token() && peek_token()->type == TOK_OPERATOR &&
           strcmp(peek_token()->lexeme, "||") == 0) {
        Token *op = peek_token();
        advance_token();
        ASTNode *new_node = ast_new_node(NODE_BINOP, "BinOp(||)");
        node_list_append(&new_node->children, node);
        node_list_append(&new_node->children, parse_logical_and());
        node = new_node;
    }
    return node;
}


// logical_and := comparison { "&&" comparison }
static ASTNode *parse_logical_and() {
    ASTNode *node = parse_comparison();
    while (peek_token() && peek_token()->type == TOK_OPERATOR &&
           strcmp(peek_token()->lexeme, "&&") == 0) {
        Token *op = peek_token();
        advance_token();
        ASTNode *new_node = ast_new_node(NODE_BINOP, "BinOp(&&)");
        node_list_append(&new_node->children, node);
        node_list_append(&new_node->children, parse_comparison());
        node = new_node;
    }
    return node;
}



// term := factor { ('*' | '/') factor }
static ASTNode *parse_term() {
    ASTNode *node = parse_factor();
    while (peek_token() && peek_token()->type == TOK_OPERATOR &&
           (strcmp(peek_token()->lexeme, "*") == 0 || strcmp(peek_token()->lexeme, "/") == 0 ||
            strcmp(peek_token()->lexeme, "%") == 0)) {
        Token *op = peek_token();
        char op_text[16]; snprintf(op_text, sizeof(op_text), "BinOp(%s)", op->lexeme);
        advance_token();
        ASTNode *new_node = ast_new_node(NODE_BINOP, op_text);
        node_list_append(&new_node->children, node);
        node_list_append(&new_node->children, parse_factor());
        node = new_node;
    }
    return node;
}

static ASTNode *parse_function_call() {
    Token *t = peek_token();
    if (!t || t->type != TOK_IDENTIFIER) {
        fprintf(stderr, "Syntax Error [line %d]: expected function name, got '%s'\n",
                t ? t->line : -1, t ? t->lexeme : "NULL");
        exit(EXIT_FAILURE);
    }

    ASTNode *call = ast_new_node(NODE_BINOP, t->lexeme);  // You can define NODE_FUNC_CALL if you want

    advance_token();  // consume function name
    expect_token(TOK_PUNCTUATION, "(");

    // Parse argument list
    while (peek_token() && !(peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, ")") == 0)) {
        ASTNode *arg = parse_expression();
        node_list_append(&call->children, arg);

        if (peek_token() && peek_token()->type == TOK_PUNCTUATION && strcmp(peek_token()->lexeme, ",") == 0) {
            advance_token();  // consume comma
        } else {
            break;
        }
    }

    expect_token(TOK_PUNCTUATION, ")");

    return call;
}


// factor := INT_LITERAL | FLOAT_LITERAL | IDENTIFIER | '(' expression ')'
static ASTNode *parse_factor() {
    Token *t = peek_token();
    if (!t) {
        fprintf(stderr, "Unexpected end of input in factor\n");
        exit(EXIT_FAILURE);
    }
// Type cast: (type) expression
    if (t->type == TOK_PUNCTUATION && strcmp(t->lexeme, "(") == 0 &&
        peek_token_offset(1) && peek_token_offset(1)->type == TOK_KEYWORD &&
        (strcmp(peek_token_offset(1)->lexeme, "int") == 0 || strcmp(peek_token_offset(1)->lexeme, "float") == 0) &&
        peek_token_offset(2) && peek_token_offset(2)->type == TOK_PUNCTUATION &&
        strcmp(peek_token_offset(2)->lexeme, ")") == 0) {

        advance_token();  // consume '('
        Token *type_tok = peek_token();  // 'int' or 'float'
        advance_token();
        expect_token(TOK_PUNCTUATION, ")");

        ASTNode *cast_expr = parse_factor();  // apply cast to next expression

        char cast_text[64];
        snprintf(cast_text, sizeof(cast_text), "Cast(%s)", type_tok->lexeme);
        ASTNode *cast_node = ast_new_node(NODE_BINOP, cast_text);
        node_list_append(&cast_node->children, cast_expr);

        return cast_node;
    }

    // Parenthesis
    if (t->type == TOK_PUNCTUATION && strcmp(t->lexeme, "(") == 0) {
        advance_token();
        ASTNode *expr = parse_expression();
        expect_token(TOK_PUNCTUATION, ")");
        return expr;
    }

    // Logical NOT
    if (t->type == TOK_OPERATOR && strcmp(t->lexeme, "!") == 0) {
        advance_token();
        ASTNode *factor = parse_factor();
        ASTNode *not_node = ast_new_node(NODE_BINOP, "BinOp(!)");
        node_list_append(&not_node->children, factor);
        return not_node;
    }

    // Number
    if (t->type == TOK_INT_LITERAL || t->type == TOK_FLOAT_LITERAL) {
        ASTNode *num = ast_new_node(NODE_NUMBER, t->lexeme);
        advance_token();
        return num;
    }

    // Variable
    if (t->type == TOK_IDENTIFIER) {
        Token *next = peek_token_offset(1);
        if (next && next->type == TOK_PUNCTUATION && strcmp(next->lexeme, "(") == 0) {
            // function call
            return parse_function_call();
        } else {
            ASTNode *var = ast_new_node(NODE_VAR, t->lexeme);
            advance_token();
            return var;
        }
    }


    fprintf(stderr, "Syntax Error [line %d]: unexpected token '%s' in factor\n", t->line, t->lexeme);
    exit(EXIT_FAILURE);
}


//--------------------------------------------------- AST Printing & ASCII Indentation to ast.txt


/*
    algorithm indentation:
    - At each depth, add 4 spaces.
    - Lines are simply shifted from the left by these spaces.
 */

// Recursively print a node and its children
static void print_ast_recursive(FILE *out, ASTNode *node, int depth) {
    /* Indentation: depth * 4 spaces */
    for (int i = 0; i < depth; i++) {
        fprintf(out, "    ");
    }

    // Print appropriate text based on the node kind
    switch (node->kind) {
        case NODE_PROGRAM:
            // For the root PROGRAM, we don't print itself; just its children
            break;

        case NODE_FUNCTION_DEF:
            fprintf(out, "FunctionDefinition: %s\n", node->text);
            break;

        case NODE_PARAM_LIST:
            fprintf(out, "%s\n", node->text);  // e.g., "Parameters: ()"
            break;

        case NODE_BODY:
            fprintf(out, "%s\n", node->text);  // "Body:"
            break;

        case NODE_VAR_DECL:
            fprintf(out, "%s\n", node->text);  // e.g., "VarDecl: int x"
            break;

        case NODE_ASSIGN:
            fprintf(out, "%s\n", node->text);  // e.g., "Assign: x ="
            break;

        case NODE_RETURN:
            fprintf(out, "%s\n", node->text);  // e.g., "Return: 0" or "Return:"
            break;

        case NODE_BINOP:
            fprintf(out, "%s\n", node->text);  // e.g., "BinOp(+)"
            break;

        case NODE_NUMBER:
            fprintf(out, "Number(%s)\n", node->text);
            break;

        case NODE_VAR:
            fprintf(out, "Var(%s)\n", node->text);
            break;

        case NODE_IF:
        case NODE_WHILE:
        case NODE_FOR:
        case NODE_ELSE:
            fprintf(out, "%s\n", node->text);
            break;
        default:
            fprintf(out, "UnknownNode\n");
            break;

    }

    // Print children (at next level)
    int child_count = node->children.count;
    for (int i = 0; i < child_count; i++) {
        print_ast_recursive(out, node->children.items[i], depth + 1);
    }
}

// Wrapper function to print the entire AST
static void print_ast(FILE *out, ASTNode *root) {
    if (root->kind == NODE_PROGRAM) {
        int n = root->children.count;
        for (int i = 0; i < n; i++) {
            print_ast_recursive(out, root->children.items[i], 0);
        }
    } else {
        print_ast_recursive(out, root, 0);
    }
}

//--------------------------------------------------- AST Memory Cleanup

// Recursively free the AST
static void free_ast(ASTNode *node) {
    for (int i = 0; i < node->children.count; i++) {
        free_ast(node->children.items[i]);
    }
    free(node->children.items);
    free(node);
}

//--------------------------------------------------- Main


int main() {
//------------------------------ Load tokens
    load_tokens("tokens.txt");

//------------------------------Parse and build the AST
    ASTNode *program = parse_program();

//------------------------------Open ast.txt and print
    FILE *fout = fopen("ast.txt", "w");
    if (!fout) {
        perror("Error opening ast.txt for write");
        free_ast(program);
        return EXIT_FAILURE;
    }
    print_ast(fout, program);
    fclose(fout);

    free_ast(program);

    return 0;
}