/*
** Copyright (c) 2021 Daniel M. Matongo
**
** This library is free software; you can redistribute it and/or modify it
** under the terms of the MIT license. See `Qn.c` for details.
** This prooject is inspired by 'https://github.com/rxi/fe'
*/

#ifndef QN_H
#define QN_H

#include <stdlib.h>
#include <stdio.h>

#define QN_VERSION "0.1"

typedef float qn_Number;
typedef double qn_Double;
typedef struct qn_Object qn_Object;
typedef struct qn_Context qn_Context;
typedef qn_Object *(*qn_Function)(qn_Object *self, qn_Object *args);
typedef void (*qn_Destructor)(qn_Object *self);
typedef void (*qn_ErrorFn)(qn_Context *ctx, const char *msg, qn_Object *cl);
typedef void (*qn_WriteFn)(qn_Context *ctx, void *data, char chr);
typedef void (*qn_ReadFn)(qn_Context *ctx, void *data);

typedef struct
{
    qn_ErrorFn error;
    qn_WriteFn write;
    qn_ReadFn read;
    qn_Function mark, gc;
} qn_IO;

enum
{
    QN_TPAIR,
    QN_TFREE,
    QN_TSTRING,
    QN_TNUMBER,
    QN_TSYMBOL,
    QN_TOBJECT,
    QN_TNIL,
    QN_TFUNC,
    QN_TPRIM,
    QN_TFUNCTION,
    QN_TPTR,
    QN_TMACRO
};

qn_Context *qn_new(void *ptr, int size);
void qn_free(qn_Context *ctx);
qn_IO *qn_io(qn_Context *ctx);
void qn_error(qn_Context *ctx, const char *msg);
qn_Object *qn_nextarg(qn_Context *ctx, qn_Object **args);
int qn_type(qn_Context *ctx, qn_Object *obj);
int qn_nil(qn_Context *ctx, qn_Object *obj);
void qn_mark(qn_Context *ctx, qn_Object *obj);
void qn_pushgc(qn_Context *ctx, qn_Object *obj);
int qn_savegc(qn_Context *ctx);
void qn_restoregc(qn_Context *ctx, int idx);

qn_Object *qn_cons(qn_Context *ctx, qn_Object *car, qn_Object *cdr);
qn_Object *qn_bool(qn_Context *ctx, int value);
qn_Object *qn_string(qn_Context *ctx, const char *value);
qn_Object *qn_number(qn_Context *ctx, qn_Number value);
qn_Object *qn_symbol(qn_Context *ctx, const char *value);
qn_Object *qn_object(qn_Context *ctx, int type, void *value);
qn_Object *qn_function(qn_Context *ctx, qn_Function fn);
qn_Object *qn_ptr(qn_Context *ctx, void *ptr);
qn_Object *qn_list(qn_Context *ctx, qn_Object **objs, int count);
qn_Object *qn_car(qn_Context *ctx, qn_Object *obj);
qn_Object *qn_cdr(qn_Context *ctx, qn_Object *obj);

void qn_write(qn_Context *ctx, qn_Object *obj, qn_WriteFn fn, void *data, int depth);
void qn_witefp(qn_Context *ctx, qn_Object *obj, FILE *fp);
int qn_tostring(qn_Context *ctx, qn_Object *obj, char *buf, int size);
qn_Number qn_tonumber(qn_Context *ctx, qn_Object *obj);
void *qn_toptr(qn_Context *ctx, qn_Object *obj);
void qn_set(qn_Context *ctx, qn_Object *obj, qn_Object *value);
qn_Object *qn_read(qn_Context *ctx, qn_ReadFn fn, void *data);
qn_Object *qn_readfp(qn_Context *ctx, FILE *fp);
qn_Object *qn_eval(qn_Context *ctx, qn_Object *obj);

#endif
