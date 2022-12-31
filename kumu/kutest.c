//
//  kutest.c
//  tests_macOS
//
//  Created by Mohsen Agsen on 12/7/21.
//

#include "kumu.h"
#include "kutest.h"
#include <math.h>

static int ktest_pass = 0;
int ktest_fail = 0;
int ktest_warn = 0;
const char *__nonnull last_test = "";

#define print_failure() printf("[  FAILED  ]\n  (line %d)", __LINE__)

// EXPECT_TRUE(kuvm *__nonnull vm, bool b, const char *__nonnull m)
#define EXPECT_TRUE(vm, b, m) \
  do { \
    last_test = (m); \
    if (b) { \
      ktest_pass++; \
    } else { \
      ktest_fail++; \
      print_failure(); \
      printf("expected true found false [%s]\n", (m)); \
    } \
  } while (0)

// EXPECT_INT(kuvm *__nonnull vm, int v1, int v2, const char *__nonnull m)
#define EXPECT_INT(vm, v1, v2, m) \
  do { \
    last_test = (m); \
    if ((v1) == (v2)) { \
      ktest_pass++; \
    } else { \
        ktest_fail++; \
        print_failure(); \
        printf("expected: %d found: %d [%s]\n", (v2), (v1), (m)); \
    } \
  } while (0)

// EXPECT_STR(kuvm *__nonnull vm, kuval v, const char *__nonnull s, const char *__nonnull m)
#define EXPECT_STR(vm, v, s, m) \
  do { \
    last_test = (m); \
    if (IS_STR(v) && \
        (int)strlen(AS_STR(v)->chars) == (int)strlen(s) && \
        strcmp(AS_STR(v)->chars, (s)) == 0) { \
      ktest_pass++; \
    } else { \
      ktest_fail++; \
      print_failure(); \
      printf("expected: %s found: %s\n", (s), AS_STR(v)->chars); \
    } \
  } while (0)

// EXPECT_VAL(kuvm *__nonnull vm, kuval v1, kuval v2, const char *__nonnull m)
#define EXPECT_VAL(vm, v1, v2, m) \
  do { \
    last_test = m; \
    if (ku_equal((v1), (v2))) { \
      ktest_pass++; \
    } else { \
      uint64_t f = vm->flags; \
      ktest_fail++; \
      print_failure(); \
      printf("expected: "); \
      ku_printval((vm), (v2)); \
      printf(" found: "); \
      ku_printval((vm), (v1)); \
      printf(" [%s]\n", (m)); \
      vm->flags = f; \
    } \
  } while(0)

static void ku_test_summary() {
  printf("[  PASSED  ] %d test\n", ktest_pass);
  printf("[   WARN   ] %d test\n", ktest_warn);
  printf("[  FAILED  ] %d test\n", ktest_fail);
}

kuval ku_get_global(kuvm *__nonnull vm, const char *__nonnull name) {
  kuval value;
  kustr *__nonnull key = ku_strfrom(vm, name, (int)strlen(name));
  if (!ku_tabget(vm, &vm->globals, key, &value)) {
    return NULL_VAL;
  }

  return value;
}

kuval ku_test_eval(kuvm *__nonnull vm, const char *__nonnull expr) {
  char buff[255];
  sprintf(buff, "let x = %s;", expr);
  kures res = ku_exec(vm, buff);
  if (res != KVM_OK) {
    return NULL_VAL;
  }
  return ku_get_global(vm, "x");
}

kuvm *__nonnull kut_new(bool reglibs) {
  kuvm *__nonnull vm = ku_newvm(STACK_MAX, NULL);
  if (reglibs) {
    ku_reglibs(vm);
  }
  return vm;
}

static kuval kutest_native_add(KU_UNUSED kuvm *__nonnull vm, int argc, KU_UNUSED kuval *__nullable argv) {
  assert(argc == 2);
  kuval b = argv[0];
  kuval a = argv[1];
  return NUM_VAL(AS_NUM(a) + AS_NUM(b));
}

static int kut_table_count(KU_UNUSED kuvm *__nonnull vm, kutab *__nonnull tab) {
  int count = 0;
  for (int i = 0; i < tab->capacity; i++) {
    kuentry *__nonnull s = &tab->entries[i];
    if (s->key != NULL) {
      count++;
    }
  }
  return count;
}

int debug_call_count = 0;

kures test_debugger_stop(KU_UNUSED kuvm *__nonnull vm) {
  debug_call_count++;
  return KVM_OK;
}

kures test_debugger_cont(KU_UNUSED kuvm *__nonnull vm) {
  debug_call_count++;
  return KVM_CONT;
}

int tclass_cons = 0;
int tclass_scall = 0;
int tclass_sget = 0;
int tclass_sput = 0;
int tclass_sfree = 0;
int tclass_smark = 0;
int tclass_icall = 0;
int tclass_iget = 0;
int tclass_iput = 0;
int tclass_imark = 0;
int tclass_ifree = 0;

typedef struct {
  kunobj base;
  double value;
} test_inst;

kuval test_cons(kuvm *__nonnull vm, KU_UNUSED int argc, kuval *__nullable argv) {
  assert(argc == 1);
  tclass_cons++;
  test_inst *__nonnull i = (test_inst *)ku_objalloc(vm, sizeof(test_inst), OBJ_CINST);
  i->value = AS_NUM(argv[0]);
  return OBJ_VAL(i);
}

kuval test_scall(
    KU_UNUSED kuvm *__nonnull vm, KU_UNUSED kustr *__nonnull m, int argc, KU_UNUSED kuval *__nullable argv) {
  tclass_scall = argc;
  return NULL_VAL;
}

kuval test_sget(KU_UNUSED kuvm *__nonnull vm, KU_UNUSED kustr *__nonnull p) {
  tclass_sget++;
  return NULL_VAL;
}
kuval test_sput(KU_UNUSED kuvm *__nonnull vm, KU_UNUSED kustr *__nonnull p, kuval v) {
  tclass_sput = (int)AS_NUM(v);
  return NULL_VAL;
}

kuval test_sfree(KU_UNUSED kuvm *__nonnull vm, KU_UNUSED kuobj *__nonnull cc) {
  tclass_sfree++;
  return NULL_VAL;
}

kuval test_smark(KU_UNUSED kuvm *__nonnull vm, KU_UNUSED kuobj *__nonnull cc) {
  tclass_smark++;
  return NULL_VAL;
}

kuval test_icall(
    KU_UNUSED kuvm *__nonnull vm,
    KU_UNUSED kuobj *__nonnull o,
    KU_UNUSED kustr *__nonnull m,
    KU_UNUSED int argc,
    KU_UNUSED kuval *__nonnull argv) {
  tclass_icall++;
  return NULL_VAL;
}

kuval test_iget(KU_UNUSED kuvm *__nonnull vm, kuobj *__nonnull o, KU_UNUSED kustr *__nonnull p) {
  test_inst *__nonnull ti = (test_inst *)o;
  tclass_iget++;
  return NUM_VAL(ti->value);
}

