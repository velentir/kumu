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

/////////////////////////
// C Validation Macros //
/////////////////////////

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

/////////////////////////
// Test Running Macros //
/////////////////////////

// Defines whether or not to register the kumu VM standard libraries.
#define REG_LIBS true
#define NO_REG_LIBS false

// TEST_SCRIPT(bool reglibs, const char *_nonnull name, const char *_nonnull code)
#define TEST_SCRIPT(reglibs, name, code) kut_kumu_test(NULL, reglibs, name, code, KVM_OK);

// TEST_SCRIPT_WITH_VM(kuvm *__nonnull vm, const char *_nonnull name, const char *_nonnull code)
#define TEST_SCRIPT_WITH_VM(vm, name, code) kut_kumu_test(vm, false, name, code, KVM_OK);

// TEST_SYNTAX_FAILURE(bool reglibs, const char *_nonnull name, const char *_nonnull code)
#define TEST_SYNTAX_FAILURE(reglibs, name, code) kut_kumu_test(NULL, reglibs, name, code, KVM_ERR_SYNTAX);

// TEST_SYNTAX_FAILURE_WITH_VM(kuvm *__nonnull vm, const char *_nonnull name, const char *_nonnull code)
#define TEST_SYNTAX_FAILURE_WITH_VM(vm, name, code) kut_kumu_test(vm, false, name, code, KVM_ERR_SYNTAX)

// TEST_RUNTIME_FAILURE(bool reglibs, const char *_nonnull name, const char *_nonnull code)
#define TEST_RUNTIME_FAILURE(reglibs, name, code) kut_kumu_test(NULL, reglibs, name, code, KVM_ERR_RUNTIME);

// TEST_RUNTIME_FAILURE_WITH_VM(kuvm *__nonnull vm, const char *_nonnull name, const char *_nonnull code)
#define TEST_RUNTIME_FAILURE_WITH_VM(vm, name, code) kut_kumu_test(vm, false, name, code, KVM_ERR_RUNTIME);

// TEST_EXPRESSION(bool reglibs, const char *_nonnull name, const char *_nonnull expected_kumu, kuval expected_kuval)
#define TEST_EXPRESSION(reglibs, code, expected_kumu, expected_kuval)   \
  do {                                                                  \
    kut_kumu_test(NULL, reglibs, (code), ("ASSERT_EQ(" expected_kumu ", " code ");"), KVM_OK); \
    kut_exec_c_test(reglibs, (code), (code), (expected_kuval));         \
  } while (0)

////////////////////////////
// Test Utility Functions //
////////////////////////////

void kut_print(const char *_Nullable fmt, va_list args) {
  vfprintf(stderr, fmt, args);
}

static void kut_print_failure(kuvm *__nonnull vm, kuval expected, kuval actual)
{
  kuprint current = vm->print;
  vm->print = kut_print;

  ku_printf(vm, "[  ASSERTION FAILED in \"%s\"  ]", last_test);
  ku_printf(vm, "\n  Actual: ");
  ku_printval(vm, actual);
  ku_printf(vm, "\n  Expected: ");
  ku_printval(vm, expected);
  ku_printf(vm, "\n");

  vm->print = current;
}

static kuval kut_assert_eq(kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  if (argc != 2) {
    ku_err(vm, "Invalid parameters to ASSERT_EQ(kuval expected, kuval actual)");
    return NULL_VAL;
  }

  kuval expected = argv[0];
  kuval actual = argv[1];

  if (ku_equal(expected, actual)) {
    return NULL_VAL;
  }

  // Print the failure.
  kuprint current = vm->print;
  vm->print = kut_print;

  ku_printf(vm, "[  ASSERT EQ FAILED in \"%s\"  ]", last_test);
  ku_printf(vm, "\n  Actual: ");
  ku_printval(vm, actual);
  ku_printf(vm, "\n  Expected: ");
  ku_printval(vm, expected);
  ku_printf(vm, "\n");

  vm->print = current;

  ku_err(vm, "ASSERT_EQ Failure");

  return NULL_VAL;
}

static kuval kut_assert_null(kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  if (argc != 1) {
    ku_err(vm, "Invalid parameters to ASSERT_NULL(kuval actual)");
    return NULL_VAL;
  }

  kuval actual = argv[0];

  if (IS_NULL(actual)) {
    return NULL_VAL;
  }

  // Print the failure.
  kuprint current = vm->print;
  vm->print = kut_print;

  ku_printf(vm, "[  ASSERT NULL FAILED in \"%s\"  ]", last_test);
  ku_printf(vm, "\n  Actual: ");
  ku_printval(vm, actual);
  ku_printf(vm, "\n");

  vm->print = current;

  ku_err(vm, "ASSERT_NULL Failure");

  return NULL_VAL;
}

kuvm *__nonnull kut_new(bool reglibs) {
  kuvm *__nonnull vm = ku_newvm(STACK_MAX, NULL);
  if (reglibs) {
    ku_reglibs(vm);
  }

  ku_cfuncdef(vm, "ASSERT_EQ", kut_assert_eq);
  ku_cfuncdef(vm, "ASSERT_NULL", kut_assert_null);
  return vm;
}

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

static kuval kut_get_kures_name(kuvm *__nonnull vm, kures res) {
  switch (res) {
    case KVM_OK:
      return OBJ_VAL(ku_strfrom(vm, "KVM_OK", strlen("KVM_OK")));
    case KVM_CONT:
      return OBJ_VAL(ku_strfrom(vm, "KVM_CONT", strlen("KVM_CONT")));
    case KVM_ERR_SYNTAX:
      return OBJ_VAL(ku_strfrom(vm, "KVM_ERR_SYNTAX", strlen("KVM_ERR_SYNTAX")));
    case KVM_ERR_RUNTIME:
      return OBJ_VAL(ku_strfrom(vm, "KVM_ERR_RUNTIME", strlen("KVM_ERR_RUNTIME")));
    case KVM_FILE_NOTFOUND:
      return OBJ_VAL(ku_strfrom(vm, "KVM_FILE_NOTFOUND", strlen("KVM_FILE_NOTFOUND")));
  }
}

static void kut_kumu_test(kuvm *__nullable nullable_vm,
                          bool reglibs,
                          const char *__nonnull test_name,
                          char *__nonnull kumu_code,
                          kures kumu_ret_code) {
  kuvm *__nonnull vm = (nullable_vm != NULL) ? ((kuvm *__nonnull)nullable_vm) : kut_new(reglibs);

  last_test = test_name;

  kures res = ku_exec(vm, kumu_code);
  if (res == kumu_ret_code) {
    ktest_pass++;
  } else {
    ktest_fail++;
    kut_print_failure(vm, kut_get_kures_name(vm, kumu_ret_code), kut_get_kures_name(vm, res));
  }

  if (nullable_vm == NULL) {
    kut_free(vm);
  }
}

