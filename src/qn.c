/*
** Copyright (c) 2021 Daniel M. Matongo
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
*/

#include <string.h>
#include "qn.h"

#define unused(x)       ( (void)(x) )
#define car(x)          ( (x)->car.o )
#define cdr(x)          ( (x)->cdr.o )
#define tag(x)          ( (x)->car.c )
#define isnil(x)        ( (x) == &nil )
#define type(x)         ( tag(x) & 0x2 ? tag(x) >> 2 : QN_TPAIR )
#define settype(x, t)   ( tag(x) = (t) << 2 | 1 )
#define number(x)       ( (x)->cdr.n )
#define prim(x)         ( (x)->cdr.c )
#define function(x)     ( (x)->cdr.f )
#define strbuf(x)       ( &(x)->car.c + 1)

#define STRBUF_SIZE     ( (int) sizeof(qn_Object*) - 1 )
#define GCMARKBIT       (0x2)
#define GCSTACKSIZE     (256)

enum
{
    P_LET,
    P_SET,
    P_IF,
    P_FN,
    P_MACRO,
    P_WHILE,
    P_QUOTE,
    P_DEF,
    P_AND,
    P_OR,
    P_DO,
    P_CONS,
    P_CAR,
    P_CDR,
    P_SETCAR,
    P_SETCDR,
    P_LIST,
    P_NOT,
    P_IS,
    P_ATOM,
    P_PRINT,
    P_EVAL,
    P_LT,
    P_LE,
    P_ADD,
    P_SUB,
    P_MUL,
    P_DIV,

    P_MAX
};

static const char *prim_names[] =
    {
        "let",
        "set",
        "if",
        "fn",
        "macro",
        "while",
        "quote",
        "and",
        "or",
        "do",
        "cons",
        "car",
        "cdr",
        "setcar",
        "setcdr",
        "list",
        "not",
        "is",
        "atom",
        "print",
        "eval",
        "<",
        "<<",
        "+",
        "-",
        "*",
        "/"
    };

static const char *type_names[] =
    {
        "nil",
        "pair",
        "number",
        "prim",
        "function",
        "string",
        "symbol",
        "error",
        "free",
        "func",
        "macro",
        "ptr"
    };

typedef union
{
    qn_Object *o;
    qn_Function f;
    qn_Number n;
    char c;
} Value;

struct qn_Object
{
    Value car, cdr;
};

struct qn_Context
{
    qn_IO handlers;
    qn_Object *gcstack[GCSTACKSIZE];
    int gcstack_idx;
    qn_Object *objects;
    int objects_count;
    qn_Object *freelist;
    qn_Object *symlist;
    qn_Object *calllist;
    qn_Object *t;
    int nextchar;
};

static qn_Object nil = {{ (void*) (QN_TNIL << 2 | 1) }, { NULL }};

qn_IO *qn_io(qn_Context *ctx)
{
    return &ctx->handlers;
}

void qn_error(qn_Context *ctx, const char *msg)
{
    qn_Object *cl = ctx->calllist;
    ctx->calllist = &nil;
    if (ctx->handlers.error)
    {
        ctx->handlers.error(ctx, msg, cl);
    }
    fprintf(stderr, "error: %s\n", msg);
    for (; cl != &nil; cl = cdr(cl))
    {
        char buf[256];
        qn_tostring(ctx, car(cl), buf, sizeof(buf));
        fprintf(stderr, "=>  %s\n", buf);
    }
    exit(EXIT_FAILURE);
}

qn_Object *qn_nextarg(qn_Context *ctx, qn_Object **args)
{
    qn_Object *a = *args;
    if (type(a) != QN_TPAIR)
    {
        if (isnil(a))
        {
            qn_error(ctx, "not enough arguments");
        }
        qn_error(ctx, "dotted pair in arguments");
    }
    *args = cdr(a);
    return car(a);
}

static qn_Object *checktype(qn_Context *ctx, qn_Object *o, int t)
{
    char buf[256];
    if (type(o) != t)
    {
        sprintf(buf, "expected %s, got %s", type_names[t], type_names[type(o)]);
        qn_error(ctx, buf);
    }
    return o;
}

int qn_type(qn_Context *ctx, qn_Object *o)
{
    unused(ctx);
    return type(o);
}

int qn_isnil(qn_Context *ctx, qn_Object *o)
{
    unused(ctx);
    return isnil(o);
}

void qn_pushgc(qn_Context *ctx, qn_Object *o)
{
    if (ctx->gcstack_idx >= GCSTACKSIZE)
    {
        qn_error(ctx, "stack overflow");
    }
    ctx->gcstack[ctx->gcstack_idx++] = o;
}

void qn_resetgc(qn_Context *ctx)
{
    ctx->gcstack_idx = 0;
}

int qn_savegc(qn_Context *ctx)
{
    return ctx->gcstack_idx;
}

void qn_mark(qn_Context *ctx, qn_Object *o)
{
    qn_Object *car;
begin:
    if (type(o) & GCMARKBIT)
    {
        return;
    }
    car = cdr(o);
    tag(o) |= GCMARKBIT;

    switch (type(o))
    {
    case QN_TPAIR:
        qn_mark(ctx, car);
    case QN_TFUNC:
    case QN_TMACRO:
    case QN_TSYMBOL:
    case QN_TSTRING:
        o = cdr(o);
        goto begin;
    case QN_TPTR:
        if (ctx->handlers.mark)
        {
            ctx->handlers.mark(ctx, o);
        }
        break;
    }
}

