//
// Based on https://craftinginterpreters.com book with many changes
//

// ********************** kumu **********************
// basic (hawaiian): small, fast, portable, familiar
// See https://github.com/velentir/kumu/tree/main


#ifndef KUMU_H
#define KUMU_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// ********************** macros **********************
#define KVM_MAJOR          0
#define KVM_MINOR          88

#define KU_UNUSED __attribute__((unused))

//#define TRACE_ENABLED 1
//#define STACK_CHECK 1
//#define TRACE_OBJ_COUNTS 1

#define UPSTACK_MAX (UINT8_MAX + 1)
#define LOCALS_MAX (UINT8_MAX + 1)
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
  struct kuobj *__nullable next;
} kuobj;

void ku_objfree(kuvm *__nonnull vm, kuobj *__nonnull obj);

// ********************** string **********************
typedef struct {
  kuobj obj;
  int len;
  char *__nonnull chars;
  uint32_t hash;
} kustr;
kustr *__nonnull ku_strfrom(kuvm *__nonnull vm, const char *__nonnull chars, int len);

// ********************** value types **********************
typedef enum {
  VAL_BOOL,
  VAL_NULL,
  VAL_NUM,
  VAL_OBJ,
} kuval_t;

#define QNAN ((uint64_t)0x7ffc000000000000)
#define SIGN_BIT ((uint64_t)0x8000000000000000)

#define TAG_NULL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
#define FALSE_VAL ((kuval)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((kuval)(uint64_t)(QNAN | TAG_TRUE))

typedef uint64_t kuval;

bool ku_objis(kuval v, kuobj_t ot);

#define NUM_VAL(v) ku_num2val(v)
static inline kuval ku_num2val(double d) {
  kuval val;
  memcpy(&val, &d, sizeof(double));
  return val;
}

#define NULL_VAL ((kuval)(uint64_t) (QNAN | TAG_NULL))
#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)
#define OBJ_VAL(o) (kuval)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(o))

#define IS_NUM(v) (((v) & QNAN) != QNAN)
#define IS_NULL(v) ((v) == NULL_VAL)
#define IS_BOOL(v) (((v) | 1) == TRUE_VAL)
#define IS_OBJ(v) (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

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

static inline double ku_val2num(kuval v) {
  double d;
  memcpy(&d, &v, sizeof(kuval));
  return d;
}

#define AS_NUM(v) ku_val2num(v)
#define AS_BOOL(v) ((v) == TRUE_VAL)
#define AS_OBJ(v) ((kuobj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))

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

void ku_printval(kuvm *__nonnull vm, kuval value);
void ku_printf(kuvm *__nonnull vm, const char *__nonnull fmt, ...);

// ********************** arrays **********************
typedef struct {
    int capacity;
    int count;
    kuval *__nullable values;
} kuarr;

void ku_arrinit(kuvm *__nonnull vm, kuarr *__nonnull array);
void ku_arrwrite(kuvm *__nonnull vm, kuarr *__nonnull array, kuval value);

// ********************** memory **********************
char *__nonnull ku_alloc(kuvm *__nonnull vm, void *__nullable p, size_t oldsize, size_t newsize);
kuobj *__nonnull ku_objalloc(kuvm *__nonnull vm, size_t size, kuobj_t type);

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
  OP_NULL,
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
  uint8_t *__nullable code;
  int *__nullable lines;
  kuarr constants;
} kuchunk;

void ku_chunkinit(kuvm *__nonnull vm, kuchunk *__nonnull chunk);
void ku_chunkwrite(kuvm *__nonnull vm, kuchunk *__nonnull chunk, uint8_t byte, int line);
void ku_chunkfree(kuvm *__nonnull vm, kuchunk *__nonnull chunk);
int ku_chunkconst(kuvm *__nonnull vm, kuchunk *__nonnull chunk, kuval value);

// ********************** upvalues **********************
typedef struct kuxobj {
  kuobj obj;
  kuval *__nonnull location;
  kuval closed;
  struct kuxobj *__nullable next;
} kuxobj;

kuxobj *__nonnull ku_xobjnew(kuvm *__nonnull vm, kuval *__nonnull slot);