kuval test_iput(KU_UNUSED kuvm *__nonnull vm, kuobj *__nonnull o, KU_UNUSED kustr *__nonnull p, KU_UNUSED kuval v) {
  tclass_iput++;
  test_inst *__nonnull ti = (test_inst *)o;
  ti->value = AS_NUM(v);
  return NULL_VAL;
}

kuval test_ifree(kuvm *__nonnull vm, kuobj *__nonnull o) {
  tclass_ifree++;
  ku_alloc(vm, o, sizeof(test_inst), 0);
  return NULL_VAL;
}

kuval test_imark(KU_UNUSED kuvm *__nonnull vm, KU_UNUSED kuobj *__nonnull cc) {
  tclass_imark++;
  return NULL_VAL;
}

void tclass_reset(KU_UNUSED kuvm *__nonnull vm) {
  tclass_cons = 0;
  tclass_scall = 0;
  tclass_sget = 0;
  tclass_sput = 0;
  tclass_sfree = 0;
  tclass_smark = 0;
  tclass_icall = 0;
  tclass_iget = 0;
  tclass_iput = 0;
  tclass_imark = 0;
  tclass_ifree = 0;
}

#define SCALL   0x00000001
#define SGET    0x00000002
#define SPUT    0x00000004
#define SMARK   0x00000008
#define SFREE   0x00000010
#define CONS    0x00000020
#define IGET    0x00000040
#define IPUT    0x00000080
#define IMARK   0x00000100
#define IFREE   0x00000200

#define ALL     0x00000fff

void tclass_init(kuvm *__nonnull vm, uint64_t flags) {
  tclass_reset(vm);
  kucclass *__nonnull cc = ku_cclassnew(vm, "test");

  if (flags & SCALL) cc->scall = test_scall;
  if (flags & SGET) cc->sget = test_sget;
  if (flags & SPUT) cc->sput = test_sput;
  if (flags & SMARK) cc->smark = test_smark;
  if (flags & SFREE) cc->sfree = test_sfree;
  if (flags & CONS) cc->cons = test_cons;
  if (flags & IGET) cc->iget = test_iget;
  if (flags & IPUT) cc->iput = test_iput;
  if (flags & IMARK) cc->imark = test_imark;
  if (flags & IFREE) cc->ifree = test_ifree;

  ku_cclassdef(vm, cc);
}

#define APPROX(a,b) ((AS_NUM(a)-(b)) < 0.00000001)


void kut_free(kuvm *__nonnull vm) {
  if (vm->sp > vm->stack) {
    printf(">>> [%s] warning stack at %d\n", last_test, (int)(vm->sp - vm->stack));
    ktest_warn++;
  }

#ifdef STACK_CHECK
  if (vm->underflow > 0) {
    printf(">>> [%s] warning stack underflow %d\n", last_test, vm->underflow);
    ktest_warn++;
  }
#endif // STACK_CHECK

  ku_freevm(vm);
}