static void kut_exec_c_test(bool reglibs,
                            const char *__nonnull test_name,
                            const char *__nonnull kumu_code,
                            kuval expected_val) {
  kuvm *__nonnull vm = kut_new(reglibs);

  last_test = test_name;

  kuval v = ku_test_eval(vm, kumu_code);
  if (ku_equal(v, expected_val)) {
    ktest_pass++;
  } else {
    ktest_fail++;
    kut_print_failure(vm, expected_val, v);
  }

  kut_free(vm);
}

static void ku_test_summary() {
  printf("[  PASSED  ] %d test\n", ktest_pass);
  printf("[   WARN   ] %d test\n", ktest_warn);
  printf("[  FAILED  ] %d test\n", ktest_fail);
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

///////////////
//// Tests ////
///////////////

int ku_test() {
  kuvm *__nonnull vm;

  TEST_EXPRESSION(NO_REG_LIBS, "-1+4", "3", NUM_VAL(-1+4));

  TEST_SCRIPT(NO_REG_LIBS, "-1+4 ret",
              "let x = -1+4;"
              "ASSERT_EQ(3, x);");

  TEST_EXPRESSION(NO_REG_LIBS, "(-(1+2)-4)*5/6", "(-(1+2)-4)*5/6", NUM_VAL((-(1.0+2.0)-4.0)*5.0/6.0));

  TEST_EXPRESSION(NO_REG_LIBS, "1+2", "3", NUM_VAL(3));

  vm = kut_new(false);
  ku_lexinit(vm, "let x = 12+3;");
  ku_lexdump(vm);
  kut_free(vm);

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "invalid syntax trailing plus", "12+;");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "invalid syntax dollar sign", "let x = $2;");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "invalid syntax numeric varname", "2 = 14;");

  TEST_EXPRESSION(NO_REG_LIBS, "(1+2)*3", "9", NUM_VAL(9));

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

  TEST_EXPRESSION(NO_REG_LIBS, "-2*3", "-6", NUM_VAL(-6));

  // unterminated string
  vm = kut_new(false);
  ku_lexinit(vm, "\"hello");
  ku_lexdump(vm);
  kut_free(vm);

  // ku_print_val
  vm = kut_new(false);
  TEST_SCRIPT_WITH_VM(vm, "ku_print_val ret",
                      "let x = 2+3;"
                      "ASSERT_EQ(5, x);");
  kuval v = ku_get_global(vm, "x");
  ku_printval(vm, v);
  kut_free(vm);

  TEST_EXPRESSION(NO_REG_LIBS, "12.3", "12.3", NUM_VAL(12.3));

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

  TEST_EXPRESSION(NO_REG_LIBS, "(12-2)/5", "2", NUM_VAL(2));

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "negate err", "-true");

  TEST_EXPRESSION(NO_REG_LIBS, "true", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "false", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "null", "null", NULL_VAL);

  TEST_EXPRESSION(NO_REG_LIBS, "!true", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "!false", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "1===1", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "1===false", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "1===2", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "1!==2", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "1!==1", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "1<1", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "1<2", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "\"abc\" < \"def\"", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "\"abcd\" < \"abc\"", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "\"abc\" > \"def\"", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "\"abcd\" > \"abc\"", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "2<=1", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "2<=3", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "3>2", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "3>7", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "3>=7", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "3>=3", "true", BOOL_VAL(true));

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "< num expected", "let x = 12 < true;");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "add num expected", "let x = 12 + true;");

  vm = kut_new(false);
  TEST_SCRIPT_WITH_VM(vm, "stradd",
                      "let x = \"hello \" + \"world\";");
  v = ku_get_global(vm, "x");
  EXPECT_INT(vm, IS_OBJ(v), true, "stradd type obj");
  EXPECT_INT(vm, AS_OBJ(v)->type, OBJ_STR, "stradd obj is str");
  char *__nonnull chars = AS_CSTR(v);
  EXPECT_INT(vm, strcmp(chars, "hello world"), 0, "str val");
  kut_free(vm);

  TEST_EXPRESSION(NO_REG_LIBS, "\"hello\" === \"world\"", "false", BOOL_VAL(false));

  TEST_EXPRESSION(NO_REG_LIBS, "\"hello\" === \"hello\"", "true", BOOL_VAL(true));

  TEST_EXPRESSION(NO_REG_LIBS, "\"hello\" !== \"world\"", "true", BOOL_VAL(true));

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

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "undeclard global assign", "x = 20;");

  TEST_SCRIPT(NO_REG_LIBS, "global init & set",
              "let x = 20;"
              "ASSERT_EQ(20, x);"
              "x = 30;"
              "ASSERT_EQ(30, x);");

  TEST_SCRIPT(NO_REG_LIBS, "local init",
              "let x = 20;"
              "let y = 0;"
              "{"
              "  let a=x*20;"
              "  y = a;"
              "}"
              "ASSERT_EQ(400, y);");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "const double decl", "const x=1; const y=2; const x=3;");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "lone semicolon", ";");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "const double init", "const x=1; x=3;");

  TEST_SCRIPT(NO_REG_LIBS, "local const init",
              "const x=20;"
              "let y = 0;"
              "{"
              "  const a=x*20;"
              "  y = a;"
              "}");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "local const late assign", "{ const a=20; a = 7; }");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "local own init", "{ let a = a; }");

  TEST_SCRIPT(NO_REG_LIBS, "if true",
              "let x = 10;"
              "if (true) {"
              "  x = 30;"
              "}"
              "ASSERT_EQ(30, x);");

  TEST_SCRIPT(NO_REG_LIBS, "if false",
              "let x = 10; if (false) { x = 30; } ASSERT_EQ(10, x);");

  TEST_SCRIPT(NO_REG_LIBS, "else",
              "let x = 10; if (false) x = 30;  else x = 20; ASSERT_EQ(20, x);");

  TEST_SCRIPT(REG_LIBS, "if print",
              "if (true) { printf(222); }");

  TEST_SYNTAX_FAILURE(REG_LIBS, "if no (", "if true) { printf(222); }");

  TEST_SYNTAX_FAILURE(REG_LIBS, "if no )", "if (true { printf(222); }");

  TEST_SCRIPT(NO_REG_LIBS, "false && true",
              "let x = false && true;"
              "ASSERT_EQ(x, false);");

  TEST_SCRIPT(NO_REG_LIBS, "false && false",
              "let x = false && false;"
              "ASSERT_EQ(x, false);");

  TEST_SCRIPT(NO_REG_LIBS, "true && false",
              "let x = true && false;"
              "ASSERT_EQ(x, false);");

  TEST_SCRIPT(NO_REG_LIBS, "true && true",
              "let x = true && true;"
              "ASSERT_EQ(true, x);");

  TEST_SCRIPT(NO_REG_LIBS, "false || true",
              "let x = false || true;"
              "ASSERT_EQ(true, x);");

  TEST_SCRIPT(NO_REG_LIBS, "false || false",
              "let x = false || false;"
              "ASSERT_EQ(x, false);");

  TEST_SCRIPT(NO_REG_LIBS, "true || false",
              "let x = true || false;"
              "ASSERT_EQ(true, x);");

  TEST_SCRIPT(NO_REG_LIBS, "true || true",
              "let x = true || true;"
              "ASSERT_EQ(true, x);");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "while no lpar", "let x = 1; while x < 20) { x = x + 1; }");

  TEST_SCRIPT(NO_REG_LIBS, "while parse",
              "let x = 1;"
              "while(x < 20) {"
              "  x = x + 1;"
              "}"
              "ASSERT_EQ(20, x);");

  TEST_SCRIPT(NO_REG_LIBS, "for parse",
              "let x = 0;"
              "for(let j=0; j < 10; j=j+1) x = j;"
              "ASSERT_EQ(9, x);");

  vm = kut_new(true);
  vm->flags = 0;
  TEST_SCRIPT_WITH_VM(vm, "for no init",
                      "let x = 0; for(; x < 10; x=x+1) {printf(x); printf(\"\\n\");}"
                      "ASSERT_EQ(10, x);");
  kut_free(vm);

  TEST_SCRIPT(NO_REG_LIBS, "for no inc",
              "let x = 0;"
              "for(; x < 10; ) x = x+1;"
              "ASSERT_EQ(10, x);");

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "function def",
                      "function foo(a) { print(\"ok\"); }");
  v = ku_get_global(vm, "foo");
  EXPECT_INT(vm, IS_OBJ(v),true, "function object");
  kut_free(vm);

  vm = kut_new(true);
  vm->max_params = 1;

  TEST_SYNTAX_FAILURE_WITH_VM(vm, "too many params", "function foo(a,b) { printf(555); }; foo(4,5,6);");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "function call def",
              "function foo(a,b) { printf(2); }");

  TEST_RUNTIME_FAILURE_WITH_VM(vm, "function call mismatch", "foo(1,2,3);");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "non-function call", "a=7; a();");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "too many args print", "function a() { b(); }\nfunction b() { b(12); }\na();");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "return from __main__", "return 2;");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "parse_skip return", "let x=2 return function foo()");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "parse_skip function", "let x=2 function foo()");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "parse_skip class", "let x=2 class foo()");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "parse_skip let", "let x=2 let foo()");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "parse_skip for", "let x=2 for foo()");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "parse_skip if", "let x=2 if foo()");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "parse_skip while", "let x=2 while foo()");

  TEST_SCRIPT(NO_REG_LIBS, "return expr val",
              "function f(a) { return a*2; }\nlet x = f(3);"
              "ASSERT_EQ(6, x);");

  TEST_SCRIPT(NO_REG_LIBS, "implicit return val",
              "function f(a) { let z = 2; }\nlet x = f(3);"
              "ASSERT_NULL(x);");

  vm = kut_new(false);
  ku_cfuncdef(vm, "nadd", kutest_native_add);
  TEST_SCRIPT_WITH_VM(vm, "cfunction return",
                      "let x = nadd(3,4);"
                      "ASSERT_EQ(7, x);");
  kut_free(vm);

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "clock res",
                      "let x = clock();");
  v = ku_get_global(vm, "x");
  EXPECT_INT(vm, IS_NUM(v), true, "clock return");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "printf res",
              "printf(12);");

  TEST_SCRIPT(NO_REG_LIBS, "closure val",
              "function M(x) { let m = x; function e(n) { return m*n; } return e; }\n let z = M(5); let x = z(3);"
              "ASSERT_EQ(15, x);");

  TEST_SCRIPT(NO_REG_LIBS, "closure2 val",
              "function o() { let a=7; let b=8; function i() { return a+b; } return i; }\n let z = o(); let x = z();"
              "ASSERT_EQ(15, x);");

  TEST_SCRIPT(NO_REG_LIBS, "closure3 val",
              "function f1(){let a1=1; function f2() {let a2=2; function f3(){ return a1+a2; } return f3; } return f2; }\n let v1=f1(); let v2=v1(); let v3=v2();"
              "ASSERT_EQ(3, v3);");

  TEST_SCRIPT(NO_REG_LIBS, "closure4 val",
              "function M(x) { let m = x; function e() { return m*m; } return e; }\n let z = M(5); let x = z();"
              "ASSERT_EQ(25, x);");

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  TEST_SCRIPT_WITH_VM(vm, "gc",
                      "let x = \"hello\"; x=null;");
  ku_gc(vm);
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "gc val");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCLOG;
  vm->gcnext = 0;
  TEST_SCRIPT_WITH_VM(vm, "gcnext val",
                      "let x = \"hello\"; x=null;"
                      "ASSERT_NULL(x);");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "printf null",
              "printf(null);");

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  TEST_SCRIPT_WITH_VM(vm, "gc closure",
                      "function M(x) { let m = x; function e() { return m*m; } return e; }\n"
                      "let z = M(5); let x = z(); x = null;");
  ku_gc(vm);
  EXPECT_VAL(vm, ku_get_global(vm, "x"), NULL_VAL, "gc closure val");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  TEST_SCRIPT_WITH_VM(vm, "gc closure2 val",
                      "function M(x) { let m = x; let mm=x*2; function e(n) { return m*n*mm; } return e; }\n let z = M(5); let x = z(3); x = null;"
                      "ASSERT_NULL(x);");
  kut_free(vm);

  vm = kut_new(false);
  TEST_SCRIPT_WITH_VM(vm, "string intern1",
                      "let x = \"hello \" + \"world\";");
  int sc = kut_table_count(vm, &vm->strings);
  TEST_SCRIPT_WITH_VM(vm, "string intern2",
                      "let y = \"hello\" + \" world\";");
  // +1 for the y value, +2 for two different substrings
  EXPECT_INT(vm, kut_table_count(vm, &vm->strings), sc+3, "string intern");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "class decl",
              "class Foo {}\n printf(Foo);");

  TEST_SCRIPT(REG_LIBS, "class cons",
              "class Foo {}\n let f = new Foo(); printf(f);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "non-class parameter to new", "function Foo() {}\n let f = new Foo();");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "non-instance setprop", "let c = 7; c.p = 9;");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "non-instance getprop", "let c = 7; let x = c.p;");

  TEST_SCRIPT(NO_REG_LIBS, "set/get prop ret",
              "class C{}\n let c = new C(); c.p = 9; let x = c.p;"
              "ASSERT_EQ(9, x);");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "set/get prop not found", "class C{}\n let c = new C(); c.p=9; let x=c.z;");

  TEST_SCRIPT(NO_REG_LIBS, "bound method ret",
              "let x=1;\n class C { M() { x=3; } }\n let c = new C();\n let m = c.M;\n m();"
              "ASSERT_EQ(3, x);");

  TEST_SCRIPT(NO_REG_LIBS, "this ret",
              "let x=1; class C{ M() { this.z=3; } }\n let c = new C(); c.M(); x=c.z;"
              "ASSERT_EQ(3, x);");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "global this res", "let x = this;");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "no ctor with args", "class C {}\n let c = new C(12,14);");

  TEST_SCRIPT(NO_REG_LIBS, "ctor args ret",
              "class C { constructor(x) { this.x = x; }}\n let c = new C(12); let x = c.x;"
              "ASSERT_EQ(12, x);");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "ctor return res", "class C { constructor(x) { this.x = x; return 7; }}\n let c = new C(12); let x = c.x;");

  TEST_SCRIPT(NO_REG_LIBS, "field invoke ret",
              "let x=1; class C { constructor() { function f() { x=8; } this.f = f; } }\n let c = new C(); c.f();"
              "ASSERT_EQ(8, x);");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "class ownsubclass res", "class A extends A {}");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "class bad inherit static res", "class A extends 12 {}");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "class bad inherit run res", "let B = 9; class A extends B {}");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "global super res", "let x = super.foo();");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "no superclass super res", "class A { f() { let x = super.foo(); } }");

  TEST_SCRIPT(NO_REG_LIBS, "super ret",
              "let x=0; class A { f() { x=2; } }\n class B extends A {}\n let b = new B(); b.f();"
              "ASSERT_EQ(2, x);");

  vm = kut_new(false);
  vm->flags = 0;
  TEST_SCRIPT_WITH_VM(vm, "super call ret",
                      "class A { f() { return 2; } }\n class B extends A { f() { let z = super.f; return z()*3; }}\n let b = new B(); let x = b.f();"
                      "ASSERT_EQ(6, x);");
  kut_free(vm);

  TEST_SCRIPT(NO_REG_LIBS, "super invoke ret",
              "class A { f() { return 2; } }\nclass B extends A { f() { return super.f()*3; }}\n let b = new B(); let x = b.f();"
              "ASSERT_EQ(6, x);");

  vm = kut_new(false);
  vm->max_const = 1;
  TEST_SYNTAX_FAILURE_WITH_VM(vm, "too many const", "let x=1; let y=2;");
  kut_free(vm);

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "invalid assign", "function=7; ");

  TEST_SCRIPT(NO_REG_LIBS, "simple ret",
              "function f() { return; }");

  vm = kut_new(false);
  vm->max_closures = 1;
  TEST_RUNTIME_FAILURE_WITH_VM(vm, "too many closures res", "function O() { let a=1; let b=2; function I() { return a*b; } return e; }\n let z=M(); let x=z();");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_jump = 1;
  TEST_SYNTAX_FAILURE_WITH_VM(vm, "max jump", "let x = 0; for(let j=0; j < 10; j=j+1) x = j;");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_body = 1;
  TEST_SYNTAX_FAILURE_WITH_VM(vm, "max body", "let x = 0; for(let j=0; j < 10; j=j+1) x = j;");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_frames = 1;
  TEST_RUNTIME_FAILURE_WITH_VM(vm, "stack overflow", "function a(){} function b(){a();} function c(){b();} c();");
  kut_free(vm);

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "invoke invalid prop", "class A{}\n let a = new A(); a.x();");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "invoke invalid receiver", "class A{}\n let a = 7; a.x();");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "invoke invalid super", "class A {} class B extends A { f() { super.x(); }} let b = new B(); b.f();");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "negate non-number", "let x=true; let y=-x;");

  TEST_SCRIPT(NO_REG_LIBS, "closure set ret",
              "function O() { let a=1; let b=2; function e() { a=9; return a*b; } return e; }\n let z=O(); let x=z();"
              "ASSERT_EQ(18, x);");

  TEST_RUNTIME_FAILURE(NO_REG_LIBS, "invalid get super", "class A {} class B extends A { f() { let m = super.x; }} let b = new B(); b.f();");

  vm = kut_new(false);
  vm->flags = KVM_F_NOEXEC;
  TEST_SCRIPT_WITH_VM(vm, "noexec",
                      "let x=9;");
  kut_free(vm);

  vm = kut_new(false);
  vm->max_locals = 1;
  TEST_SYNTAX_FAILURE_WITH_VM(vm, "max_locals", "function f() { let x=9; let m=2; }");
  kut_free(vm);

  vm = kut_new(false);
  vm->flags = KVM_F_GCSTRESS | KVM_F_GCLOG;
  TEST_SCRIPT_WITH_VM(vm, "gc class",
                      "class A{ f(){}} let a = new A(); let z = a.f; a = null;");
  ku_gc(vm);
  EXPECT_VAL(vm, ku_get_global(vm, "a"), NULL_VAL, "gc class val");
  kut_free(vm);

  TEST_SCRIPT(NO_REG_LIBS, "anonymous function ret",
              "let f=function(a) {return a*2;}; let x=f(3);"
              "ASSERT_EQ(6, x);");

  TEST_SCRIPT(NO_REG_LIBS, "function arg ret",
              "function f(x) { return x(7);} let x=f(function(a) { return a*2; });"
              "ASSERT_EQ(14, x);");

  ku_lexinit(vm, "let x = 12+3;\nlet m=2;\nlet mm=99;");
  ku_lexdump(vm);

  TEST_SCRIPT(NO_REG_LIBS, "lambda ret",
              "let f = a => a*2; let x=f(3);"
              "ASSERT_EQ(6, x);");

  TEST_SCRIPT(NO_REG_LIBS, "lambda arg ret",
              "function f(x) { return x(2); } let x = f(a => a*3);"
              "ASSERT_EQ(6, x);");

  TEST_SCRIPT(NO_REG_LIBS, "lambda (args) ret",
              "let f = (a,b) => a*b; let x=f(3,4);"
              "ASSERT_EQ(12, x);");


  TEST_SCRIPT(NO_REG_LIBS, "lambda args body ret",
              "let max = (a,b) => { if (a>b) return a; else return b; }; let x=max(3,14);"
              "ASSERT_EQ(14, x);");

  TEST_SCRIPT(NO_REG_LIBS, "lambda body ret",
              "let abs = a => { if (a<0) return -a; else return a; }; let x=abs(-12);"
              "ASSERT_EQ(12, x);");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "global break", "let x =3; break;");

  TEST_SYNTAX_FAILURE(NO_REG_LIBS, "global continue", "let x =3; continue;");

  TEST_SCRIPT(NO_REG_LIBS, "while break ret",
              "let x=0; while(x < 5) { if (x > 2) break; x=x+1; }"
              "ASSERT_EQ(3, x);");

  // TODO(122): For loop break is broken.