// ********************** functions **********************
typedef struct {
  kuobj obj;
  int arity;
  int upcount;
  kuchunk chunk;
  kustr *__nullable name;
} kufunc;

kufunc *__nonnull ku_funcnew(kuvm *__nonnull vm);

// ********************** closures **********************
typedef struct {
  kuobj obj;
  kufunc *__nonnull func;
  kuxobj *__nullable *__nullable upvals;
  int upcount;
} kuclosure;

kuclosure *__nonnull ku_closurenew(kuvm *__nonnull vm, kufunc *__nonnull f);

// ********************** native functions **********************
typedef kuval (*cfunc)(kuvm *__nonnull vm, int argc, kuval *__nullable argv);

typedef struct {
  kuobj obj;
  cfunc __nonnull fn;
} kucfunc;

kucfunc *__nonnull ku_cfuncnew(kuvm *__nonnull vm, cfunc __nonnull f);
void ku_cfuncdef(kuvm *__nonnull vm, const char *__nonnull name, cfunc __nonnull f);
void ku_reglibs(kuvm *__nonnull vm);

// ********************** native class **********************
typedef struct {
  kuobj obj;
  kustr *__nonnull name;
  kuval (*__nullable cons)(kuvm *__nonnull vm, int argc, kuval *__nullable argv);
  kuval (*__nullable scall)(kuvm *__nonnull vm, kustr *__nonnull m, int argc, kuval *__nullable argv);
  kuval (*__nullable sget)(kuvm *__nonnull vm, kustr *__nonnull p);
  kuval (*__nullable sput)(kuvm *__nonnull vm, kustr *__nonnull p, kuval v);
  kuval (*__nullable sfree)(kuvm *__nonnull vm, kuobj *__nonnull cc);
  kuval (*__nullable smark)(kuvm *__nonnull vm, kuobj *__nonnull cc);
  kuval (*__nullable icall)(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull m, int argc, kuval *__nullable argv);
  kuval (*__nullable iget)(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull p);
  kuval (*__nullable iput)(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull p, kuval v);
  kuval (*__nullable ifree)(kuvm *__nonnull vm, kuobj *__nonnull o);
  kuval (*__nullable imark)(kuvm *__nonnull vm, kuobj *__nonnull cc);
} kucclass;

kucclass *__nonnull ku_cclassnew(kuvm *__nonnull vm, const char *__nonnull name);
void ku_cclassdef(kuvm *__nonnull vm, kucclass *__nonnull cc);

// ********************** hash tables **********************
typedef struct {
  kustr *__nullable key;
  kuval value;
} kuentry;

typedef struct {
  int count;
  int capacity;
  kuentry *__nullable entries;
} kutab;

void ku_tabinit(kuvm *__nonnull vm, kutab *__nonnull t);
void ku_tabfree(kuvm *__nonnull vm, kutab *__nonnull t);
bool ku_tabset(kuvm *__nonnull vm, kutab *__nonnull t, kustr *__nonnull key, kuval value);
bool ku_tabget(kuvm *__nonnull vm, kutab *__nonnull t, kustr *__nonnull key, kuval *__nonnull value);
bool ku_tabdel(kuvm *__nonnull vm, kutab *__nonnull t, kustr *__nonnull key);
void ku_tabcopy(kuvm *__nonnull vm, kutab *__nonnull t, kutab *__nonnull to);
kustr *__nullable ku_tabfindc(
    kuvm *__nonnull vm, kutab *__nonnull t, const char *__nonnull chars, int len, uint32_t hash);

// ********************** classes **********************
typedef struct {
  kuobj obj;
  kustr *__nonnull name;
  kutab methods;
} kuclass;

kuclass *__nonnull ku_classnew(kuvm *__nonnull vm, kustr *__nonnull name);

// ********************** instances **********************
typedef struct {
  kuobj obj;
  kuclass *__nonnull klass;
  kutab fields;
} kuiobj;

kuiobj *__nonnull ku_instnew(kuvm *__nonnull vm, kuclass *__nonnull klass);

// ********************** native instance **********************
typedef struct {
  kuobj obj;
  kucclass *__nonnull klass;
} kunobj;

// ********************** array object **********************
typedef struct {
  kuobj obj;
  kuarr elements;
} kuaobj;

