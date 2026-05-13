#ifndef NOL_NOLE_H
#define NOL_NOLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOL_NULL, NOL_BOOL, NOL_INT, NOL_FLOAT, NOL_STRING, NOL_ARRAY, NOL_OBJECT
} nol_type;

struct nol_value;
typedef struct nol_object_entry { char* key; struct nol_value* value; } nol_object_entry;
typedef struct nol_value {
    nol_type type;
    union {
        bool b; long long i; double f; char* s;
        struct { struct nol_value** elements; size_t count; } a;
        struct { nol_object_entry* entries; size_t count; } o;
    } u;
} nol_value;

static void nol_free(nol_value* v) {
    if (!v) return;
    if (v->type == NOL_STRING) free(v->u.s);
    else if (v->type == NOL_ARRAY) { for (size_t i = 0; i < v->u.a.count; i++) nol_free(v->u.a.elements[i]); free(v->u.a.elements); }
    else if (v->type == NOL_OBJECT) { for (size_t i = 0; i < v->u.o.count; i++) { free(v->u.o.entries[i].key); nol_free(v->u.o.entries[i].value); } free(v->u.o.entries); }
    free(v);
}

typedef struct { const char* input; size_t pos; int line, col; } lexer_t;
typedef enum { T_EOF, T_ID, T_STR, T_NUM, T_TRUE, T_FALSE, T_NULL, T_COLON, T_COMMA, T_LBRACKET, T_RBRACKET, T_LBRACE, T_RBRACE, T_NEWLINE, T_DOT, T_STAR } tok_t;
typedef struct { tok_t type; char* text; int line, col; } token_t;

static char l_peek(lexer_t* l, int n) { return (l->pos + n < strlen(l->input)) ? l->input[l->pos + n] : '\0'; }
static char l_adv(lexer_t* l) { char c = l_peek(l, 0); l->pos++; if (c == '\n') { l->line++; l->col = 1; } else l->col++; return c; }
static token_t next_tok(lexer_t* l) {
    while (1) {
        char c = l_peek(l, 0);
        if (c == '\0') return (token_t){ T_EOF, NULL, l->line, l->col };
        if (isspace(c)) { if (c == '\n') { l_adv(l); return (token_t){ T_NEWLINE, NULL, l->line-1, 1 }; } l_adv(l); continue; }
        if (c == '#') { l_adv(l); while (l_peek(l, 0) != '\n' && l_peek(l, 0) != '\0') l_adv(l); continue; }
        if (c == '"' || c == '\'') {
            char q = l_adv(l); char* s = (char*)malloc(1); s[0] = '\0'; size_t len = 0;
            while (l_peek(l, 0) != q && l_peek(l, 0) != '\0') {
                s = (char*)realloc(s, len + 2); s[len++] = l_adv(l); s[len] = '\0';
            }
            l_adv(l); return (token_t){ T_STR, s, l->line, l->col };
        }
        if (isdigit(c) || (c == '-' && isdigit(l_peek(l, 1)))) {
            char* s = (char*)malloc(1); size_t len = 0;
            while (isalnum(l_peek(l, 0)) || strchr(".-", l_peek(l, 0))) {
                s = (char*)realloc(s, len + 2); s[len++] = l_adv(l); s[len] = '\0';
            }
            return (token_t){ T_NUM, s, l->line, l->col };
        }
        if (isalpha(c) || c == '_') {
            char* s = (char*)malloc(1); size_t len = 0;
            while (isalnum(l_peek(l, 0)) || l_peek(l, 0) == '_' || l_peek(l, 0) == '-') {
                s = (char*)realloc(s, len + 2); s[len++] = l_adv(l); s[len] = '\0';
            }
            tok_t t = T_ID;
            if (!strcmp(s, "true")) t = T_TRUE; else if (!strcmp(s, "false")) t = T_FALSE; else if (!strcmp(s, "null")) t = T_NULL;
            return (token_t){ t, s, l->line, l->col };
        }
        char* tc = (char*)malloc(2); tc[0] = l_adv(l); tc[1] = '\0';
        tok_t t = T_EOF;
        if (tc[0] == ':') t = T_COLON; else if (tc[0] == ',') t = T_COMMA; else if (tc[0] == '[') t = T_LBRACKET;
        else if (tc[0] == ']') t = T_RBRACKET; else if (tc[0] == '{') t = T_LBRACE; else if (tc[0] == '}') t = T_RBRACE;
        else if (tc[0] == '.') t = T_DOT; else if (tc[0] == '*') t = T_STAR;
        return (token_t){ t, tc, l->line, l->col };
    }
}

typedef struct { lexer_t l; token_t cur; } parser_t;
static void p_adv(parser_t* p) { if (p->cur.text) free(p->cur.text); p->cur = next_tok(&p->l); }
static nol_value* p_val(parser_t* p);

static nol_value* p_obj(parser_t* p) {
    p_adv(p); nol_value* v = (nol_value*)calloc(1, sizeof(nol_value)); v->type = NOL_OBJECT;
    while (p->cur.type != T_RBRACE && p->cur.type != T_EOF) {
        while (p->cur.type == T_NEWLINE) p_adv(p);
        if (p->cur.type == T_RBRACE) break;
        char* k = strdup(p->cur.text); p_adv(p); p_adv(p);
        nol_value* val = p_val(p);
        v->u.o.entries = (nol_object_entry*)realloc(v->u.o.entries, sizeof(nol_object_entry)*(v->u.o.count+1));
        v->u.o.entries[v->u.o.count].key = k; v->u.o.entries[v->u.o.count++].value = val;
        while (p->cur.type == T_NEWLINE) p_adv(p);
        if (p->cur.type == T_COMMA) p_adv(p);
    }
    p_adv(p); return v;
}

