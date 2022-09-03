//
//  kumain.c
//  kumu
//
//  Created by Mohsen Agsen on 12/7/21.
//

#include "kumain.h"
#include "kumu.h"

#define KU_MAXINPUT 1024

#if defined(USE_READLINE)

#include <readline/readline.h>
#include <readline/history.h>
#define ku_initreadline(K)  ((void)K, rl_readline_name="kumu")
#define ku_readline(K,b,p)  ((void)K, ((b)=readline(p)) != NULL)
#define ku_saveline(K,line)  ((void)K, add_history(line))
#define ku_freeline(K,b)  ((void)K, free(b))

#else

#define ku_initreadline(K)  ((void)K)
#define ku_readline(K,b,p) \
        ((void)K, fputs(p, stdout), fflush(stdout),  /* show prompt */ \
        fgets(b, KU_MAXINPUT, stdin) != NULL)  /* get line */
#define ku_saveline(K,line)  { (void)K; (void)line; }
#define ku_freeline(K,b)  { (void)K; (void)b; }

#endif

void trim_line(char *buffer) {
  size_t l = strlen(buffer);
  if (l > 0 && buffer[l-1] == '\n') {
    buffer[l-1] = '\0';
  }
}

kuchunk *ku_chunk_runtime(kuvm *vm);
int ku_bytedis(kuvm *vm, kuchunk *chunk, int offset);

kures debugger(kuvm *vm) {

  kuframe *frame = &vm->frames[vm->framecount - 1];

  kuchunk *ck = ku_chunk_runtime(vm);
  ku_bytedis(vm, ck, (int) (frame->ip - ck->code));

  char line[KU_MAXINPUT];
  char *b = line;
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

static char *ku_readfile(kuvm *vm, const char *path) {
  FILE * file = fopen(path , "rb");

  if (file == NULL) {
    return NULL;
  }
  fseek(file , 0L , SEEK_END);
  size_t fsize = ftell(file);
  rewind(file);
  char * buffer = (char *) malloc(fsize + 1);

  if (!buffer) {
    fclose(file);
    return NULL;
  }

  size_t read = fread(buffer , sizeof (char), fsize, file);

  if (read < fsize) {
    free(buffer);
    return NULL;
  }
  buffer [read] = '\0' ;
  fclose(file);
  return buffer ;
}

kures ku_runfile(kuvm *vm, const char *file) {
  char *source = ku_readfile(vm, file);

  if (source == NULL) {
    return KVM_FILE_NOTFOUND;
  }
  kures res = ku_exec(vm, source);
  free(source);
  return res;
}

typedef struct {
  const char *name;
  uint64_t mask;
} ku_repl_flag;

static ku_repl_flag ku_repl_flags[] = {
  { "trace", KVM_F_TRACE },
  { "list", KVM_F_LIST },
  { "stack", KVM_F_STACK },
  { "noexec", KVM_F_NOEXEC },
};

static bool ku_check_flag(kuvm *vm, char *line,
                       const char *name, uint64_t flag) {
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

static bool ku_check_flags(kuvm *vm, char *line) {
  for (int i = 0; i < sizeof(ku_repl_flags)/sizeof(ku_repl_flag); i++) {
    ku_repl_flag *flag = &ku_repl_flags[i];
    if (ku_check_flag(vm, line, flag->name, flag->mask)) {
      return true;
    }
  }
  return false;
}

static void ku_printarr(kuvm* vm, kuval arr) {
  kuaobj* ao = AS_ARRAY(arr);
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

static void ku_printr(kuvm* vm, kuval v) {
  if (IS_ARRAY(v)) {
    ku_printarr(vm, v);
  }
  else {
    ku_printval(vm, v);
  }
}

static void ku_replexec(kuvm *vm, char *expr, kustr *under) {
  uint64_t oldflags = vm->flags;
  vm->flags |= KVM_F_QUIET;
  kufunc *fn = ku_compile(vm, expr);
  if (fn == NULL) {
    size_t len = strlen(expr);
    char alt[1024];
    sprintf(alt, "_ = %s", expr);
    if (expr[len-1] != ';') {
      strcat(alt, ";");
      vm->flags = oldflags;
      ku_exec(vm, alt);
      kuval v;
      ku_tabget(vm, &vm->globals, under, &v);
      ku_printr(vm, v);
      printf("\n");
    }
  } else {
    vm->flags = oldflags;
    ku_exec(vm, expr);
    if (vm->sp > vm->stack) {
      kuval v = ku_pop(vm);
      ku_printr(vm, v);
      printf("\n");
    }
  }

}

static void ku_printbuild(kuvm *vm) {
  printf("kumu v%d.%d [", KVM_MAJOR, KVM_MINOR);
#ifdef DEBUG
  printf("D");
#endif

#ifdef USE_READLINE
  printf("R");
#endif

#ifdef TRACE_OBJ_COUNTS
  printf("O");
#endif

#ifdef STACK_CHECK
  printf("S");
#endif

  double s = (double)(sizeof(kuvm)+vm->max_stack*sizeof(kuval))/1024.0;
  printf("] vmsize=%.2fkb ", s);
  printf(".quit to exit\n");

}
static void ku_repl(kuvm *vm) {
  ku_printbuild(vm);
  kustr* under = ku_strfrom(vm, "_", 1);
  ku_tabset(vm, &vm->globals, under, NULL_VAL);

  ku_initreadline(vm);
  while(true) {
    char line[KU_MAXINPUT];
    char *b = line;
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
      for (int i = 0; i < sizeof(ku_repl_flags)/sizeof(ku_repl_flag); i++) {
        ku_repl_flag *flag = &ku_repl_flags[i];
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
    ku_replexec(vm, b, under);
    ku_freeline(vm, b);
  }
}

int ku_main(int argc, const char * argv[]) {
  int stack = STACK_MAX;
  const char *file = NULL;


  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (strncmp(a, "-s=", 3) == 0) {
      stack = atoi(&a[3]);
    } else {
      file = a;
    }
  }

  kuvm *vm = ku_newvm(stack == 0 ? STACK_MAX : stack);
  ku_reglibs(vm);
  if (file == NULL) {
    ku_repl(vm);
  } else {
    kures res = ku_runfile(vm, file);
    if (res == KVM_ERR_RUNTIME) {
      ku_free(vm);
      exit(70);
    }
    if (res == KVM_ERR_SYNTAX)  {
      ku_free(vm);
      exit(65);
    }
    if (res == KVM_FILE_NOTFOUND) {
      fprintf(stderr, "file error '%s'\n", argv[1]);
      ku_free(vm);
      exit(74);
    }
  }
  ku_free(vm);
  return 0;
}