kuaobj *__nonnull ku_arrnew(kuvm *__nonnull vm, int capacity);
void ku_arrset(kuvm *__nonnull vm, kuaobj *__nullable array, int index, kuval value);
kuval ku_arrget(kuvm *__nonnull vm, kuaobj *__nullable array, int index);

// ********************** table objects **********************
typedef struct {
  kunobj base;
  kutab data;
} kutobj;
kuval table_cons(kuvm *__nonnull vm, int argc, kuval *__nullable argv);
kuval table_iget(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull p);
kuval table_iput(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull p, kuval v);
kuval ku_cinstance(kuvm *__nonnull vm, const char *__nonnull cname);

// ********************** bound methods **********************
typedef struct {
  kuobj obj;
  kuval receiver;
  kuclosure *__nonnull method;
} kubound;

kubound *__nonnull ku_boundnew(kuvm *__nonnull vm, kuval receiver, kuclosure *__nonnull method);

// ********************** scanner **********************
typedef enum {
  // Single-character tokens.
  TOK_LPAR, TOK_RPAR, TOK_LBRACE, TOK_RBRACE, TOK_COMMA,
  TOK_DOT, TOK_MINUS, TOK_PLUS, TOK_SEMI, TOK_SLASH, TOK_STAR,
  TOK_LBRACKET, TOK_RBRACKET, TOK_AMP, TOK_PIPE,
  // One or two character tokens.
  TOK_BANG, TOK_NE, TOK_EQ, TOK_EQUALITY, TOK_GT, TOK_GE, TOK_LT,
  TOK_LE, TOK_ARROW, TOK_LTLT, TOK_GTGT,
  // Literals.
  TOK_IDENT, TOK_STR, TOK_STRESC, TOK_NUM, TOK_HEX,
  // Keywords.
  TOK_AND, TOK_CLASS, TOK_ELSE, TOK_FALSE, TOK_FOR, TOK_FUN,
  TOK_IF, TOK_NULL, TOK_OR, TOK_RETURN, TOK_SUPER,
  TOK_THIS, TOK_TRUE, TOK_LET, TOK_WHILE, TOK_ERR, TOK_EOF,
  TOK_BREAK, TOK_CONTINUE, TOK_CONST,
} kutok_t;

typedef struct {
  kutok_t type;
  const char *__nonnull start;
  int len;
  int line;
} kutok;

typedef struct {
  const char *__nonnull start;
  const char *__nonnull curr;
  int line;
} kulex;

void ku_lexinit(kuvm *__nonnull vm, const char *__nonnull source);
void ku_lexdump(kuvm *__nonnull vm);
kutok ku_scan(kuvm *__nonnull vm);

// ********************** locals **********************
#define KU_LOCAL_NONE     0x00000000
#define KU_LOCAL_CAPTURED 0x00000001
#define KU_LOCAL_CONST    0x00000002
#define KU_LOCAL_INIT     0x00000004
#define KU_IS_CAPTURED(l) ((((l).flags) & KU_LOCAL_CAPTURED) != 0)

typedef struct {
  kutok name;
  int depth;
  uint32_t flags;
} kulocal;

typedef struct {
  uint8_t index;
  bool local;
} kuxval;

// ********************** compiler **********************
typedef struct kucomp {
  struct kucomp *__nullable enclosing;
  kufunc *__nullable function;
  kufunc_t type;

  kulocal locals[LOCALS_MAX];
  int count;
  kuxval upvals[UPSTACK_MAX];
  int depth;
} kucomp;

void ku_compinit(kuvm *__nonnull vm, kucomp *__nonnull compiler, kufunc_t type);
void ku_beginscope(kuvm *__nonnull vm);
void ku_endscope(kuvm *__nonnull vm);
void ku_declare_let(kuvm *__nonnull vm, bool isconst);
void ku_addlocal(kuvm *__nonnull vm, kutok name, bool isconst);
bool ku_identeq(kuvm *__nonnull vm, kutok *__nonnull a, kutok *__nonnull b);
int ku_resolvelocal(kuvm *__nonnull vm, kucomp *__nonnull compiler, kutok *__nonnull name);
void ku_markinit(kuvm *__nonnull vm);
int ku_opslotdis(kuvm *__nonnull vm, const char *__nonnull name, kuchunk *__nonnull chunk, int offset);
void ku_markobj(kuvm *__nonnull vm, kuobj *__nullable o);

