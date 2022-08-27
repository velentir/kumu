//
// Based on https://craftinginterpreters.com/ and the associated book with many changes
//

// ********************** kumu **********************
// basic (hawaiian): small, fast, portable, familiar
// See https://github.com/velentir/kumu/tree/main


#ifndef KUMU_H
#define KUMU_H

#ifdef __cplusplus
extern "C" {
#endif

// ********************** macros **********************
#define KVM_MAJOR          0
#define KVM_MINOR          88

#define NAN_BOX
//#define TRACE_ENABLED 1
//#define STACK_CHECK 1
//#define TRACE_OBJ_COUNTS 1

#define UPSTACK_MAX (UINT8_MAX + 1)
#define LOCALS_MAX    (UINT8_MAX + 1)
#define FRAMES_MAX 64

#define STACK_MAX (FRAMES_MAX * UPSTACK_MAX)

// ********************** includes **********************
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define __STDC_WANT_LIB_EXT1__ 1
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

// ********************** forwards **********************
typedef struct kuvm kuvm;

// ********************** object types **********************
typedef enum {
  OBJ_FUNC,
  OBJ_CFUNC,
  OBJ_CCLASS,
  OBJ_CLOSURE,
  OBJ_STR,
  OBJ_UPVAL,
  OBJ_CLASS,
  OBJ_INSTANCE,
  OBJ_CINST,
  OBJ_ARRAY,
  OBJ_BOUND_METHOD,
} kuobj_t;

// ********************** function types **********************
typedef enum {
  FUNC_STD,
  FUNC_MAIN,
  FUNC_METHOD,
  FUNC_INIT,
} kufunc_t;

// ********************** object **********************
typedef struct kuobj {
  kuobj_t type;
  bool marked;
  struct kuobj *next;
} kuobj;

void ku_objfree(kuvm* vm, kuobj* obj);

// ********************** string **********************
typedef struct {
  kuobj obj;
  int len;
  char* chars;
  uint32_t hash;
} kustr;
kustr* ku_strfrom(kuvm* vm, const char* chars, int len);

// ********************** value types **********************
typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUM,
  VAL_OBJ,
} kuval_t;

#ifdef NAN_BOX

#define QNAN ((uint64_t)0x7ffc000000000000)
#define SIGN_BIT ((uint64_t)0x8000000000000000)

#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
#define FALSE_VAL ((kuval)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((kuval)(uint64_t)(QNAN | TAG_TRUE))

typedef uint64_t kuval;

#else

typedef struct {
  kuval_t type;
  union {
    bool bval;
    double dval;
    kuobj* oval;
  } as;
} kuval;

#endif

bool ku_objis(kuval v, kuobj_t ot);

#ifdef NAN_BOX

#define NUM_VAL(v) ku_num2val(v)
static inline kuval ku_num2val(double d) {
  kuval val;
  memcpy(&val, &d, sizeof(double));
  return val;
}

#define NIL_VAL ((kuval)(uint64_t) (QNAN | TAG_NIL))
#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)
#define OBJ_VAL(o) (kuval)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(o))
#else

#define BOOL_VAL(v) ((kuval){ VAL_BOOL, { .bval = v }})
#define NIL_VAL ((kuval) { VAL_NIL, { .dval = 0 }})
#define NUM_VAL(v) ((kuval) { VAL_NUM, { .dval = v }})
#define OBJ_VAL(v) ((kuval) { VAL_OBJ, { .oval = (kuobj*)v} })

#endif

#ifdef NAN_BOX

#define IS_NUM(v) (((v) & QNAN) != QNAN)
#define IS_NIL(v) ((v) == NIL_VAL)
#define IS_BOOL(v) (((v) | 1) == TRUE_VAL)
#define IS_OBJ(v) (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#else

#define IS_BOOL(v) ((v).type == VAL_BOOL)
#define IS_NIL(v) ((v).type == VAL_NIL)
#define IS_NUM(v) ((v).type == VAL_NUM)
#define IS_OBJ(v) ((v).type == VAL_OBJ)

#endif

