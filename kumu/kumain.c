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
  
  sprintf(buff, ".%s\n", name);
  if (strcmp(line, buff) == 0) {
    printf("%s is %s\n", name, (vm->flags & flag) ? "on" : "off");
    return true;
  }
  
  sprintf(buff, ".%s on\n", name);
  if (strcmp(line, buff) == 0) {
    vm->flags |= flag;
    printf("%s on\n", name);
    return true;
  }
  sprintf(buff, ".%s off\n", name);
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
      ku_printval(vm, v);
      printf("\n");
    }
  } else {
    vm->flags = oldflags;
    ku_exec(vm, expr);
    if (vm->sp > vm->stack) {
      kuval v = ku_pop(vm);
      ku_printval(vm, v);
      printf("\n");
    }
  }

}

static void ku_printbuild(kuvm *vm) {
  printf("kumu v%d.%d [", KVM_MAJOR, KVM_MINOR);
#ifdef DEBUG
  printf("D");
#endif
  
#ifdef NAN_BOX
  printf("N");
#endif
  
#ifdef USE_READLINE
  printf("R");
#endif
  printf("]\n");
}
static void ku_repl(kuvm *vm) {
  ku_printbuild(vm);
  kustr* under = ku_strfrom(vm, "_", 1);
  ku_tabset(vm, &vm->globals, under, NIL_VAL);

  

  ku_initreadline(vm);
  while(true) {
    char line[KU_MAXINPUT];
    char *b = line;
    int readstatus = ku_readline(vm, b, "> ");
    if (readstatus == 0)
      continue;

    size_t l = strlen(b);
    if (l > 0 && b[l-1] == '\n') {
      b[l-1] = '\0';
    }
//    printf("> ");
//    if (!fgets(line, sizeof(line), stdin)) {
//      printf("\n");
//      break;
//    }
    
    if (strcmp(b, ".quit") == 0) {
      break;
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
    
    ku_replexec(vm, b, under);
    ku_saveline(vm, b);
    ku_freeline(vm, b);
  }
}

int ku_main(int argc, const char * argv[]) {
  kuvm *vm = ku_new();
  
  ku_reglibs(vm);
  if (argc == 1) {
    ku_repl(vm);
  } else if (argc == 2) {
    kures res = ku_runfile(vm, argv[1]);
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
    
  } else {
    ku_free(vm);
    fprintf(stderr, "usage kumu [file]\n");
    exit(64);
  }
  ku_free(vm);
  return 0;
}
