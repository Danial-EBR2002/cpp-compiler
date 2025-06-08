#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//--------------------------------------------------- Defines
#define MAX_LEXEME_LEN 256
#define INPUT_FILE  "source_file.cpp"
#define OUTPUT_FILE "tokens.txt"

//--------------------------------------------------- Data Types
typedef enum {
    TOK_KEYWORD,
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,
    TOK_STRING_LITERAL,
    TOK_OPERATOR,
    TOK_PUNCTUATION,
    TOK_PREPROCESSOR,
    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char lexeme[MAX_LEXEME_LEN];
    int line;
} Token;

//--------------------------------------------------- Globals
static const char *keywords[] = {
        "int", "float", "void", "return",
        "if", "else", "while", "for",
        NULL
};

static FILE *source_file;
static FILE *out_file;
static int current_char;
static int line_number;

//--------------------------------------------------- Function Declarations
void advance();
char peek();
char peek_next();
void skip_whitespace_and_comments();
Token make_token(TokenType type, const char *lexeme, int line);
int is_keyword(const char *str);
Token identifier_or_keyword();
Token number_literal();
Token string_literal();
Token operator_or_punctuation();
Token preprocess_directive();
void print_token(const Token *tok);

//--------------------------------------------------- Helpers
void advance() {
    current_char = fgetc(source_file);
    if (current_char == '\n') line_number++;
}

char peek() {
    return (char)current_char;
}

char peek_next() {
    int c = fgetc(source_file);
    ungetc(c, source_file);
    return (char)c;
}

// Skip whitespace and comments
void skip_whitespace_and_comments() {
    while (1) {
        while (isspace(peek())) advance();
        if (peek() == EOF) return;
        // single-line
        if (peek() == '/' && peek_next() == '/') {
            advance(); advance();
            while (peek() != '\n' && peek() != EOF) advance();
            continue;
        }
        // multi-line
        if (peek() == '/' && peek_next() == '*') {
            advance(); advance();
            while (!(peek() == '*' && peek_next() == '/')) {
                if (peek() == EOF) {
                    fprintf(stderr, "Unterminated comment at line %d\n", line_number);
                    exit(EXIT_FAILURE);
                }
                advance();
            }
            advance(); advance();
            continue;
        }
        break;
    }
}

Token make_token(TokenType type, const char *lexeme, int line) {
    Token tok;
    tok.type = type;
    strncpy(tok.lexeme, lexeme, MAX_LEXEME_LEN - 1);
    tok.lexeme[MAX_LEXEME_LEN - 1] = '\0';
    tok.line = line;
    return tok;
}

int is_keyword(const char *str) {
    for (int i = 0; keywords[i]; i++) if (!strcmp(str, keywords[i])) return 1;
    return 0;
}

//--------------------------------------------------- Lexer Functions
Token preprocess_directive() {
    char buffer[MAX_LEXEME_LEN];
    int length = 0;
    int start_line = line_number;
    while (peek() != '\n' && peek() != EOF) {
        if (length < MAX_LEXEME_LEN - 1) buffer[length++] = peek();
        advance();
    }
    buffer[length] = '\0';
    return make_token(TOK_PREPROCESSOR, buffer, start_line);
}

Token identifier_or_keyword() {
    char buffer[MAX_LEXEME_LEN];
    int length = 0;
    int start_line = line_number;
    while (isalnum(peek()) || peek() == '_') {
        if (length < MAX_LEXEME_LEN - 1) buffer[length++] = peek();
        advance();
    }
    buffer[length] = '\0';
    if (is_keyword(buffer)) return make_token(TOK_KEYWORD, buffer, start_line);
    return make_token(TOK_IDENTIFIER, buffer, start_line);
}

