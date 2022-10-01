//
//  kumain.c
//  kumu
//
//  Created by Mohsen Agsen on 12/7/21.
//

#include "kumain.h"
#include "kumu.h"

#define KU_MAXINPUT 1024

#ifdef USE_READLINE

#include <readline/readline.h>
#include <readline/history.h>
#define ku_initreadline(K)  ((void)K, rl_readline_name="kumu")
#define ku_readline(K,b,p)  ((void)K, ((b)=readline(p)) != NULL)
#define ku_saveline(K,line)  ((void)K, add_history(line))
#define ku_freeline(K,b)  ((void)K, free(b))

#else // USE_READLINE

#define ku_initreadline(K)  ((void)K)
#define ku_readline(K,b,p) \
        ((void)K, fputs(p, stdout), fflush(stdout),  /* show prompt */ \
        fgets(b, KU_MAXINPUT, stdin) != NULL)  /* get line */
#define ku_saveline(K,line)  { (void)K; (void)line; }
#define ku_freeline(K,b)  { (void)K; (void)b; }

#endif // USE_READLINE

// ********************** BEGIN DISASSEMBLY ***************************
static int ku_opdis(kuvm *__nonnull vm, const char *__nonnull name, int offset) {
  ku_printf(vm, "%-16s %4s", name, "");
  return offset + 1;
}

static int ku_constdis(kuvm *__nonnull vm, const char *__nonnull name, kuchunk *__nonnull chunk, int offset) {
  uint8_t con = chunk->code[offset+1];
  ku_printf(vm, "%-16s %4d '", name, con);
  ku_printval(vm, chunk->constants.values[con]);
  ku_printf(vm, "'");
  return offset+2;
}

static int ku_invokedis(kuvm *__nonnull vm, const char *__nonnull name, kuchunk *__nonnull chunk, int offset) {
  uint8_t cons = chunk->code[offset + 1];
  uint8_t argc = chunk->code[offset + 2];
  ku_printf(vm, "%-16s (%d args) %4d'", name, argc, cons);
  ku_printval(vm, chunk->constants.values[cons]);
  ku_printf(vm, "'\n");
  return offset + 3;
}

int ku_arraydis(kuvm *__nonnull vm, const char *__nonnull name, kuchunk *__nonnull chunk, int offset) {
  uint16_t count = (uint16_t)(chunk->code[offset + 1] << 8);
  count |= chunk->code[offset + 2];
  ku_printf(vm, "%-16s %4d", name, count);
  return offset + 3;
}

int ku_jumpdis(kuvm *__nonnull vm, const char *__nonnull name, int sign, kuchunk *__nonnull chunk,
int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  ku_printf(vm, "%-16s %4d -> %d", name, offset,
            offset + 3 + sign * jump);
  return offset + 3;
}

