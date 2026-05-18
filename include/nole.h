#ifndef NOLE_H
#define NOLE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { NOL_NULL, NOL_BOOL, NOL_INT, NOL_FLOAT, NOL_STRING, NOL_ARRAY, NOL_OBJECT } nol_type;
struct nol_val;
typedef struct { char* k; struct nol_val* v; } nol_entry;
typedef struct { struct nol_val** e; size_t n; } nol_array;
typedef struct { nol_entry* e; size_t n; } nol_object;
typedef struct nol_val {
    nol_type type;
    union { bool b; long long i; double f; char* s; nol_array a; nol_object o; } u;
} nol_val;
typedef struct { char msg[256]; int line, col; } nol_err_t;

nol_val* nol_new_null() { return (nol_val*)calloc(1, sizeof(nol_val)); }
nol_val* nol_new_bool(bool b) { nol_val* v = nol_new_null(); v->type = NOL_BOOL; v->u.b = b; return v; }
nol_val* nol_new_int(long long i) { nol_val* v = nol_new_null(); v->type = NOL_INT; v->u.i = i; return v; }
nol_val* nol_new_float(double f) { nol_val* v = nol_new_null(); v->type = NOL_FLOAT; v->u.f = f; return v; }
nol_val* nol_new_string(const char* s) { nol_val* v = nol_new_null(); v->type = NOL_STRING; v->u.s = strdup(s?s:""); return v; }
nol_val* nol_new_array() { nol_val* v = nol_new_null(); v->type = NOL_ARRAY; return v; }
nol_val* nol_new_object() { nol_val* v = nol_new_null(); v->type = NOL_OBJECT; return v; }

void nol_free(nol_val* v) {
    if (!v) return;
    if (v->type == NOL_STRING) free(v->u.s);
    else if (v->type == NOL_ARRAY) { for (size_t i = 0; i < v->u.a.n; i++) nol_free(v->u.a.e[i]); free(v->u.a.e); }
    else if (v->type == NOL_OBJECT) { for (size_t i = 0; i < v->u.o.n; i++) { free(v->u.o.e[i].k); nol_free(v->u.o.e[i].v); } free(v->u.o.e); }
    free(v);
}

void nol_array_add(nol_val* a, nol_val* v) {
    if (!a || a->type != NOL_ARRAY || !v) return;
    a->u.a.e = (nol_val**)realloc(a->u.a.e, sizeof(nol_val*) * (a->u.a.n + 1));
    a->u.a.e[a->u.a.n++] = v;
}

void nol_object_set(nol_val* o, const char* k, nol_val* v) {
    if (!o || o->type != NOL_OBJECT || !k) { if(v) nol_free(v); return; }
    for (size_t i=0; i<o->u.o.n; i++) if (o->u.o.e[i].k && strcmp(o->u.o.e[i].k, k) == 0) {
        nol_free(o->u.o.e[i].v); o->u.o.e[i].v = v; return;
    }
    o->u.o.e = (nol_entry*)realloc(o->u.o.e, sizeof(nol_entry) * (o->u.o.n + 1));
    o->u.o.e[o->u.o.n].k = strdup(k); o->u.o.e[o->u.o.n++].v = v;
}

nol_val* nol_get(const nol_val* v, const char* path) {
    if (!v || v->type != NOL_OBJECT || !path) return NULL;
    char p[512]; strncpy(p, path, 511); p[511]=0;
    char* start = p; nol_val* curr = (nol_val*)v;
    while (start && *start) {
        char* dot = strchr(start, '.'); if (dot) *dot = 0;
        bool found = false; if (curr->type == NOL_OBJECT) {
            for (size_t i = 0; i < curr->u.o.n; i++) if (curr->u.o.e[i].k && strcmp(curr->u.o.e[i].k, start) == 0) { curr = curr->u.o.e[i].v; found = true; break; }
        }
        if (!found) return NULL;
        start = dot ? dot+1 : NULL;
    }
    return curr;
}

#ifdef NOLE_IMPLEMENTATION
#define NOL_IMPLEMENTATION
#endif