static nol_value* p_arr(parser_t* p) {
    p_adv(p); nol_value* v = (nol_value*)calloc(1, sizeof(nol_value)); v->type = NOL_ARRAY;
    while (p->cur.type != T_RBRACKET && p->cur.type != T_EOF) {
        while (p->cur.type == T_NEWLINE) p_adv(p);
        if (p->cur.type == T_RBRACKET) break;
        nol_value* val = p_val(p);
        v->u.a.elements = (nol_value**)realloc(v->u.a.elements, sizeof(nol_value*)*(v->u.a.count+1));
        v->u.a.elements[v->u.a.count++] = val;
        while (p->cur.type == T_NEWLINE) p_adv(p);
        if (p->cur.type == T_COMMA) p_adv(p);
    }
    p_adv(p); return v;
}

static nol_value* p_val(parser_t* p) {
    if (p->cur.type == T_STR) { nol_value* v = (nol_value*)calloc(1, sizeof(nol_value)); v->type = NOL_STRING; v->u.s = strdup(p->cur.text); p_adv(p); return v; }
    if (p->cur.type == T_NUM) {
        nol_value* v = (nol_value*)calloc(1, sizeof(nol_value));
        if (strchr(p->cur.text, '.')) { v->type = NOL_FLOAT; v->u.f = atof(p->cur.text); }
        else { v->type = NOL_INT; v->u.i = atoll(p->cur.text); }
        p_adv(p); return v;
    }
    if (p->cur.type == T_TRUE) { nol_value* v = (nol_value*)calloc(1, sizeof(nol_value)); v->type = NOL_BOOL; v->u.b = true; p_adv(p); return v; }
    if (p->cur.type == T_FALSE) { nol_value* v = (nol_value*)calloc(1, sizeof(nol_value)); v->type = NOL_BOOL; v->u.b = false; p_adv(p); return v; }
    if (p->cur.type == T_NULL) { nol_value* v = (nol_value*)calloc(1, sizeof(nol_value)); v->type = NOL_NULL; p_adv(p); return v; }
    if (p->cur.type == T_LBRACE) return p_obj(p);
    if (p->cur.type == T_LBRACKET) return p_arr(p);
    return NULL;
}

static nol_value* find_or_create_path(nol_value* root, char** path, size_t path_len, bool is_array) {
    nol_value* curr = root;
    for (size_t i = 0; i < path_len; i++) {
        char* part = path[i]; nol_value* next = NULL;
        for (size_t j = 0; j < curr->u.o.count; j++) {
            if (!strcmp(curr->u.o.entries[j].key, part)) { next = curr->u.o.entries[j].value; break; }
        }
        if (!next) {
            next = (nol_value*)calloc(1, sizeof(nol_value));
            if (is_array && i == path_len - 1) { next->type = NOL_ARRAY; } else { next->type = NOL_OBJECT; }
            curr->u.o.entries = (nol_object_entry*)realloc(curr->u.o.entries, sizeof(nol_object_entry)*(curr->u.o.count+1));
            curr->u.o.entries[curr->u.o.count].key = strdup(part); curr->u.o.entries[curr->u.o.count++].value = next;
        }
        if (is_array && i == path_len - 1) return next;
        curr = next;
    }
    return curr;
}

nol_value* nol_parse(const char* input) {
    if (strlen(input) > 10 * 1024 * 1024) return NULL;
    parser_t p; p.l.input = input; p.l.pos = 0; p.l.line = 1; p.l.col = 1; p.cur.text = NULL; p.cur = next_tok(&p.l);
    nol_value* root = (nol_value*)calloc(1, sizeof(nol_value)); root->type = NOL_OBJECT;
    nol_value* target = root;
    while (p.cur.type != T_EOF) {
        while (p.cur.type == T_NEWLINE) p_adv(&p);
        if (p.cur.type == T_EOF) break;
        if (p.cur.type == T_LBRACKET) {
            p_adv(&p); char** path = NULL; size_t path_len = 0; bool is_array = false;
            while (p.cur.type != T_RBRACKET && p.cur.type != T_EOF) {
                if (p.cur.type == T_STAR) { is_array = true; p_adv(&p); }
                else { path = (char**)realloc(path, sizeof(char*)*(path_len+1)); path[path_len++] = strdup(p.cur.text); p_adv(&p); }
                if (p.cur.type == T_DOT) p_adv(&p);
            }
            p_adv(&p);
            nol_value* node = find_or_create_path(root, path, path_len, is_array);
            if (is_array) {
                nol_value* new_obj = (nol_value*)calloc(1, sizeof(nol_value)); new_obj->type = NOL_OBJECT;
                node->u.a.elements = (nol_value**)realloc(node->u.a.elements, sizeof(nol_value*)*(node->u.a.count+1));
                node->u.a.elements[node->u.a.count++] = new_obj; target = new_obj;
            } else target = node;
            for (size_t i = 0; i < path_len; i++) free(path[i]); free(path);
        } else {
            char* k = strdup(p.cur.text); p_adv(&p); p_adv(&p);
            nol_value* v = p_val(&p);
            target->u.o.entries = (nol_object_entry*)realloc(target->u.o.entries, sizeof(nol_object_entry)*(target->u.o.count+1));
            target->u.o.entries[target->u.o.count].key = k; target->u.o.entries[target->u.o.count++].value = v;
        }
    }
    if (p.cur.text) free(p.cur.text); return root;
}

#ifdef __cplusplus
}
#endif
#endif