#define IS_STR(v) (ku_objis(v, OBJ_STR))
#define IS_FUNC(v) (ku_objis(v, OBJ_FUNC))
#define IS_CFUNC(v) (ku_objis(v, OBJ_CFUNC))
#define IS_CCLASS(v) (ku_objis(v, OBJ_CCLASS))
#define IS_CLOSURE(v) (ku_objis(v, OBJ_CLOSURE))
#define IS_CLASS(v) (ku_objis(v, OBJ_CLASS))
#define IS_INSTANCE(v) (ku_objis(v, OBJ_INSTANCE))
#define IS_CINST(v) (ku_objis(v, OBJ_CINST))
#define IS_ARRAY(v) (ku_objis(v, OBJ_ARRAY))
#define IS_BOUND_METHOD(v) (ku_objis(v, OBJ_BOUND_METHOD))

#ifdef NAN_BOX

#define AS_NUM(v) ku_val2num(v)
static inline double ku_val2num(kuval v) {
  double d;
  memcpy(&d, &v, sizeof(kuval));
  return d;
}

#define AS_BOOL(v) ((v) == TRUE_VAL)
#define AS_OBJ(v) ((kuobj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))
#else

#define AS_BOOL(v) ((v).as.bval)
#define AS_NUM(v) ((v).as.dval)
#define AS_OBJ(v) ((v).as.oval)

#endif

#define AS_STR(v) ((kustr*)AS_OBJ(v))
#define AS_CSTR(v) (((kustr*)AS_OBJ(v))->chars)
#define AS_FUNC(v) ((kufunc*)AS_OBJ(v))
#define AS_CFUNC(v) (((kucfunc*)AS_OBJ(v))->fn)
#define AS_CCLASS(v) ((kucclass*)AS_OBJ(v))
#define AS_CLOSURE(v) ((kuclosure*)AS_OBJ(v))
#define AS_CLASS(v) ((kuclass*)AS_OBJ(v))
#define AS_INSTANCE(v) ((kuiobj*)AS_OBJ(v))
#define AS_BOUND_METHOD(v) ((kubound*)AS_OBJ(v))
#define AS_CINST(v) ((kunobj*)AS_OBJ(v))
#define AS_ARRAY(v) ((kuaobj*)AS_OBJ(v))

#define OBJ_TYPE(v) (AS_OBJ(v)->type)

bool ku_equal(kuval v1, kuval v2);

void ku_printval(kuvm* vm, kuval value);
void ku_printf(kuvm* vm, const char* fmt, ...);

// ********************** arrays **********************
typedef struct {
    int capacity;
    int count;
    kuval* values;
} kuarr;

void ku_arrinit(kuvm* vm, kuarr* array);
void ku_arrwrite(kuvm* vm, kuarr* array, kuval value);

// ********************** memory **********************
char* ku_alloc(kuvm* vm, void* p, size_t oldsize, size_t newsize);
kuobj* ku_objalloc(kuvm* vm, size_t size, kuobj_t type);
// ********************** bytecodes **********************
typedef enum {
  OP_ADD,
  OP_ARRAY,
  OP_ASET,
  OP_AGET,
  OP_CALL,
  OP_CLASS,
  OP_CLOSE_UPVAL,
  OP_CLOSURE,
  OP_CONST,
  OP_DEF_GLOBAL,
  OP_DIV,
  OP_EQ,
  OP_FALSE,
  OP_GET_GLOBAL,
  OP_GET_LOCAL,
  OP_GET_PROP,
  OP_GET_SUPER,
  OP_GET_UPVAL,
  OP_GT,
  OP_INHERIT,
  OP_INVOKE,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_LT,
  OP_METHOD,
  OP_MUL,
  OP_NEG,
  OP_NIL,
  OP_NOT,
  OP_POP,
  OP_RET,
  OP_SET_GLOBAL,
  OP_SET_LOCAL,
  OP_SET_PROP,
  OP_SET_UPVAL,
  OP_SUB,
  OP_SUPER_INVOKE,
  OP_TRUE,
  OP_BAND,
  OP_BOR,
  OP_SHL,
  OP_SHR,
  OP_TABLE,
  OP_DUP,
} k_op;

