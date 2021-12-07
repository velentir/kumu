//
//  kumain.c
//  kumu
//
//  Created by Mohsen Agsen on 12/7/21.
//

#include "kumain.h"
#include "kumu.h"


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


static bool ku_check_flag(kuvm *vm, char *line,
                       const char *name, uint64_t flag) {
  char buff[256];
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
static void ku_repl(kuvm *vm) {
  printf("kumu %d.%d\n", KVM_MAJOR, KVM_MINOR);
  char line[1024];
  
  while(true) {
    printf("$ ");
    
    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }
    
    if (strcmp(line, ".quit\n") == 0) {
      break;
    }

    if (ku_check_flag(vm, line, "trace", KVM_F_TRACE)) continue;
    if (ku_check_flag(vm, line, "stack", KVM_F_STACK)) continue;
    if (ku_check_flag(vm, line, "list", KVM_F_LIST)) continue;

    if (strcmp(line, ".mem\n") == 0) {
      ku_print_mem(vm);
      continue;
    }
    
    ku_exec(vm, line);
    if (vm->sp > vm->stack) {
      kuval v = ku_pop(vm);
      ku_print_val(vm, v);
      printf("\n");
    }

  }
}

int ku_main(int argc, const char * argv[]) {
  kuvm *vm = ku_new();
  
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