static void garbagecollector(qn_Context *ctx)
{
    qn_Object *o;
    int i;
    for (i = 0; i < ctx->gcstack_idx; i++)
    {
        qn_mark(ctx, ctx->gcstack[i]);
    }
    qn_mark(ctx, ctx->symlist);
    for (i = 0; i < ctx->objects_count; i++)
    {
        qn_Object *o = &ctx->objects[i];
        if (type(o) == QN_TFREE)
        {
            continue;
        }
        if (~tag(car(o)) & GCMARKBIT)
        {
            if (type(o) == QN_TPTR && ctx->handlers.gc)
            {
                ctx->handlers.gc(ctx, o);
            }
            settype(o, QN_TFREE);
            cdr(o) = ctx->freelist;
            ctx->freelist = o;
        }
        else
        {
            tag(o) &= ~GCMARKBIT;
        }
    }
}

static int equal(qn_Object *a, qn_Object *b)
{
    if (a == b) { return 1; }
    if (type(a) != type(b)) { return 0; }
    if (type(a) == QN_TNUMBER) { return number(a) == number(b); }
    if (type(a) == QN_TSTRING) {
        for (; !isnil(a); a = cdr(a), b = cdr(b))
        {
            if (car(a) != car(b)) { return 0; }
        }
        return a == b;
     }
    return 0;
}

static int streq(qn_Object *o, const char *s) {
  while (!isnil(o)) {
    int i;
    for (i = 0; i < STRBUF_SIZE; i++) {
      if (strbuf(o)[i] != *s) { return 0; }
      if (*s) { s++; }
    }
    o = cdr(o);
  }
  return *s == '\0';
}

static qn_Object* object(qn_Context *ctx) {
  qn_Object *o;
  if (isnil(ctx->freelist)) {
    garbagecollector(ctx);
    if (isnil(ctx->freelist)) { qn_error(ctx, "OOM error"); }
  }
  o = ctx->freelist;
  ctx->freelist = cdr(o);
  qn_pushgc(ctx, o);
  return o;
}

qn_Object* qn_cons(qn_Context *ctx, qn_Object *car, qn_Object *cdr) {
  qn_Object *o = object(ctx);
  car(o) = car;
  cdr(o) = cdr;
  return o;
}

qn_Object* qn_bool(qn_Context *ctx, int b) {
  return b ? ctx->t : &nil;
}

qn_Object* qn_number(qn_Context *ctx, float value) {
  qn_Object *o = object(ctx);
  settype(o, QN_TNUMBER);
  number(o) = value;
  return o;
}



static qn_Object* buildstring(qn_Context *ctx, qn_Object *tail, int chr) {
    if(!tail || strbuf(tail)[STRBUF_SIZE - 1] == '\0') {
        qn_Object *o = qn_cons(ctx, NULL, &nil);
        settype(o, QN_TSTRING);
        if(tail) {
            cdr(tail) = o;
            ctx->gcstack_idx--;
        } 
        tail = o;
    }
    strbuf(tail)[strlen(strbuf(tail))] = chr;
    return tail;
}

qn_Object* qn_string(qn_Context *ctx, const char *s) {
    qn_Object *o = buildstring(ctx, NULL, '\0');
    qn_Object *tail = o;
    while(*s) {
        tail = buildstring(ctx, tail, *s);
        s++;
    }
    return o;
}

qn_Object* qn_symbol(qn_Context *ctx, const char *s) {
    qn_Object *o;
    for (o = ctx->symlist; !isnil(o); o = cdr(o))
    {
       if (streq(car(cdr(o)), s)) { return car(o); }
    }
    o = object(ctx);
    settype(o, QN_TSYMBOL);
    cdr(o) = qn_cons(ctx, qn_string(ctx, s), &nil);
    ctx->symlist = qn_cons(ctx, o, ctx->symlist);
    return o;
}

qn_Object* qn_function(qn_Context *ctx, qn_Function fn) {
    qn_Object *o = object(ctx);
    settype(o, QN_TFUNC);
    function(o) = fn;
    return o;
}

qn_Object* qn_ptr(qn_Context *ctx, void *ptr) {
  qn_Object *obj = object(ctx);
  settype(obj, QN_TPTR);
  cdr(obj) = ptr;
  return obj;
}

qn_Object* qn_list(qn_Context *ctx, qn_Object **objs, int n) {
    qn_Object *res = &nil;
    while (n--) {
        res = qn_cons(ctx, objs[n], res);
    }
    return res;
}

qn_Object* qn_car(qn_Context *ctx, qn_Object *obj) {
  if (isnil(obj)) { return obj; }
  return car(checktype(ctx, obj, QN_TPAIR));
}

qn_Object* qn_cdr(qn_Context *ctx, qn_Object *obj) {
  if (isnil(obj)) { return obj; }
  return cdr(checktype(ctx, obj, QN_TPAIR));
}


static void writestr(qn_Context *ctx, qn_WriteFn fn, void *udata, const char *s) {
  while (*s) { fn(ctx, udata, *s++); }
}