// ********************** code chunks **********************
typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  int* lines;
  kuarr constants;
} kuchunk;

void ku_chunkinit(kuvm* vm, kuchunk* chunk);
void ku_chunkwrite(kuvm* vm, kuchunk* chunk, uint8_t byte, int line);
void ku_chunkfree(kuvm* vm, kuchunk* chunk);
int ku_chunkconst(kuvm* vm, kuchunk* chunk, kuval value);

// ********************** upvalues **********************
typedef struct kuxobj {
  kuobj obj;
  kuval *location;
  kuval closed;
  struct kuxobj *next;
} kuxobj;

kuxobj *ku_xobjnew(kuvm *vm, kuval *slot);

// ********************** functions **********************
typedef struct {
  kuobj obj;
  int arity;
  int upcount;
  kuchunk chunk;
  kustr *name;
} kufunc;

kufunc *ku_funcnew(kuvm *vm);

// ********************** closures **********************
typedef struct {
  kuobj obj;
  kufunc *func;
  kuxobj **upvals;
  int upcount;
} kuclosure;

kuclosure *ku_closurenew(kuvm *vm, kufunc *f);

// ********************** native functions **********************
typedef kuval (*cfunc)(kuvm *vm, int argc, kuval *argv);

typedef struct {
  kuobj obj;
  cfunc fn;
} kucfunc;

kucfunc *ku_cfuncnew(kuvm *vm, cfunc f);
void ku_cfuncdef(kuvm *vm, const char *name, cfunc f);
void ku_reglibs(kuvm *vm);

// ********************** native class **********************
typedef struct {
  kuobj obj;
  kustr *name;
  kuval (*cons)(kuvm *vm, int argc, kuval *argv);
  kuval (*scall)(kuvm *vm, kustr *m, int argc, kuval *argv);
  kuval (*sget)(kuvm *vm, kustr *p);
  kuval (*sput)(kuvm *vm, kustr *p, kuval v);
  kuval (*sfree)(kuvm *vm, kuobj *cc);
  kuval (*smark)(kuvm *vm, kuobj *cc);
  kuval (*icall)(kuvm *vm, kuobj *o, kustr *m, int argc, kuval *argv);
  kuval (*iget)(kuvm *vm, kuobj *o, kustr *p);
  kuval (*iput)(kuvm *vm, kuobj *o, kustr *p, kuval v);
  kuval (*ifree)(kuvm *vm, kuobj *o);
  kuval (*imark)(kuvm *vm, kuobj *cc);
} kucclass;

kucclass *ku_cclassnew(kuvm *vm, const char *name);
void ku_cclassdef(kuvm *vm, kucclass *cc);

// ********************** hash tables **********************
typedef struct {
  kustr* key;
  kuval value;
} kuentry;

typedef struct {
  int count;
  int capacity;
  kuentry* entries;
} kutab;

void ku_tabinit(kuvm *vm, kutab* t);
void ku_tabfree(kuvm *vm, kutab* t);
bool ku_tabset(kuvm* vm, kutab* t, kustr *key, kuval value);
bool ku_tabget(kuvm* vm, kutab* t, kustr* key, kuval *value);
bool ku_tabdel(kuvm* vm, kutab* t, kustr* key);
void ku_tabcopy(kuvm* vm, kutab* t, kutab* to);
kustr* ku_tabfindc(kuvm* vm, kutab* t, const char* chars, int len, uint32_t hash);

// ********************** classes **********************
typedef struct {
  kuobj obj;
  kustr *name;
  kutab methods;
} kuclass;

kuclass *ku_classnew(kuvm *vm, kustr *name);

// ********************** instances **********************
typedef struct {
  kuobj obj;
  kuclass *klass;
  kutab fields;
} kuiobj;

kuiobj *ku_instnew(kuvm *vm, kuclass *klass);

// ********************** native instance **********************
typedef struct {
  kuobj obj;
  kucclass *klass;
} kunobj;

// ********************** array object **********************
typedef struct {
  kuobj obj;
  kuarr elements;
} kuaobj;

