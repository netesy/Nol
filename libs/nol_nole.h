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
typedef enum { NOL_NULL, NOL_BOOL, NOL_INT, NOL_FLOAT, NOL_STRING, NOL_ARRAY, NOL_OBJECT } nol_t;
struct nol_val;
typedef struct { char* k; struct nol_val* v; } nol_entry;
typedef struct nol_val {
    nol_t type;
    union { bool b; long long i; double f; char* s; struct { struct nol_val** e; size_t n; } a; struct { nol_entry* e; size_t n; } o; } u;
} nol_val;
static void nol_free(nol_val* v) {
    if (!v) return;
    if (v->type == NOL_STRING) free(v->u.s);
    else if (v->type == NOL_ARRAY) { for (size_t i=0; i<v->u.a.n; i++) nol_free(v->u.a.e[i]); free(v->u.a.e); }
    else if (v->type == NOL_OBJECT) { for (size_t i=0; i<v->u.o.n; i++) { free(v->u.o.e[i].k); nol_free(v->u.o.e[i].v); } free(v->u.o.e); }
    free(v);
}
typedef struct { const char* i; size_t p; int l, c; } lex_t;
typedef enum { T_EOF, T_ID, T_STR, T_NUM, T_TRUE, T_FALSE, T_NULL, T_COLON, T_COMMA, T_LBRK, T_RBRK, T_LBRC, T_RBRC, T_NL, T_DOT, T_STAR, T_AMP, T_LARR, T_RARR, T_LSHIFT } tok_t;
typedef struct { tok_t t; char* s; int l, c; } token_t;
static char lp(lex_t* l, int n) { return (l->p+n < strlen(l->i)) ? l->i[l->p+n] : '\0'; }
static char la(lex_t* l) { char c=lp(l,0); l->p++; if (c=='\n') { l->l++; l->c=1; } else l->c++; return c; }
static token_t nt(lex_t* l) {
    while (1) {
        char c=lp(l,0); if (c=='\0') return (token_t){T_EOF,0,l->l,l->c};
        if (isspace(c)) { if (c=='\n') { la(l); return (token_t){T_NL,0,l->l-1,1}; } la(l); continue; }
        if (c=='#') { la(l); while (lp(l,0)!='\n' && lp(l,0)!='\0') la(l); continue; }
        if (c=='"' || c=='\'') {
            char q=la(l); char* s=(char*)malloc(1); s[0]='\0'; size_t n=0;
            while (lp(l,0)!=q && lp(l,0)!='\0') {
                if (n > 1024*1024) break;
                s=(char*)realloc(s,n+2); s[n++]=la(l); s[n]='\0';
            }
            la(l); return (token_t){T_STR,s,l->l,l->c};
        }
        if (isdigit(c) || (c=='-' && isdigit(lp(l,1)))) {
            char* s=(char*)malloc(1); size_t n=0;
            while (isalnum(lp(l,0)) || strchr(".-", lp(l,0))) { s=(char*)realloc(s,n+2); s[n++]=la(l); s[n]='\0'; }
            return (token_t){T_NUM,s,l->l,l->c};
        }
        if (isalpha(c) || c=='_') {
            char* s=(char*)malloc(1); size_t n=0;
            while (isalnum(lp(l,0)) || lp(l,0)=='_' || lp(l,0)=='-') { s=(char*)realloc(s,n+2); s[n++]=la(l); s[n]='\0'; }
            tok_t t=T_ID; if (!strcmp(s,"true")) t=T_TRUE; else if (!strcmp(s,"false")) t=T_FALSE; else if (!strcmp(s,"null")) t=T_NULL;
            return (token_t){t,s,l->l,l->c};
        }
        char* tc=(char*)malloc(2); tc[0]=la(l); tc[1]='\0'; tok_t t=T_EOF;
        if (tc[0]==':') t=T_COLON; else if (tc[0]==',') t=T_COMMA; else if (tc[0]=='[') t=T_LBRK; else if (tc[0]==']') t=T_RBRK;
        else if (tc[0]=='{') t=T_LBRC; else if (tc[0]=='}') t=T_RBRC; else if (tc[0]=='.') t=T_DOT; else if (tc[0]=='*') t=T_STAR;
        else if (tc[0]=='&') t=T_AMP; else if (tc[0]=='>') t=T_RARR;
        else if (tc[0]=='<') { if (lp(l,0)=='<') { la(l); free(tc); tc=strdup("<<"); t=T_LSHIFT; } else t=T_LARR; }
        return (token_t){t,tc,l->l,l->c};
    }
}
typedef struct { lex_t l; token_t c; int d; } par_t;
static void pa(par_t* p) { if (p->c.s) free(p->c.s); p->c=nt(&p->l); }
static nol_val* pv(par_t* p);
static nol_val* po(par_t* p) {
    pa(p); nol_val* v=(nol_val*)calloc(1,sizeof(nol_val)); v->type=NOL_OBJECT;
    while (p->c.t!=T_RBRC && p->c.t!=T_EOF) {
        if (v->u.o.n > 10000) break;
        while (p->c.t==T_NL) pa(p); if (p->c.t==T_RBRC) break;
        char* k = (p->c.t == T_LSHIFT) ? strdup("<<") : strdup(p->c.s); pa(p); if (p->c.t==T_COLON) pa(p);
        nol_val* val=pv(p); v->u.o.e=(nol_entry*)realloc(v->u.o.e,sizeof(nol_entry)*(v->u.o.n+1));
        v->u.o.e[v->u.o.n].k=k; v->u.o.e[v->u.o.n++].v=val;
        while (p->c.t==T_NL) pa(p); if (p->c.t==T_COMMA) pa(p);
    }
    pa(p); return v;
}
static nol_val* pr(par_t* p) {
    pa(p); nol_val* v=(nol_val*)calloc(1,sizeof(nol_val)); v->type=NOL_ARRAY;
    while (p->c.t!=T_RBRK && p->c.t!=T_EOF) {
        if (v->u.a.n > 100000) break;
        while (p->c.t==T_NL) pa(p); if (p->c.t==T_RBRK) break;
        nol_val* val=pv(p); v->u.a.e=(nol_val**)realloc(v->u.a.e,sizeof(nol_val*)*(v->u.a.n+1));
        v->u.a.e[v->u.a.n++]=val;
        while (p->c.t==T_NL) pa(p); if (p->c.t==T_COMMA) pa(p);
    }
    pa(p); return v;
}
static nol_val* pv(par_t* p) {
    p->d++; if (p->d>100) return NULL; nol_val* v=(nol_val*)calloc(1,sizeof(nol_val));
    if (p->c.t==T_STR) { v->type=NOL_STRING; v->u.s=strdup(p->c.s); pa(p); }
    else if (p->c.t==T_NUM) { if (strchr(p->c.s,'.')) { v->type=NOL_FLOAT; v->u.f=atof(p->c.s); } else { v->type=NOL_INT; v->u.i=atoll(p->c.s); } pa(p); }
    else if (p->c.t==T_TRUE) { v->type=NOL_BOOL; v->u.b=true; pa(p); }
    else if (p->c.t==T_FALSE) { v->type=NOL_BOOL; v->u.b=false; pa(p); }
    else if (p->c.t==T_NULL) { v->type=NOL_NULL; pa(p); }
    else if (p->c.t==T_LBRC) { free(v); v=po(p); }
    else if (p->c.t==T_LBRK) { free(v); v=pr(p); }
    else if (p->c.t==T_AMP) { pa(p); pa(p); free(v); v=pv(p); }
    else if (p->c.t==T_STAR) { pa(p); pa(p); v->type=NOL_NULL; }
    else if (p->c.t==T_LARR) { pa(p); char* t=strdup(p->c.s); pa(p); pa(p); nol_val* raw=pv(p); if (!strcmp(t,"int") && raw->type==NOL_STRING) { v->type=NOL_INT; v->u.i=atoll(raw->u.s); } else { v->type=NOL_NULL; } free(t); nol_free(raw); }
    else { free(v); v=NULL; }
    p->d--; return v;
}
static nol_val* fcp(nol_val* r, char** p, size_t n, bool a) {
    nol_val* c=r; for (size_t i=0; i<n; i++) {
        nol_val* x=NULL; for (size_t j=0; j<c->u.o.n; j++) if (!strcmp(c->u.o.e[j].k,p[i])) { x=c->u.o.e[j].v; break; }
        if (!x) { x=(nol_val*)calloc(1,sizeof(nol_val)); x->type=(a && i==n-1)?NOL_ARRAY:NOL_OBJECT; c->u.o.e=(nol_entry*)realloc(c->u.o.e,sizeof(nol_entry)*(c->u.o.n+1)); c->u.o.e[c->u.o.n].k=strdup(p[i]); c->u.o.e[c->u.o.n++].v=x; }
        if (a && i==n-1) return x; c=x;
    }
    return c;
}
nol_val* nol_parse(const char* i) {
    if (strlen(i)>10*1024*1024) return NULL;
    par_t p={{i,0,1,1},{T_EOF,0,1,1},0}; p.c=nt(&p.l); nol_val* r=(nol_val*)calloc(1,sizeof(nol_val)); r->type=NOL_OBJECT; nol_val* t=r;
    while (p.c.t!=T_EOF) {
        while (p.c.t==T_NL) pa(&p); if (p.c.t==T_EOF) break;
        if (p.c.t==T_LBRK) {
            pa(&p); char** path=NULL; size_t n=0; bool a=false;
            while (p.c.t!=T_RBRK && p.c.t!=T_EOF) { if (p.c.t==T_STAR) { a=true; pa(&p); } else { path=(char**)realloc(path,sizeof(char*)*(n+1)); path[n++]=strdup(p.c.s); pa(&p); } if (p.c.t==T_DOT) pa(&p); }
            pa(&p); nol_val* nd=fcp(r,path,n,a); if (a) { nol_val* o=(nol_val*)calloc(1,sizeof(nol_val)); o->type=NOL_OBJECT; nd->u.a.e=(nol_val**)realloc(nd->u.a.e,sizeof(nol_val*)*(nd->u.a.n+1)); nd->u.a.e[nd->u.a.n++]=o; t=o; } else t=nd;
            for (size_t k=0; k<n; k++) free(path[k]); free(path);
        } else {
            char* k=strdup(p.c.s); pa(&p); if (p.c.t==T_COLON) pa(&p); nol_val* v=pv(&p);
            t->u.o.e=(nol_entry*)realloc(t->u.o.e,sizeof(nol_entry)*(t->u.o.n+1)); t->u.o.e[t->u.o.n].k=k; t->u.o.e[t->u.o.n++].v=v;
        }
    }
    if (p.c.s) free(p.c.s); return r;
}
static nol_val* nol_at(nol_val* v, const char* p) {
    if (!v || v->type != NOL_OBJECT) return NULL;
    char* copy = strdup(p); char* token = strtok(copy, "."); nol_val* curr = v;
    while (token) {
        bool found = false; for (size_t i=0; i<curr->u.o.n; i++) { if (!strcmp(curr->u.o.e[i].k, token)) { curr = curr->u.o.e[i].v; found = true; break; } }
        if (!found) { curr = NULL; break; } token = strtok(NULL, "."); if (token && curr->type != NOL_OBJECT) { curr = NULL; break; }
    }
    free(copy); return curr;
}
#ifdef __cplusplus
}
#endif
#endif