//  TEST_SCRIPT(NO_REG_LIBS, "for break ret",
//              "let x = 0; "
//              "for(let i=0; i<10; i=i+1) {"
//              "   if (i > 2) break;"
//              "   x = i; "
//              "} "
//              "ASSERT_EQ(2, x);");

  TEST_SCRIPT(NO_REG_LIBS, "while continue ret",
              "let x=0;"
              "let y=0;"
              "while(x < 5) {"
              "  x=x+1;"
              "  if (x > 2) continue;"
              "  y=x;"
              "}"
              "ASSERT_EQ(2, y);");

  TEST_SCRIPT(NO_REG_LIBS, "for continue ret",
              "let y=0; for(let x=0; x<5; x=x+1) { if (x > 2) continue; y=x; }"
              "ASSERT_EQ(2, y);");

  TEST_SCRIPT(REG_LIBS, "string.count ret",
              "let y=\"12\"; let x=y.count;"
              "ASSERT_EQ(2, x);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.count null", "let x=v.count;");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.count non num", "let x=12; let y=x.count;");

  TEST_SCRIPT(REG_LIBS, "strlit escape ret",
              "let y=\"12\\n\\r\\t\"; let x=y.count;"
              "ASSERT_EQ(5, x);");

  TEST_EXPRESSION(REG_LIBS, "1.2e3", "1.2e3", NUM_VAL(1.2e3));

  TEST_EXPRESSION(REG_LIBS, "0xcafeb10b", "0xcafeb10b", NUM_VAL(0xcafeb10b));

  vm = kut_new(false);
  tclass_init(vm, 0);
  TEST_SCRIPT_WITH_VM(vm, "class res",
                      "let x=test;");
  EXPECT_TRUE(vm, IS_CCLASS(ku_get_global(vm, "x")), "class ret");
  kut_free(vm);
  EXPECT_INT(vm, tclass_sfree, 0, "class no sfree");

  vm = kut_new(false);
  tclass_init(vm, SFREE);
  TEST_SCRIPT_WITH_VM(vm, "class sfree",
                      "let x=test;");
  EXPECT_TRUE(vm, IS_CCLASS(ku_get_global(vm, "x")), "class ret");
  kut_free(vm);
  EXPECT_INT(vm, tclass_sfree, 1, "class sfree");

  vm = kut_new(false);
  tclass_init(vm, 0);
  TEST_RUNTIME_FAILURE_WITH_VM(vm, "class no sget res", "let x=test.prop;");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SGET);
  TEST_SCRIPT_WITH_VM(vm, "class sget",
                      "let x=test.prop;");
  EXPECT_INT(vm, tclass_sget, 1, "class sget ret");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SPUT);
  TEST_SCRIPT_WITH_VM(vm, "class sput",
                      "test.prop=8;");
  EXPECT_INT(vm, tclass_sput, 8, "class sput ret");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, 0);
  TEST_RUNTIME_FAILURE_WITH_VM(vm, "class no scall res", "test.method(5,2,1);");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SCALL);
  TEST_SCRIPT_WITH_VM(vm, "class scall",
                      "test.method(5,2,1);");
  EXPECT_INT(vm, tclass_scall, 3, "class scall ret");
  kut_free(vm);

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "Math.sin res",
                      "let x = Math.sin(Math.PI);");
  EXPECT_TRUE(vm, APPROX(ku_get_global(vm, "x"), 0), "Math.sin ret");
  kut_free(vm);

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "Math.cos res",
                      "let x = Math.cos(Math.PI);");
  EXPECT_TRUE(vm, APPROX(ku_get_global(vm, "x"), -1), "Math.cos ret");
  kut_free(vm);

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "Math.tan res",
                      "let x = Math.tan(Math.PI/4);");
  EXPECT_TRUE(vm, APPROX(ku_get_global(vm, "x"), 1), "Math.tan ret");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, SMARK);
  TEST_SCRIPT_WITH_VM(vm, "class smark",
                      "let x=test;");
  ku_gc(vm);
  EXPECT_INT(vm, tclass_smark, 1, "class smark false");
  TEST_SCRIPT_WITH_VM(vm, "class smark 2",
                      "x=null;");
  ku_gc(vm);
  EXPECT_TRUE(vm, tclass_smark > 1, "class smark false");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, 0);
  TEST_RUNTIME_FAILURE_WITH_VM(vm, "class no cons", "let x = test(4);");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE);
  TEST_SCRIPT_WITH_VM(vm, "class cons",
                      "let x = test(4);");
  EXPECT_TRUE(vm, IS_CINST(ku_get_global(vm, "x")), "class cons ret");
  kut_free(vm);
  EXPECT_INT(vm, tclass_ifree, 1, "class ifree");

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE | IMARK);
  vm->flags |= KVM_F_GCSTRESS;
  TEST_SCRIPT_WITH_VM(vm, "class imark",
                      "let x=test(4);");
  ku_gc(vm);
  EXPECT_TRUE(vm, tclass_sfree == 0, "class sfree");
  EXPECT_TRUE(vm, tclass_imark > 0, "class imark");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE | IGET);
  TEST_SCRIPT_WITH_VM(vm, "iget res",
                      "let x=test(4); let y=x.prop;");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(4), "iget");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE );
  TEST_RUNTIME_FAILURE_WITH_VM(vm, "no iput res", "let x=test(4); x.prop=9;");
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE | IPUT | IGET);
  TEST_SCRIPT_WITH_VM(vm, "iput res",
                      "let x=test(4); x.prop=9; let y=x.prop;");
  EXPECT_VAL(vm, ku_get_global(vm, "y"), NUM_VAL(9), "iput");
  kut_free(vm);

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "string.format res",
                      "let x=string.format(\"ABC %g,%s,%b,%b\",123.45,\"FF\", true, false);");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_STR(v), "string.format ret");
  kustr *__nonnull str = AS_STR(v);
  EXPECT_INT(vm, strcmp(str->chars, "ABC 123.45,FF,true,false"), 0, "string.format value");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "string.format(%f) ret",
              "let x=string.format(\"%f\", 123.456);"
              "ASSERT_EQ(\"123.456000\", x);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.format(%f) invalid res", "let x=string.format(\"%f\", true);");

  TEST_SCRIPT(REG_LIBS, "string.format %x",
              "let x=string.format(\"%x\",0xfb1cfd);"
              "ASSERT_EQ(\"fb1cfd\", x);");

  TEST_SCRIPT(REG_LIBS, "string.format %d",
              "let x=string.format(\"%d\",123.45);"
              "ASSERT_EQ(\"123\", x);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.format bool to %d", "let x=string.format(\"%d\",true);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.format bool to %s", "let x=string.format(\"%s\",true);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.format bool to %g", "let x=string.format(\"%g\",true);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.format bool to %x", "let x=string.format(\"%x\",true);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.format num to %b", "let x=string.format(\"%b\",12);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "string.format trailing %", "let x=string.format(\"hello%\",12);");

  TEST_SCRIPT(REG_LIBS, "string.format ret",
              "let x=string.format(12);"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "string.bogus num res",
              "let x=string.bogus(12);"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "s.bogus ret",
              "let s=\"123\"; let x=s.bogus;"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "Math.bogus scall res",
              "let x = Math.bogus(12);"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "Math.bogus sget res",
              "let x = Math.bogus;"
              "ASSERT_NULL(x);");

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
  TEST_SCRIPT_WITH_VM(vm, "tclass_init",
                      "let x=test(4);");
  v = ku_get_global(vm, "x");
  ku_printval(vm, v);
  kut_free(vm);

  vm = kut_new(false);
  tclass_init(vm, CONS | IFREE );
  TEST_SCRIPT_WITH_VM(vm, "let + string",
                      "let z=\"he\"; let x=z+\"llo\";");
  v = ku_get_global(vm, "x");
  str = AS_STR(v);
  EXPECT_INT(vm, strcmp(str->chars, "hello"), 0, "let + string ret");
  kut_free(vm);

  TEST_EXPRESSION(REG_LIBS, "0xCaFeb10B", "0xCaFeb10B", NUM_VAL(0xcafeb10b));

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

  TEST_SYNTAX_FAILURE(REG_LIBS, "array missing ']'", "let x=[1,2,3;");

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "array init",
                      "let x=[1,2,3];");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array type");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 0), NUM_VAL(1), "array[0]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 1), NUM_VAL(2), "array[1]");
  EXPECT_VAL(vm, ku_arrget(vm, AS_ARRAY(v), 2), NUM_VAL(3), "array[2]");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "mix array res",
              "let x=[1,\"hello\",3,4];"
              "ASSERT_EQ(1, x[0]);"
              "ASSERT_EQ(\"hello\", x[1]);"
              "ASSERT_EQ(3, x[2]);"
              "ASSERT_EQ(4, x[3]);");

  TEST_SCRIPT(REG_LIBS, "array get res",
              "let a=[1,3,4]; let x=a[1];"
              "ASSERT_EQ(3, x);");

  TEST_SYNTAX_FAILURE(REG_LIBS, "array get ']'", "let a=[1,3,4]; let x=a[1;");

  TEST_SCRIPT(REG_LIBS, "array set res",
              "let a=[1,3,4]; a[1]=9; let x=a[1];"
              "ASSERT_EQ(9, x);");

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "array cons res",
                      "let a=array(7);");
  v = ku_get_global(vm, "a");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array cons type");
  ao = AS_ARRAY(v);
  EXPECT_INT(vm, ao->elements.capacity, 7, "array cons cap");
  kut_free(vm);

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "array cons(0) res",
                      "let a=array(0);");
  v = ku_get_global(vm, "a");
  EXPECT_TRUE(vm, IS_ARRAY(v), "array cons(0) type");
  ao = AS_ARRAY(v);
  EXPECT_INT(vm, ao->elements.capacity, 0, "array cons(0) cap");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "array count res",
              "let a=[1,3,4]; let x=a.count;"
              "ASSERT_EQ(3, x);");

  TEST_SCRIPT(REG_LIBS, "array.map res",
              "let a=[1,3,4];"
              "let x=a.map(e => e*2);"
              "ASSERT_EQ(2, x[0]);"
              "ASSERT_EQ(6, x[1]);"
              "ASSERT_EQ(8, x[2]);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.map null", "let a=[1,3,4]; let x=a.map();");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.map num", "let a=[1,3,4]; let x=a.map(9);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.map args", "let a=[1,3,4]; let x=a.map((e, b) => e*2 );");

  TEST_SCRIPT(REG_LIBS, "array.reduce res",
              "let sum=[1,3,4].reduce(0, (v,e) => v+e);"
              "ASSERT_EQ(8, sum);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.reduce bad fn", "let sum=[1,3,4].reduce(0, (v,e) => bad());");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.reduce num", "let sum=[1,3,4].reduce(9);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.reduce null", "let sum=[1,3,4].reduce();");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.reduce 1 arg", "let sum=[1,3,4].reduce(0, v => v*2);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.invoke bogus",

                       "let a=[1,3,4]; let x =a.bogus();"
                       "ASSERT_NULL(x);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.map arg err res", "let a=[1,3,4]; let x=a.map(e => f(e));");

  TEST_SCRIPT(REG_LIBS, "1.23e-2",
              "let x=1.23e-2;"
              "ASSERT_EQ(1.23e-2, x);");

  TEST_SCRIPT(REG_LIBS, "two break",
              "for(let i=0; i<10; i=i+1) { if (i>2) break; if (i>3) break; }");

  vm = kut_new(true);
  vm->max_patches = 1;
  TEST_SYNTAX_FAILURE_WITH_VM(vm, "patch limit", "for(let i=0; i<10; i=i+1) { if (i>2) break; if (i>3) break; }");
  kut_free(vm);

  vm = kut_new(true);
  vm->flags |= KVM_F_GCSTRESS;
  TEST_SCRIPT_WITH_VM(vm, "table ret",
                      "let t=table(); t.x=1; t.y=2; let x=t.x+t.y;"
                      "ASSERT_EQ(3, x);");

  TEST_SCRIPT(REG_LIBS, "table res",
              "let t=table(); t.x=1; t.y=2; let x=t.z;"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "table set get ret",
              "let t=table(); t.set(\"x\",1); let x=t.get(\"x\");"
              "ASSERT_EQ(1, x);");

  TEST_SCRIPT(REG_LIBS, "table set(null)",
              "let t=table(); t.set(1,1); let x=t.get(\"x\");"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "table get(null)",
              "let t=table(); t.set(\"x\",1); let x=t.get(1);"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "table bogus res",
              "let t=table(); let x = t.bogus();"
              "ASSERT_NULL(x);");

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "table iter res",
                      "let t=table(); t.x=1; t.y=2; t.z=3; let K=\"\"; let V=0; t.iter( (k,v) => { K=K+k; V=V+v; } );");
  v = ku_get_global(vm, "K");
  EXPECT_TRUE(vm, IS_STR(v), "table iter K type");
  // this is fragile depends on hashing and key insertion order
  EXPECT_INT(vm, strcmp(AS_STR(v)->chars, "yzx"), 0, "table iter K");
  EXPECT_VAL(vm, ku_get_global(vm, "V"), NUM_VAL(6), "table iter V");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "table iter argc res",
              "let t=table(); t.iter(k => k);");

  TEST_SCRIPT(REG_LIBS, "table iter null res",
              "let t=table(); t.iter();");

  TEST_SCRIPT(REG_LIBS, "table iter num res",
              "let t=table(); t.iter(12);");

  TEST_SCRIPT(REG_LIBS, "imod res",
              "let x = Math.imod(9,5);"
              "ASSERT_EQ(4, x);");

  TEST_SCRIPT(REG_LIBS, "sqrt res",
              "let x = Math.sqrt(4);"
              "ASSERT_EQ(2, x);");

  TEST_SCRIPT(REG_LIBS, "pow res",
              "let x = Math.pow(3,2);"
              "ASSERT_EQ(9, x);");

  TEST_SCRIPT(REG_LIBS, "int res",
              "let x = int(3.2);"
              "ASSERT_EQ(3, x);");

  TEST_SCRIPT(REG_LIBS, "int(bool) ret",
              "let x = int(true);"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "parseFloat ret",
              "let x = parseFloat(\"3.2\");"
              "ASSERT_EQ(3.2, x);");

  TEST_SCRIPT(REG_LIBS, "parseFloat(bool) ret",
              "let x = parseFloat(true);"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "bit || res",
              "let x = 0xf7f1 | 0x9854;"
              "ASSERT_EQ(65525, x);");

  TEST_SCRIPT(REG_LIBS, "bit && res",
              "let x = 0xf7f1 & 0x9854;"
              "ASSERT_EQ(36944, x);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "bit || invalid res", "let x = 0xf7f1 | true;");

  TEST_RUNTIME_FAILURE(REG_LIBS, "bit && invalid res", "let x = 0xf7f1 & true;");

  vm = kut_new(true);
  v = ku_cinstance(vm, "table");
  EXPECT_TRUE(vm, AS_CINST(v)->klass->iget == table_iget, "table new");
  kut_free(vm);

  vm = kut_new(true);
  ku_exec(vm, "let t=0;");
  ku_exec(vm, "let samples=array(6);");
  ku_exec(vm, "samples[0] = (1 + Math.sin(t*1.2345 + Math.cos(t*0.33457)*0.44))*0.5;");
  TEST_SCRIPT_WITH_VM(vm, "array index crash res",
                      "let x = samples[0];");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "multi-let res",
              "let x=7, y=9;"
              "ASSERT_EQ(7, x);"
              "ASSERT_EQ(9, y);");

  TEST_SCRIPT(REG_LIBS, "multi-let null res",
              "let x,y;"
              "ASSERT_NULL(x);"
              "ASSERT_NULL(y);");

  TEST_EXPRESSION(REG_LIBS, "2 << 4", "32", NUM_VAL(2 << 4));

  TEST_RUNTIME_FAILURE(REG_LIBS, "shl arg res", "let x = 2 << true;");

  TEST_EXPRESSION(REG_LIBS, "16 >> 2", "4", NUM_VAL(16 >> 2));

  TEST_RUNTIME_FAILURE(REG_LIBS, "shr arg res", "let x = true >> 2;");

  TEST_SCRIPT(REG_LIBS, "string.frombytes res",
              "let x = string.frombytes([65,66,67]);"
              "ASSERT_EQ(\"ABC\", x);");

  TEST_SCRIPT(REG_LIBS, "arr change res",
              "let a=[1,2,3]; a[0]=4; let x=a.count;"
              "ASSERT_EQ(3, x);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "non-array set res", "let a=true; a[0]=4; ");

  TEST_RUNTIME_FAILURE(REG_LIBS, "non-array get res", "let a=true; let x = a[0]; ");

  TEST_SCRIPT(REG_LIBS, "arr nogc res",
              "function F() { let a=array(2); a[0]=1; a[1]=2; a[2]=3; return a[2]; } let x=F();"
              "ASSERT_EQ(3, x);");

  vm = kut_new(true);
  vm->flags = KVM_F_GCSTRESS;
  TEST_SCRIPT_WITH_VM(vm, "arr gc ret",
                      "function F() { let a=array(2); a[0]=1; a[1]=2; a[2]=3; return a[2]; } let x=F();"
                      "ASSERT_EQ(3, x);");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "arr empty first res",
              "let a = []; let x = a.first;"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "arr first res",
              "let a = [12,3]; let x = a.first;"
              "ASSERT_EQ(12, x);");

  TEST_SCRIPT(REG_LIBS, "arr empty last res",
              "let a = []; let x = a.last;"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "arr last res",
              "let a = [12,3]; let x = a.last;"
              "ASSERT_EQ(3, x);");

  TEST_SCRIPT(REG_LIBS, "tertiary res",
              "let x=2; let a = (x > 2) && 7 || 6;"
              "ASSERT_EQ(6, a);");

  TEST_SCRIPT(REG_LIBS, "array.filter res",
              "let a=[1,3,4]; let x=a.filter(e => e > 1);"
              "ASSERT_EQ(3, x[0]);"
              "ASSERT_EQ(4, x[1]);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.filter null", "let a=[1,3,4]; let x=a.filter();");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.filter num", "let a=[1,3,4]; let x=a.filter(9);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.filter args", "let a=[1,3,4]; let x=a.filter((e, b) => e*2 );");

  TEST_SCRIPT(REG_LIBS, "array.sort res",
              "let x=[7,3,4];"
              "x.sort((a,b) => a-b );"
              "ASSERT_EQ(3, x.count);"
              "ASSERT_EQ(3, x[0]);"
              "ASSERT_EQ(4, x[1]);"
              "ASSERT_EQ(7, x[2]);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.sort argc res", "let x=[7,3,4]; x.sort(a => 0);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.sort null res", "let x=[7,3,4]; x.sort();");

  TEST_RUNTIME_FAILURE(REG_LIBS, "array.sort res", "let x=[7,3,4]; x.sort((a,b) => false);");

  vm = kut_new(true);
  TEST_SCRIPT_WITH_VM(vm, "table lit res",
                      "let x={\"a\"=1, \"b\"=2}; let z=x.a; let w=x.b;");
  v = ku_get_global(vm, "x");
  EXPECT_TRUE(vm, IS_CINST(v), "table instance");
  EXPECT_VAL(vm, ku_get_global(vm, "z"), NUM_VAL(1), "table lit prop1");
  EXPECT_VAL(vm, ku_get_global(vm, "w"), NUM_VAL(2), "table lit prop2");
  kut_free(vm);

  TEST_SCRIPT(REG_LIBS, "tertiary comp res",
              "let comp = (a, b) => ( (a < b) && -1 || ( (a > b) && 1 || 0));"
              "let x = comp(9, 7);"
              "ASSERT_EQ(1, x);");

  TEST_SCRIPT(REG_LIBS, "db comp res",
              "let comp = (a, b) => ( (a < b) && -1 || ( (a > b) && 1 || 0)) ;"
              "let db = [ { \"name\" = \"mohsen\", \"age\"=55 }, { \"name\" = \"josh\", \"age\"=40 }];"
              "db.sort((a,b) => comp(a.name, b.name));"
              "ASSERT_EQ(2, db.count);"
              "ASSERT_EQ(\"josh\", db.first.name);");

  TEST_SCRIPT(REG_LIBS, "eval ret",
              "let x=eval(\"12/3\");"
              "ASSERT_EQ(4, x);");

  TEST_SCRIPT(REG_LIBS, "eval arg res",
              "let x=eval();"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "eval stack ret",
              "let x=eval(\"12/3\", 10);"
              "ASSERT_EQ(4, x);");

  TEST_SCRIPT(REG_LIBS, "substr() ret",
              "let s=\"012345678\"; let x=s.substr();"
              "ASSERT_EQ(\"012345678\", x);");

  TEST_SCRIPT(REG_LIBS, "substr(3) ret",
              "let s=\"012345678\"; let x=s.substr(3);"
              "ASSERT_EQ(\"345678\", x);");

  TEST_SCRIPT(REG_LIBS, "substr(3,5) ret",
              "let s=\"012345678\"; let x=s.substr(3,5);"
              "ASSERT_EQ(\"345\", x);");

  TEST_SCRIPT(REG_LIBS, "substr(-1) ret",
              "let s=\"012345678\"; let x=s.substr(-1);"
              "ASSERT_EQ(\"8\", x);");

  TEST_SCRIPT(REG_LIBS, "substr(-1) ret",
              "let s=\"012345678\"; let x=s.substr(-3,-1);"
              "ASSERT_EQ(\"678\", x);");

  TEST_SCRIPT(REG_LIBS, "substr(-12,5) ret",
              "let s=\"012345678\"; let x=s.substr(-12,55);"
              "ASSERT_EQ(\"012345678\", x);");

  TEST_SCRIPT(REG_LIBS, "substr(-12,5, true) ret",
              "let s=\"012345678\"; let x=s.substr(-12,55, true);"
              "ASSERT_EQ(\"\", x);");

  TEST_SCRIPT(REG_LIBS, "string.bogus ret",
              "let s=\"012345678\"; let x=s.bogus(-1,55);"
              "ASSERT_NULL(x);");

  TEST_SCRIPT(REG_LIBS, "str[idx] ret",
              "let s=\"012345678\"; let x=s[2];"
              "ASSERT_EQ(\"2\", x);");

  TEST_RUNTIME_FAILURE(REG_LIBS, "str[idx] bounds low res", "let s=\"012345678\"; let x=s[-1];");

  TEST_RUNTIME_FAILURE(REG_LIBS, "str[idx] bounds high res", "let s=\"012345678\"; let x=s[22];");

  TEST_RUNTIME_FAILURE(REG_LIBS, "str[bool]  res", "let s=\"012345678\"; let x=s[true];");

  TEST_EXPRESSION(REG_LIBS, "Math.fmod(9,7)", "Math.fmod(9,7)", NUM_VAL(fmod(9,7)));

  TEST_SCRIPT(REG_LIBS, "abs negative res",
              "let x = Math.abs(-7);"
              "ASSERT_EQ(-1, x);");

  TEST_SCRIPT(REG_LIBS, "abs positive res",
              "let x = Math.abs(7);"
              "ASSERT_EQ(1, x);");

  TEST_SCRIPT(REG_LIBS, "abs 0 res",
              "let x = Math.abs(0);"
              "ASSERT_EQ(0, x);");

  TEST_EXPRESSION(REG_LIBS, "Math.floor(3.767)", "Math.floor(3.767)", NUM_VAL(floor(3.767)));

  TEST_EXPRESSION(REG_LIBS, "Math.round(3.767)", "Math.round(3.767)", NUM_VAL(round(3.767)));

  debug_call_count = 0;
  vm = kut_new(true);
  vm->debugger = test_debugger_stop;
  TEST_SCRIPT_WITH_VM(vm, "debug stop",
                      "let x = 2;");
  EXPECT_INT(vm, debug_call_count, 1, "debug stop count");
  kut_free(vm);

  debug_call_count = 0;
  vm = kut_new(true);
  vm->debugger = test_debugger_cont;
  TEST_SCRIPT_WITH_VM(vm, "debug cont",
                      "let x = 2;");
  EXPECT_INT(vm, debug_call_count, 4, "debug cont count");
  kut_free(vm);

  ku_test_summary();
  return ktest_fail;
}