kuaobj *ku_arrnew(kuvm* vm, int capacity);
void ku_arrset(kuvm* vm, kuaobj* array, int index, kuval value);
kuval ku_arrget(kuvm* vm, kuaobj* array, int index);

// ********************** table objects **********************
typedef struct {
  kunobj base;
  kutab data;
} kutobj;
kuval table_cons(kuvm *vm, int argc, kuval *argv);
kuval table_iget(kuvm *vm, kuobj *o, kustr *p);
kuval table_iput(kuvm *vm, kuobj *o, kustr *p, kuval v);
kuval ku_cinstance(kuvm *vm, const char *cname);

// ********************** bound methods **********************
typedef struct {
  kuobj obj;
  kuval receiver;
  kuclosure *method;
} kubound;

kubound *ku_boundnew(kuvm *vm, kuval receiver, kuclosure *method);

// ********************** scanner **********************
typedef enum {
  // Single-character tokens.
  TOK_LPAR, TOK_RPAR, TOK_LBRACE, TOK_RBRACE, TOK_COMMA,
  TOK_DOT, TOK_MINUS, TOK_PLUS, TOK_SEMI, TOK_SLASH, TOK_STAR,
  TOK_LBRACKET, TOK_RBRACKET, TOK_AMP, TOK_PIPE,
  // One or two character tokens.
  TOK_BANG, TOK_NE, TOK_EQ, TOK_EQEQ, TOK_GT, TOK_GE, TOK_LT,
  TOK_LE, TOK_ARROW, TOK_LTLT, TOK_GTGT,
  // Literals.
  TOK_IDENT, TOK_STR, TOK_STRESC, TOK_NUM, TOK_HEX,
  // Keywords.
  TOK_AND, TOK_CLASS, TOK_ELSE, TOK_FALSE, TOK_FOR, TOK_FUN,
  TOK_IF, TOK_NIL, TOK_OR, TOK_RETURN, TOK_SUPER,
  TOK_THIS, TOK_TRUE, TOK_LET, TOK_WHILE, TOK_ERR, TOK_EOF,
  TOK_BREAK, TOK_CONTINUE,
} kutok_t;

typedef struct {
  kutok_t type;
  const char* start;
  int len;
  int line;
} kutok;

typedef struct {
  const char* start;
  const char* curr;
  int line;
} kulex;

void ku_lexinit(kuvm *vm, const char *source);
void ku_lexdump(kuvm *vm);
kutok ku_scan(kuvm *vm);

// ********************** locals **********************

typedef struct {
  kutok name;
  int depth;
  bool captured;
} kulocal;

typedef struct {
  uint8_t index;
  bool local;
} kuxval;

// ********************** compiler **********************
typedef struct kucomp {
  struct kucomp *enclosing;
  kufunc *function;
  kufunc_t type;
  
  kulocal locals[LOCALS_MAX];
  int count;
  kuxval upvals[UPSTACK_MAX];
  int depth;
} kucomp;

void ku_compinit(kuvm *vm, kucomp *compiler, kufunc_t type);
void ku_beginscope(kuvm *vm);
void ku_endscope(kuvm *vm);
void ku_declare_let(kuvm *vm);
void ku_addlocal(kuvm *vm, kutok name);
bool ku_identeq(kuvm *vm, kutok *a, kutok *b);
int ku_resolvelocal(kuvm *vm, kucomp *compiler, kutok *name);
void ku_markinit(kuvm *vm);
int ku_opslotdis(kuvm *vm, const char *name, kuchunk *chunk, int offset);
void ku_markobj(kuvm *vm, kuobj *o);

// ********************** parser **********************
typedef struct {
  kutok curr;
  kutok prev;
  bool err;
  bool panic;
} kuparser;

// ********************** frames **********************
typedef struct {
  kuclosure *closure;
  uint8_t *ip;
  kuval *bp;
} kuframe;

// ********************** class compiler **********************
typedef struct kuclasscomp {
  struct kuclasscomp *enclosing;
  bool hassuper;
} kuclasscomp;