// ********************** parser **********************
typedef struct {
  kutok curr;
  kutok prev;
  bool err;
  bool panic;
} kuparser;

// ********************** frames **********************
typedef struct {
  kuclosure *__nonnull closure;
  uint8_t *__nonnull ip;
  kuval *__nonnull bp;
} kuframe;

// ********************** class compiler **********************
typedef struct kuclasscomp {
  struct kuclasscomp *__nonnull enclosing;
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

typedef kures(*debugcallback)(kuvm *__nonnull vm);

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

  debugcallback __nullable debugger;

  bool err;
  size_t allocated;
#ifdef TRACE_OBJ_COUNTS
  size_t alloc_counts[OBJ_BOUND_METHOD+1];
#endif // TRACE_OBJ_COUNTS
  size_t gcnext;
  kuclasscomp *__nullable curclass;

  kuframe frames[FRAMES_MAX];
  int framecount;
  int baseframe;      // used for native calls
  kuval *__nonnull sp;

#ifdef STACK_CHECK
  int underflow;
#endif // STACK_CHECK

  kustr *__nullable initstr;
  kustr *__nullable countstr;

  kuobj *__nullable objects;
  kutab strings;
  kutab globals;
  kutab gconst;      // value is initialized state

  kuxobj *__nullable openupvals;

  kucomp *__nullable compiler;
  kulex scanner;
  kuparser parser;

  int gccount;      // gray object count
  int gccap;        // gray object capacity
  kuobj *__nonnull *__nullable gcstack;  // gray object stack
  kuval stack[];
} kuvm;

#define ku_new()  ku_newvm(STACK_MAX)
kuvm *__nonnull ku_newvm(int stack_size);
void ku_free(kuvm *__nonnull vm);
kures ku_run(kuvm *__nonnull vm);
kures ku_runfile(kuvm *__nonnull vm, const char *__nonnull file);
kures ku_exec(kuvm *__nonnull vm, char *__nonnull source);
kuchunk *__nonnull ku_chunk(kuvm *__nonnull vm);
kufunc *__nullable ku_compile(kuvm *__nonnull vm, char *__nonnull source);

// ********************** stack **********************
void ku_reset(kuvm *__nonnull vm);
void ku_push(kuvm *__nonnull vm, kuval val);
kuval ku_pop(kuvm *__nonnull vm);

// ********************** garbage collection **********************
void ku_gc(kuvm *__nonnull vm);
void ku_printmem(kuvm *__nonnull vm);
void ku_printstack(kuvm *__nonnull vm);

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

void ku_loopinit(kuvm *__nonnull vm, kuloop *__nonnull loop);
void ku_emitpatch(kuvm *__nonnull vm, kupatch *__nonnull patch, uint8_t op);
void ku_patchall(kuvm *__nonnull vm, kupatch *__nonnull patch, uint16_t to, bool rev);

// ********************** branching **********************
void ku_ifstmt(kuvm *__nonnull vm, kuloop *__nonnull loop);
int ku_emitjump(kuvm *__nonnull vm, k_op op);
void ku_patchjump(kuvm *__nonnull vm, int offset);
void ku_emitloop(kuvm *__nonnull vm, int start);
void ku_whilestmt(kuvm *__nonnull vm, kuloop *__nonnull loop);
void ku_forstmt(kuvm *__nonnull vm, kuloop *__nonnull loop);
int ku_jumpdis(kuvm *__nonnull vm, const char *__nonnull name, int sign, kuchunk *__nonnull chunk, int offset);
void ku_and(kuvm *__nonnull vm, bool lhs);
void ku_or(kuvm *__nonnull vm, bool lhs);
void ku_block(kuvm *__nonnull vm, kuloop *__nullable loop);
void ku_err(kuvm *__nonnull vm, const char *__nonnull fmt, ...);
bool ku_invoke(kuvm *__nonnull vm, kustr *__nonnull name, int argc, bool *__nonnull native);
kures ku_nativecall(kuvm *__nonnull vm, kuclosure *__nonnull cl, int argc);
#ifdef __cplusplus
}
#endif // __cplusplus

#endif // KUMU_H