int ku_test() {
  kuvm *__nonnull vm = kut_new(false);
  kures res = ku_exec(vm, "let x= -1+4;");
  EXPECT_INT(vm, res, KVM_OK, "let x= -1+4 res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = -1+4;");
  EXPECT_INT(vm, res, KVM_OK, "-1+4 res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(-1+4), "-1+4 ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = (-(1+2)-4)*5/6;");
  EXPECT_INT(vm, res, KVM_OK, "ku_run res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL((-(1.0+2.0)-4.0)*5.0/6.0), "ku_run ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 1+2;");
  EXPECT_INT(vm, res, KVM_OK, "ku_exec res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "ku_exec ret");
  kut_free(vm);

  vm = kut_new(false);
  ku_lexinit(vm, "let x = 12+3;");
  ku_lexdump(vm);
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "12+");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "12+");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = $2");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "12+");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "2 = 14");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "12+");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = (1+2)*3;");
  EXPECT_INT(vm, res, KVM_OK, "grouping res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(9), "grouping ret");
  kut_free(vm);

  vm = kut_new(false);
  ku_printmem(vm);
  kut_free(vm);

  vm = kut_new(false);
  ku_lexinit(vm, "let x=30; \n  x=\"hello\";");
  ku_lexdump(vm);
  kut_free(vm);

  vm = kut_new(false);
  ku_push(vm, NUM_VAL(2));
  ku_push(vm, NUM_VAL(3));
  ku_printstack(vm);
  ku_pop(vm);
  ku_pop(vm);
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = -2*3;");
  EXPECT_INT(vm, res, KVM_OK, "unary res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(-6), "unary ret");
  kut_free(vm);

  // unterminated string
  vm = kut_new(false);
  ku_lexinit(vm, "\"hello");
  ku_lexdump(vm);
  kut_free(vm);

  // ku_print_val
  vm = kut_new(false);
  res = ku_exec(vm, "let x = 2+3;");
  kuval v = ku_get_global(vm, "x");
  EXPECT_VAL(vm, v, NUM_VAL(5), "ku_print_val ret");
  ku_printval(vm, v);
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 12.3;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(12.3), "ku_lex_peeknext ret");
  kut_free(vm);

  vm = kut_new(false);
  ku_lexinit(vm, "&& class else false for function if new null || return super this true while extends {}[]!+-*/=!==><>=<= === => break continue const far\ttrick\nart\rcool eek too functiond");
  kutok t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_AND, "[&&]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_CLASS, "[class]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_ELSE, "[else]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_FALSE, "[false]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_FOR, "[for]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_FUN, "[function]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_IF, "[if]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_NEW, "[new]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_NULL, "[null]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_OR, "[||]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_RETURN, "[return]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_SUPER, "[super]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_THIS, "[this]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_TRUE, "[true]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_WHILE, "[while]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_EXTENDS, "[extends]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_LBRACE, "[{]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_RBRACE, "[}]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_LBRACKET, "[[]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_RBRACKET, "[]]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_BANG, "[!]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_PLUS, "[+]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_MINUS, "[-]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_STAR, "[*]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_SLASH, "[/]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_EQ, "[=]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_NE, "[!==]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_GT, "[>]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_LT, "[<]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_GE, "[>=]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_LE, "[<=]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_EQUALITY, "[===]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_ARROW, "[=>]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_BREAK, "[break]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_CONTINUE, "[continue]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_CONST, "[const]");


  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_IDENT, "[identifier]");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_IDENT, "[identifier]");
  kut_free(vm);

  vm = kut_new(false);
  ku_lexinit(vm, "// this is a comment");
  t = ku_scan(vm);
  EXPECT_INT(vm, t.type, TOK_EOF, "comment");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "(12-2)/5");
  EXPECT_VAL(vm, v, NUM_VAL(2), "sub div ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "-true");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "negate err");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "true");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "true literal eval");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "false");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "false literal eval");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "null");
  EXPECT_VAL(vm, v, NULL_VAL, "null literal eval");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "!true");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "!true eval");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "!false");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "!false eval");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "1===1");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "=== true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "1===false");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "=== mismatch types");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "1===2");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "=== false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "1!==2");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "!== true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "1!==1");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "!== false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "1<1");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "< false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "1<2");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "< true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"abc\" < \"def\"");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "string < true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"abcd\" < \"abc\"");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "string < false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"abc\" > \"def\"");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "string > false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"abcd\" > \"abc\"");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "string > true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "2<=1");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "<= false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "2<=3");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "<= true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "3>2");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "> true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "3>7");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "> false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "3>=7");
  EXPECT_VAL(vm, v, BOOL_VAL(false), ">= false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "3>=3");
  EXPECT_VAL(vm, v, BOOL_VAL(true), ">= true");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 12 < true;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "< num expected");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 12 + true;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "add num expected");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"hello \" + \"world\"");
  EXPECT_INT(vm, IS_OBJ(v), true, "stradd type obj");
  EXPECT_INT(vm, AS_OBJ(v)->type, OBJ_STR, "stradd obj is str");
  char *__nonnull chars = AS_CSTR(v);
  EXPECT_INT(vm, strcmp(chars, "hello world"), 0, "str val");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"hello \" === \"world\"");
  EXPECT_VAL(vm, v, BOOL_VAL(false), "str === false");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"hello\" === \"hello\"");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "str === true");
  kut_free(vm);

  vm = kut_new(false);
  v = ku_test_eval(vm, "\"hello \" !== \"world\"");
  EXPECT_VAL(vm, v, BOOL_VAL(true), "str !== true");
  kut_free(vm);

  vm = kut_new(false);
  kutab map;
  ku_tabinit(vm, &map);
  kustr *__nonnull k1 = ku_strfrom(vm, "key1", 4);
  kustr *__nonnull k2 = ku_strfrom(vm, "key1", 4);
  EXPECT_TRUE(vm, k1 == k2, "string intern equal");
  kustr *__nonnull k3 = ku_strfrom(vm, "key2", 4);
  EXPECT_TRUE(vm, k3 != k2, "string intern not equal");
  bool isnew = ku_tabset(vm, &map, k1, NUM_VAL(3.14));
  EXPECT_TRUE(vm, isnew, "map set new");
  bool found = ku_tabget(vm, &map, k1, &v);
  EXPECT_TRUE(vm, found, "map get found");
  EXPECT_VAL(vm, v, NUM_VAL(3.14), "map get found value");
  found = ku_tabget(vm, &map, k3, &v);
  EXPECT_TRUE(vm, !found, "map get not found");
  found = ku_tabdel(vm, &map, k1);
  EXPECT_TRUE(vm, found, "map del found");
  found = ku_tabdel(vm, &map, k3);
  EXPECT_TRUE(vm, !found, "map del not found");


  found = ku_tabget(vm, &map, k1, &v);
  EXPECT_TRUE(vm, !found, "map del not found");
  kutab map2;
  ku_tabinit(vm, &map2);
  ku_tabcopy(vm, &map, &map2);
  ku_tabfree(vm, &map);
  ku_tabfree(vm, &map2);
  ku_tabdel(vm, &map, k1);
  found = ku_tabget(vm, &map, k1, &v);
  EXPECT_TRUE(vm, !found, "empty map get");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "x = 20;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "undeclard global assign");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 20;");
  EXPECT_INT(vm, res, KVM_OK, "global decl");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(20), "global init");
  res = ku_exec(vm, "x = 30;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(30), "global set");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 20; let y = 0; { let a=x*20; y = a; }");
  EXPECT_INT(vm, res, KVM_OK, "local decl");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(400), "local init");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "const x=1; const y=2; const x=3;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "const double decl");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "const x=1; x=3;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "const double init");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "const x=20; let y = 0; { const a=x*20; y = a; }");
  EXPECT_INT(vm, res, KVM_OK, "const decl");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(400), "local const init");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "{ const a=20; a = 7; }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "local const late assign");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "{ let a = a; }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "local own init");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 10; if (true) { x = 30; }");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(30), "if true");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 10; if (false) { x = 30; }");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(10), "if false");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 10; if (false) x = 30;  else x = 20;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(20), "else");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "if (true) { printf(222); }");
  EXPECT_INT(vm, res, KVM_OK, "if print");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "if true) { printf(222); }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "if no (");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "if (true { printf(222); }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "if no )");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = false && true;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(false), "false && true");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = false && false;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(false), "false && false");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = true && false;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(false), "true && false");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = true && true;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(true), "true && true");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = false || true;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(true), "false || true");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = false || false;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(false), "false || false");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = true || false;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(true), "true || false");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = true || true;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), BOOL_VAL(true), "true || true");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 1; while(x < 20) { x = x + 1; }");
  EXPECT_INT(vm, res, KVM_OK, "while parse");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(20), "while simple");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 1; while x < 20) { x = x + 1; }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "while no lpar");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 1; while (x < 20 { x = x + 1; }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "while no rpar");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 0; for(let j=0; j < 10; j=j+1) x = j;");
  EXPECT_INT(vm, res, KVM_OK, "for parse");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(9), "for simple");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 0; for let j=0; j < 10; j=j+1) x = j;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "for no lpar");
  kut_free(vm);

  vm = kut_new(true);
  vm->flags = 0;
  res = ku_exec(vm, "let x = 0; for(; x < 10; x=x+1) {printf(x); printf(\"\\n\");}");
  EXPECT_INT(vm, res, KVM_OK, "for no init");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(10), "for no init");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = 0; for(; x < 10; ) x=x+1;");
  EXPECT_INT(vm, res, KVM_OK, "for no inc");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(10), "for no inc");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "function foo(a) { print(\"ok\"); }");
  EXPECT_INT(vm, res, KVM_OK, "function def");
  v = ku_get_global(vm, "foo");
  EXPECT_INT(vm, IS_OBJ(v),true, "function object");
  kut_free(vm);

  vm = kut_new(true);
  vm->max_params = 1;
  res = ku_exec(vm, "function foo(a,b) { printf(555); }; foo(4,5,6);");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "too many params");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "function foo(a,b) { printf(2); }");
  EXPECT_INT(vm, res, KVM_OK, "function call def");
  res = ku_exec(vm, "foo(1,2,3);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "function call mismatch");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "a=7; a();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "non-function call");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function a() { b(); }\nfunction b() { b(12); }\na();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "too many args print");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "return 2;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "return from __main__");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=2 return function foo()");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "parse_skip return");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=2 function foo()");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "parse_skip function");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=2 class foo()");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "parse_skip class");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=2 let foo()");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "parse_skip let");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=2 for foo()");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "parse_skip for");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=2 if foo()");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "parse_skip if");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=2 while foo()");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "parse_skip while");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function f(a) { return a*2; }\nlet x = f(3);");
  EXPECT_INT(vm, res, KVM_OK, "return expr res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(6), "return expr val");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function f(a) { let z = 2; }\nlet x = f(3);");
  EXPECT_INT(vm, res, KVM_OK, "implicit return res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "implicit return val");
  kut_free(vm);

  vm = kut_new(false);
  ku_cfuncdef(vm, "nadd", kutest_native_add);
  res = ku_exec(vm, "let x = nadd(3,4);");
  EXPECT_INT(vm, res, KVM_OK, "cfunction res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(7), "cfunction return");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = clock();");
  EXPECT_INT(vm, res, KVM_OK, "clock res");
  v = ku_get_global(vm, "x");
  EXPECT_INT(vm, IS_NUM(v), true, "clock return");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "printf(12);");
  EXPECT_INT(vm, res, KVM_OK, "printf res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function M(x) { let m = x; function e(n) { return m*n; } return e; }\n let z = M(5); let x = z(3);");
  EXPECT_INT(vm, res, KVM_OK, "closure res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(15), "closure val");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function o() { let a=7; let b=8; function i() { return a+b; } return i; }\n let z = o(); let x = z();");
  EXPECT_INT(vm, res, KVM_OK, "closure2 res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(15), "closure2 val");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function f1(){let a1=1; function f2() {let a2=2; function f3(){ return a1+a2; } return f3; } return f2; }\n let v1=f1(); let v2=v1(); let v3=v2();");
  EXPECT_INT(vm, res, KVM_OK, "closure3 res");
  EXPECT_VAL(vm, ku_get_global(vm, "v3"), NUM_VAL(3), "closure3 val");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function M(x) { let m = x; function e() { return m*m; } return e; }\n let z = M(5); let x = z();");
  EXPECT_INT(vm, res, KVM_OK, "closure4 res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(25), "closure4 val");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  res = ku_exec(vm, "let x = \"hello\"; x=null;");
  ku_gc(vm);
  EXPECT_INT(vm, res, KVM_OK, "gc res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "gc val");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCLOG;
  vm->gcnext = 0;
  res = ku_exec(vm, "let x = \"hello\"; x=null;");
  EXPECT_INT(vm, res, KVM_OK, "gcnext res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "gcnext val");
  kut_free(vm);


  vm = kut_new(true);
  res = ku_exec(vm, "printf(null);");
  EXPECT_INT(vm, res, KVM_OK, "printf null");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  res = ku_exec(vm, "function M(x) { let m = x; function e() { return m*m; } return e; }\n let z = M(5); let x = z(); x = null;");
  ku_gc(vm);
  EXPECT_INT(vm, res, KVM_OK, "gc closure res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "gc closure val");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  res = ku_exec(vm, "function M(x) { let m = x; let mm=x*2; function e(n) { return m*n*mm; } return e; }\n let z = M(5); let x = z(3); x = null;");
  EXPECT_INT(vm, res, KVM_OK, "gc closure2 res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "gc closure2 val");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = \"hello \" + \"world\";");
  int sc = kut_table_count(vm, &vm->strings);
  res = ku_exec(vm, "let y = \"hello\" + \" world\";");
  // +1 for the y value, +2 for two different substrings
  EXPECT_INT(vm, kut_table_count(vm, &vm->strings), sc+3, "string intern");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "class Foo {}\n printf(Foo);");
  EXPECT_INT(vm, res, KVM_OK, "class decl");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "class Foo {}\n let f = new Foo(); printf(f);");
  EXPECT_INT(vm, res, KVM_OK, "class cons");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "function Foo() {}\n let f = new Foo();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "non-class parameter to new");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let c = 7; c.p = 9;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "non-instance setprop");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let c = 7; let x = c.p;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "non-instance getprop");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class C{}\n let c = new C(); c.p = 9; let x = c.p;");
  EXPECT_INT(vm, res, KVM_OK, "set/get prop res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(9), "set/get prop ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class C{}\n let c = new C(); c.p=9; let x=c.z;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "set/get prop not found");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=1;\n class C { M() { x=3; } }\n let c = new C();\n let m = c.M;\n m();");
  EXPECT_INT(vm, res, KVM_OK, "bound method res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "bound method ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=1; class C{ M() { this.z=3; } }\n let c = new C(); c.M(); x=c.z;");
  EXPECT_INT(vm, res, KVM_OK, "this res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "this ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = this;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "global this res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class C {}\n let c = new C(12,14);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "no ctor with args");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class C { constructor(x) { this.x = x; }}\n let c = new C(12); let x = c.x;");
  EXPECT_INT(vm, res, KVM_OK, "ctor args res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(12), "ctor args ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class C { constructor(x) { this.x = x; return 7; }}\n let c = new C(12); let x = c.x;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "ctor return res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=1; class C { constructor() { function f() { x=8; } this.f = f; } }\n let c = new C(); c.f();");
  EXPECT_INT(vm, res, KVM_OK, "field invoke res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(8), "field invoke ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A extends A {}");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "class ownsubclass res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A extends 12 {}");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "class bad inherit static res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let B = 9; class A extends B {}");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "class bad inherit run res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x = super.foo();");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "global super res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A { f() { let x = super.foo(); } }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "no superclass super res");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=0; class A { f() { x=2; } }\n class B extends A {}\n let b = new B(); b.f();");
  EXPECT_INT(vm, res, KVM_OK, "super res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(2), "super ret");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = 0;
  res = ku_exec(vm, "class A { f() { return 2; } }\n class B extends A { f() { let z = super.f; return z()*3; }}\n let b = new B(); let x = b.f();");
  EXPECT_INT(vm, res, KVM_OK, "super call res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(6), "super call ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A { f() { return 2; } }\nclass B extends A { f() { return super.f()*3; }}\n let b = new B(); let x = b.f();");
  EXPECT_INT(vm, res, KVM_OK, "super invoke res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(6), "super invoke ret");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_const = 1;
  res = ku_exec(vm, "let x=1; let y=2;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "too many const");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function=7; ");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "invalid assign");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function f() { return; }");
  EXPECT_INT(vm, res, KVM_OK, "simple ret");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_closures = 1;
  res = ku_exec(vm, "function O() { let a=1; let b=2; function I() { return a*b; } return e; }\n let z=M(); let x=z();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "too many closures res");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_jump = 1;
  res = ku_exec(vm, "let x = 0; for(let j=0; j < 10; j=j+1) x = j;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "max jump");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_body = 1;
  res = ku_exec(vm, "let x = 0; for(let j=0; j < 10; j=j+1) x = j;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "max body");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_frames = 1;
  res = ku_exec(vm, "function a(){} function b(){a();} function c(){b();} c();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "stack overflow");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A{}\n let a = new A(); a.x();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "invoke invalid prop");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A{}\n let a = 7; a.x();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "invoke invalid receiver");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A {} class B extends A { f() { super.x(); }} let b = new B(); b.f();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "invoke invalid super");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=true; let y=-x;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "negate non-number");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function O() { let a=1; let b=2; function e() { a=9; return a*b; } return e; }\n let z=O(); let x=z();");
  EXPECT_INT(vm, res, KVM_OK, "closure set res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(18), "closure set ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "class A {} class B extends A { f() { let m = super.x; }} let b = new B(); b.f();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "invalid get super");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_NOEXEC;
  res = ku_exec(vm, "let x=9;");
  EXPECT_INT(vm, res, KVM_OK, "noexec");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_locals = 1;
  res = ku_exec(vm, "function f() { let x=9; let m=2; }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "max_locals");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  res = ku_exec(vm, "class A{ f(){}} let a = new A(); let z = a.f; a = null;");
  ku_gc(vm);
  EXPECT_INT(vm, res, KVM_OK, "gc class res");
  EXPECT_VAL(vm, ku_get_global(vm, "a"), NULL_VAL, "gc class val");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let f=function(a) {return a*2;}; let x=f(3);");
  EXPECT_INT(vm, res, KVM_OK, "anonymous function res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(6), "anonymous function ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function f(x) { return x(7);} let x=f(function(a) { return a*2; });");
  EXPECT_INT(vm, res, KVM_OK, "function arg res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(14), "function arg ret");
  kut_free(vm);

  ku_lexinit(vm, "let x = 12+3;\nlet m=2;\nlet mm=99;");
  ku_lexdump(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let f = a => a*2; let x=f(3);");
  EXPECT_INT(vm, res, KVM_OK, "lambda res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(6), "lambda ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "function f(x) { return x(2); } let x = f(a => a*3);");
  EXPECT_INT(vm, res, KVM_OK, "lambda arg res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(6), "lambda arg ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let f = (a,b) => a*b; let x=f(3,4);");
  EXPECT_INT(vm, res, KVM_OK, "lambda (args) res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(12), "lambda (args) ret");
  kut_free(vm);


  vm = kut_new(false);
  res = ku_exec(vm, "let max = (a,b) => { if (a>b) return a; else return b; }; let x=max(3,14);");
  EXPECT_INT(vm, res, KVM_OK, "lambda args body res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(14), "lambda args body ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let abs = a => { if (a<0) return -a; else return a; }; let x=abs(-12);");
  EXPECT_INT(vm, res, KVM_OK, "lambda body res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(12), "lambda body ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x =3; break;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "global break");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x =3; continue;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "global continue");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=0; while(x < 5) { if (x > 2) break; x=x+1; }");
  EXPECT_INT(vm, res, KVM_OK, "while break res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "while break ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=0; for(let i=0; i<10; i=i+1) { if (i > 2) break;  x = i;}");
  EXPECT_INT(vm, res, KVM_OK, "for break res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(2), "for break ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let x=0; let y=0; while(x < 5) { x=x+1; if (x > 2) continue; y=x; }");
  EXPECT_INT(vm, res, KVM_OK, "while continue res");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(2), "while continue ret");
  kut_free(vm);

  vm = kut_new(false);
  res = ku_exec(vm, "let y=0; for(let x=0; x<5; x=x+1) { if (x > 2) continue; y=x; }");
  EXPECT_INT(vm, res, KVM_OK, "for continue res");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(2), "for continue ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let y=\"12\"; let x=y.count;");
  EXPECT_INT(vm, res, KVM_OK, "string.count res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(2), "string.count ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=v.count;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.count null");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=12; let y=x.count;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.count non num");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let y=\"12\\n\\r\\t\"; let x=y.count;");
  EXPECT_INT(vm, res, KVM_OK, "strlit escape res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(5), "strlit escape ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=1.2e3;");
  EXPECT_INT(vm, res, KVM_OK, "1.2e3 res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(1.2e3), "1.2e3 ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=0xcafeb10b;");
  EXPECT_INT(vm, res, KVM_OK, "hex res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(0xcafeb10b), "hex ret");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, 0);
  res = ku_exec(vm, "let x=test;");
  EXPECT_INT(vm, res, KVM_OK, "class res");
  EXPECT_TRUE(vm, IS_CCLASS(ku_get_global(vm, "x")), "class ret");
  kut_free(vm);
  EXPECT_INT(vm, tclass_sfree, 0, "class no sfree");

  vm = kut_new(false);
  tclass_init(vm, SFREE);
  res = ku_exec(vm, "let x=test;");
  EXPECT_INT(vm, res, KVM_OK, "class res");
  EXPECT_TRUE(vm, IS_CCLASS(ku_get_global(vm, "x")), "class ret");
  kut_free(vm);
  EXPECT_INT(vm, tclass_sfree, 1, "class sfree");

  vm = kut_new(false);
  tclass_init(vm, 0);
  res = ku_exec(vm, "let x=test.prop;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "class no sget res");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SGET);
  res = ku_exec(vm, "let x=test.prop;");
  EXPECT_INT(vm, res, KVM_OK, "class sget res");
  EXPECT_INT(vm, tclass_sget, 1, "class sget ret");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SPUT);
  res = ku_exec(vm, "test.prop=8;");
  EXPECT_INT(vm, res, KVM_OK, "class sput res");
  EXPECT_INT(vm, tclass_sput, 8, "class sput ret");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, 0);
  res = ku_exec(vm, "test.method(5,2,1);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "class no scall res");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SCALL);
  res = ku_exec(vm, "test.method(5,2,1);");
  EXPECT_INT(vm, res, KVM_OK, "class scall res");
  EXPECT_INT(vm, tclass_scall, 3, "class scall ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.sin(Math.PI);");
  EXPECT_INT(vm, res, KVM_OK, "Math.sin res");
  EXPECT_TRUE(vm, APPROX(ku_get_global(vm, "x"), 0), "Math.sin ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.cos(Math.PI);");
  EXPECT_INT(vm, res, KVM_OK, "Math.cos res");
  EXPECT_TRUE(vm, APPROX(ku_get_global(vm, "x"), -1), "Math.cos ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.tan(Math.PI/4);");
  EXPECT_INT(vm, res, KVM_OK, "Math.tan res");
  EXPECT_TRUE(vm, APPROX(ku_get_global(vm, "x"), 1), "Math.tan ret");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SMARK);
  res = ku_exec(vm, "let x=test;");
  ku_gc(vm);
  EXPECT_INT(vm, tclass_smark, 1, "class smark false");
  res = ku_exec(vm, "x=null;");
  ku_gc(vm);
  EXPECT_TRUE(vm, tclass_smark > 1, "class smark false");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, 0);
  res = ku_exec(vm, "let x = test(4);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "class no cons");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE);
  res = ku_exec(vm, "let x = test(4);");
  EXPECT_INT(vm, res, KVM_OK, "class cons res");
  EXPECT_TRUE(vm, IS_CINST(ku_get_global(vm, "x")), "class cons ret");
  kut_free(vm);
  EXPECT_INT(vm, tclass_ifree, 1, "class ifree");

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE | IMARK);
  vm->flags |= KVM_F_GCSTRESS;
  res = ku_exec(vm, "let x=test(4);");
  ku_gc(vm);
  EXPECT_TRUE(vm, tclass_sfree == 0, "class sfree");
  EXPECT_INT(vm, res, KVM_OK, "class imark res");
  EXPECT_TRUE(vm, tclass_imark > 0, "class imark");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE | IGET);
  res = ku_exec(vm, "let x=test(4); let y=x.prop;");
  EXPECT_INT(vm, res, KVM_OK, "iget res");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(4), "iget");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE );
  res = ku_exec(vm, "let x=test(4); x.prop=9;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "no iput res");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE | IPUT | IGET);
  res = ku_exec(vm, "let x=test(4); x.prop=9; let y=x.prop;");
  EXPECT_INT(vm, res, KVM_OK, "iput res");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(9), "iput");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"ABC %g,%s,%b,%b\",123.45,\"FF\", true, false);");
  EXPECT_INT(vm, res, KVM_OK, "string.format res");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_STR(v), "string.format ret");
  kustr *__nonnull str = AS_STR(v);
  EXPECT_INT(vm, strcmp(str->chars, "ABC 123.45,FF,true,false"), 0, "string.format value");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%f\", 123.456);");
  EXPECT_INT(vm, res, KVM_OK, "string.format(%f) res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "123.456000", "string.format(%f) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%f\", true);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.format(%f) invalid res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%x\",0xfb1cfd);");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "fb1cfd", "string.format %x");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%d\",123.45);");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "123", "string.format %d");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%d\",true);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.format bool to %d");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%s\",true);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.format bool to %s");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%g\",true);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.format bool to %g");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%x\",true);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.format bool to %x");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"%b\",12);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.format num to %b");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(\"hello%\",12);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "string.format trailing %");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.format(12);");
  EXPECT_INT(vm, res, KVM_OK, "string.format num res");
  EXPECT_TRUE(vm, IS_NULL(ku_get_global(vm, "x")), "string.format ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=string.bogus(12);");
  EXPECT_INT(vm, res, KVM_OK, "string.bogus num res");
  EXPECT_TRUE(vm, IS_NULL(ku_get_global(vm, "x")), "string.bogus ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"123\"; let x=s.bogus;");
  EXPECT_INT(vm, res, KVM_OK, "s.bogus res");
  EXPECT_TRUE(vm, IS_NULL(ku_get_global(vm, "x")), "s.bogus ret");
  kut_free(vm);


  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.bogus(12);");
  EXPECT_INT(vm, res, KVM_OK, "Math.bogus scall res");
  EXPECT_TRUE(vm, IS_NULL(ku_get_global(vm, "x")), "Math.bogus scall ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.bogus;");
  EXPECT_INT(vm, res, KVM_OK, "Math.bogus sget res");
  EXPECT_TRUE(vm, IS_NULL(ku_get_global(vm, "x")), "Math.bogus sget ret");
  kut_free(vm);

  vm = kut_new(true);
  v = ku_get_global(vm, "Math");
  ku_printval(vm, v);
  kut_free(vm);

  vm = kut_new(true);
  v = ku_get_global(vm, "clock");
  ku_printval(vm, v);
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE );
  res = ku_exec(vm, "let x=test(4);");
  v = ku_get_global(vm, "x");
  ku_printval(vm, v);
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE );
  res = ku_exec(vm, "let z=\"he\"; let x=z+\"llo\";");
  v = ku_get_global(vm, "x");
  str = AS_STR(v);
  EXPECT_INT(vm, strcmp(str->chars, "hello"), 0, "let + string ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=0xCaFeb10B;");
  EXPECT_INT(vm, res, KVM_OK, "hex mixed case res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(0xcafeb10b), "hex mixed case ret");
  kut_free(vm);

  vm = kut_new(false);
  kuaobj *__nonnull ao = ku_arrnew(vm, 0);
  EXPECT_TRUE(vm, ao->elements.capacity == 0, "array(0) alloc free");
  kut_free(vm);

  vm = kut_new(false);
  ao = ku_arrnew(vm, 24);
  EXPECT_TRUE(vm, ao->elements.capacity == 24, "array(24) alloc free");
  kut_free(vm);

  vm = kut_new(false);
  ao = ku_arrnew(vm, 0);
  ku_arrset(vm, ao, 5, NUM_VAL(12));
  EXPECT_TRUE(vm, ao->elements.capacity > 0, "array(0) set(5) cap");
  v = ku_arrget(vm, ao, 5);
  EXPECT_VAL(vm, v, NUM_VAL(12), "array(0) set(5) get (5)");
  v = ku_arrget(vm, ao, 1);
  EXPECT_VAL(vm, v, NULL_VAL, "array(0) set(5) get (1)");
  v = ku_arrget(vm, ao, 1000);
  EXPECT_VAL(vm, v, NULL_VAL, "array(0) set(5) get (1000)");
  ku_printval(vm, OBJ_VAL(ao));
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=[1,2,3;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "array missing ']'");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=[1,2,3];");
  EXPECT_INT(vm, res, KVM_OK, "array init");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array type");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 0), NUM_VAL(1), "array[0]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 1), NUM_VAL(2), "array[1]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 2), NUM_VAL(3), "array[2]");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=[1,\"hello\",3,4];");
  EXPECT_INT(vm, res, KVM_OK, "mix array res");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_ARRAY(v), "mix array type");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 0), NUM_VAL(1), "mix array[0]");
  kuval s = ku_arrget(vm, AS_ARRAY(v), 1);
  EXPECT_TRUE(vm, IS_STR(s), "mix array[1] type");
  EXPECT_INT(vm, strcmp(AS_STR(s)->chars, "hello"), 0, "mixarray[1] value");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 2), NUM_VAL(3), "mix array[2]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 3), NUM_VAL(4), "mix array[3]");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a[1];");
  EXPECT_INT(vm, res, KVM_OK, "array get res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "array get value");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a[1;");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "array get ']'");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; a[1]=9; let x=a[1];");
  EXPECT_INT(vm, res, KVM_OK, "array set res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(9), "array get value");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=array(7);");
  EXPECT_INT(vm, res, KVM_OK, "array cons res");
  v = ku_get_global(vm, "a");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array cons type");
  ao = AS_ARRAY(v);
  EXPECT_INT(vm, ao->elements.capacity, 7, "array cons cap");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=array(0);");
  EXPECT_INT(vm, res, KVM_OK, "array cons(0) res");
  v = ku_get_global(vm, "a");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array cons(0) type");
  ao = AS_ARRAY(v);
  EXPECT_INT(vm, ao->elements.capacity, 0, "array cons(0) cap");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.count;");
  EXPECT_INT(vm, res, KVM_OK, "array count res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "array count ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.map(e => e*2);");
  EXPECT_INT(vm, res, KVM_OK, "array.map res");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array.map type");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 0), NUM_VAL(2), "array.map[0]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 1), NUM_VAL(6), "array.map[1]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 2), NUM_VAL(8), "array.map[2]");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.map();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.map null");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.map(9);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.map num");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.map((e, b) => e*2 );");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.map args");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let sum=[1,3,4].reduce(0, (v,e) => v+e);");
  EXPECT_INT(vm, res, KVM_OK, "array.reduce res");
  EXPECT_VAL(vm, ku_get_global(vm, "sum"), NUM_VAL(8), "array.reduce value");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let sum=[1,3,4].reduce(0, (v,e) => bad());");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.reduce bad fn");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let sum=[1,3,4].reduce(9);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.reduce num");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let sum=[1,3,4].reduce();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.reduce null");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let sum=[1,3,4].reduce(0, v => v*2);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.reduce 1 arg");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x =a.bogus();");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "array.invoke bogus");
  kut_free(vm);


  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.map(e => f(e));");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.map arg err res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=1.23e-2;");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(1.23e-2), "1.23e-2");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "for(let i=0; i<10; i=i+1) { if (i>2) break; if (i>3) break; }");
  EXPECT_INT(vm, res, KVM_OK, "two break");
  kut_free(vm);

  vm = kut_new(true);
  vm->max_patches = 1;
  res = ku_exec(vm, "for(let i=0; i<10; i=i+1) { if (i>2) break; if (i>3) break; }");
  EXPECT_INT(vm, res, KVM_ERR_SYNTAX, "patch limit");
  kut_free(vm);

  vm = kut_new(true);
  vm->flags |= KVM_F_GCSTRESS;
  res = ku_exec(vm, "let t=table(); t.x=1; t.y=2; let x=t.x+t.y;");
  EXPECT_INT(vm, res, KVM_OK, "table res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "table ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.x=1; t.y=2; let x=t.z;");
  EXPECT_INT(vm, res, KVM_OK, "table res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "table null ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.set(\"x\",1); let x=t.get(\"x\");");
  EXPECT_INT(vm, res, KVM_OK, "table set get res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(1), "table set get ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.set(1,1); let x=t.get(\"x\");");
  EXPECT_INT(vm, res, KVM_OK, "table set(null) res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "table set(null)");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.set(\"x\",1); let x=t.get(1);");
  EXPECT_INT(vm, res, KVM_OK, "table get(null) res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "table get(null)");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); let x = t.bogus();");
  EXPECT_INT(vm, res, KVM_OK, "table bogus res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "table bogus");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.x=1; t.y=2; t.z=3; let K=\"\"; let V=0; t.iter( (k,v) => { K=K+k; V=V+v; } );");
  EXPECT_INT(vm, res, KVM_OK, "table iter res");
  v = ku_get_global(vm, "K");
  EXPECT_TRUE(vm, IS_STR(v), "table iter K type");
  // this is fragile depends on hashing and key insertion order
  EXPECT_INT(vm, strcmp(AS_STR(v)->chars, "yzx"), 0, "table iter K");
  EXPECT_VAL(vm, ku_get_global(vm, "V"), NUM_VAL(6), "table iter V");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.iter(k => k);");
  EXPECT_INT(vm, res, KVM_OK, "table iter argc res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.iter();");
  EXPECT_INT(vm, res, KVM_OK, "table iter null res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let t=table(); t.iter(12);");
  EXPECT_INT(vm, res, KVM_OK, "table iter num res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.imod(9,5);");
  EXPECT_INT(vm, res, KVM_OK, "imod res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(4), "imod ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.sqrt(4);");
  EXPECT_INT(vm, res, KVM_OK, "sqrt res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(2), "sqrt ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.pow(3,2);");
  EXPECT_INT(vm, res, KVM_OK, "pow res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(9), "pow ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = int(3.2);");
  EXPECT_INT(vm, res, KVM_OK, "int res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "int ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = int(true);");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "int(bool) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = parseFloat(\"3.2\");");
  EXPECT_INT(vm, res, KVM_OK, "parseFloat res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3.2), "parseFloat ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = parseFloat(true);");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "parseFloat(bool) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = 0xf7f1 | 0x9854;");
  EXPECT_INT(vm, res, KVM_OK, "bit || res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(65525), "bit || ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = 0xf7f1 & 0x9854;");
  EXPECT_INT(vm, res, KVM_OK, "bit && res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(36944), "bit && ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = 0xf7f1 | true;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "bit || invalid res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = 0xf7f1 & true;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "bit && invalid res");
  kut_free(vm);

  vm = kut_new(true);
  v = ku_cinstance(vm, "table");
  EXPECT_TRUE(vm, AS_CINST(v)->klass->iget == table_iget, "table new");
  kut_free(vm);

  vm = kut_new(true);
  ku_exec(vm, "let t=0;");
  ku_exec(vm, "let samples=array(6);");
  ku_exec(vm, "samples[0] = (1 + Math.sin(t*1.2345 + Math.cos(t*0.33457)*0.44))*0.5;");
  res = ku_exec(vm, "let x = samples[0];");
  EXPECT_INT(vm, res, KVM_OK, "array index crash res");
  kut_free(vm);

  vm = kut_new(true);
  ku_exec(vm, "let x=7, y=9;");
  EXPECT_INT(vm, res, KVM_OK, "multi-let res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(7), "multi-let x");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(9), "multi-let y");
  kut_free(vm);

  vm = kut_new(true);
  ku_exec(vm, "let x,y;");
  EXPECT_INT(vm, res, KVM_OK, "multi-let null res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "multi-let null x");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NULL_VAL, "multi-let null y");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = 2 << 4;");
  EXPECT_INT(vm, res, KVM_OK, "shl res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(2 << 4), "shl ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = 2 << true;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "shl arg res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = 16 >> 2;");
  EXPECT_INT(vm, res, KVM_OK, "shr res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(16 >> 2), "shr ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = true >> 2;");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "shr arg res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = string.frombytes([65,66,67]);");
  EXPECT_INT(vm, res, KVM_OK, "string.frombytes res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "ABC", "string.frombytes ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,2,3]; a[0]=4; let x=a.count;");
  EXPECT_INT(vm, res, KVM_OK, "arr change res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "arr change count");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=true; a[0]=4; ");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "non-array set res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=true; let x = a[0]; ");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "non-array get res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "function F() { let a=array(2); a[0]=1; a[1]=2; a[2]=3; return a[2]; } let x=F();");
  EXPECT_INT(vm, res, KVM_OK, "arr nogc res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "arr no gc ret");
  kut_free(vm);

  vm = kut_new(true);
  vm->flags = KVM_F_GCSTRESS;
  res = ku_exec(vm, "function F() { let a=array(2); a[0]=1; a[1]=2; a[2]=3; return a[2]; } let x=F();");
  EXPECT_INT(vm, res, KVM_OK, "arr gc res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "arr gc ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a = []; let x = a.first;");
  EXPECT_INT(vm, res, KVM_OK, "arr empty first res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "arr empty first ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a = [12,3]; let x = a.first;");
  EXPECT_INT(vm, res, KVM_OK, "arr first res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(12), "arr first ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a = []; let x = a.last;");
  EXPECT_INT(vm, res, KVM_OK, "arr empty last res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "arr empty last ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a = [12,3]; let x = a.last;");
  EXPECT_INT(vm, res, KVM_OK, "arr last res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(3), "arr last ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=2; let a = (x > 2) && 7 || 6;");
  EXPECT_INT(vm, res, KVM_OK, "tertiary res");
  EXPECT_VAL(vm, ku_get_global(vm, "a"), NUM_VAL(6), "tertiary ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.filter(e => e > 1);");
  EXPECT_INT(vm, res, KVM_OK, "array.filter res");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array.filter type");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 0), NUM_VAL(3), "array.filter[0]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 1), NUM_VAL(4), "array.filter[1]");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.filter();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.filter null");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.filter(9);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.filter num");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let a=[1,3,4]; let x=a.filter((e, b) => e*2 );");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.filter args");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=[7,3,4]; x.sort((a,b) => a-b );");
  EXPECT_INT(vm, res, KVM_OK, "array.sort res");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array.sort type");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 0), NUM_VAL(3), "array.sort[0]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 1), NUM_VAL(4), "array.sort[1]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 2), NUM_VAL(7), "array.sort[2]");
  res = ku_exec(vm, "let c = x.count;");
  EXPECT_VAL(vm, ku_get_global(vm, "c"), NUM_VAL(3), "array sort count");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=[7,3,4]; x.sort(a => 0);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.sort argc res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=[7,3,4]; x.sort();");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.sort null res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=[7,3,4]; x.sort((a,b) => false);");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "array.sort res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x={\"a\"=1, \"b\"=2}; let z=x.a; let w=x.b;");
  EXPECT_INT(vm, res, KVM_OK, "table lit res");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_CINST(v), "table instance");
  EXPECT_VAL(vm, ku_get_global(vm, "z"), NUM_VAL(1), "table lit prop1");
  EXPECT_VAL(vm, ku_get_global(vm, "w"), NUM_VAL(2), "table lit prop2");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let comp = (a, b) => ( (a < b) && -1 || ( (a > b) && 1 || 0)) ;");
  EXPECT_INT(vm, res, KVM_OK, "tertiary comp res");
  res = ku_exec(vm, "let x = comp(9, 7);");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(1), "tertiary comp ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let comp = (a, b) => ( (a < b) && -1 || ( (a > b) && 1 || 0)) ;");
  EXPECT_INT(vm, res, KVM_OK, "db comp res");
  res = ku_exec(vm, "let db = [ { \"name\" = \"mohsen\", \"age\"=55 }, { \"name\" = \"josh\", \"age\"=40 }];");
  EXPECT_INT(vm, res, KVM_OK, "db init res");
  res = ku_exec(vm, "db.sort((a,b) => comp(a.name, b.name));");
  EXPECT_INT(vm, res, KVM_OK, "db sort res");
  res = ku_exec(vm, "let c = db.count;");
  EXPECT_VAL(vm, ku_get_global(vm, "c"), NUM_VAL(2), "db sort count");
  res = ku_exec(vm, "let f = db.first.name;");
  EXPECT_STR(vm, ku_get_global(vm, "f"), "josh", "db sort first name");
  kut_free(vm);


  vm = kut_new(true);
  res = ku_exec(vm, "let x=eval(\"12/3\");");
  EXPECT_INT(vm, res, KVM_OK, "eval res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(4), "eval ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=eval();");
  EXPECT_INT(vm, res, KVM_OK, "eval arg res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "eval arg ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x=eval(\"12/3\", 10);");
  EXPECT_INT(vm, res, KVM_OK, "eval stack res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(4), "eval stack ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.substr();");
  EXPECT_INT(vm, res, KVM_OK, "substr() res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "012345678", "substr() ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.substr(3);");
  EXPECT_INT(vm, res, KVM_OK, "substr(3) res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "345678", "substr(3) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.substr(3,5);");
  EXPECT_INT(vm, res, KVM_OK, "substr(3,5) res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "345", "substr(3,5) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.substr(-1);");
  EXPECT_INT(vm, res, KVM_OK, "substr(-1) res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "8", "substr(-1) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.substr(-3,-1);");
  EXPECT_INT(vm, res, KVM_OK, "substr(-3,-1) res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "678", "substr(-1) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.substr(-12,55);");
  EXPECT_INT(vm, res, KVM_OK, "substr(-12,55) res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "012345678", "substr(-12,5) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.substr(-12,55, true);");
  EXPECT_INT(vm, res, KVM_OK, "substr(-12,55, true) res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "", "substr(-12,5, true) ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s.bogus(-1,55);");
  EXPECT_INT(vm, res, KVM_OK, "string.bogus res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "string.bogus ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s[2];");
  EXPECT_INT(vm, res, KVM_OK, "str[idx] res");
  EXPECT_STR(vm, ku_get_global(vm, "x"), "2", "str[idx] ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s[-1];");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "str[idx] bounds low res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s[22];");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "str[idx] bounds high res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let s=\"012345678\"; let x=s[true];");
  EXPECT_INT(vm, res, KVM_ERR_RUNTIME, "str[bool]  res");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.fmod(9,7);");
  EXPECT_INT(vm, res, KVM_OK, "fmod res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(fmod(9,7)), "fmod ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.abs(-7);");
  EXPECT_INT(vm, res, KVM_OK, "abs negative res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(-1), "abs negative ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.abs(7);");
  EXPECT_INT(vm, res, KVM_OK, "abs positive res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(1), "abs positive ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.abs(0);");
  EXPECT_INT(vm, res, KVM_OK, "abs 0 res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(0), "abs 0 ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.floor(3.767);");
  EXPECT_INT(vm, res, KVM_OK, "floor res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(floor(3.767)), "floor ret");
  kut_free(vm);

  vm = kut_new(true);
  res = ku_exec(vm, "let x = Math.round(3.767);");
  EXPECT_INT(vm, res, KVM_OK, "round res");
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NUM_VAL(round(3.767)), "round ret");
  kut_free(vm);

  debug_call_count = 0;
  vm = kut_new(true);
  vm->debugger = test_debugger_stop;
  res = ku_exec(vm, "let x = 2;");
  EXPECT_INT(vm, debug_call_count, 1, "debug stop count");
  kut_free(vm);

  debug_call_count = 0;
  vm = kut_new(true);
  vm->debugger = test_debugger_cont;
  res = ku_exec(vm, "let x = 2;");
  EXPECT_INT(vm, debug_call_count, 4, "debug cont count");
  kut_free(vm);

  ku_test_summary();
  return ktest_fail;
}