// ********************** virtual machine **********************
typedef enum {
  KVM_OK,
  KVM_CONT,
  KVM_ERR_SYNTAX,
  KVM_ERR_RUNTIME,
  KVM_FILE_NOTFOUND,
} kures;

#define KVM_F_TRACE     0x00000001   // trace each instruction as it runs
#define KVM_F_STACK     0x00000002   // print stack in repl
#define KVM_F_LIST      0x00000004   // list instructions after compile
#define KVM_F_QUIET     0x00000008   // Supress error output (for tests)
#define KVM_F_TRACEMEM  0x00000010   // Trace memory
#define KVM_F_DISASM    0x00000020   // Disassemble after compile
#define KVM_F_NOEXEC    0x00000040   // Disable execution only compile
#define KVM_F_GCSTRESS  0x00000080   // GC every alloc increase
#define KVM_F_GCLOG     0x00000100   // Log GC action


typedef struct kuvm {
  uint64_t flags;
  
  int max_stack;
  int max_params;
  int max_const;
  int max_closures;
  int max_jump;
  int max_body;
  int max_frames;
  int max_locals;
  int max_patches;
  
  bool err;
  size_t allocated;
#ifdef TRACE_OBJ_COUNTS
  size_t alloc_counts[OBJ_BOUND_METHOD+1];
#endif
  size_t gcnext;
  kuclasscomp *curclass;

  kuframe frames[FRAMES_MAX];
  int framecount;
  int baseframe;      // used for native calls
  kuval* sp;

#ifdef STACK_CHECK
  int underflow;
#endif
  
  kustr *initstr;
  kustr *countstr;
  
  kuobj* objects;
  kutab strings;
  kutab globals;

  kuxobj *openupvals;
  
  kucomp *compiler;
  kulex scanner;
  kuparser parser;
  
  int gccount;      // gray object count
  int gccap;        // gray object capacity
  kuobj **gcstack;  // gray object stack
  kuval stack[0];

} kuvm;

#define ku_new()  ku_newvm(STACK_MAX)
kuvm* ku_newvm(int stack_size);
void ku_free(kuvm* vm);
kures ku_run(kuvm* vm);
kures ku_runfile(kuvm* vm, const char* file);
kures ku_exec(kuvm *vm, char *source);
kuchunk *ku_chunk(kuvm *vm);
kufunc *ku_compile(kuvm *vm, char *source);

// ********************** stack **********************
void ku_reset(kuvm* vm);
void ku_push(kuvm* vm, kuval val);
kuval ku_pop(kuvm* vm);

// ********************** garbage collection **********************
void ku_gc(kuvm *vm);
void ku_printmem(kuvm *vm);
void ku_printstack(kuvm *vm);

// ********************** loop patch **********************
#define PATCH_MAX  32
typedef struct {
  int count;
  uint16_t offset[PATCH_MAX];
} kupatch;

typedef struct {
  kupatch breakpatch;
  kupatch continuepatch;
} kuloop;

void ku_loopinit(kuvm *vm, kuloop *loop);
void ku_emitpatch(kuvm *vm, kupatch *patch, uint8_t op);
void ku_patchall(kuvm *vm, kupatch *patch, uint16_t to, bool rev);

// ********************** branching **********************
void ku_ifstmt(kuvm *vm, kuloop *loop);
int ku_emitjump(kuvm *vm, k_op op);
void ku_patchjump(kuvm *vm, int offset);
void ku_emitloop(kuvm *vm, int start);
void ku_whilestmt(kuvm *vm, kuloop *loop);
void ku_forstmt(kuvm *vm, kuloop *loop);
int ku_jumpdis(kuvm *vm, const char *name,
                     int sign, kuchunk *chunk, int offset);
void ku_and(kuvm *vm, bool lhs);
void ku_or(kuvm *vm, bool lhs);
void ku_block(kuvm *vm, kuloop *loop);
void ku_err(kuvm *vm, const char *fmt, ...);
bool ku_invoke(kuvm *vm, kustr *name, int argc, bool *native);
kures ku_nativecall(kuvm *vm, kuclosure *cl, int argc);
#ifdef __cplusplus
}
#endif

#endif /* KUMU_H */