int ku_bytedis(kuvm *__nonnull vm, kuchunk *__nonnull chunk, int offset) {
  ku_printf(vm, "%04d ", offset);

  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset-1]) {
    ku_printf(vm, "   | ");
  } else {
    ku_printf(vm, "%4d ", chunk->lines[offset]);
  }
  uint8_t op = chunk->code[offset];
  switch (op) {
    case OP_RET: return ku_opdis(vm, "OP_RET", offset);
    case OP_NEG: return ku_opdis(vm, "OP_NEG", offset);
    case OP_ADD: return ku_opdis(vm, "OP_ADD", offset);
    case OP_BAND: return ku_opdis(vm, "OP_BAND", offset);
    case OP_SHL: return ku_opdis(vm, "OP_SHL", offset);
    case OP_SHR: return ku_opdis(vm, "OP_SHR", offset);
    case OP_BOR: return ku_opdis(vm, "OP_BOR", offset);
    case OP_SUB: return ku_opdis(vm, "OP_SUB", offset);
    case OP_MUL: return ku_opdis(vm, "OP_MUL", offset);
    case OP_DIV: return ku_opdis(vm, "OP_DIV", offset);
    case OP_NULL: return ku_opdis(vm, "OP_NULL", offset);
    case OP_TRUE: return ku_opdis(vm, "OP_TRUE", offset);
    case OP_FALSE: return ku_opdis(vm, "OP_FALSE", offset);
    case OP_GT: return ku_opdis(vm, "OP_GT", offset);
    case OP_LT: return ku_opdis(vm, "OP_LT", offset);
    case OP_EQ: return ku_opdis(vm, "OP_EQ", offset);
    case OP_POP: return ku_opdis(vm, "OP_POP", offset);
    case OP_DUP: return ku_opdis(vm, "OP_DUP", offset);
    case OP_ASET: return ku_opdis(vm, "OP_ASET", offset);
    case OP_AGET: return ku_opdis(vm, "OP_AGET", offset);
    case OP_CLASS: return ku_constdis(vm, "OP_CLASS", chunk, offset);
    case OP_METHOD: return ku_constdis(vm, "OP_METHOD", chunk, offset);
    case OP_INVOKE: return ku_invokedis(vm, "OP_INVOKE", chunk, offset);
    case OP_SUPER_INVOKE:
      return ku_invokedis(vm, "OP_SUPER_INVOKE", chunk, offset);
    case OP_INHERIT: return ku_opdis(vm, "OP_INHERIT", offset);
    case OP_CONST: return ku_constdis(vm, "OP_CONST", chunk, offset);
    case OP_DEF_GLOBAL: return ku_constdis(vm, "OP_DEF_GLOBAL", chunk, offset);
    case OP_GET_GLOBAL: return ku_constdis(vm, "OP_GET_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL: return ku_constdis(vm, "OP_SET_GLOBAL", chunk, offset);
    case OP_GET_LOCAL: return ku_opslotdis(vm, "OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL: return ku_opslotdis(vm, "OP_SET_LOCAL", chunk, offset);
    case OP_GET_UPVAL:
      return ku_opslotdis(vm, "OP_GET_UPVAL", chunk, offset);
    case OP_SET_PROP: return ku_opslotdis(vm, "OP_SET_PROP", chunk, offset);
    case OP_GET_PROP: return ku_opslotdis(vm, "OP_GET_PROP", chunk, offset);
    case OP_SET_UPVAL:
      return ku_opslotdis(vm, "OP_SET_UPVAL", chunk, offset);
    case OP_GET_SUPER:
      return ku_constdis(vm, "OP_GET_SUPER", chunk, offset);
    case OP_JUMP: return ku_jumpdis(vm, "OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE: return ku_jumpdis(vm, "OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_LOOP: return ku_jumpdis(vm, "OP_LOOP", -1, chunk, offset);
    case OP_CALL: return ku_opslotdis(vm, "OP_CALL", chunk, offset);
    case OP_ARRAY: return ku_arraydis(vm, "OP_ARRAY", chunk, offset);
    case OP_TABLE: return ku_arraydis(vm, "OP_TABLE", chunk, offset);
    case OP_CLOSE_UPVAL: return ku_opdis(vm, "OP_CLOSE_UPVAL", offset);
    case OP_CLOSURE: {
      offset++;
      uint8_t con = chunk->code[offset++];
      ku_printf(vm, "%-16s %4d ", "OP_CLOSURE", con);
      ku_printval(vm, chunk->constants.values[con]);
//       ku_printf(vm, "\n");
      kufunc *__nonnull fn = AS_FUNC(chunk->constants.values[con]);
      for (int j = 0; j < fn->upcount; j++) {
        int local = chunk->code[offset++];
        int index = chunk->code[offset++];
        ku_printf(vm, "%04d | %s %d\n", offset - 2, local ? "local" : "upval", index);
      }
    }
      return offset;
    default:
      ku_printf(vm, "Unknown opcode %d\n", op);
      return offset + 1;
  }
}

#if false
static void ku_chunkdump(kuvm *__nonnull vm, kuchunk *__nonnull chunk, const char *__nonnull name) {
  ku_printf(vm, "== %s ==\n", name);
  for (int offset = 0; offset < chunk->count; ) {
    offset = ku_bytedis(vm, chunk, offset);
    ku_printf(vm, "\n");
  }
}
#endif

// ********************** END DISASSEMBLY ***************************



void trim_line(char *__nonnull buffer) {
  size_t l = strlen(buffer);
  if (l > 0 && buffer[l-1] == '\n') {
    buffer[l-1] = '\0';
  }
}

#ifdef TRACE_ENABLED
kuchunk *__nonnull ku_chunk_runtime(kuvm *__nonnull vm);
int ku_bytedis(kuvm *__nonnull vm, kuchunk *__nonnull chunk, int offset);
#endif // TRACE_ENABLED

kures debugger(kuvm *__nonnull vm) {

#ifdef TRACE_ENABLED
  kuframe *__nonnull frame = &vm->frames[vm->framecount - 1];

  kuchunk *__nonnull ck = ku_chunk_runtime(vm);
  ku_bytedis(vm, ck, (int) (frame->ip - ck->code));
#endif // TRACE_ENABLED

  char line[KU_MAXINPUT];
  char *__nonnull b = line;
  int readstatus = ku_readline(vm, b, "\ndbg: ");
  if (readstatus == 0)
    return KVM_CONT;
  trim_line(b);
  ku_saveline(vm, b);

  if (strcmp(b, "c") == 0) {
    return KVM_CONT;
  } else if (strcmp(b, "x") == 0) {
    return KVM_OK;
  }

  return KVM_CONT;
}

static char *__nullable ku_readfile(KU_UNUSED kuvm *__nonnull vm, const char *__nonnull path) {
  FILE *__nullable file = fopen(path , "rb");

  if (file == NULL) {
    return NULL;
  }
  fseek(file , 0L , SEEK_END);
  size_t fsize = ftell(file);
  rewind(file);
  char *__nullable buffer = (char *)malloc(fsize + 1);

  if (!buffer) {
    fclose(file);
    return NULL;
  }

  size_t read = fread(buffer, sizeof (char), fsize, file);

  if (read < fsize) {
    free(buffer);
    return NULL;
  }
  buffer [read] = '\0' ;
  fclose(file);
  return buffer ;
}

kures ku_runfile(kuvm *__nonnull vm, const char *__nonnull file) {
  char *__nullable source = ku_readfile(vm, file);

  if (source == NULL) {
    return KVM_FILE_NOTFOUND;
  }
  kures res = ku_exec(vm, KU_NONNULL(source));
  free(source);
  return res;
}

typedef struct {
  const char *__nonnull name;
  uint64_t mask;
} ku_repl_flag;

static ku_repl_flag ku_repl_flags[] = {
  { "noexec", KVM_F_NOEXEC },
};

static bool ku_check_flag(kuvm *__nonnull vm, char *__nonnull line, const char *__nonnull name, uint64_t flag) {
  char buff[256];

  sprintf(buff, ".%s", name);
  if (strcmp(line, buff) == 0) {
    printf("%s is %s\n", name, (vm->flags & flag) ? "on" : "off");
    return true;
  }

  sprintf(buff, ".%s on", name);
  if (strcmp(line, buff) == 0) {
    vm->flags |= flag;
    printf("%s on\n", name);
    return true;
  }
  sprintf(buff, ".%s off", name);
  if (strcmp(line, buff) == 0) {
    vm->flags &= ~flag;
    printf("%s off\n", name);
    return true;
  }
  return false;
}

static bool ku_check_flags(kuvm *__nonnull vm, char *__nonnull line) {
  for (size_t i = 0; i < sizeof(ku_repl_flags)/sizeof(ku_repl_flag); i++) {
    ku_repl_flag *__nonnull flag = &ku_repl_flags[i];
    if (ku_check_flag(vm, line, flag->name, flag->mask)) {
      return true;
    }
  }
  return false;
}

static void ku_printarr(kuvm *__nonnull vm, kuval arr) {
  kuaobj *__nonnull ao = AS_ARRAY(arr);
  int limit = ao->elements.count < 5 ? ao->elements.count : 5;

  ku_printf(vm, "[");
  for (int i = 0; i < limit; i++) {
    ku_printval(vm, ao->elements.values[i]);
    if (i < limit - 1) {
      ku_printf(vm, ",");
    }
  }

  if (limit < ao->elements.count) {
    ku_printf(vm, "...");
  }
  ku_printf(vm, "]");
}

static void ku_printr(kuvm *__nonnull vm, kuval v) {
  if (IS_ARRAY(v)) {
    ku_printarr(vm, v);
  }
  else {
    ku_printval(vm, v);
  }
}

static void ku_replexec(kuvm *__nonnull vm, char *__nonnull expr) {
    ku_exec(vm, expr);
    if (vm->sp > vm->stack) {
      kuval v = ku_pop(vm);
      ku_printr(vm, v);
      printf("\n");
    }
}

static void ku_printbuild(kuvm *__nonnull vm) {
  printf("kumu v%d.%d [", KVM_MAJOR, KVM_MINOR);
#ifdef DEBUG
  printf("D");
#endif // DEBUG

#ifdef USE_READLINE
  printf("R");
#endif // USE_READLINE

#ifdef TRACE_OBJ_COUNTS
  printf("O");
#endif // TRACE_OBJ_COUNTS

#ifdef STACK_CHECK
  printf("S");
#endif // STACK_CHECK

  double s = (double)(sizeof(kuvm)+vm->max_stack*sizeof(kuval))/1024.0;
  printf("] vmsize=%.2fkb ", s);
  printf(".quit to exit\n");

}
static void ku_repl(kuvm *__nonnull vm) {
  ku_printbuild(vm);

  ku_initreadline(vm);
  while(true) {
    char line[KU_MAXINPUT];
    char *__nonnull b = line;
    int readstatus = ku_readline(vm, b, "> ");
    if (readstatus == 0)
      continue;
    trim_line(b);

    ku_saveline(vm, b);

    if (strcmp(b, ".quit") == 0) {
      break;
    }

    if (strcmp(b, ".debug on") == 0) {
      vm->debugger = debugger;
      printf("debugging on\n");
      continue;
    } else if (strcmp(b, ".debug off") == 0) {
      vm->debugger = NULL;
      printf("debugging off\n");
      continue;
    } else if (strcmp(b, ".debug") == 0) {
      printf("debugging %s\n", (vm->debugger) ? "on" : "off");
      continue;
    }

    if (strcmp(b, ".help") == 0) {
      for (size_t i = 0; i < sizeof(ku_repl_flags)/sizeof(ku_repl_flag); i++) {
        ku_repl_flag *__nonnull flag = &ku_repl_flags[i];
        printf(".%s\n", flag->name);
      }
      continue;
    }

    if (ku_check_flags(vm, b)) continue;;

    if (strcmp(b, ".mem") == 0) {
      ku_printmem(vm);
      continue;
    }

    if (strcmp(b, ".gc") == 0) {
      ku_gc(vm);
      continue;;
    }
    ku_replexec(vm, b);
    ku_freeline(vm, b);
  }
}

void repl_print(const char *fmt, va_list args) {
    vprintf(fmt, args);
}

int ku_main(int argc, const char *__nonnull argv[__nullable]) {
  int stack = STACK_MAX;
  const char *__nullable file = NULL;

  for (int i = 1; i < argc; i++) {
    const char *__nonnull a = argv[i];
    if (strncmp(a, "-s=", 3) == 0) {
      stack = atoi(&a[3]);
    } else {
      file = a;
    }
  }

  kuvm *__nonnull vm = ku_newvm(stack == 0 ? STACK_MAX : stack, NULL);
  vm->print = repl_print;
  ku_reglibs(vm);
  if (file == NULL) {
    ku_repl(vm);
  } else {
    kures res = ku_runfile(vm, KU_NONNULL(file));
    if (res == KVM_ERR_RUNTIME) {
      ku_freevm(vm);
      exit(70);
    }
    if (res == KVM_ERR_SYNTAX)  {
      ku_freevm(vm);
      exit(65);
    }
    if (res == KVM_FILE_NOTFOUND) {
      fprintf(stderr, "file error '%s'\n", argv[1]);
      ku_freevm(vm);
      exit(74);
    }
  }
  ku_freevm(vm);
  return 0;
}