Token number_literal() {
    char buffer[MAX_LEXEME_LEN];
    int length = 0, is_float = 0, start_line = line_number;
    while (isdigit(peek())) { buffer[length++] = peek(); advance(); }
    if (peek() == '.' && isdigit(peek_next())) {
        is_float = 1; buffer[length++] = peek(); advance();
        while (isdigit(peek())) { buffer[length++] = peek(); advance(); }
    }
    buffer[length] = '\0';
    return make_token(is_float ? TOK_FLOAT_LITERAL : TOK_INT_LITERAL, buffer, start_line);
}

Token string_literal() {
    char buffer[MAX_LEXEME_LEN];
    int length = 0, start_line = line_number;

    buffer[length++] = peek(); advance();

    while (peek() != '"' && peek() != EOF) {
        if (peek() == '\\' && peek_next() == '"') {
            buffer[length++] = peek(); advance();
        }
        if (length < MAX_LEXEME_LEN - 1)
            buffer[length++] = peek();
        advance();
    }

    if (peek() == '"') {
        buffer[length++] = peek(); advance();
    } else {
        fprintf(stderr, "Unterminated string literal at line %d\n", start_line);
        exit(EXIT_FAILURE);
    }

    buffer[length] = '\0';
    return make_token(TOK_STRING_LITERAL, buffer, start_line);
}


Token operator_or_punctuation() {
    char buf[3] = {0};
    int start_line = line_number;

    // Double-character operators
    if ((peek() == '=' && peek_next() == '=') ||
        (peek() == '!' && peek_next() == '=') ||
        (peek() == '<' && peek_next() == '=') ||
        (peek() == '>' && peek_next() == '=') ||
        (peek() == '+' && peek_next() == '+') ||
        (peek() == '-' && peek_next() == '-') ||
        (peek() == '+' && peek_next() == '=') ||
        (peek() == '-' && peek_next() == '=') ||
        (peek() == '*' && peek_next() == '=') ||
        (peek() == '/' && peek_next() == '=') ||
        (peek() == '&' && peek_next() == '&') ||
        (peek() == '|' && peek_next() == '|')) {

        buf[0] = peek(); advance();
        buf[1] = peek(); advance();
        return make_token(TOK_OPERATOR, buf, start_line);
    }

    // Single-character operators
    if (strchr("+-*/<>=!&|%", peek())) {
        buf[0] = peek(); advance();
        return make_token(TOK_OPERATOR, buf, start_line);
    }

    // Punctuations
    if (strchr("[],;(){}", peek())) {
        buf[0] = peek(); advance();
        return make_token(TOK_PUNCTUATION, buf, start_line);
    }

    fprintf(stderr, "Invalid character '%c' at line %d\n", peek(), line_number);
    exit(EXIT_FAILURE);
}


void print_token(const Token *tok) {
    static const char *type_names[] = { "KEYWORD","IDENTIFIER","INT_LITERAL","FLOAT_LITERAL","STRING_LITERAL","OPERATOR","PUNCTUATION","PREPROCESSOR","EOF" };
    fprintf(out_file, "[line:%d] %-16s \"%s\"\n", tok->line, type_names[tok->type], tok->lexeme);
}
//--------------------------------------------------- main
int main(void) {
    source_file = fopen(INPUT_FILE, "r"); if (!source_file) { perror("Cannot open input file"); return 1; }
    out_file = fopen(OUTPUT_FILE, "w"); if (!out_file) { perror("Cannot open output file"); fclose(source_file); return 1; }
    line_number = 1; advance();
    while (peek() != EOF) {
        skip_whitespace_and_comments(); if (peek() == EOF) break;
        Token tok;
        if (peek() == '#') { advance(); tok = preprocess_directive(); }
        else if (peek() == '"') { tok = string_literal(); }
        else if (isalpha(peek()) || peek() == '_') { tok = identifier_or_keyword(); }
        else if (isdigit(peek())) { tok = number_literal(); }
        else { tok = operator_or_punctuation(); }
        print_token(&tok);
    }
    Token eof = make_token(TOK_EOF, "EOF", line_number); print_token(&eof);
    fclose(source_file); fclose(out_file);
    return 0;
}