#ifdef NOL_IMPLEMENTATION
static void _set_err(nol_err_t* err, const char* m, int l, int c) { if (err) { strncpy(err->msg, m, 255); err->msg[255]=0; err->line=l; err->col=c; } }
typedef struct { const char* s; size_t p, len; int l, c; clock_t start; int depth; bool nole; } _prs_t;
static char _pk(_prs_t* p) { return p->p < p->len ? p->s[p->p] : '\0'; }
static char _ad(_prs_t* p) { char ch = _pk(p); if (ch) { p->p++; if (ch == '\n') { p->l++; p->c = 1; } else p->c++; } return ch; }
static void _sk(_prs_t* p) { while (1) { char ch = _pk(p); if (isspace((unsigned char)ch)) _ad(p); else if (ch == '#') { _ad(p); if (_pk(p) == '#') { _ad(p); while (p->p < p->len-1 && !(p->s[p->p] == '#' && p->s[p->p+1] == '#')) _ad(p); if (p->p < p->len) { _ad(p); _ad(p); } } else while (_pk(p) && _pk(p) != '\n') _ad(p); } else break; } }
static nol_val* _prs_v(_prs_t* p, nol_err_t* err);
static char* _prs_k(_prs_t* p) { _sk(p); char buf[4096]; int i=0; if (_pk(p) == '"' || _pk(p) == '\'') { char q = _ad(p); while (_pk(p) && _pk(p) != q && i<4095) { if (_pk(p) == '\\') { _ad(p); char e = _ad(p); if (e == 'n') buf[i++] = '\n'; else buf[i++] = e; } else buf[i++] = _ad(p); } if (_pk(p) == q) _ad(p); } else while ((isalnum((unsigned char)_pk(p)) || _pk(p) == '_' || _pk(p) == '-') && i<4095) buf[i++] = _ad(p); buf[i]=0; return strdup(buf); }
static void _prs_p(_prs_t* p, nol_val* o, nol_err_t* err) {
    _sk(p); if (!_pk(p)) return;
    if (p->nole && _pk(p) == '&') { _ad(p); char* n = _prs_k(p); nol_val* ai = nol_new_object(); nol_object_set(ai, "name", nol_new_string(n)); _sk(p); if (_pk(p) == ':') { _ad(p); nol_object_set(ai, "value", _prs_v(p, err)); } else nol_object_set(ai, "value", nol_new_null()); if (!nol_get(o, "_anchors")) nol_object_set(o, "_anchors", nol_new_array()); nol_array_add(nol_get(o, "_anchors"), ai); free(n); return; }
    bool is_m = false; if (p->nole && _pk(p) == '<' && p->p+1 < p->len && p->s[p->p+1] == '<') { _ad(p); _ad(p); is_m = true; }
    char* k = is_m ? strdup("<<") : _prs_k(p); if (!is_m && o->type == NOL_OBJECT) { for (size_t i=0; i<o->u.o.n; i++) if (strcmp(o->u.o.e[i].k, k) == 0) _set_err(err, "Duplicate key", p->l, p->c); }
    _sk(p); if (_pk(p) == ':') _ad(p);
    nol_val* v = _prs_v(p, err); if (is_m) { nol_val* ma = NULL; for (size_t i=0; i<o->u.o.n; i++) if (strcmp(o->u.o.e[i].k, "<<") == 0) ma = o->u.o.e[i].v; if (!ma) { ma = nol_new_array(); nol_object_set(o, "<<", ma); } nol_array_add(ma, v); } else nol_object_set(o, k, v); free(k);
}
static nol_val* _prs_v(_prs_t* p, nol_err_t* err) {
    _sk(p); if (++p->depth > 100) return NULL;
    char ch = _pk(p); nol_val* r = NULL;
    if (ch == '{') { _ad(p); r = nol_new_object(); while (1) { _sk(p); if (_pk(p) == '}' || !_pk(p)) break; _prs_p(p, r, err); _sk(p); if (_pk(p) == ',') _ad(p); } if (_pk(p) == '}') _ad(p); }
    else if (ch == '[') { _ad(p); r = nol_new_array(); while (1) { _sk(p); if (_pk(p) == ']' || !_pk(p)) break; nol_array_add(r, _prs_v(p, err)); _sk(p); if (_pk(p) == ',') _ad(p); } if (_pk(p) == ']') _ad(p); }
    else if (p->nole && (ch == '*' || ch == '<')) { char t = _ad(p); char b[256]; int i=0; if (t == '<') { while (_pk(p) && _pk(p) != '>' && i<255) b[i++] = _ad(p); b[i]=0; if (_pk(p) == '>') _ad(p); nol_val* co = nol_new_object(); nol_val* ci = nol_new_object(); nol_object_set(ci, "type", nol_new_string(b)); nol_object_set(ci, "value", _prs_v(p, err)); nol_object_set(co, "_coerce", ci); r = co; } else { while (isalnum((unsigned char)_pk(p)) || _pk(p) == '_' || _pk(p) == '-') b[i++] = _ad(p); b[i]=0; char ab[260]; snprintf(ab, 260, "*%s", b); r = nol_new_string(ab); } }
    else if (ch == '"' || ch == '\'') { char q = _ad(p); size_t cap = 1024, i=0; char* b = (char*)malloc(cap); while (_pk(p) && _pk(p) != q) { if (i+10 >= cap) { cap *= 2; b = (char*)realloc(b, cap); } if (_pk(p) == '\\') { _ad(p); char e = _ad(p); if (e == 'n') b[i++] = '\n'; else if (e == 'r') b[i++] = '\r'; else if (e == 't') b[i++] = '\t'; else if (e == 'u' || e == 'U') { int n = (e == 'u' ? 4 : 8); char hb[9]; for(int j=0; j<n; j++) hb[j] = _ad(p); hb[n]=0; unsigned int u = strtoul(hb, NULL, 16); if (u < 0x80) b[i++] = u; else if (u < 0x800) { b[i++] = 0xc0 | (u >> 6); b[i++] = 0x80 | (u & 0x3f); } else if (u < 0x10000) { b[i++] = 0xe0 | (u >> 12); b[i++] = 0x80 | ((u >> 6) & 0x3f); b[i++] = 0x80 | (u & 0x3f); } else { b[i++] = 0xf0 | (u >> 18); b[i++] = 0x80 | ((u >> 12) & 0x3f); b[i++] = 0x80 | ((u >> 6) & 0x3f); b[i++] = 0x80 | (u & 0x3f); } } else b[i++] = e; } else b[i++] = _ad(p); } b[i]=0; if (_pk(p) == q) _ad(p); r = nol_new_string(b); free(b); }
    else if (isdigit((unsigned char)ch) || ch == '-') { char b[64]; int i=0; while ((isdigit((unsigned char)_pk(p)) || strchr(".-eE+", _pk(p))) && i<63) b[i++] = _ad(p); b[i]=0; if (strchr(b, '.')) r = nol_new_float(atof(b)); else r = nol_new_int(atoll(b)); }
    else { char b[32]; int i=0; while (isalpha((unsigned char)_pk(p)) && i<31) b[i++] = _ad(p); b[i]=0; if (strcmp(b, "true") == 0) r = nol_new_bool(true); else if (strcmp(b, "false") == 0) r = nol_new_bool(false); else if (strcmp(b, "null") == 0) r = nol_new_null(); }
    p->depth--; return r;
}
static void _prs_into(_prs_t* p, nol_val* o, nol_err_t* err) { while (1) { _sk(p); char c = _pk(p); if (c == '[' || !c) break; _prs_p(p, o, err); } }
nol_val* nole_parse_ex(const char* input, bool nole, nol_err_t* err) {
    if (!input) return nol_new_object(); _prs_t p = { input, 0, strlen(input), 1, 1, clock(), 0, nole }; nol_val* root = nol_new_object();
    while (1) { _sk(&p); if (!_pk(&p)) break; if (_pk(&p) == '[') { _ad(&p); bool is_a = false; if (_pk(&p) == '*') { _ad(&p); is_a = true; } char path[256]; int i=0; while (_pk(&p) && _pk(&p) != ']' && i<255) path[i++] = _ad(&p); path[i]=0; if (_pk(&p) == ']') _ad(&p); char* start = path; nol_val* curr = root; while (1) { char* d = strchr(start, '.'); if (d) *d = 0; bool last = (d == NULL); nol_val* next = NULL; for (size_t j=0; j<curr->u.o.n; j++) if (strcmp(curr->u.o.e[j].k, start) == 0) next = curr->u.o.e[j].v; if (!next) { next = (last && is_a) ? nol_new_array() : nol_new_object(); nol_object_set(curr, start, next); } if (last) { if (is_a) { nol_val* entry = nol_new_object(); nol_array_add(next, entry); _prs_into(&p, entry, err); } else _prs_into(&p, next, err); break; } curr = next; start = d + 1; } } else _prs_p(&p, root, err); }
    return root;
}
nol_val* nol_parse(const char* input, nol_err_t* err) { return nole_parse_ex(input, false, err); }
static void _ap(char** b, size_t* s, size_t* c, const char* x) { size_t l=strlen(x); while (*s+l+1 >= *c) { *c *= 2; *b = (char*)realloc(*b, *c); } strcpy(*b+*s, x); *s += l; }
static int _cmp_ent(const void* a, const void* b) { return strcmp(((nol_entry*)a)->k, ((nol_entry*)b)->k); }
static void _dm(const nol_val* v, int ind, int lvl, bool root, char** b, size_t* s, size_t* c) {
    if (!v) { _ap(b, s, c, "null"); return; }
    switch (v->type) {
        case NOL_NULL: _ap(b, s, c, "null"); break; case NOL_BOOL: _ap(b, s, c, v->u.b?"true":"false"); break;
        case NOL_INT: { char buf[64]; snprintf(buf, 64, "%lld", v->u.i); _ap(b, s, c, buf); break; }
        case NOL_FLOAT: { char buf[64]; snprintf(buf, 64, "%.5f", v->u.f); char* p=buf+strlen(buf)-1; while(p>buf && *p=='0') *p--=0; if(*p=='.') *p--=0; _ap(b, s, c, buf); break; }
        case NOL_STRING: _ap(b, s, c, "\""); _ap(b, s, c, v->u.s); _ap(b, s, c, "\""); break;
        case NOL_ARRAY: _ap(b, s, c, "["); for (size_t i=0; i<v->u.a.n; i++) { _dm(v->u.a.e[i], ind, lvl+1, false, b, s, c); if (i+1<v->u.a.n) _ap(b, s, c, ", "); } _ap(b, s, c, "]"); break;
        case NOL_OBJECT: if (v->u.o.n > 0) qsort(v->u.o.e, v->u.o.n, sizeof(nol_entry), _cmp_ent); if (!root) _ap(b, s, c, "{"); for (size_t i=0; i<v->u.o.n; i++) { if (!v->u.o.e[i].k || (v->u.o.e[i].k[0] == '_' && !root)) continue; _ap(b, s, c, "\n"); for (int j=0; j<(lvl+1)*ind; j++) _ap(b, s, c, " "); _ap(b, s, c, v->u.o.e[i].k); _ap(b, s, c, ": "); _dm(v->u.o.e[i].v, ind, lvl+1, false, b, s, c); if (i+1<v->u.o.n) _ap(b, s, c, ","); } _ap(b, s, c, "\n"); for (int j=0; j<lvl*ind; j++) _ap(b, s, c, " "); if (!root) _ap(b, s, c, "}"); break;
    }
}
char* nol_dump(const nol_val* v, int indent) { size_t c=1024, s=0; char* b=(char*)malloc(c); b[0]=0; _dm(v, indent, 0, true, &b, &s, &c); return b; }
typedef struct { char* k; nol_val* v; } _anchor_t;
typedef struct { _anchor_t* a; size_t n; const char** app_env; size_t app_env_n; char** doc_env; size_t doc_env_n; nol_val* root; } _eval_t;
static nol_val* _clon(const nol_val* v) { if (!v) return NULL; nol_val* r = (nol_val*)calloc(1, sizeof(nol_val)); r->type = v->type; if (v->type == NOL_BOOL) r->u.b = v->u.b; else if (v->type == NOL_INT) r->u.i = v->u.i; else if (v->type == NOL_FLOAT) r->u.f = v->u.f; else if (v->type == NOL_STRING) r->u.s = strdup(v->u.s); else if (v->type == NOL_ARRAY) for (size_t i=0; i<v->u.a.n; i++) nol_array_add(r, _clon(v->u.a.e[i])); else if (v->type == NOL_OBJECT) for (size_t i=0; i<v->u.o.n; i++) if(v->u.o.e[i].k) nol_object_set(r, v->u.o.e[i].k, _clon(v->u.o.e[i].v)); return r; }
static nol_val* _cm(nol_val* v, _eval_t* e) { if (!v || v->type != NOL_OBJECT) return v; nol_val* ans = nol_get(v, "_anchors"); if (ans && ans->type == NOL_ARRAY) for (size_t i=0; i<ans->u.a.n; i++) { nol_val* an = ans->u.a.e[i]; nol_val* name = nol_get(an, "name"); nol_val* val = nol_get(an, "value"); if (name && name->type == NOL_STRING) { e->a = (_anchor_t*)realloc(e->a, sizeof(_anchor_t) * (e->n + 1)); e->a[e->n].k = strdup(name->u.s); e->a[e->n++].v = _clon(val && val->type != NOL_NULL ? val : v); } } nol_val* env_cfg = nol_get(v, "_env"); if (env_cfg && env_cfg->type == NOL_OBJECT) { nol_val* al = nol_get(env_cfg, "allowed"); if (al && al->type == NOL_ARRAY) for (size_t i=0; i<al->u.a.n; i++) if (al->u.a.e[i]->type == NOL_STRING) { e->doc_env = (char**)realloc(e->doc_env, sizeof(char*) * (e->doc_env_n + 1)); e->doc_env[e->doc_env_n++] = strdup(al->u.a.e[i]->u.s); } } for (size_t i=0; i<v->u.o.n; i++) if (v->u.o.e[i].k) v->u.o.e[i].v = _cm(v->u.o.e[i].v, e); return v; }
static nol_val* _rm(nol_val* v, _eval_t* e, int depth) { if (!v || depth > 20) return v; if (v->type == NOL_OBJECT) { for (size_t i=0; i<v->u.o.n; i++) { if (v->u.o.e[i].k && strcmp(v->u.o.e[i].k, "<<") == 0) { nol_val* m = v->u.o.e[i].v; nol_array ma = (m->type == NOL_ARRAY) ? m->u.a : (nol_array){&m, 1}; for (size_t j=0; j<ma.n; j++) { nol_val* rm = ma.e[j]; if (rm->type == NOL_STRING && rm->u.s[0] == '*') for (size_t k=0; k<e->n; k++) if (strcmp(e->a[k].k, rm->u.s + 1) == 0) { rm = e->a[k].v; break; } rm = _rm(rm, e, depth + 1); if (rm && rm->type == NOL_OBJECT) for(size_t k=0; k<rm->u.o.n; k++) { if (!rm->u.o.e[k].k || rm->u.o.e[k].k[0] == '_') continue; bool ex = false; for(size_t l=0; l<v->u.o.n; l++) if(v->u.o.e[l].k && strcmp(v->u.o.e[l].k, rm->u.o.e[k].k) == 0) ex = true; if(!ex) nol_object_set(v, rm->u.o.e[k].k, _clon(rm->u.o.e[k].v)); } } } if (v->u.o.e[i].k) v->u.o.e[i].v = _rm(v->u.o.e[i].v, e, depth); } } else if (v->type == NOL_ARRAY) for (size_t i=0; i<v->u.a.n; i++) v->u.a.e[i] = _rm(v->u.a.e[i], e, depth); return v; }
static nol_val* _re(nol_val* v, _eval_t* e) { if (!v) return v; if (v->type == NOL_OBJECT) { if (v->u.o.n == 1 && strcmp(v->u.o.e[0].k, "env") == 0 && v->u.o.e[0].v->type == NOL_STRING) { char* val = getenv(v->u.o.e[0].v->u.s); nol_val* nv = nol_new_string(val ? val : ""); nol_free(v); return nv; } for (size_t i=0; i<v->u.o.n; i++) if (v->u.o.e[i].k) v->u.o.e[i].v = _re(v->u.o.e[i].v, e); } else if (v->type == NOL_ARRAY) for (size_t i=0; i<v->u.a.n; i++) v->u.a.e[i] = _re(v->u.a.e[i], e); return v; }
static nol_val* _ri(nol_val* v, _eval_t* e, int depth) { if (!v || depth > 50) return v; if (v->type == NOL_STRING && strstr(v->u.s, "${")) { size_t cap = 4096, sz = 0; char* res = (char*)malloc(cap); char* s = v->u.s; while (*s) { if (*s == '$' && *(s+1) == '{') { char* end = strchr(s, '}'); if (!end) break; char path[256]; strncpy(path, s+2, end-s-2); path[end-s-2]=0; nol_val* node = nol_get(e->root, path); char* vs = node ? (node->type == NOL_STRING ? node->u.s : nol_dump(node, 2)) : ""; size_t vl = strlen(vs); while (sz + vl + 1 >= cap) { cap *= 2; res = (char*)realloc(res, cap); } strcpy(res + sz, vs); sz += vl; s = end + 1; } else { if (sz + 2 >= cap) { cap *= 2; res = (char*)realloc(res, cap); } res[sz++] = *s++; res[sz] = 0; } } nol_val* nv = nol_new_string(res); free(res); nol_free(v); return nv; } else if (v->type == NOL_OBJECT) for (size_t i=0; i<v->u.o.n; i++) if (v->u.o.e[i].k) v->u.o.e[i].v = _ri(v->u.o.e[i].v, e, depth); else if (v->type == NOL_ARRAY) for (size_t i=0; i<v->u.a.n; i++) v->u.a.e[i] = _ri(v->u.a.e[i], e, depth); return v; }
static nol_val* _rc(nol_val* v, _eval_t* e) { if (!v) return v; if (v->type == NOL_OBJECT) { nol_val* cv = nol_get(v, "_coerce"); if (cv && cv->type == NOL_OBJECT) { nol_val* t = nol_get(cv, "type"); nol_val* val = nol_get(cv, "value"); if (t && t->type == NOL_STRING && val) { val = _rc(val, e); char* s = (val->type == NOL_STRING) ? val->u.s : nol_dump(val, 2); nol_val* nv = NULL; if (strcmp(t->u.s, "int") == 0) nv = nol_new_int(atoll(s)); else if (strcmp(t->u.s, "float") == 0) nv = nol_new_float(atof(s)); else if (strcmp(t->u.s, "bool") == 0) nv = nol_new_bool(strcasecmp(s, "true") == 0); else nv = nol_new_string(s); nol_free(v); return nv; } } for (size_t i=0; i<v->u.o.n; i++) if (v->u.o.e[i].k) v->u.o.e[i].v = _rc(v->u.o.e[i].v, e); } else if (v->type == NOL_ARRAY) for (size_t i=0; i<v->u.a.n; i++) v->u.a.e[i] = _rc(v->u.a.e[i], e); return v; }
nol_val* nole_parse(const char* input, const char** app_env, size_t env_n, nol_err_t* err) { nol_val* root = nole_parse_ex(input, true, err); _eval_t e = { NULL, 0, app_env, env_n, NULL, 0, root }; root = _cm(root, &e); root = _rm(root, &e, 0); root = _re(root, &e); root = _ri(root, &e, 0); root = _rc(root, &e); for (size_t i=0; i<e.doc_env_n; i++) free(e.doc_env[i]); free(e.doc_env); for (size_t i=0; i<e.n; i++) { free(e.a[i].k); nol_free(e.a[i].v); } free(e.a); return root; }
#endif
#ifdef __cplusplus
}
#endif
#endif
