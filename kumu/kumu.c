//  kumu.c

#include "kumu.h"
#include <stdio.h>

// ********************** macros **********************
#define CAPACITY_GROW(cap)  ((cap) < 8 ? 8 : (cap) * 2)
#define ARRAY_GROW(k, type, ptr, old, new)\
(type*)ku_alloc(k, ptr, sizeof(type) * (old), sizeof(type) * (new))
#define ARRAY_FREE(vm, type, ptr, old) ku_alloc(vm, ptr, sizeof(type) * (old), 0)
#define KALLOC(vm, type, count) \
    (type*)ku_alloc(vm, NULL, 0, sizeof(type) * (count))
#define KALLOC_OBJ(vm, type, objtype) \
    (type*)ku_objalloc(vm, sizeof(type), objtype)
#define FREE(vm, type, ptr) \
  ku_alloc(vm, ptr, sizeof(type), 0)

#define READ_SHORT(vm) \
(frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define BYTE_READ(vm) (*(frame->ip++))
#define CONST_READ(vm) (frame->closure->func->chunk.constants.values[BYTE_READ(vm)])
#define READ_STRING(vm) AS_STR(CONST_READ(vm))

// TODO: add code coverage of ku_err line below
#define BIN_OP(v, vt, op) \
  do { \
  if (!IS_NUM(ku_peek(v,0)) || !IS_NUM(ku_peek(v,1))) { \
    ku_err(v, "numbers expected"); \
    return KVM_ERR_RUNTIME; \
  } \
  double b = AS_NUM(ku_pop(v)); \
  double a = AS_NUM(ku_pop(v)); \
  ku_push(v, vt(a op b)); \
} while (false)

// ********************** forwards **********************
kuval ku_peek(kuvm *vm, int distance);

static void ku_printobj(kuvm* vm, kuval val);
void ku_printval(kuvm *vm, kuval value);
static void ku_printfunc(kuvm *vm, kufunc *fn);

int ku_bytedis(kuvm *vm, kuchunk *chunk, int offset);
static void ku_chunkdump(kuvm *vm, kuchunk *chunk, const char * name);

static void ku_function(kuvm *vm, kufunc_t type);
kuval string_iget(kuvm *vm, kuobj *obj, kustr *prop);
kuval string_icall(kuvm *vm, kuobj *o, kustr *m, int argc, kuval *argv);
bool array_invoke(kuvm *vm, kuval arr, kustr *method, int argc, kuval *argv);
static void ku_markval(kuvm *vm, kuval v);
void ku_markobj(kuvm *vm, kuobj *o);
static void ku_marktable(kuvm *vm, kutab *tab);

// ********************** object **********************
kuobj* ku_objalloc(kuvm* vm, size_t size, kuobj_t type) {
#ifdef  TRACE_OBJ_COUNTS
  vm->alloc_counts[type]++;
#endif // TRACE_OBJ_COUNTS
  kuobj* obj = (kuobj*)ku_alloc(vm, NULL, 0, size);
  obj->type = type;
  obj->marked = false;
  obj->next = (struct kuobj*)vm->objects;
  vm->objects = obj;
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p alloc %zu for %d\n", (void*)obj, size, type);
  }
  return obj;
}

void ku_objfree(kuvm* vm, kuobj* obj) {
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p free type %d\n", (void*)obj, obj->type);
  }

#ifdef TRACE_OBJ_COUNTS
  vm->alloc_counts[obj->type]--;
#endif // TRACE_OBJ_COUNTS
  switch (obj->type) {
    case OBJ_FUNC: {
      kufunc *fn = (kufunc*)obj;
      ku_chunkfree(vm, &fn->chunk);
      FREE(vm, kufunc, obj);
    }
      break;

    case OBJ_ARRAY: {
      kuaobj *ao = (kuaobj*)obj;
      ku_alloc(vm, ao->elements.values, sizeof(kuval)*ao->elements.capacity, 0);
      ao->elements.values = NULL;
      ao->elements.count = 0;
      ao->elements.count = 0;
      FREE(vm, kuaobj, obj);
    }
      break;

    case OBJ_CCLASS: {
      kucclass *cc = (kucclass*)obj;
      if (cc->sfree) {
        cc->sfree(vm, obj);
      }
      FREE(vm, kucclass, obj);
    }
      break;

    case OBJ_CINST: {
      kunobj *i = (kunobj*)obj;
      if (i->klass->ifree) {
        i->klass->ifree(vm, obj);
      }
    }
      break;

    case OBJ_CFUNC:
      FREE(vm, kucfunc, obj);
      break;

    case OBJ_CLOSURE: {
      kuclosure *cl = (kuclosure*)obj;
      ARRAY_FREE(vm, kuxobj*, cl->upvals, cl->upcount);
      FREE(vm, kuclosure, obj);
    }
      break;

    case OBJ_CLASS: {
      kuclass *c = (kuclass*)obj;
      ku_tabfree(vm, &c->methods);
      FREE(vm, kuclass, obj);
    }
      break;

    case OBJ_INSTANCE: {
      kuiobj *i = (kuiobj*)obj;
      ku_tabfree(vm, &i->fields);
      FREE(vm, kuiobj, obj);
    }
      break;

    case OBJ_BOUND_METHOD:
      FREE(vm, kubound, obj);
      break;

    case OBJ_STR: {
      kustr* str = (kustr*)obj;
      ARRAY_FREE(vm, char, str->chars, (size_t)str->len + 1);
      FREE(vm, kustr, obj);
    }
      break;

    case OBJ_UPVAL:
      FREE(vm, kuxobj, obj);
      break;
  }
}

bool ku_objis(kuval v, kuobj_t ot) {
  return IS_OBJ(v) && AS_OBJ(v)->type == ot;
}

// ********************** string **********************
// FNV-1a hashing function
static uint32_t ku_strhash(const char* key, int len) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < len; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

static kustr* ku_stralloc(kuvm* vm, char* chars, int len, uint32_t hash) {
  kustr* str = KALLOC_OBJ(vm, kustr, OBJ_STR);
  str->len = len;
  str->chars = chars;
  str->hash = hash;
  ku_push(vm, OBJ_VAL(str)); // for GC
  ku_tabset(vm, &vm->strings, str, NIL_VAL);
  ku_pop(vm);
  return str;
}

kustr* ku_strfrom(kuvm* vm, const char* chars, int len) {
  uint32_t hash = ku_strhash(chars, len);

  kustr* interned = ku_tabfindc(vm, &vm->strings, chars, len, hash);
  if (interned != NULL) {
    return interned;
  }

  char* buff = KALLOC(vm, char, (size_t)len + 1);
  memcpy(buff, chars, len);
  buff[len] = '\0';
  return ku_stralloc(vm, buff, len, hash);
}

static kustr* ku_strtake(kuvm* vm, char* buff, int len) {
  uint32_t hash = ku_strhash(buff, len);
  kustr* interned = ku_tabfindc(vm, &vm->strings, buff, len, hash);
  if (interned != NULL) {
    ARRAY_FREE(vm, char, buff, (size_t)len + 1);
    return interned;
  }

  return ku_stralloc(vm, buff, len, hash);
}

static void ku_strcat(kuvm* vm) {
  kustr *b = AS_STR(ku_peek(vm,0)); // for GC
  kustr* a = AS_STR(ku_peek(vm,1)); // for GC
  int len = a->len + b->len;
  char* buff = KALLOC(vm, char, (size_t)len + 1);
  memcpy(buff, a->chars, a->len);
  memcpy(buff + a->len, b->chars, b->len);
  buff[len] = '\0';
  kustr* res = ku_strtake(vm, buff, len);
  ku_pop(vm);
  ku_pop(vm);
  ku_push(vm, OBJ_VAL(res));
}

// ********************** value **********************
bool ku_equal(kuval v1, kuval v2) {

  if (IS_NUM(v1) && IS_NUM(v2)) {
    return AS_NUM(v1) == AS_NUM(v2);
  }
  return v1 == v2;
}

// ********************** hash table **********************
void ku_tabinit(kuvm* vm, kutab* map) {
  map->count = 0;
  map->capacity = 0;
  map->entries = NULL;
}

void ku_tabfree(kuvm* vm, kutab* map) {
  ARRAY_FREE(vm, kuentry, map->entries, map->capacity);
  ku_tabinit(vm, map);
}

#define MAP_MAX_LOAD 0.75

#define MOD2(a,b) ((a) & (b - 1))

static kuentry* ku_tabfinds(kuvm* vm, kuentry* entries, int capacity, kustr* key) {
  uint32_t index = MOD2(key->hash, capacity);
  kuentry* tombstone = NULL;

  for (;;) {
    kuentry* e = &entries[index];

    if (e->key == NULL) {
      if (IS_NIL(e->value)) {
        // empty entry and have a tombstone ~> return the tombstone
        // otherwise return this entry.
        // this allows reusing tombstone slots for added efficiency
        return tombstone != NULL ? tombstone : e;
      }
      else {
        // a tombstone has NULL key and BOOL(true) value, remember
        // the first tombstone we found so we can reused it
        if (tombstone == NULL) {
          tombstone = e;
        }
      }
    } else if (e->key == key) {
      return e;
    }

    index = MOD2(index + 1, capacity);
  }
}

static void ku_tabadjust(kuvm* vm, kutab* map, int capacity) {
  kuentry* entries = KALLOC(vm, kuentry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  map->count = 0;
  for (int i = 0; i < map->capacity; i++) {
    kuentry* src = &map->entries[i];
    if (src->key == NULL) {
      continue;
    }

    kuentry* dest = ku_tabfinds(vm, entries, capacity, src->key);
    dest->key = src->key;
    dest->value = src->value;
    map->count++;
  }

  ARRAY_FREE(vm, kuentry, map->entries, map->capacity);
  map->entries = entries;
  map->capacity = capacity;
}

bool ku_tabset(kuvm* vm, kutab* map, kustr* key, kuval value) {
  if ((size_t)map->count + 1 > map->capacity * MAP_MAX_LOAD) {
    int capacity = CAPACITY_GROW(map->capacity);
    ku_tabadjust(vm, map, capacity);
  }

  kuentry* e = ku_tabfinds(vm, map->entries, map->capacity, key);
  bool isnew = e->key == NULL;
  // we don't increase the count if we use a tombstone slot
  if (isnew && IS_NIL(e->value)) {
    map->count++;
  }
  e->key = key;
  e->value = value;
  return isnew;
}

void ku_tabcopy(kuvm* vm, kutab* from, kutab* to) {
  for (int i = 0; i < from->capacity; i++) {
    kuentry* e = &from->entries[i];
    if (e->key != NULL) {
      ku_tabset(vm, to, e->key, e->value);
    }
  }
}

bool ku_tabget(kuvm* vm, kutab* map, kustr* key, kuval* value) {
  if (map->count == 0) {
    return false;
  }

  kuentry* e = ku_tabfinds(vm, map->entries, map->capacity, key);
  if (e->key == NULL) {
    return false;
  }

  *value = e->value;
  return true;
}

bool ku_tabdel(kuvm* vm, kutab* map, kustr* key) {
  if (map->count == 0) {
    return false;
  }

  kuentry* e = ku_tabfinds(vm, map->entries, map->capacity, key);
  if (e->key == NULL) {
    return false;
  }
  e->key = NULL;
  e->value = BOOL_VAL(true);
  return true;
}

kustr* ku_tabfindc(kuvm* vm, kutab* map, const char* chars, int len, uint32_t hash) {
  if (map->count == 0) {
    return NULL;
  }

  uint32_t index = MOD2(hash, map->capacity);
  for (;;) {
    kuentry* e = &map->entries[index];
    if (e->key == NULL) {
      if (IS_NIL(e->value)) {
        return NULL;    // empty non-tombstone
      }
    }
    else if (e->key->len == len && e->key->hash == hash &&
      memcmp(e->key->chars, chars, len) == 0) {
      return e->key;
    }
    index = MOD2(index + 1,map->capacity);
  }
}

// ********************** scanner **********************
static char ku_advance(kuvm *vm) {
  vm->scanner.curr++;
  return vm->scanner.curr[-1];
}

void ku_lexinit(kuvm *vm, const char *source) {
  vm->scanner.start = source;
  vm->scanner.curr = source;
  vm->scanner.line = 1;
}

static bool ku_lexend(kuvm *vm) {
  return (*(vm->scanner.curr) == '\0');
}

static bool ku_isdigit(char c) {
  return (c >= '0' && c <= '9');
}

static bool ku_ishexdigit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static bool ku_isalpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c == '_');
}

static char ku_lexpeeknext(kuvm *vm) {
  if (ku_lexend(vm)) return '\0';
  return vm->scanner.curr[1];
}

static char ku_lexpeek(kuvm *vm) {
  return *vm->scanner.curr;
}

static void ku_lexspace(kuvm *vm) {
  while (true) {
    char c = ku_lexpeek(vm);
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        ku_advance(vm);
        break;
      case '\n':
        vm->scanner.line++;
        ku_advance(vm);
        break;
      case '/':
        if (ku_lexpeeknext(vm) == '/') {
          while (ku_lexpeek(vm) != '\n' && !ku_lexend(vm)) {
            ku_advance(vm);
          }
          break;
        } else {
          return;
        }
      default:
        return;
    }
  }
}

static kutok ku_tokmake(kuvm *vm, kutok_t type) {
  kutok token;
  token.type = type;
  token.start = vm->scanner.start;
  token.len = (int) (vm->scanner.curr - vm->scanner.start);
  token.line = vm->scanner.line;
  return token;
}

static kutok ku_lexerr(kuvm *vm, const char *msg) {
  kutok token;
  token.type = TOK_ERR;
  token.start = msg;
  token.len = (int)strlen(msg);
  token.line = vm->scanner.line;
  return token;
}

static bool ku_lexmatch(kuvm *vm, char expected) {
  if (ku_lexend(vm)) return false;
  if (*vm->scanner.curr != expected) return false;
  vm->scanner.curr++;
  return true;
}

static kutok ku_hexnum(kuvm *vm) {
  ku_advance(vm); // skip 'x'
  while(ku_ishexdigit(ku_lexpeek(vm))) ku_advance(vm);
  return ku_tokmake(vm, TOK_HEX);
}

static kutok ku_lexnum(kuvm *vm) {
  while(ku_isdigit(ku_lexpeek(vm))) ku_advance(vm);

  if (ku_lexpeek(vm) == '.' && ku_isdigit(ku_lexpeeknext(vm))) {
    ku_advance(vm);
    while(ku_isdigit(ku_lexpeek(vm))) ku_advance(vm);
  }

  if (ku_lexpeek(vm) == 'e' || ku_lexpeek(vm) == 'E') {
    ku_advance(vm);
    if (ku_lexpeek(vm) == '-') {
      ku_advance(vm);
    }
    while(ku_isdigit(ku_lexpeek(vm))) ku_advance(vm);
  }

  return ku_tokmake(vm, TOK_NUM);
}

static kutok ku_lexstr(kuvm *vm) {
  kutok_t type = TOK_STR;

  while(ku_lexpeek(vm) != '"' && !ku_lexend(vm)) {
    if (ku_lexpeek(vm) == '\\') type = TOK_STRESC;
    if (ku_lexpeek(vm) == '\n') vm->scanner.line++;
    ku_advance(vm);
  }
  if (ku_lexend(vm)) return ku_lexerr(vm, "unterminated string");
  ku_advance(vm);
  return ku_tokmake(vm, type);
}

static kutok_t ku_lexkey(kuvm *vm, int start, int len,
                        const char *rest, kutok_t type) {
  if (vm->scanner.curr - vm->scanner.start == (size_t)start + len &&
      memcmp(vm->scanner.start + start, rest, len) == 0) {
    return type;
  }
  return TOK_IDENT;
}

static kutok_t ku_keyword(kuvm *vm) {
  switch (vm->scanner.start[0]) {
    case 'a': return ku_lexkey(vm, 1,2,"nd", TOK_AND);
    case 'b': return ku_lexkey(vm, 1,4,"reak", TOK_BREAK);
    case 'c': {
      if (vm->scanner.curr - vm->scanner.start > 1) {
        switch (vm->scanner.start[1]) {
          case 'l': return ku_lexkey(vm, 2, 3, "ass", TOK_CLASS);
          case 'o':
            if (vm->scanner.start[2] == 'n' && vm->scanner.start[3] == 's')
              return ku_lexkey(vm, 4, 1, "t", TOK_CONST);
            return ku_lexkey(vm, 2, 6, "ntinue", TOK_CONTINUE);
        }
      }
      break;
    }
    case 'e': return ku_lexkey(vm, 1,3,"lse", TOK_ELSE);
    case 'f':
      if (vm->scanner.curr - vm->scanner.start > 1) {
        switch (vm->scanner.start[1]) {
          case 'a': return ku_lexkey(vm, 2, 3, "lse", TOK_FALSE);
          case 'o': return ku_lexkey(vm, 2, 1, "r", TOK_FOR);
          case 'u': return ku_lexkey(vm, 2, 6, "nction", TOK_FUN);
        }
      }
    case 'i': return ku_lexkey(vm, 1,1,"f", TOK_IF);
    case 'l': return ku_lexkey(vm, 1,2,"et", TOK_LET);
    case 'n': return ku_lexkey(vm, 1,2,"il", TOK_NIL);
    case 'o': return ku_lexkey(vm, 1,1,"r", TOK_OR);
    case 'r': return ku_lexkey(vm, 1,5,"eturn", TOK_RETURN);
    case 's': return ku_lexkey(vm, 1,4,"uper", TOK_SUPER);
    case 't':
      if (vm->scanner.curr - vm->scanner.start > 1) {
        switch (vm->scanner.start[1]) {
          case 'h': return ku_lexkey(vm, 2, 2, "is", TOK_THIS);
          case 'r': return ku_lexkey(vm, 2, 2, "ue", TOK_TRUE);
        }
      }
    case 'w': return ku_lexkey(vm, 1,4,"hile", TOK_WHILE);
  }
  return TOK_IDENT;
}

static kutok ku_lexid(kuvm *vm) {
  while (ku_isalpha(ku_lexpeek(vm)) || ku_isdigit(ku_lexpeek(vm))) {
    ku_advance(vm);
  }
  return ku_tokmake(vm, ku_keyword(vm));
}

kutok ku_scan(kuvm *vm) {
  ku_lexspace(vm);
  vm->scanner.start = vm->scanner.curr;

  if (ku_lexend(vm)) {
    return ku_tokmake(vm, TOK_EOF);
  }

  char c = ku_advance(vm);
  if (ku_isalpha(c)) return ku_lexid(vm);
  if (c == '0' && (ku_lexpeek(vm) == 'x' || ku_lexpeek(vm) == 'X')) {
    return ku_hexnum(vm);
  }
  if (ku_isdigit(c)) return ku_lexnum(vm);
  switch (c) {
    case '(': return ku_tokmake(vm, TOK_LPAR);
    case ')': return ku_tokmake(vm, TOK_RPAR);
    case '[': return ku_tokmake(vm, TOK_LBRACKET);
    case ']': return ku_tokmake(vm, TOK_RBRACKET);
    case '{': return ku_tokmake(vm, TOK_LBRACE);
    case '}': return ku_tokmake(vm, TOK_RBRACE);
    case ';': return ku_tokmake(vm, TOK_SEMI);
    case ',': return ku_tokmake(vm, TOK_COMMA);
    case '.': return ku_tokmake(vm, TOK_DOT);
    case '+': return ku_tokmake(vm, TOK_PLUS);
    case '-': return ku_tokmake(vm, TOK_MINUS);
    case '*': return ku_tokmake(vm, TOK_STAR);
    case '&': return ku_tokmake(vm, TOK_AMP);
    case '|': return ku_tokmake(vm, TOK_PIPE);
    case '/':
      return ku_tokmake(vm, TOK_SLASH);
    case '!':
      if (ku_lexmatch(vm, '=') && ku_lexmatch(vm, '=')) {
        return ku_tokmake(vm, TOK_NE);
      }
      return ku_tokmake(vm, TOK_BANG);
    case '=': {
      if (ku_lexmatch(vm, '=') && ku_lexmatch(vm, '=')) {
        return ku_tokmake(vm, TOK_EQUALITY);
      } else if (ku_lexmatch(vm, '>')) {
        return ku_tokmake(vm, TOK_ARROW);
      }
      return ku_tokmake(vm, TOK_EQ);
    }
    case '<':
      if (ku_lexmatch(vm, '<'))
        return ku_tokmake(vm, TOK_LTLT);
      return ku_tokmake(vm, ku_lexmatch(vm, '=') ? TOK_LE : TOK_LT);
    case '>':
      if (ku_lexmatch(vm, '>'))
        return ku_tokmake(vm, TOK_GTGT);
      return ku_tokmake(vm, ku_lexmatch(vm, '=') ? TOK_GE : TOK_GT);
    case '"':
      return ku_lexstr(vm);
  }
  return ku_lexerr(vm, "unexpected character");
}

void ku_lexdump(kuvm *vm) {
  int line = -1;

  while (true) {
    kutok token = ku_scan(vm);
    if (token.line != line) {
      ku_printf(vm, "%4d ", token.line);
      line = token.line;
    } else {
      ku_printf(vm, "  |  ");
    }
    ku_printf(vm, "%2d '%.*s'\n", token.type, token.len, token.start);

    if (token.type == TOK_EOF) {
      break;
    }
  }
}

// ********************** parser **********************
static void ku_perrat(kuvm *vm, kutok *tok, const char *msg) {
  if (vm->parser.panic) return;
  vm->parser.panic = true;

  ku_printf(vm, "[line %d] error ", tok->line);

  if (tok->type == TOK_EOF) {
    ku_printf(vm, " at end ");
  } else if (tok->type == TOK_ERR) {
    // nothing
  } else {
    ku_printf(vm, " at '%.*s'",  tok->len, tok->start);
  }

  ku_printf(vm, "%s\n", msg);
  vm->parser.err = true;
}

static void ku_perr(kuvm *vm, const char *msg) {
  ku_perrat(vm, &vm->parser.curr, msg);
}

static void ku_pnext(kuvm *vm) {
  vm->parser.prev = vm->parser.curr;

  while (true) {
    vm->parser.curr = ku_scan(vm);
    if (vm->parser.curr.type != TOK_ERR) break;
    ku_perr(vm, vm->parser.curr.start);
  }
}

static void ku_pconsume(kuvm *vm, kutok_t type, const char *msg) {
  if (vm->parser.curr.type == type) {
    ku_pnext(vm);
    return;
  }
  ku_perr(vm, msg);
}

kuchunk *ku_chunk(kuvm *vm) {
  return &vm->compiler->function->chunk;
}

static void ku_emitbyte(kuvm* vm, uint8_t byte) {
  ku_chunkwrite(vm, ku_chunk(vm), byte, vm->parser.prev.line);
}

static void ku_emitop(kuvm *vm, uint8_t byte) {
  ku_emitbyte(vm, byte);
}

static void ku_emitbytes(kuvm *vm, uint8_t b1, uint8_t b2);
static void ku_emitop2(kuvm* vm, uint8_t b1, uint8_t b2);

static void ku_emitret(kuvm *vm, bool lambda) {
  if (lambda) {
    ku_emitop(vm, OP_RET);
    return;
  }
  if (vm->compiler->type == FUNC_INIT) {
    ku_emitop2(vm, OP_GET_LOCAL, 0);
  } else {
    ku_emitop(vm, OP_NIL);
  }
  ku_emitop(vm, OP_RET);
}

static kufunc *ku_pend(kuvm *vm, bool lambda) {
  ku_emitret(vm, lambda);
  kufunc *fn = vm->compiler->function;
  vm->compiler = vm->compiler->enclosing;
  return fn;
}

static void ku_emitbytes(kuvm *vm, uint8_t b1, uint8_t b2) {
  ku_emitbyte(vm, b1);
  ku_emitbyte(vm, b2);
}

static void ku_emitop2(kuvm* vm, uint8_t b1, uint8_t b2) {
  ku_emitbytes(vm, b1, b2);
}

static uint8_t ku_pconst(kuvm *vm, kuval val) {
  int cons = ku_chunkconst(vm, ku_chunk(vm), val);
  if (cons > vm->max_const) {
    ku_perr(vm, "out of constant space");
    return 0;
  }
  return (uint8_t)cons;
}
static void ku_emitconst(kuvm *vm, kuval val) {
  ku_emitop2(vm, OP_CONST, ku_pconst(vm, val));
}

typedef enum {
  P_NONE,
  P_ASSIGN,     // =
  P_OR,         // or
  P_AND,        // and
  P_EQ,         // === and !==
  P_COMP,       // < > <= >=
  P_TERM,       // + -
  P_FACTOR,     // * /
  P_BIT,        // & |
  P_SHIFT,      // >> <<
  P_UNARY,      // ! -
  P_CALL,       // . ()
  P_PRIMARY
} kup_precedence;

typedef void (*kupfunc)(kuvm *, bool lhs);

typedef struct {
  kupfunc prefix;
  kupfunc infix;
  kup_precedence precedence;
} kuprule;

static kuprule *ku_getrule(kuvm *vm, kutok_t optype);

static bool ku_pcheck(kuvm* vm, kutok_t type) {
  return vm->parser.curr.type == type;
}

static bool ku_pmatch(kuvm* vm, kutok_t type) {
  if (!ku_pcheck(vm, type)) {
    return false;
  }
  ku_pnext(vm);
  return true;
}

static void ku_prec(kuvm *vm, kup_precedence prec) {
  ku_pnext(vm);
  kupfunc prefix = ku_getrule(vm, vm->parser.prev.type)->prefix;
  if (prefix == NULL) {
    ku_perr(vm, "expected expression");
    return;
  }

  bool lhs = prec <= P_ASSIGN;
  prefix(vm, lhs);

  while (prec <= ku_getrule(vm, vm->parser.curr.type)->precedence) {
    ku_pnext(vm);
    kupfunc infix = ku_getrule(vm, vm->parser.prev.type)->infix;
    infix(vm, lhs);
  }

  if (lhs && ku_pmatch(vm, TOK_EQ)) {
    ku_perr(vm, "invalid assignment target");
  }
}

static void ku_lit(kuvm *vm, bool lhs) {
  switch (vm->parser.prev.type) {
    case TOK_FALSE: ku_emitop(vm, OP_FALSE); break;
    case TOK_TRUE: ku_emitop(vm, OP_TRUE); break;
    case TOK_NIL: ku_emitop(vm, OP_NIL); break;
    default: return; // unreachable - TODO: figure out code coverage
  }
}

static void ku_xstring(kuvm* vm, bool lhs) {
  const char *chars = vm->parser.prev.start + 1;
  int len = vm->parser.prev.len - 2;

  // len(encoded) always <= len(chars);
  char *encoded = ku_alloc(vm, NULL, 0, len);

  int esclen = len;
  char *ep = encoded;
  for (int i = 0; i < len; i++) {
    char ch = chars[i];
    if (ch == '\\') {
      char next = (i < len-1) ? chars[i+1] : 0;
      if (next) {
        i++;
        esclen--;
        switch (next) {
          case 'n':
            *ep++ = '\n';
            break;
          case 'r':
            *ep++ = '\r';
            break;
          case 't':
            *ep++ = '\t';
            break;
        }
      }
    } else {
      *ep++ = chars[i];
    }
  }

  ku_emitconst(vm, OBJ_VAL(ku_strfrom(vm, encoded, esclen)));
  ku_alloc(vm, encoded, len, 0);
}
static void ku_string(kuvm* vm, bool lhs) {
  const char *chars = vm->parser.prev.start + 1;
  int len = vm->parser.prev.len - 2;
  ku_emitconst(vm, OBJ_VAL(ku_strfrom(vm, chars, len)));
}

static void ku_hex(kuvm *vm, bool lhs) {
  const char *start = vm->parser.prev.start + 2;  // skip 0x
  int len = vm->parser.prev.len - 2;

  double val = 0;
  for (int i = 0; i < len; i++) {
    unsigned char ch = start[i];
    if (ch >= '0' && ch <= '9') {
      val = val*16 + (double)(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      val = val*16 + (double)(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      val = val*16 + (double)(ch - 'A' + 10);
    }
  }
  ku_emitconst(vm, NUM_VAL(val));
}

static void ku_number(kuvm *vm, bool lhs) {
  double val = strtod(vm->parser.prev.start, NULL);
  ku_emitconst(vm, NUM_VAL(val));
}

static void ku_funcexpr(kuvm *vm, bool lhs) {
  ku_function(vm, FUNC_STD);
}

static void ku_expr(kuvm *vm) {
  ku_prec(vm, P_ASSIGN);
}

static uint8_t ku_arglist(kuvm *vm, kutok_t right) {
  uint8_t argc = 0;
  if (!ku_pcheck(vm, right)) {
    do {
      ku_expr(vm);
      if (argc > vm->max_params) {
        ku_perr(vm, "too many parameters");
      }
      argc++;
    } while (ku_pmatch(vm, TOK_COMMA));
  }

  if (right == TOK_RPAR)
    ku_pconsume(vm, right, "')' expected");
  else
    ku_pconsume(vm, right, "']' expected");
  return argc;
}

static void ku_call(kuvm *vm, bool lhs) {
  uint8_t argc = ku_arglist(vm, TOK_RPAR);
  ku_emitop2(vm, OP_CALL, argc);
}

static void ku_grouping(kuvm *vm, bool lhs) {
  ku_expr(vm);
  ku_pconsume(vm, TOK_RPAR, "')' expected");
}

static bool ku_peek_lambda(kuvm *vm) {
  bool ret = false;
  const char *start = vm->scanner.start;
  const char *curr = vm->scanner.curr;

  if (ku_isalpha(*vm->scanner.start)) {
    while (ku_isalpha(ku_lexpeek(vm)) || ku_isdigit(ku_lexpeek(vm))) {
      // TODO: add code coverage
      ku_advance(vm);
    }
    kutok_t type = ku_keyword(vm);
    if (type == TOK_IDENT) {
      ku_lexspace(vm);
      if (ku_lexpeek(vm) == ',') {
        ret = true;
      }
    }
  }
  vm->scanner.start = start;
  vm->scanner.curr = curr;
  return ret;
}

static void ku_params(kuvm *vm);
static void ku_lbody(kuvm *vm, kucomp *compiler);

static void ku_lambda_or_group(kuvm *vm, bool lhs) {
  if (ku_peek_lambda(vm)) {
    kucomp compiler;
    ku_compinit(vm, &compiler, FUNC_STD);
    ku_beginscope(vm);
    ku_params(vm);
    ku_pconsume(vm, TOK_RPAR, "')' expected");
    ku_pconsume(vm, TOK_ARROW, "'=>' expected");
    ku_lbody(vm, &compiler);
    return;
  }

  ku_grouping(vm, lhs);
}

static void ku_array(kuvm *vm, bool lhs) {
  int count = ku_arglist(vm, TOK_RBRACKET);
  ku_emitop(vm, OP_ARRAY);
  ku_emitbyte(vm, (count >> 8) & 0xff);
  ku_emitbyte(vm, count & 0xff);
}

static void ku_index(kuvm *vm, bool lhs) {
  ku_expr(vm);
  ku_pconsume(vm, TOK_RBRACKET, "']' expected");
  if (lhs && ku_pmatch(vm, TOK_EQ)) {
    ku_expr(vm);
    ku_emitop(vm, OP_ASET);
  } else {
    ku_emitop(vm, OP_AGET);
  }
}

static void ku_unary(kuvm *vm, bool lhs) {
  kutok_t optype = vm->parser.prev.type;

  ku_prec(vm, P_UNARY);

  switch (optype) {
    case TOK_MINUS: ku_emitop(vm, OP_NEG); break;
    case TOK_BANG: ku_emitop(vm, OP_NOT); break;
    default: return; // unreachable - TODO: figure out code coverage
  }
}

static void ku_exprstmt(kuvm* vm, kuloop *loop) {
  ku_expr(vm);
  ku_pconsume(vm, TOK_SEMI, "; expected");
  ku_emitop(vm, OP_POP);
}

static void ku_return(kuvm *vm, kuloop *loop) {
  if (vm->compiler->type == FUNC_MAIN) {
    ku_perr(vm, "can't return from top-level");
  }

  if (ku_pmatch(vm, TOK_SEMI)) {
    ku_emitret(vm, false);
  } else {

    if (vm->compiler->type == FUNC_INIT) {
      ku_err(vm, "cannot return from initializer");
    }
    ku_expr(vm);
    ku_pconsume(vm, TOK_SEMI, "';' expected");
    ku_emitop(vm, OP_RET);
  }
}

static void ku_break(kuvm *vm, kuloop *loop) {
  if (loop == NULL) {
    ku_perr(vm, "break must be in a loop");
  } else {
    ku_pconsume(vm, TOK_SEMI, "; expected");
    ku_emitpatch(vm, &loop->breakpatch, OP_JUMP);
  }
}

static void ku_continue(kuvm *vm, kuloop *loop) {
  if (loop == NULL) {
    ku_perr(vm, "continue must be in a loop");
  } else {
    ku_pconsume(vm, TOK_SEMI, "; expected");
    ku_emitpatch(vm, &loop->continuepatch, OP_LOOP);
  }
}

static void ku_stmt(kuvm* vm, kuloop *loop) {
  if (ku_pmatch(vm, TOK_IF)) {
    ku_ifstmt(vm, loop);
  } else if (ku_pmatch(vm, TOK_RETURN)) {
    ku_return(vm, loop);
  } else if (ku_pmatch(vm, TOK_WHILE)) {
    ku_whilestmt(vm, loop);
  } else if (ku_pmatch(vm, TOK_FOR)) {
    ku_forstmt(vm, loop);
  } else if (ku_pmatch(vm, TOK_BREAK)) {
    ku_break(vm, loop);
  } else if (ku_pmatch(vm, TOK_CONTINUE)) {
    ku_continue(vm, loop);
  } else if (ku_pmatch(vm, TOK_LBRACE)) {
    ku_beginscope(vm);
    ku_block(vm, loop);
    ku_endscope(vm);
  } else {
    ku_exprstmt(vm, loop);
  }
}

static void ku_pskip(kuvm* vm) {
  vm->parser.panic = false;

  while (vm->parser.curr.type != TOK_EOF) {
    if (vm->parser.prev.type == TOK_SEMI) {
      return;
    }

    switch (vm->parser.curr.type) {
    case TOK_CLASS:
    case TOK_FUN:
    case TOK_LET:
    case TOK_CONST:
    case TOK_FOR:
    case TOK_IF:
    case TOK_WHILE:
    case TOK_RETURN:
      return;
    default:
      ;
    }

    ku_pnext(vm);
  }
}

static uint8_t ku_pidconst(kuvm* vm, kutok* name) {
  return ku_pconst(vm, OBJ_VAL(ku_strfrom(vm, name->start, name->len)));
}

static uint8_t ku_strtoname(kuvm* vm, kutok* name) {
  const char *chars = name->start + 1;
  int len = name->len - 2;
  return ku_pconst(vm, OBJ_VAL(ku_strfrom(vm, chars, len)));
}

static uint8_t ku_let(kuvm* vm, bool isconst, const char* msg) {
  ku_pconsume(vm, TOK_IDENT, msg);
  ku_declare_let(vm, isconst);
  if (vm->compiler->depth > 0) {
    return 0;
  }
  return ku_pidconst(vm, &vm->parser.prev);
}

static void ku_vardef(kuvm* vm, uint8_t index, bool isconst) {
  if (vm->compiler->depth > 0) {
    ku_markinit(vm);
    return;
  }
  if (isconst) {
    kustr *key = AS_STR(ku_chunk(vm)->constants.values[index]);
    kuval cinit;
    if (ku_tabget(vm, &vm->gconst, key, &cinit)) {
        ku_perr(vm, "const already defined");
    } else {
      ku_tabset(vm, &vm->gconst, key, NIL_VAL);
    }
  }
  ku_emitop2(vm, OP_DEF_GLOBAL, index);
}

static void ku_vardecl(kuvm* vm, bool isconst) {
  do {
    uint8_t g = ku_let(vm, isconst, "name expected");
    if (ku_pmatch(vm, TOK_EQ)) {
      ku_expr(vm);
    }
    else {
      ku_emitop(vm, OP_NIL);
    }
    ku_vardef(vm, g, isconst);
  } while(ku_pmatch(vm, TOK_COMMA));

  ku_pconsume(vm, TOK_SEMI, "; expected");
}

// common between functions and lambdas
static void ku_functail(kuvm *vm, kucomp *compiler, bool lambda) {
  kufunc *fn = ku_pend(vm, lambda);
  ku_emitbytes(vm, OP_CLOSURE, ku_pconst(vm, OBJ_VAL(fn)));
  for (int i = 0; i < fn->upcount; i++) {
    ku_emitbyte(vm, compiler->upvals[i].local ? 1: 0);
    ku_emitbyte(vm, compiler->upvals[i].index);
  }
}

static void ku_params(kuvm *vm) {
  do {
    vm->compiler->function->arity++;
    if (vm->compiler->function->arity > vm->max_params) {
      ku_perr(vm, "too many params");
    }

    uint8_t constant = ku_let(vm, false, "expected parameter name");
    ku_vardef(vm, constant, false);
  } while(ku_pmatch(vm, TOK_COMMA));
}

static void ku_lbody(kuvm *vm, kucomp *compiler) {
  if (ku_pmatch(vm, TOK_LBRACE)) {
    ku_block(vm, NULL);
    ku_functail(vm, compiler, false);
  } else {
    ku_expr(vm);
    ku_functail(vm, compiler, true);
  }
}

static void ku_lblock(kuvm *vm, bool lhs) {
  if (ku_pcheck(vm, TOK_STR)) {
    ku_emitop(vm, OP_TABLE);
    do {
      ku_emitop(vm, OP_DUP);
      ku_pconsume(vm, TOK_STR, "string expected");
      uint8_t name = ku_strtoname(vm, &vm->parser.prev);
      ku_pconsume(vm, TOK_EQ, "table '=' expected");
      ku_expr(vm);
      ku_emitop2(vm, OP_SET_PROP, name);
      ku_emitop(vm, OP_POP); // remove the value
    } while (ku_pmatch(vm, TOK_COMMA));
    ku_pconsume(vm, TOK_RBRACE, "'}' expected");
    // leave table on the stack
    return;
  }
}

static void ku_function(kuvm *vm, kufunc_t type) {
  kucomp compiler;
  ku_compinit(vm, &compiler, type);
  ku_beginscope(vm);
  ku_pconsume(vm, TOK_LPAR, "'(' expected after function name");
  if (!ku_pcheck(vm, TOK_RPAR)) {
    ku_params(vm);
  }
  ku_pconsume(vm, TOK_RPAR, "')' expected after parameters");
  ku_pconsume(vm, TOK_LBRACE, "'{' expected before function body");
  ku_block(vm, NULL);
  ku_functail(vm, &compiler, false);
}


static void ku_funcdecl(kuvm *vm) {
  uint8_t global = ku_let(vm, false, "function name expected");
  ku_markinit(vm);
  ku_function(vm, FUNC_STD);
  ku_vardef(vm, global, false);
}

static void ku_method(kuvm *vm) {
  ku_pconsume(vm, TOK_IDENT, "method name expected");
  uint8_t name = ku_pidconst(vm, &vm->parser.prev);
  kufunc_t type = FUNC_METHOD;

  if (vm->parser.prev.len == 4 && memcmp(vm->parser.prev.start, "init", 4) == 0) {
    type = FUNC_INIT;
  }
  ku_function(vm, type);
  ku_emitop2(vm, OP_METHOD, name);
}

static void ku_namedvar(kuvm* vm, kutok name, bool lhs);
static void ku_pvar(kuvm* vm, bool lhs);

static kutok ku_maketok(kuvm *vm, const char *text) {
  kutok tok;
  tok.start = text;
  tok.len = (int)strlen(text);
  return tok;
}

static void ku_classdecl(kuvm *vm) {
  ku_pconsume(vm, TOK_IDENT, "class name expected");
  kutok cname = vm->parser.prev;
  uint8_t name = ku_pidconst(vm, &vm->parser.prev);
  ku_declare_let(vm, false);
  ku_emitop2(vm, OP_CLASS, name);
  ku_vardef(vm, name, false);
  kuclasscomp cc;
  cc.enclosing = vm->curclass;
  cc.hassuper = false;
  vm->curclass = &cc;

  if (ku_pmatch(vm, TOK_LT)) {
    ku_pconsume(vm, TOK_IDENT, "class name expected");
    ku_pvar(vm, false); //  [.. GET_GLOBAL <name>;]

    if (ku_identeq(vm, &cname, &vm->parser.prev)) {
      ku_perr(vm, "cannot inherit from self");
    }

    ku_beginscope(vm);
    ku_addlocal(vm, ku_maketok(vm, "super"), false);
    ku_vardef(vm, 0, false);
    ku_namedvar(vm, cname, false);
    ku_emitop(vm, OP_INHERIT);
    cc.hassuper = true;
  }

  ku_namedvar(vm, cname, false);
  ku_pconsume(vm, TOK_LBRACE, "'{' expected");
  while (!ku_pcheck(vm, TOK_RBRACE) && !ku_pcheck(vm, TOK_EOF)) {
    ku_method(vm);
  }
  ku_pconsume(vm, TOK_RBRACE, "'}' expected");
  ku_emitop(vm, OP_POP);

  if (cc.hassuper) {
    ku_endscope(vm);
  }
  vm->curclass = vm->curclass->enclosing;
}

static void ku_decl(kuvm* vm, kuloop *loop) {
  if (ku_pmatch(vm, TOK_CLASS)) {
    ku_classdecl(vm);
  } else if (ku_pmatch(vm, TOK_FUN)) {
    ku_funcdecl(vm);
  } else if (ku_pmatch(vm, TOK_LET)) {
    ku_vardecl(vm, false);
  } else if (ku_pmatch(vm, TOK_CONST)) {
    ku_vardecl(vm, true);
  }
  else {
    ku_stmt(vm, loop);
  }
  if (vm->parser.panic) {
    ku_pskip(vm);
  }
}

static int ku_xvaladd(kuvm *vm, kucomp *compiler, uint8_t index, bool local) {
  int upcount = compiler->function->upcount;

  for (int i = 0; i < upcount; i++) {
    kuxval *uv = &compiler->upvals[i];
    if (uv->index == index && uv->local == local) {
      return i;
    }
  }

  if (upcount == vm->max_closures) {
    ku_err(vm, "too many closures");
    return 0;
  }
  compiler->upvals[upcount].local = local;
  compiler->upvals[upcount].index = index;
  return compiler->function->upcount++;
}

static int ku_xvalresolve(kuvm *vm, kucomp *compiler, kutok *name) {
  if (compiler->enclosing == NULL) {
    return -1;
  }

  int local = ku_resolvelocal(vm, compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].flags |= KU_LOCAL_CAPTURED;
    return ku_xvaladd(vm, compiler, (uint8_t)local, true);
  }

  int upv = ku_xvalresolve(vm, compiler->enclosing, name);
  if (upv != -1) {
    return ku_xvaladd(vm, compiler, (uint8_t)upv, false);
  }
  return -1;
}

static void ku_lambda(kuvm *vm, kutok name) {
  kucomp compiler;
  ku_compinit(vm, &compiler, FUNC_STD);
  ku_beginscope(vm);
  ku_addlocal(vm, name, false);
  ku_markinit(vm);
  ku_lbody(vm, &compiler);
  compiler.function->arity = 1;
}


static void ku_namedvar(kuvm* vm, kutok name, bool lhs) {
  int arg = ku_resolvelocal(vm, vm->compiler, &name);
  uint8_t set, get;
  bool isglobal = false;

  if (arg != -1) {
    get = OP_GET_LOCAL;
    set = OP_SET_LOCAL;
  } else if ((arg = ku_xvalresolve(vm, vm->compiler, &name)) != -1) {
    get = OP_GET_UPVAL;
    set = OP_SET_UPVAL;
  } else {
    arg = ku_pidconst(vm, &name);
    get = OP_GET_GLOBAL;
    set = OP_SET_GLOBAL;
    isglobal = true;
  }
  if (lhs && ku_pmatch(vm, TOK_EQ)) {
    if (isglobal) {
        kustr *key = AS_STR(ku_chunk(vm)->constants.values[arg]);
        kuval cinit;
        if (ku_tabget(vm, &vm->gconst, key, &cinit)) {
            ku_perr(vm, "const cannot be assigned");
        }
    }
    else {
      kulocal* local = &vm->compiler->locals[arg];
      if (local->flags & KU_LOCAL_CONST && local->flags & KU_LOCAL_INIT) {
        ku_perr(vm, "const cannot be assigned");
      }
    }
    ku_expr(vm);
    ku_emitop2(vm, set, (uint8_t)arg);
  } else if (ku_pmatch(vm, TOK_ARROW)) {
    ku_lambda(vm, name);
  } else {
    ku_emitop2(vm, get, (uint8_t)arg);
  }
}

static void ku_pvar(kuvm* vm, bool lhs) {
  ku_namedvar(vm, vm->parser.prev, lhs);
}

static void ku_this(kuvm *vm, bool lhs) {
  if (vm->curclass == NULL) {
    ku_perr(vm, "cannot use this outside a class");
    return;
  }
  ku_pvar(vm, false);
}

static void ku_super(kuvm *vm, bool lhs) {
  if (vm->curclass == NULL) {
    ku_perr(vm, "'super' must be used inside a class");
  } else if (!vm->curclass->hassuper) {
    ku_perr(vm, "cannot use super in a class with no superclass");
  }
  ku_pconsume(vm, TOK_DOT, "'.' expected after super");
  ku_pconsume(vm, TOK_IDENT, "superclass method expected");
  uint8_t name = ku_pidconst(vm, &vm->parser.prev);
  ku_namedvar(vm, ku_maketok(vm, "this"), false); // [ ... GET_LOCAL <0> ]

  if (ku_pmatch(vm, TOK_LPAR)) {
    uint8_t argc = ku_arglist(vm, TOK_RPAR);
    ku_namedvar(vm, ku_maketok(vm, "super"), false);
    ku_emitop2(vm, OP_SUPER_INVOKE, name);
    ku_emitbyte(vm, argc);
  } else {
    ku_namedvar(vm, ku_maketok(vm, "super"), false); // [ ... GET_UPVAL <0> ]
    ku_emitop2(vm, OP_GET_SUPER, name);
  }
}

static void ku_bin(kuvm *vm, bool lhs) {
  kutok_t optype = vm->parser.prev.type;
  kuprule *rule = ku_getrule(vm, optype);
  ku_prec(vm, (kup_precedence)(rule->precedence + 1));

  switch (optype) {
    case TOK_LTLT: ku_emitop(vm, OP_SHL); break;
    case TOK_GTGT: ku_emitop(vm, OP_SHR); break;
    case TOK_AMP: ku_emitop(vm, OP_BAND); break;
    case TOK_PIPE: ku_emitop(vm, OP_BOR); break;
    case TOK_PLUS: ku_emitop(vm, OP_ADD); break;
    case TOK_MINUS: ku_emitop(vm, OP_SUB); break;
    case TOK_STAR: ku_emitop(vm, OP_MUL); break;
    case TOK_SLASH: ku_emitop(vm, OP_DIV); break;
    case TOK_NE: ku_emitop2(vm, OP_EQ, OP_NOT); break;
    case TOK_EQUALITY: ku_emitop(vm, OP_EQ); break;
    case TOK_GT: ku_emitop(vm, OP_GT); break;
    case TOK_GE: ku_emitop2(vm, OP_LT, OP_NOT); break;
    case TOK_LT: ku_emitop(vm, OP_LT); break;
    case TOK_LE: ku_emitop2(vm, OP_GT, OP_NOT); break;
    default: return; // unreachable - TODO: figure out code coverage
  }
}

static void ku_dot(kuvm *vm, bool lhs) {
  ku_pconsume(vm, TOK_IDENT, "property name expected");
  uint8_t name = ku_pidconst(vm, &vm->parser.prev);
  if (lhs && ku_pmatch(vm, TOK_EQ)) {
    ku_expr(vm);
    ku_emitop2(vm, OP_SET_PROP, name);
  } else if (ku_pmatch(vm, TOK_LPAR)) {
    uint8_t argc = ku_arglist(vm, TOK_RPAR);
    ku_emitop2(vm, OP_INVOKE, name);
    ku_emitbyte(vm, argc);
  } else {
    ku_emitop2(vm, OP_GET_PROP, name);
  }
}

int ku_arraydis(kuvm *vm, const char *name, kuchunk *chunk, int offset) {
  uint16_t count = (uint16_t)(chunk->code[offset + 1] << 8);
  count |= chunk->code[offset + 2];
  ku_printf(vm, "%-16s %4d", name, count);
  return offset + 3;
}

int ku_jumpdis(kuvm *vm, const char *name, int sign, kuchunk *chunk,
int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  ku_printf(vm, "%-16s %4d -> %d", name, offset,
            offset + 3 + sign * jump);
  return offset + 3;
}

void ku_ifstmt(kuvm *vm, kuloop *loop) {
  ku_pconsume(vm, TOK_LPAR, "'(' expected after 'if'");
  ku_expr(vm);
  ku_pconsume(vm, TOK_RPAR, "'R' expected after condition");
  int then_jump = ku_emitjump(vm, OP_JUMP_IF_FALSE);
  ku_emitop(vm, OP_POP);
  ku_stmt(vm, loop);
  int else_jump = ku_emitjump(vm, OP_JUMP);
  ku_patchjump(vm, then_jump);
  ku_emitop(vm, OP_POP);
  if (ku_pmatch(vm, TOK_ELSE)) {
    ku_stmt(vm, loop);
  }
  ku_patchjump(vm, else_jump);
}

int ku_emitjump(kuvm *vm, k_op op) {
  ku_emitop(vm, op);
  ku_emitbyte(vm, 0xff);
  ku_emitbyte(vm, 0xff);
  return ku_chunk(vm)->count - 2;
}

void ku_patchjump(kuvm *vm, int offset) {
  int jump = ku_chunk(vm)->count - offset - 2;

  if (jump > vm->max_jump) {
    ku_perr(vm, "too much code to jump over");
  }

  ku_chunk(vm)->code[offset] = (jump >> 8) & 0xff;
  ku_chunk(vm)->code[offset + 1] = jump & 0xff;
}

void ku_emitloop(kuvm *vm, int start) {
  ku_emitop(vm, OP_LOOP);
  int offset = ku_chunk(vm)->count - start + 2;
  if (offset > vm->max_body) {
    ku_perr(vm, "loop body too large");
  }
  ku_emitbyte(vm, (offset >> 8) & 0xff);
  ku_emitbyte(vm, offset  & 0xff);
}

void ku_whilestmt(kuvm *vm, kuloop *loop) {
  int loop_start = ku_chunk(vm)->count;
  ku_pconsume(vm, TOK_LPAR, "'(' expected after 'while'");
  ku_expr(vm);
  ku_pconsume(vm, TOK_RPAR, "')' expected after 'while'");
  int jump_exit = ku_emitjump(vm, OP_JUMP_IF_FALSE);
  ku_emitop(vm, OP_POP);
  kuloop inner;
  ku_loopinit(vm, &inner);
  ku_stmt(vm, &inner);
  ku_emitloop(vm, loop_start);
  ku_patchjump(vm, jump_exit);

  //       +----------------------+
  //       |                      v See #37
  // POP; JUMP ; .... LOOP; POP; NIL; RET;
  uint16_t loop_end = ku_chunk(vm)->count - 1;
  ku_emitop(vm, OP_POP);
  ku_patchall(vm, &inner.continuepatch, loop_start, true);
  ku_patchall(vm, &inner.breakpatch, loop_end, false);
}

void ku_forstmt(kuvm *vm, kuloop *loop) {
  ku_beginscope(vm);
  ku_pconsume(vm, TOK_LPAR, "'(' expected after 'for'");
  if (ku_pmatch(vm, TOK_SEMI)) {
    // no init
  } else if (ku_pmatch(vm, TOK_LET)) {
    ku_vardecl(vm, false);
  } else {
    ku_exprstmt(vm, loop);
  }
  int loop_start = ku_chunk(vm)->count;
  int exit_jump = -1;

  if (!ku_pmatch(vm, TOK_SEMI)) {
    ku_expr(vm);
    ku_pconsume(vm, TOK_SEMI, "';' expected");
    exit_jump = ku_emitjump(vm, OP_JUMP_IF_FALSE);
    ku_emitop(vm, OP_POP);
  }

  if (!ku_pmatch(vm, TOK_RPAR)) {
    int body_jump = ku_emitjump(vm, OP_JUMP);
    int inc_start = ku_chunk(vm)->count;
    ku_expr(vm);
    ku_emitop(vm, OP_POP);
    ku_pconsume(vm, TOK_RPAR, "')' expected");
    ku_emitloop(vm, loop_start);
    loop_start = inc_start;
    ku_patchjump(vm, body_jump);
  }

  kuloop inner;
  ku_loopinit(vm, &inner);
  ku_stmt(vm, &inner);
  ku_emitloop(vm, loop_start);

  if (exit_jump != -1) {
    ku_patchjump(vm, exit_jump);
    ku_emitop(vm, OP_POP);
  }

  uint16_t loop_end = ku_chunk(vm)->count;
  ku_patchall(vm, &inner.continuepatch, loop_start, true);
  ku_patchall(vm, &inner.breakpatch, loop_end, false);

  ku_endscope(vm);
}

void ku_and(kuvm *vm, bool lhs) {
  int end_jump = ku_emitjump(vm, OP_JUMP_IF_FALSE);
  ku_emitop(vm, OP_POP);
  ku_prec(vm, P_AND);
  ku_patchjump(vm, end_jump);
}

void ku_or(kuvm *vm, bool lhs) {
  int else_jump = ku_emitjump(vm, OP_JUMP_IF_FALSE);
  int end_jump = ku_emitjump(vm, OP_JUMP);
  ku_patchjump(vm, else_jump);
  ku_emitop(vm, OP_POP);
  ku_prec(vm, P_OR);
  ku_patchjump(vm, end_jump);
}
kuprule ku_rules[] = {
  [TOK_LPAR] =        { ku_lambda_or_group, ku_call,  P_CALL },
  [TOK_RPAR] =        { NULL,        NULL,     P_NONE },
  [TOK_LBRACE] =      { ku_lblock,   NULL,     P_NONE },
  [TOK_RBRACE] =      { NULL,        NULL,     P_NONE },
  [TOK_LBRACKET] =    { ku_array,    ku_index, P_CALL },
  [TOK_RBRACKET] =    { NULL,        NULL,     P_NONE },

  [TOK_COMMA] =       { NULL,        NULL,     P_NONE },
  [TOK_DOT] =         { NULL,        ku_dot,   P_CALL },
  [TOK_MINUS] =       { ku_unary,    ku_bin,   P_TERM },
  [TOK_PLUS] =        { NULL,        ku_bin,   P_TERM },
  [TOK_SEMI] =        { NULL,        NULL,     P_NONE },
  [TOK_SLASH] =       { NULL,        ku_bin,   P_FACTOR },
  [TOK_STAR] =        { NULL,        ku_bin,   P_FACTOR },
  [TOK_BANG] =        { ku_unary,    NULL,     P_NONE },
  [TOK_NE] =          { NULL,        ku_bin,   P_EQ },
  [TOK_EQ] =          { NULL,        NULL,     P_NONE },
  [TOK_EQUALITY] =    { NULL,        ku_bin,   P_EQ },
  [TOK_GT] =          { NULL,        ku_bin,   P_COMP },
  [TOK_GE] =          { NULL,        ku_bin,   P_COMP },
  [TOK_LT] =          { NULL,        ku_bin,   P_COMP },
  [TOK_LE] =          { NULL,        ku_bin,   P_COMP },
  [TOK_IDENT] =       { ku_pvar,     NULL,     P_NONE },
  [TOK_STR] =         { ku_string,   NULL,     P_NONE },
  [TOK_STRESC] =      { ku_xstring,  NULL,     P_NONE },
  [TOK_NUM] =         { ku_number,   NULL,     P_NONE },
  [TOK_HEX] =         { ku_hex,      NULL,     P_NONE },
  [TOK_AND] =         { NULL,        ku_and,   P_AND },
  [TOK_CLASS] =       { NULL,        NULL,     P_NONE },
  [TOK_ELSE] =        { NULL,        NULL,     P_NONE },
  [TOK_FALSE] =       { ku_lit,      NULL,     P_NONE },
  [TOK_FOR] =         { NULL,        NULL,     P_NONE },
  [TOK_FUN] =         { ku_funcexpr, NULL,     P_NONE },
  [TOK_IF] =          { NULL,        NULL,     P_NONE },
  [TOK_NIL] =         { ku_lit,      NULL,     P_NONE },
  [TOK_OR] =          { NULL,        ku_or,    P_OR },
  [TOK_SUPER] =       { ku_super,    NULL,     P_NONE },
  [TOK_THIS] =        { ku_this,     NULL,     P_NONE },
  [TOK_TRUE] =        { ku_lit,      NULL,     P_NONE },
  [TOK_LET] =         { NULL,        NULL,     P_NONE },
  [TOK_WHILE] =       { NULL,        NULL,     P_NONE },
  [TOK_ERR] =         { NULL,        NULL,     P_NONE },
  [TOK_EOF] =         { NULL,        NULL,     P_NONE },
  [TOK_AMP] =         { NULL,        ku_bin,   P_BIT },
  [TOK_PIPE] =        { NULL,        ku_bin,   P_BIT },
  [TOK_LTLT] =        { NULL,        ku_bin,   P_SHIFT },
  [TOK_GTGT] =        { NULL,        ku_bin,   P_SHIFT },
};

static kuprule *ku_getrule(kuvm *vm, kutok_t optype) {
  return &ku_rules[optype];
}

// ********************** virtual machine **********************
void ku_reset(kuvm *vm) {
  vm->sp = vm->stack;
  vm->framecount = 0;
#ifdef STACK_CHECK
  vm->underflow = 0;
#endif // STACK_CHECK
}

void ku_push(kuvm *vm, kuval val) {
#ifdef STACK_CHECK
  if (vm->sp >= vm->stack + vm->max_stack) {
    assert(false);
  }
#endif // STACK_CHECK

  *(vm->sp) = val;
  vm->sp++;
}

kuval ku_pop(kuvm *vm) {
#ifdef STACK_CHECK
  if (vm->sp <= vm->stack) {
    vm->underflow++;
  }
#endif // STACK_CHECK
  vm->sp--;
  return *(vm->sp);
}

static bool ku_falsy(kuval v) {
  return IS_NIL(v) || (IS_BOOL(v) && !AS_BOOL(v));
}

kuval ku_peek(kuvm *vm, int distance) {
  return vm->sp[-1 - distance];
}

kuvm *ku_newvm(int stack_max) {
  kuvm *vm = malloc(sizeof(kuvm) + stack_max*sizeof(kuval));
  if (!vm) {
    // TODO: add code coverage
    return NULL;
  }
  
  vm->debugger = NULL;
  vm->allocated = sizeof(kuvm);
  vm->max_params = 255;
  vm->max_const = UINT8_MAX;
  vm->max_closures = UPSTACK_MAX;
  vm->max_jump = UINT16_MAX;
  vm->max_body = UINT16_MAX;
  vm->max_frames = FRAMES_MAX;
  vm->max_locals = LOCALS_MAX;
  vm->max_patches = PATCH_MAX;
  vm->max_stack = stack_max;

  vm->flags = 0;
  vm->curclass = NULL;
  vm->gcnext = 1024*1024;
  vm->gccount = 0;
  vm->gccap = 0;
  vm->gcstack = NULL;
  vm->err = false;
  vm->objects = NULL;
  vm->openupvals = NULL;
  vm->compiler = NULL;
  ku_tabinit(vm, &vm->strings);
  ku_tabinit(vm, &vm->globals);
  ku_tabinit(vm, &vm->gconst);
  ku_reset(vm);
  vm->initstr = NULL; // GC can run when we do str_copy below
  vm->countstr = NULL;
  vm->initstr = ku_strfrom(vm, "init", 4);
  vm->countstr = ku_strfrom(vm, "count", 5);

  return vm;
}

static void ku_freeobjects(kuvm* vm) {
  kuobj* obj = vm->objects;
  while (obj != NULL) {
    if (vm->flags & KVM_F_GCLOG) {
      ku_printf(vm, "%p dangling ", (void*)obj);
      ku_printval(vm, OBJ_VAL(obj));
      ku_printf(vm, "\n");
    }
    kuobj* next = (kuobj*)obj->next;
    ku_objfree(vm, obj);
    obj = next;
  }
}

void ku_free(kuvm *vm) {
  vm->initstr = NULL; // free_objects will take care of it
  vm->countstr = NULL;

  ku_freeobjects(vm);
  ku_tabfree(vm, &vm->strings);
  ku_tabfree(vm, &vm->globals);
  ku_tabfree(vm, &vm->gconst);
  vm->allocated -= sizeof(kuvm);
  assert(vm->allocated == 0); // TODO: figure out code coverage
  free(vm->gcstack);
  free(vm);
}

void ku_err(kuvm *vm, const char *fmt, ...) {
  va_list args;
  char out[1024];

  vm->err = true;
  va_start(args, fmt);
  vsprintf(out, fmt, args);
  va_end(args);

  ku_printf(vm, "error %s\n", out);
  for (int f = vm->framecount - 1; f >= 0; f--) {
    kuframe *frame = &vm->frames[f];
    kufunc *fn = frame->closure->func;
    size_t inst = frame->ip - fn->chunk.code - 1;
    ku_printf(vm, "[line %d] in ", fn->chunk.lines[inst]);
    if (fn->name == NULL) {
      ku_printf(vm, "__main__\n");
    } else {
      ku_printf(vm, "%s()\n", fn->name->chars);
    }
  }

  ku_reset(vm);
}

static bool ku_docall(kuvm *vm, kuclosure *cl, int argc) {
  if (argc != cl->func->arity) {
    ku_err(vm, "%d expected got %d", cl->func->arity, argc);
    return false;
  }

  if (vm->framecount == vm->max_frames) {
    ku_err(vm, "stack overflow");
    return false;
  }

  kuframe *frame = &vm->frames[vm->framecount++];
  frame->closure = cl;
  frame->ip = cl->func->chunk.code;
  frame->bp = vm->sp - argc - 1;
  return true;
}

kures ku_nativecall(kuvm *vm, kuclosure *cl, int argc) {
  int oldbase = vm->baseframe;
  vm->baseframe = vm->framecount;
  if (ku_docall(vm, cl, argc)) {
    kures res = ku_run(vm);
    vm->baseframe = oldbase;
    return res;
  }
  // TODO: add code coverage
  vm->baseframe = oldbase;
  return KVM_ERR_RUNTIME;
}

static bool ku_callvalue(kuvm *vm, kuval callee, int argc, bool *native) {
  *native = false;
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CCLASS: {
        kucclass *cc = AS_CCLASS(callee);
        if (cc->cons) {
          kuval res = cc->cons(vm, argc, vm->sp - argc);
          vm->sp -= (size_t)argc + 1;
          ku_push(vm, res);
          *native = true;
          if (!ku_objis(res, OBJ_CINST)) {
            // TODO: add code coverage
            ku_err(vm, "invalid instance");
          } else {
            kunobj *i = AS_CINST(res);
            i->klass = cc;
          }
          return !vm->err;
        }
        break;
      }
      case OBJ_CFUNC: {
        cfunc cf = AS_CFUNC(callee);
        kuval res = cf(vm, argc, vm->sp - argc);
        vm->sp -= (size_t)argc + 1;
        ku_push(vm, res);
        *native = true;
        return !vm->err;
      }
      case OBJ_CLOSURE:
        return ku_docall(vm, AS_CLOSURE(callee), argc);
      case OBJ_CLASS: {
        kuclass *c = AS_CLASS(callee);
        kuval initfn;
        vm->sp[-argc - 1] = OBJ_VAL(ku_instnew(vm, c));
        if (ku_tabget(vm, &c->methods, vm->initstr, &initfn)) {
          return ku_docall(vm, AS_CLOSURE(initfn), argc);
        } else if (argc != 0) {
          ku_err(vm, "no args expected got %d", argc);
          return false;
        }
        return true;
      }

      case OBJ_BOUND_METHOD: {
        kubound *bm = AS_BOUND_METHOD(callee);
        vm->sp[-argc - 1] = bm->receiver;
        return ku_docall(vm, bm->method, argc);
      }

      case OBJ_FUNC: // not allowed anymore
      default:
        // TODO: add code coverage
        break;
    }
  }
  ku_err(vm, "not callable");
  return false;
}

#ifdef TRACE_ENABLED
// We can't use vm->compiler at runtime
kuchunk *ku_chunk_runtime(kuvm *vm) {
  // TODO: add code coverage
  return &vm->frames[vm->framecount-1].closure->func->chunk;
}
#endif // TRACE_ENABLED

static kuxobj *ku_capture(kuvm *vm, kuval *local) {
  kuxobj *prev = NULL;
  kuxobj *uv = vm->openupvals;

  while (uv != NULL && uv->location > local) {
    // TODO: add code coverage
    prev = uv;
    uv = uv->next;
  }

  if (uv != NULL && uv->location == local) {
    // TODO: add code coverage
    return uv;
  }

  kuxobj *created = ku_xobjnew(vm, local);

  created->next = uv;

  if (prev == NULL) {
    vm->openupvals = created;
  } else {
    // TODO: add code coverage
    prev->next = created;
  }
  return created;
}

static void ku_close(kuvm *vm, kuval *last) {
  while (vm->openupvals != NULL && vm->openupvals->location >= last) {
    kuxobj *uo = vm->openupvals;
    uo->closed = *uo->location;
    uo->location = &uo->closed;
    vm->openupvals = uo->next;
  }
}

static void ku_defmethod(kuvm *vm, kustr *name) {
  kuval method = ku_peek(vm, 0);
  kuclass *c = AS_CLASS(ku_peek(vm, 1));
//  ku_printf(vm, ">> class %p method %s\n", (void*)c, name->chars);
  ku_tabset(vm, &c->methods, name, method);
  ku_pop(vm);
}

static bool ku_bindmethod(kuvm *vm, kuclass *klass, kustr *name) {
  kuval method;
  if (!ku_tabget(vm, &klass->methods, name, &method)) {
    ku_err(vm, "undefined property %s", name->chars);
    return false;
  }
  kubound *bm = ku_boundnew(vm, ku_peek(vm,0), AS_CLOSURE(method));
  ku_pop(vm);
  ku_push(vm, OBJ_VAL(bm));
  return true;
}

static bool ku_classinvoke(kuvm *vm, kuclass *klass, kustr *name, int argc) {
  kuval method;
  if (!ku_tabget(vm, &klass->methods, name, &method)) {
    ku_err(vm, "undefined property '%s'", name->chars);
    return false;
  }
  return ku_docall(vm, AS_CLOSURE(method), argc);
}

bool ku_invoke(kuvm *vm, kustr *name, int argc, bool *native) {
  kuval receiver = ku_peek(vm, argc);
  *native = false;
  if (IS_CCLASS(receiver)) {
    kucclass *cc = AS_CCLASS(receiver);
    if (cc->scall) {
      *native = true;
      kuval v = cc->scall(vm, name, argc, vm->sp - argc);
      vm->sp -= (size_t)argc +1;
      ku_push(vm, v);
      return !vm->err;
    }
    return false;
  }

  if (IS_STR(receiver)) {
    *native = true;
    kuval v = string_icall(vm, AS_OBJ(receiver), name, argc,vm->sp - argc);
    vm->sp -= (size_t)argc + 1;
    ku_push(vm, v);
    return true;
  }

  if (IS_CINST(receiver)) {
    kunobj *no = AS_CINST(receiver);
    if (no->klass->icall) {
      *native = true;
      kuval v = no->klass->icall(vm, (kuobj*)no, name, argc, vm->sp - argc);
      vm->sp -= (size_t)argc + 1;
      ku_push(vm, v);
      return true;
    }
  }

  if (IS_ARRAY(receiver)) {
    return array_invoke(vm, receiver, name, argc, vm->sp - argc);
  }
  if (!IS_INSTANCE(receiver)) {
    ku_err(vm, "instance expected");
    return false;
  }
  kuiobj *inst = AS_INSTANCE(receiver);

  kuval val;
  if (ku_tabget(vm, &inst->fields, name, &val)) {
    vm->sp[-argc - 1] = val;
    bool native;
    return ku_callvalue(vm, val, argc, &native);
  }

  return ku_classinvoke(vm, inst->klass, name, argc);
}

kures ku_run(kuvm *vm) {
  kuframe *frame = &vm->frames[vm->framecount - 1];

  vm->err = false;
  kures res = KVM_CONT;
  while (res == KVM_CONT) {
    uint8_t op;
    
    if (vm->debugger) {
      if (vm->debugger(vm) != KVM_CONT) {
        return KVM_OK;
      }
    }

#ifdef TRACE_ENABLED
    if (vm->flags & KVM_F_TRACE) {
      kuchunk *ck = ku_chunk_runtime(vm);
      ku_bytedis(vm, ck, (int) (frame->ip - ck->code));
    }
#endif // TRACE_ENABLED

    switch (op = BYTE_READ(vm)) {
      case OP_TABLE:
        ku_push(vm, ku_cinstance(vm, "table"));
        break;

      case OP_ARRAY: {
        int count = READ_SHORT(vm);
        kuaobj *ao = ku_arrnew(vm, count);
        kuval *argv = vm->sp - count;

        for (int i = 0; i < count; i++) {
          ku_arrset(vm, ao, i, argv[i]);
        }

        vm->sp -= count;
        ku_push(vm, OBJ_VAL(ao));
      }
        break;

      case OP_CALL: {
        int argc = BYTE_READ(vm);
        bool native;
        if (!ku_callvalue(vm, ku_peek(vm, argc), argc, &native)) {
          return KVM_ERR_RUNTIME;
        }
        if (!native) {
          frame = &vm->frames[vm->framecount - 1];
        }
      }
        break; // TODO: figure out code coverage

      case OP_CLASS:
        ku_push(vm, OBJ_VAL(ku_classnew(vm, READ_STRING(vm))));
        break;

      case OP_METHOD:
        ku_defmethod(vm, READ_STRING(vm));
        break;

      case OP_INVOKE: {
        bool native;
        kustr *method = READ_STRING(vm);
        int argc = BYTE_READ(vm);
        if (!ku_invoke(vm, method, argc, &native)) {
          return KVM_ERR_RUNTIME;
        }
        if (!native) {
          frame = &vm->frames[vm->framecount - 1];
        }
      }
        break; // TODO: figure out code coverage

      case OP_SUPER_INVOKE: {
        kustr *method = READ_STRING(vm);
        int argc = BYTE_READ(vm);
        kuclass *superclass = AS_CLASS(ku_pop(vm));
        if (!ku_classinvoke(vm, superclass, method, argc)) {
          return KVM_ERR_RUNTIME;
        }
        frame = &vm->frames[vm->framecount - 1];
      }
        break; // TODO: figure out code coverage

      case OP_INHERIT: {
        kuval sc = ku_peek(vm, 1);

        if (!IS_CLASS(sc)) {
          ku_err(vm, "superclass must be a class");
          return KVM_ERR_RUNTIME;
        }
        kuclass *subclass = AS_CLASS(ku_peek(vm, 0));
        kuclass *superclass = AS_CLASS(sc);
//        ku_printf(vm, ">> class %p inherits from %p\n", (void*)subclass, (void*)superclass);
        ku_tabcopy(vm, &superclass->methods, &subclass->methods);
        ku_pop(vm); // subclass
      }
        break; // TODO: figure out code coverage

      case OP_CLOSURE: {
        kufunc *fn = AS_FUNC(CONST_READ(vm));
        ku_push(vm, OBJ_VAL(fn));  // for GC
        kuclosure *cl = ku_closurenew(vm, fn);
        ku_pop(vm);
        ku_push(vm, OBJ_VAL(cl));
        for (int i = 0; i < cl->upcount; i++) {
          uint8_t local = BYTE_READ(vm);
          uint8_t index = BYTE_READ(vm);
          if (local) {
            cl->upvals[i] = ku_capture(vm, frame->bp + index);
          } else {
            cl->upvals[i] = frame->closure->upvals[index];
          }
        }
      }
        break;

      case OP_CLOSE_UPVAL:
        ku_close(vm, vm->sp - 1);
        ku_pop(vm);
        break;

      case OP_NIL:
        ku_push(vm, NIL_VAL);
        break;

      case OP_TRUE:
        ku_push(vm, BOOL_VAL(true));
        break;

      case OP_FALSE:
        ku_push(vm, BOOL_VAL(false));
        break;

      case OP_EQ: {
        kuval b = ku_pop(vm);
        kuval a = ku_pop(vm);
        ku_push(vm, BOOL_VAL(ku_equal(a, b)));
      }
        break;

      case OP_RET: {
        kuval v = ku_pop(vm);
        ku_close(vm, frame->bp);
        vm->framecount--;
        if (vm->framecount <= vm->baseframe) {
          ku_pop(vm);
          ku_push(vm, v);
          res = KVM_OK;
          break;
        }
        vm->sp = frame->bp;
        ku_push(vm, v);
        frame = &vm->frames[vm->framecount - 1];
      }
        break; // TODO: figure out code coverage

      case OP_CONST: {
        kuval con = CONST_READ(vm);
        ku_push(vm, con);
      }
        break;

      case OP_NEG: {
        if (! IS_NUM(ku_peek(vm, 0))) {
          ku_err(vm, "number expected" );
          return KVM_ERR_RUNTIME;
        }
        kuval v = ku_pop(vm);
        double dv = AS_NUM(v);
        kuval nv = NUM_VAL(-dv);
        ku_push(vm, nv);
      }
        break; // TODO: figure out code coverage

      case OP_DUP:
        ku_push(vm, ku_peek(vm, 0));
        break;

      case OP_POP:
        ku_pop(vm);
        break;

      case OP_GET_GLOBAL: {
        kustr* name = READ_STRING(vm);
        kuval value;

        if (!ku_tabget(vm, &vm->globals, name, &value)) {
          ku_err(vm, "undefined variable %s", name->chars);
          return KVM_ERR_RUNTIME;
        }
        ku_push(vm, value);
      }
        break; // TODO: figure out code coverage

      case OP_DEF_GLOBAL: {
        kustr* name = READ_STRING(vm);
        ku_tabset(vm, &vm->globals, name, ku_peek(vm, 0));
        ku_pop(vm);
      }
        break;

      case OP_SET_GLOBAL: {
        kustr* name = READ_STRING(vm);
        if (ku_tabset(vm, &vm->globals, name, ku_peek(vm, 0))) {
          ku_tabdel(vm, &vm->globals, name);
          ku_err(vm, "undefined variable %s", name->chars);
          return KVM_ERR_RUNTIME;
        }
      }
        break;

      case OP_ASET: {
        kuval val = ku_pop(vm);
        kuval ival = ku_pop(vm);
        kuval aval = ku_peek(vm, 0); // for GC

        if (!IS_ARRAY(aval)) {
          ku_err(vm, "array expected");
          return KVM_ERR_RUNTIME;
        }
        if (!IS_NUM(ival)) {
          // TODO: add code coverage
          ku_err(vm, "number index expected");
          return KVM_ERR_RUNTIME;
        }
        kuaobj *aobj = AS_ARRAY(aval);
        ku_arrset(vm, aobj, (int)AS_NUM(ival), val);
        ku_pop(vm); // array
        ku_push(vm, val);
      }
        break; // TODO figure out code coverage

      case OP_AGET: {
        kuval ival = ku_pop(vm);
        kuval aval = ku_peek(vm, 0); // for GC

        if (IS_STR(aval) && IS_NUM(ival)) {
          kustr *st = AS_STR(aval);
          int idx = (int)AS_NUM(ival);
          if (idx >= 0 && idx < st->len) {
            kustr *sr = ku_strfrom(vm, &st->chars[idx], 1);
            ku_pop(vm);
            ku_push(vm, OBJ_VAL(sr));
            break;
          } else {
            ku_err(vm, "string index out of bounds");
            return KVM_ERR_RUNTIME;
          }
        }
        if (!IS_ARRAY(aval)) {
          ku_err(vm, "array expected");
          return KVM_ERR_RUNTIME;
        }
        if (!IS_NUM(ival)) {
          // TODO: add code coverage
          ku_err(vm, "number index expected");
          return KVM_ERR_RUNTIME;
        }
        kuval v = ku_arrget(vm, AS_ARRAY(aval), (int)AS_NUM(ival));
        ku_pop(vm);
        ku_push(vm, v);
      }
        break; // TODO: figure out code coverage

      case OP_GET_LOCAL: {
        uint8_t slot = BYTE_READ(vm);
        ku_push(vm, frame->bp[slot]);
      }
        break;

      case OP_SET_LOCAL: {
        uint8_t slot = BYTE_READ(vm);
        frame->bp[slot] = ku_peek(vm, 0);
      }
        break;

      case OP_GET_UPVAL: {
        uint8_t slot = BYTE_READ(vm);
        ku_push(vm, *frame->closure->upvals[slot]->location);
      }
        break;

      case OP_SET_UPVAL: {
        uint8_t slot = BYTE_READ(vm);
        *frame->closure->upvals[slot]->location = ku_peek(vm, 0);
      }
        break;

      case OP_GET_PROP: {
        kustr *name = READ_STRING(vm);
        kuval target = ku_peek(vm, 0);
        ku_pop(vm);   // pop the target

        if (!IS_OBJ(target)) {
          ku_err(vm, "object expected");
          return KVM_ERR_RUNTIME;
        }

        kuobj *obj = AS_OBJ(target);

        if (obj->type == OBJ_ARRAY) {
          kuaobj *ao = AS_ARRAY(target);
          if (name == vm->countstr) {
            ku_push(vm, NUM_VAL(ao->elements.count));
          } else if (strcmp(name->chars, "first") == 0) {
            if (ao->elements.count > 0) {
              ku_push(vm, ao->elements.values[0]);
            } else {
              ku_push(vm, NIL_VAL);
            }
          } else if (strcmp(name->chars, "last") == 0) {
            if (ao->elements.count > 0) {
              ku_push(vm, ao->elements.values[ao->elements.count - 1]);
            } else {
              ku_push(vm, NIL_VAL);
            }
          }
          break;
        }
        if (obj->type == OBJ_STR) {
          ku_push(vm, string_iget(vm, obj, name));
          break;
        }

        if (obj->type == OBJ_CCLASS) {
          kucclass *cc = (kucclass*)obj;
          if (cc->sget) {
            ku_push(vm, cc->sget(vm, name));
            break;
          }
        }

        if (obj->type == OBJ_CINST) {
          kunobj *i = (kunobj*)obj;
          if (i->klass->iget) {
            ku_push(vm, i->klass->iget(vm, (kuobj*)i, name));
            break;
          }
        }

        if (obj->type != OBJ_INSTANCE) {
          ku_err(vm, "instance expected");
          return KVM_ERR_RUNTIME;
        }
        kuiobj *i = (kuiobj*)obj;
        kuval val;
        if (ku_tabget(vm, &i->fields, name, &val)) {
          ku_push(vm, val);
          break;
        }

        ku_push(vm, target); // See Issue #39
        if (!ku_bindmethod(vm, i->klass, name)) {
          return KVM_ERR_RUNTIME;
        }
      }
        break;

      case OP_SET_PROP: {
        if (IS_CCLASS(ku_peek(vm, 1))) {
          kucclass *cc = AS_CCLASS(ku_peek(vm, 1));
          if (cc->sput) {
            kustr *name = READ_STRING(vm);
            kuval val = ku_pop(vm);
            ku_pop(vm); // cclass
            val = cc->sput(vm, name, val);
            ku_push(vm, val);
            break;
          }
        }

        if (IS_CINST(ku_peek(vm, 1))) {
          kunobj *i = AS_CINST(ku_peek(vm, 1));
          if (i->klass->iput) {
            kustr *name = READ_STRING(vm);
            kuval val = ku_pop(vm);
            ku_pop(vm); // instance
            val = i->klass->iput(vm, (kuobj*)i, name, val);
            ku_push(vm, val);
            break;
          }
        }

        if (!IS_INSTANCE(ku_peek(vm, 1))) {
          ku_err(vm, "instance expected");
          return KVM_ERR_RUNTIME;
        }
        kuiobj *i = AS_INSTANCE(ku_peek(vm, 1));
        ku_tabset(vm, &i->fields, READ_STRING(vm), ku_peek(vm, 0));
        kuval val = ku_pop(vm);
        ku_pop(vm); // instance
        ku_push(vm, val);
      }
        break; // TODO: figure out code coverage

      case OP_GET_SUPER: {
        kustr *name = READ_STRING(vm);
        kuval v = ku_pop(vm);
        kuclass *superclass = AS_CLASS(v);
        if (!ku_bindmethod(vm, superclass, name)) {
          return KVM_ERR_RUNTIME;
        }
      }
        break;

      case OP_ADD: {
        if (IS_STR(ku_peek(vm, 0)) && IS_STR(ku_peek(vm, 1))) {
          ku_strcat(vm);
        }
        else if (IS_NUM(ku_peek(vm, 0)) && IS_NUM(ku_peek(vm, 1))) {
          double a = AS_NUM(ku_pop(vm));
          double b = AS_NUM(ku_pop(vm));
          ku_push(vm, NUM_VAL(a + b));
        }
        else {
          ku_err(vm, "numbers expected");
          return KVM_ERR_RUNTIME;
        }
      }
        break;

      case OP_SHL:
      case OP_SHR:
        if (IS_NUM(ku_peek(vm, 0)) && IS_NUM(ku_peek(vm, 1))) {
          int b = (int)AS_NUM(ku_pop(vm));
          int a = (int)AS_NUM(ku_pop(vm));
          int c = (op == OP_SHR) ? (a >> b) : (a << b);
          ku_push(vm, NUM_VAL(c));
        } else {
          ku_err(vm, "numbers expected");
          return KVM_ERR_RUNTIME;
        }
        break;

      case OP_BAND:
      case OP_BOR:
        if (IS_NUM(ku_peek(vm, 0)) && IS_NUM(ku_peek(vm, 1))) {
          int b = (int)AS_NUM(ku_pop(vm));
          int a = (int)AS_NUM(ku_pop(vm));
          int c = (op == OP_BAND) ? (a & b) : (a | b);
          ku_push(vm, NUM_VAL(c));
        } else {
          ku_err(vm, "numbers expected");
          return KVM_ERR_RUNTIME;
        }
        break;

      case OP_SUB: BIN_OP(vm,NUM_VAL, -); break;
      case OP_MUL: BIN_OP(vm,NUM_VAL, *); break;
      case OP_DIV: BIN_OP(vm,NUM_VAL, /); break;
      case OP_GT:
      case OP_LT:
        if (IS_NUM(ku_peek(vm, 0)) && IS_NUM((ku_peek(vm, 1)))) {
          double b = (int)AS_NUM(ku_pop(vm));
          double a = (int)AS_NUM(ku_pop(vm));
          if (op == OP_GT)
            ku_push(vm, BOOL_VAL(a > b));
          else
            ku_push(vm, BOOL_VAL(a < b));
        } else if (IS_STR(ku_peek(vm, 0)) && IS_STR(ku_peek(vm, 1))) {
          kustr *b = AS_STR(ku_pop(vm));
          kustr *a = AS_STR(ku_pop(vm));
          int len = (a->len > b->len) ? a->len : b->len;
          int res = strncmp(a->chars, b->chars, len);
          if (op == OP_GT)
            ku_push(vm, BOOL_VAL(res > 0));
          else
            ku_push(vm, BOOL_VAL(res < 0));
        } else {
          ku_err(vm, "numbers or strings expected");
          return KVM_ERR_RUNTIME;
        }
        break;

//      case OP_LT: BIN_OP(vm,BOOL_VAL, <); break;
      case OP_NOT:
        ku_push(vm, BOOL_VAL(ku_falsy(ku_pop(vm))));
        break;
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT(vm);
        if (ku_falsy(ku_peek(vm, 0))) {
          frame->ip += offset;
        }
      }
        break;

      case OP_JUMP: {
        uint16_t offset = READ_SHORT(vm);
        frame->ip += offset;
      }
        break;

      case OP_LOOP: {
        uint16_t offset = READ_SHORT(vm);
        frame->ip -= offset;
      }
        break;
    }

#ifdef TRACE_ENABLED
    if (vm->flags & KVM_F_TRACE && vm->flags & KVM_F_STACK) {
     ku_printstack(vm);
    } else {
      if (vm->flags & KVM_F_TRACE) {
        ku_printf(vm, "\n");
      }
    }
#endif // TRACE_ENABLED
  }
  return KVM_OK;
}

kufunc *ku_compile(kuvm *vm, char *source) {
  kucomp compiler;
  ku_compinit(vm, &compiler, FUNC_MAIN);
  ku_lexinit(vm, source);
  vm->parser.err = false;
  vm->parser.panic = false;
  ku_pnext(vm);
  while (!ku_pmatch(vm, TOK_EOF)) {
    ku_decl(vm, NULL);
  }

  if (vm->flags & KVM_F_DISASM) {
    ku_chunkdump(vm, ku_chunk(vm), "code");
  }

  kufunc *fn = ku_pend(vm, false);


  return vm->parser.err ? NULL : fn;
}

kures ku_exec(kuvm *vm, char *source) {
  kufunc *fn = ku_compile(vm, source);
  if (fn == NULL) {
    return KVM_ERR_SYNTAX;
  }

  if (vm->flags & KVM_F_LIST) {
    ku_chunkdump(vm, &fn->chunk, fn->name ? fn->name->chars : "__main__");
  }

  if (vm->flags & KVM_F_NOEXEC) {
    return KVM_OK;
  }

  ku_push(vm, OBJ_VAL(fn));
  kuclosure *closure = ku_closurenew(vm, fn);
  ku_pop(vm);
  ku_push(vm, OBJ_VAL(closure));
  vm->baseframe = 0;
  ku_docall(vm, closure, 0);
  kures res = ku_run(vm);

  // runtime error -> stack is invalid, reset
  if (res != KVM_OK) {
    ku_reset(vm);
  } else {
    ku_pop(vm); // remove last return item for global call
  }
  return res;
}

// ********************** memory **********************
char *ku_alloc(kuvm *vm, void *ptr, size_t oldsize, size_t nsize) {
  if ((vm->flags & KVM_F_GCSTRESS) && nsize > oldsize) {
    ku_gc(vm);
  }

  // We don't trigger GC if we're trying to free things
  // so we don't recurse during GC
  if (vm->allocated > vm->gcnext && nsize > oldsize) {
    if (vm->flags & KVM_F_GCLOG) {
      ku_printf(vm, "%zu allocated %zu next -> gc()\n",
                vm->allocated, vm->gcnext);
    }
    ku_gc(vm);
  }

  if (vm->flags & KVM_F_TRACEMEM) {
    ku_printf(vm, "malloc %d -> %d\n", (int)oldsize, (int)nsize);
  }

  vm->allocated += nsize - oldsize;

  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

// ********************** chunk **********************
void ku_chunkinit(kuvm *vm, kuchunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  ku_arrinit(vm, &chunk->constants);
}

void ku_chunkwrite(kuvm *vm, kuchunk *chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int cap = chunk->capacity;
    chunk->capacity = CAPACITY_GROW(cap);
    chunk->code = ARRAY_GROW(vm, uint8_t, chunk->code, cap, chunk->capacity);
    chunk->lines = ARRAY_GROW(vm, int, chunk->lines, cap, chunk->capacity);
    assert(chunk->code != NULL); // TODO: figure out code coverage
  }
  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

void ku_chunkfree(kuvm *vm, kuchunk *chunk) {
  ARRAY_FREE(vm, uint8_t, chunk->code, chunk->capacity);
  ARRAY_FREE(vm, int, chunk->lines, chunk->capacity);
  ARRAY_FREE(vm, kuval, chunk->constants.values, chunk->constants.capacity);
}

int ku_chunkconst(kuvm *vm, kuchunk *chunk, kuval value) {
  ku_push(vm, value); // for GC
  for (int i = 0; i < chunk->constants.count; i++) {
    if (ku_equal(value, chunk->constants.values[i])) {
      ku_pop(vm);
      return i;
    }
  }
  ku_arrwrite(vm, &chunk->constants, value);
  ku_pop(vm);
  return chunk->constants.count - 1;
}

// ********************** array **********************
void ku_arrinit(kuvm* vm, kuarr *array) {
  array->values = NULL;
  array->count = 0;
  array->capacity = 0;
}

void ku_arrwrite(kuvm* vm, kuarr *array, kuval value) {
  if (array->capacity < array->count + 1) {
    int old = array->capacity;
    array->capacity = CAPACITY_GROW(old);
    array->values = ARRAY_GROW(vm, kuval, array->values, old, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}

// ********************** debugging **********************
static void ku_chunkdump(kuvm *vm, kuchunk *chunk, const char * name) {
  ku_printf(vm, "== %s ==\n", name);
  for (int offset = 0; offset < chunk->count; ) {
    offset = ku_bytedis(vm, chunk, offset);
    ku_printf(vm, "\n");
  }
}

static int ku_opdis(kuvm *vm, const char *name, int offset) {
  ku_printf(vm, "%-16s %4s", name, "");
  return offset + 1;
}

static int ku_constdis(kuvm *vm, const char *name, kuchunk *chunk, int offset) {
  uint8_t con = chunk->code[offset+1];
  ku_printf(vm, "%-16s %4d '", name, con);
  ku_printval(vm, chunk->constants.values[con]);
  ku_printf(vm, "'");
  return offset+2;
}

static int ku_invokedis(kuvm *vm, const char *name, kuchunk *chunk, int offset) {
  uint8_t cons = chunk->code[offset + 1];
  uint8_t argc = chunk->code[offset + 2];
  ku_printf(vm, "%-16s (%d args) %4d'", name, argc, cons);
  ku_printval(vm, chunk->constants.values[cons]);
  ku_printf(vm, "'\n");
  return offset + 3;
}

int ku_bytedis(kuvm *vm, kuchunk *chunk, int offset) {
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
    case OP_NIL: return ku_opdis(vm, "OP_NIL", offset);
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
      // TODO: add code coverage
      return ku_invokedis(vm, "OP_SUPER_INVOKE", chunk, offset);
    case OP_INHERIT: return ku_opdis(vm, "OP_INHERIT", offset);
    case OP_CONST: return ku_constdis(vm, "OP_CONST", chunk, offset);
    case OP_DEF_GLOBAL: return ku_constdis(vm, "OP_DEF_GLOBAL", chunk, offset);
    case OP_GET_GLOBAL: return ku_constdis(vm, "OP_GET_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL: return ku_constdis(vm, "OP_SET_GLOBAL", chunk, offset);
    case OP_GET_LOCAL: return ku_opslotdis(vm, "OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL: return ku_opslotdis(vm, "OP_SET_LOCAL", chunk, offset);
    case OP_GET_UPVAL:
      // TODO: add code coverage
      return ku_opslotdis(vm, "OP_GET_UPVAL", chunk, offset);
    case OP_SET_PROP: return ku_opslotdis(vm, "OP_SET_PROP", chunk, offset);
    case OP_GET_PROP: return ku_opslotdis(vm, "OP_GET_PROP", chunk, offset);
    case OP_SET_UPVAL:
      // TODO: add code coverage
      return ku_opslotdis(vm, "OP_SET_UPVAL", chunk, offset);
    case OP_GET_SUPER:
      // TODO: add code coverage
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
      kufunc *fn = AS_FUNC(chunk->constants.values[con]);
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
#undef OP_DEF1
}

// ------------------------------------------------------------
// Locals
// ------------------------------------------------------------
void ku_compinit(kuvm *vm, kucomp *compiler, kufunc_t type) {
  compiler->function = NULL; // for GC
  compiler->enclosing = vm->compiler;
  compiler->count = 0;
  compiler->depth = 0;
  compiler->type = type;
  compiler->function = ku_funcnew(vm);

  vm->compiler = compiler;
  if (type != FUNC_MAIN) {
    compiler->function->name = ku_strfrom(vm, vm->parser.prev.start, vm->parser.prev.len);
  }
  kulocal *local = &vm->compiler->locals[vm->compiler->count++];
  local->depth = 0;
  local->flags = KU_LOCAL_NONE;

  if (type != FUNC_STD) {
    local->name.start = "this";
    local->name.len = 4;
  } else {
    local->name.start = "";
    local->name.len = 0;
  }
}

void ku_block(kuvm *vm, kuloop *loop) {
  while (!ku_pcheck(vm, TOK_RBRACE) && !ku_pcheck(vm, TOK_EOF)) {
    ku_decl(vm, loop);
  }
  ku_pconsume(vm, TOK_RBRACE, "'}' expected");
}

void ku_beginscope(kuvm *vm) {
  vm->compiler->depth++;
}

void ku_endscope(kuvm *vm) {
  vm->compiler->depth--;

  while (vm->compiler->count > 0 &&
         vm->compiler->locals[vm->compiler->count - 1].depth >
         vm->compiler->depth) {

    if (KU_IS_CAPTURED(vm->compiler->locals[vm->compiler->count - 1])) {
      ku_emitop(vm, OP_CLOSE_UPVAL);
    } else {
      ku_emitop(vm, OP_POP);
    }
    vm->compiler->count--;
    }
}

void ku_declare_let(kuvm *vm, bool isconst) {
  kutok *name = &vm->parser.prev;
  
  if (vm->compiler->depth == 0) {
    return;
  }
  for (int i = vm->compiler->count - 1; i >= 0; i--) {
    kulocal *local = &vm->compiler->locals[i];
    if (local->depth != -1 && local->depth < vm->compiler->depth) {
      break;
    }

    if (ku_identeq(vm, name, &local->name)) {
      // TODO: add code coverage
      ku_perr(vm, "local already defined");
    }
  }
  ku_addlocal(vm, *name, isconst);
}

void ku_addlocal(kuvm *vm, kutok name, bool isconst) {
  if (vm->compiler->count == vm->max_locals) {
    ku_perr(vm, "too many locals");
    return;
  }

  kulocal *local = &vm->compiler->locals[vm->compiler->count++];
  local->name = name;
  local->depth = -1;
  if ((local->flags & KU_LOCAL_CONST) && (local->flags & KU_LOCAL_INIT)) {
    ku_perr(vm, "const already initialized");
  }

  local->flags = (isconst) ? KU_LOCAL_CONST : KU_LOCAL_NONE;
}

bool ku_identeq(kuvm *vm, kutok *a, kutok *b) {
  if (a->len != b->len) {
    return false;
  }

  return memcmp(a->start, b->start, a->len) == 0;
}

int ku_resolvelocal(kuvm *vm, kucomp *compiler, kutok *name) {
  for (int i = compiler->count - 1; i >= 0; i--) {
    kulocal *local = &compiler->locals[i];
    if (ku_identeq(vm, name, &local->name)) {
      if (local->depth == -1) {
        ku_perr(vm, "own initialization disallowed");
      }
      return i;
    }
  }
  return -1;
}

void ku_markinit(kuvm *vm) {
  // functions can reference themselves unlike global
  // variables which can't use their own value in their
  // initialization
  if (vm->compiler->depth == 0) return;
  kulocal *local = &vm->compiler->locals[vm->compiler->count - 1];
  local->depth = vm->compiler->depth;
  local->flags |= KU_LOCAL_INIT;
}

int ku_opslotdis(kuvm *vm, const char *name, kuchunk *chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  ku_printf(vm, "%-16s %4d", name, slot);
  return offset + 2;
}

// ********************** function **********************
kufunc *ku_funcnew(kuvm *vm) {
  kufunc *fn = (kufunc*)KALLOC_OBJ(vm, kufunc, OBJ_FUNC);
  fn->arity = 0;
  fn->upcount = 0;
  fn->name = NULL;
  ku_chunkinit(vm, &fn->chunk);
  return fn;
}

// ********************** native functions **********************
kucfunc *ku_cfuncnew(kuvm *vm, cfunc f) {
  kucfunc *cf = KALLOC_OBJ(vm, kucfunc, OBJ_CFUNC);
  cf->fn = f;
  return cf;
}

void ku_cfuncdef(kuvm *vm, const char *name, cfunc f) {
  int len = (int)strlen(name);
  kustr *sname = ku_strfrom(vm, name, len);
  ku_push(vm, OBJ_VAL(sname));
  ku_push(vm, OBJ_VAL(ku_cfuncnew(vm, f)));
  ku_tabset(vm, &vm->globals, AS_STR(vm->stack[0]), vm->stack[1]);
  ku_pop(vm);
  ku_pop(vm);
}

// ********************** library **********************
#define M2(c,s) (c->len==2 && c->chars[0]==s[0] && \
                              c->chars[1]==s[1])

#define M3(c,s) (c->len==3 && c->chars[0]==s[0] && \
                              c->chars[1]==s[1] && \
                              c->chars[2]==s[2])

#include <time.h>

// ********************** array **********************
static kuval ku_arraycons(kuvm *vm, int argc, kuval *argv) {
  int cap = 0;
  if (argc > 0 && IS_NUM(argv[0])) {
    cap = (int)AS_NUM(argv[0]);
    kuaobj *ao = ku_arrnew(vm, cap);
    ku_push(vm, OBJ_VAL(ao)); // for GC
    for (int i = 0; i < cap; i++) {
      ku_arrset(vm, ao, i, NIL_VAL);
    }
    ku_pop(vm); // for GC
    return OBJ_VAL(ao);
  }
  // TODO: add code coverage
  return OBJ_VAL(ku_arrnew(vm, cap));
}

void array_swap(kuval *a, kuval *b) {
  kuval t = *a;
  *a = *b;
  *b = t;
}

int array_partition(kuvm *vm, kuval array[], int low, int high, kuclosure *cmp) {
  kuval pivot = array[high];
  int i = (low - 1);

  for (int j = low; j < high; j++) {
    int compres = 0;
    ku_push(vm, array[j]);
    ku_push(vm, pivot);
    if (ku_nativecall(vm, cmp, 2) == KVM_OK) {
      kuval ret = ku_pop(vm);
      ku_pop(vm); // arg
      if (!IS_NUM(ret)) {
        ku_err(vm, "sort compare non-number return");
        return 0;
      }
      compres = (int)AS_NUM(ret);
    } else {
      // TODO: add code coverage
      ku_err(vm, "sort invalid compare function");
      return 0;
    }

    if (compres < 0) {
      i++;
      array_swap(&array[i], &array[j]);
    }
  }
  array_swap(&array[i + 1], &array[high]);
  return (i + 1);
}

void array_qsort(kuvm *vm, kuval array[], int low, int high, kuclosure *cmp) {
  if (low < high) {
    int pi = array_partition(vm, array, low, high, cmp);
    if (vm->err) return;
    array_qsort(vm, array, low, pi - 1, cmp);
    array_qsort(vm, array, pi + 1, high, cmp);
  }
}

bool array_sort(kuvm *vm, kuaobj *src, int argc, kuval *argv) {
  if (argc != 1 || !IS_CLOSURE(argv[0])) {
    return false;
  }

  kuclosure *cl = AS_CLOSURE(argv[0]);
  if (cl->func->arity != 2) {
    return false;
  }
  array_qsort(vm, src->elements.values, 0, src->elements.count-1, cl);
  ku_pop(vm);   // closure
  ku_pop(vm);   // receiver
  return !vm->err;
}

bool array_filter(kuvm *vm, kuaobj *src, int argc, kuval *argv, kuval *ret) {
  if (argc != 1 || !IS_CLOSURE(argv[0])) {
    return false;
  }

  kuclosure *cl = AS_CLOSURE(argv[0]);
  if (cl->func->arity != 1) {
    return false;
  }

  kuaobj *dest = ku_arrnew(vm, 0);
  ku_push(vm, OBJ_VAL(dest)); // for GC
  int j = 0;
  for (int i = 0; i < src->elements.count; i++) {
    kuval e = ku_arrget(vm, src, i);
    ku_push(vm, e);
    if (ku_nativecall(vm, cl, 1) == KVM_OK) {
      kuval n = ku_pop(vm);
      if (!ku_falsy(n)) {
        ku_arrset(vm, dest, j++, src->elements.values[i]);
      }
    } else {
      // TODO: add code coverage
      ku_pop(vm); // GC
      return false;
    }
  }
  ku_pop(vm); // GC
  ku_pop(vm);   // closure
  ku_pop(vm);   // receiver
  *ret = OBJ_VAL(dest);
  return true;
}

bool array_map(kuvm *vm, kuaobj *src, int argc, kuval *argv, kuval *ret) {
  if (argc != 1 || !IS_CLOSURE(argv[0])) {
    return false;
  }

  kuclosure *cl = AS_CLOSURE(argv[0]);
  if (cl->func->arity != 1) {
    return false;
  }

  kuaobj *dest = ku_arrnew(vm, src->elements.capacity);
  ku_push(vm, OBJ_VAL(dest)); // for GC
  for (int i = 0; i < src->elements.count; i++) {
    kuval e = ku_arrget(vm, src, i);
    ku_push(vm, e);
    if (ku_nativecall(vm, cl, 1) == KVM_OK) {
      kuval n = ku_pop(vm);
      ku_arrset(vm, dest, i, n);
    } else {
      ku_pop(vm); // GC
      return false;
    }
  }
  ku_pop(vm); // GC
  ku_pop(vm);   // closure
  ku_pop(vm);   // receiver
  *ret = OBJ_VAL(dest);
  return true;
}

bool array_reduce(kuvm *vm, kuaobj *src, int argc, kuval *argv, kuval *ret) {
  if (argc != 2 || !IS_CLOSURE(argv[1])) {
    return false;
  }

  kuclosure *cl = AS_CLOSURE(argv[1]);
  if (cl->func->arity != 2) {
    return false;
  }

  kuval arg = argv[0];
  for (int i = 0; i < src->elements.count; i++) {
    kuval e = ku_arrget(vm, src, i);
    ku_push(vm, arg);
    ku_push(vm, e);
    if (ku_nativecall(vm, cl, 2) == KVM_OK) {
      arg = ku_pop(vm);
      ku_pop(vm); // arg
    } else {
      return false;
    }
  }
  ku_pop(vm);   // closure
  ku_pop(vm);   // arg
  ku_pop(vm);   // receiver
  *ret = arg;
  return true;
}

bool array_invoke(kuvm *vm, kuval obj, kustr *method, int argc, kuval *argv) {
  if (M3(method, "map")) {
    kuval ret;
    if(array_map(vm, AS_ARRAY(obj), argc, argv, &ret)) {
      ku_push(vm, ret);
      return true;
    }
    return false;
  }

  if (method->len == 4 && strcmp(method->chars, "sort") == 0) {
    bool ret = array_sort(vm, AS_ARRAY(obj), argc, argv);
    if (ret)
      ku_push(vm, NIL_VAL);
    return ret;
  }

  if (method->len == 6 && strcmp(method->chars, "filter") == 0) {
    kuval ret;
    if(array_filter(vm, AS_ARRAY(obj), argc, argv, &ret)) {
      ku_push(vm, ret);
      return true;
    }
    return false;
  }

  if (method->len == 6 && strcmp(method->chars, "reduce") == 0) {
    kuval ret;
    if(array_reduce(vm, AS_ARRAY(obj), argc, argv, &ret)) {
      ku_push(vm, ret);
      return true;
    }
  }
  return false;
}

// ********************** utility **********************
static int arglen(kuvm *vm, const char *f, kuval arg) {
  char s = *(f+1);
  if (s == 0) {
    return 0;
  }

  int len = 0;
  char buff[255];

  switch (s) {
    case 'd':
      if (IS_NUM(arg)) {
        sprintf(buff, "%d", (int)AS_NUM(arg));
        return (int)strlen(buff);
      }
      return -1;
    case 'g':
      if (IS_NUM(arg)) {
        sprintf(buff, "%g", AS_NUM(arg));
        return (int)strlen(buff);
      }
      return -1;
    case 'f':
      if (IS_NUM(arg)) {
        sprintf(buff, "%f", AS_NUM(arg));
        return (int)strlen(buff);
      }
      return -1;
    case 'x':
      if (IS_NUM(arg)) {
        sprintf(buff, "%x", (int)AS_NUM(arg));
        return (int)strlen(buff);
      }
      return -1;
    case 's':
      if (IS_STR(arg)) {
        return AS_STR(arg)->len;
      }
      return -1;
    case 'b':
      if (IS_BOOL(arg)) {
        return 5;
      }
      return -1;
  }
  // TODO: add code coverage
  return len;
}

char *format_core(kuvm *vm, int argc, kuval *argv, int *count) {
  if (argc < 1 || !IS_STR(argv[0])) {
    return NULL;
  }

  kustr *sfmt = AS_STR(argv[0]);
  const char *fmt = sfmt->chars;
  int fmtlen = 0;
  int needed = fmtlen;

  int iarg = 0;
  for (int i = 0; i < sfmt->len; i++) {
    if (fmt[i] == '%') {
      if (iarg < argc) {
        int len = arglen(vm, fmt+i, argv[++iarg]);
        if (len < 0) {
          ku_err(vm, "%%%c invalid argument %d", fmt[i+1], iarg);
          return NULL;
        }
        needed += len;
        i++;
      }
    } else {
      needed++;
    }
  }

  char *chars = ku_alloc(vm, NULL, 0, (size_t)needed+1);

  iarg = 0;
  char *d = chars;

  for (char *p = (char*)fmt; *p; p++) {
    if (*p != '%') {
      *d++ = *p;
      continue;
    }

    char esc = p[1];
    kuval arg = argv[++iarg];
    if (esc == 'd') {
      sprintf(d, "%d", (int)AS_NUM(arg));
    } else if (esc == 'x') {
      sprintf(d, "%x", (int)AS_NUM(arg));
    } else if (esc == 'g') {
      sprintf(d, "%g", AS_NUM(arg));
    } else if (esc == 'f') {
      sprintf(d, "%f", AS_NUM(arg));
    } else if (esc == 's') {
      strcpy(d, AS_STR(arg)->chars);
    } else if (esc == 'b')  {
      if (AS_BOOL(arg)) strcpy(d, "true");
      else strcpy(d, "false");
    } else {
      ku_alloc(vm, chars, (size_t)needed + 1, 0);
      ku_err(vm, "unexpected format escape %c", esc);
      return NULL;
    }
    d = d + strlen(d);
    p++;
  }
  *d = '\0';

  if (count) {
    *count = needed;
  }
  return chars;
}

static kuval ku_intf(kuvm *vm, int argc, kuval *argv) {
  if (argc > 0 && IS_NUM(argv[0])) {
    double d = AS_NUM(argv[0]);
    int i = (int)d;
    return NUM_VAL((double)i);
  }
  return NIL_VAL;
}

static kuval ku_parseFloat(kuvm *vm, int argc, kuval *argv) {
  if (argc > 0 && IS_STR(argv[0])) {
    kustr *s = AS_STR(argv[0]);
    double d = atof(s->chars);
    return NUM_VAL(d);
  }
  return NIL_VAL;
}

static kuval ku_eval(kuvm *vm, int argc, kuval *argv) {
  if (argc > 0 && IS_STR(argv[0])) {
    int stack = (argc > 1 && IS_NUM(argv[1])) ? (int)AS_NUM(argv[1]) : 128;
    kuvm *temp = ku_newvm(stack);
    temp->flags = KVM_F_QUIET;
    const char *line = AS_STR(argv[0])->chars;
    size_t len = strlen(line) + 8;
    char *buffer = malloc(len);
    assert(buffer); // TODO: figure out code coverage
    sprintf(buffer, "let _=%s;", line);
    kures res = ku_exec(temp, buffer);
    kuval ret = NIL_VAL;
    free(buffer);
    if (res == KVM_OK) {
      kustr *key = ku_strfrom(temp, "_", 1);
      ku_tabget(temp, &temp->globals, key, &ret);
    }
    ku_free(temp);
    return ret;
  }
  return NIL_VAL;
}

static kuval ku_clock(kuvm *vm, int argc, kuval *argv) {
  return NUM_VAL((double)clock() / CLOCKS_PER_SEC);
}

static kuval ku_print(kuvm *vm, int argc, kuval *argv) {
  if (argc > 0 && !IS_STR(argv[0])) {
    for (int i = 0; i < argc; i++) {
      ku_printval(vm, argv[i]);
    }
    return NIL_VAL;
  }

  int needed;
  char *str = format_core(vm, argc, argv, &needed);
  if (!str) {
    // TODO: add code coverage
    return NIL_VAL;
  }
  ku_printf(vm, str);
  ku_alloc(vm, str, (size_t)needed+1, 0);
  return NIL_VAL;
}

// ********************** math **********************
#define  _USE_MATH_DEFINES    // for windows
#include <math.h>

kuval math_scall(kuvm *vm, kustr *m, int argc, kuval *argv) {
  double x = AS_NUM(argv[0]);

  if (M3(m, "sin")) {
    return NUM_VAL(sin(x));
  } else if (M3(m, "cos")) {
    return NUM_VAL(cos(x));
  } else if (M3(m, "tan")) {
    return NUM_VAL(tan(x));
  } else if (strcmp(m->chars, "sqrt")==0) {
    return NUM_VAL(sqrt(x));
  } else if (strcmp(m->chars, "imod")==0 && argc == 2) {
    int a = (int)AS_NUM(argv[0]);
    int b = (int)AS_NUM(argv[1]);
    return NUM_VAL(a % b);
  } else if (strcmp(m->chars, "fmod") == 0 && argc == 2) {
    return NUM_VAL(fmod(AS_NUM(argv[0]), AS_NUM(argv[1])));
  } else if (strcmp(m->chars, "abs") == 0 && argc == 1) {
    double d = AS_NUM(argv[0]);
    if (d < 0)
      return NUM_VAL(-1);
    if (d > 0)
      return NUM_VAL(1);
    return NUM_VAL(0);
  } else if (strcmp(m->chars, "floor") == 0 && argc == 1) {
    return NUM_VAL(floor(AS_NUM(argv[0])));
  } else if (strcmp(m->chars, "round") == 0 && argc == 1) {
    return NUM_VAL(round(AS_NUM(argv[0])));
  } else if (M3(m, "pow") && argc == 2) {
    return NUM_VAL(pow(x,AS_NUM(argv[1])));
  }
  return NIL_VAL;
}


// ********************** table **********************
kuval table_cons(kuvm *vm, int argc, kuval *argv) {
  kutobj *to = (kutobj*)ku_objalloc(vm, sizeof(kutobj), OBJ_CINST);
  ku_tabinit(vm, &to->data);
  return OBJ_VAL(to);
}

kuval ku_cinstance(kuvm *vm, const char *cname) {
  kuval tcv;
  if (!ku_tabget(vm, &vm->globals, ku_strfrom(vm, cname, 5), &tcv)) {
    // TODO: add code coverage
    return NIL_VAL;
  }

  if (!IS_CCLASS(tcv)) {
    // TODO: add code coverage
    return NIL_VAL;
  }

  kucclass *tcc = AS_CCLASS(tcv);
  kuval tab = table_cons(vm, 0, NULL);
  kutobj *to = (kutobj*)AS_OBJ(tab);
  to->base.klass = tcc;
  return tab;
}

kuval table_icall(kuvm *vm, kuobj *o, kustr *m, int argc, kuval *argv) {
  if (M3(m, "get") && argc > 0 && IS_STR(argv[0])) {
    kutobj *to = (kutobj*)o;
    kustr *s = AS_STR(argv[0]);
    kuval ret;
    if (ku_tabget(vm, &to->data, s, &ret)) {
      return ret;
    }
    return NIL_VAL;
  }

  if (M3(m, "set") && argc > 1 && IS_STR(argv[0])) {
    kutobj *to = (kutobj*)o;
    kustr *s = AS_STR(argv[0]);
    ku_tabset(vm, &to->data, s, argv[1]);
    return argv[1];
  }

  if (m->len == 4 && strcmp(m->chars, "iter") == 0) {
    if (argc != 1 || !IS_CLOSURE(argv[0])) {
      return NIL_VAL;
    }

    kuclosure *cl = AS_CLOSURE(argv[0]);
    if (cl->func->arity != 2) {
      return NIL_VAL;
    }

    kutobj *to = (kutobj*)o;
    for (int i = 0; i < to->data.capacity; i++) {
      kuentry *e = &to->data.entries[i];
      if (e->key != NULL) {
        ku_push(vm, OBJ_VAL(e->key));
        ku_push(vm, e->value);
        ku_nativecall(vm, cl, 2);
        ku_pop(vm); // call result
        ku_pop(vm); // key
      }
    }
  }
  return NIL_VAL;
}

kuval table_iget(kuvm *vm, kuobj *o, kustr *p) {
  kutobj *to = (kutobj*)o;
  kuval ret;

  if (ku_tabget(vm, &to->data, p, &ret)) {
    return ret;
  }
  return NIL_VAL;
}

kuval table_iput(kuvm *vm, kuobj *o, kustr *p, kuval v) {
  kutobj *to = (kutobj*)o;
  ku_tabset(vm, &to->data, p, v);
  return v;
}

kuval table_ifree(kuvm *vm, kuobj *o) {
  kutobj *to = (kutobj*)o;
  ku_tabfree(vm, &to->data);
  FREE(vm, kutobj, to);
  return NIL_VAL;
}

kuval table_imark(kuvm *vm, kuobj *o) {
  kutobj *to = (kutobj*)o;
  ku_marktable(vm, &to->data);
  return NIL_VAL;
}

// ********************** string **********************
kuval string_format(kuvm *vm, int argc, kuval *argv) {
  int needed;
  char *chars = format_core(vm, argc, argv, &needed);
  if (!chars) {
    return NIL_VAL;
  }
  return OBJ_VAL(ku_strtake(vm, chars, needed));
}

static kuval string_frombytes(kuvm *vm, kuaobj *arr) {
  int len = arr->elements.count;
  char* buff = KALLOC(vm, char, (size_t)len + 1);
  for (int i = 0; i < len; i++) {
    buff[i] = (int)AS_NUM(ku_arrget(vm, arr, i));
  }
  buff[len] = '\0';
  kustr* res = ku_strtake(vm, buff, len);
  return OBJ_VAL(res);
}

kuval string_scall(kuvm *vm, kustr *m, int argc, kuval *argv) {
  if (strcmp(m->chars, "format")==0) {
    return string_format(vm, argc, argv);
  } else if (strcmp(m->chars, "frombytes") == 0 && argc == 1 && IS_ARRAY(argv[0])) {
    return string_frombytes(vm, AS_ARRAY(argv[0]));
  }
  return NIL_VAL;
}

kuval string_iget(kuvm *vm, kuobj *obj, kustr *prop) {
  if (prop == vm->countstr) {
    kustr *str = (kustr*)obj;
    return NUM_VAL(str->len);
  }
  return NIL_VAL;
}

kuval string_icall(kuvm *vm, kuobj *o, kustr *m, int argc, kuval *argv) {
  if (m->len == 6 && strcmp(m->chars, "substr") == 0) {
    kustr *s = (kustr*)o;
    int start = 0;
    int end = s->len;
    bool empty = false;

    if (argc > 0 && IS_NUM(argv[0]))
      start = (int)AS_NUM(argv[0]);

    if (argc > 1 && IS_NUM(argv[1]))
      end = (int)AS_NUM(argv[1]);

    if (argc > 2 && IS_BOOL(argv[2]))
      empty = AS_BOOL(argv[2]);

    if (start < 0 && !empty) start = s->len + start;
    if (end < 0  && !empty) end = s->len + end;

    if (start < 0 || start >= s->len) {
      if (empty) return OBJ_VAL(ku_strfrom(vm, "", 0));
      start = 0;
    }
    if (end >= s->len || end < start) {
      if (empty) return OBJ_VAL(ku_strfrom(vm, "", 0));
      end = s->len - 1;
    }

    int len = end - start + 1;
    char* buff = KALLOC(vm, char, (size_t)len + 1);
    for (int i = 0; i < len; i++) {
      buff[i] = s->chars[start + i];
    }
    buff[len] = '\0';
    kustr* res = ku_strtake(vm, buff, len);
    return OBJ_VAL(res);

  }
  return NIL_VAL;
}

kuval math_sget(kuvm *vm, kustr *p) {
  if (M2(p, "pi")) {
    return NUM_VAL(M_PI);
  }
  return NIL_VAL;
}

void ku_reglibs(kuvm *vm) {
  ku_cfuncdef(vm, "clock", ku_clock);
  ku_cfuncdef(vm, "int", ku_intf);
  ku_cfuncdef(vm, "parseFloat", ku_parseFloat);
  ku_cfuncdef(vm, "printf", ku_print);
  ku_cfuncdef(vm, "array", ku_arraycons);
  ku_cfuncdef(vm, "eval", ku_eval);

  kucclass *math = ku_cclassnew(vm, "math");
  math->sget = math_sget;
  math->scall = math_scall;
  ku_cclassdef(vm, math);

  kucclass *string = ku_cclassnew(vm, "string");
  string->scall = string_scall;
  ku_cclassdef(vm, string);

  kucclass *table = ku_cclassnew(vm, "table");
  table->cons = table_cons;
  table->icall = table_icall;
  table->iget = table_iget;
  table->iput = table_iput;
  table->ifree = table_ifree;
  table->imark = table_imark;
  ku_cclassdef(vm, table);
}

// ********************** closure **********************
kuclosure *ku_closurenew(kuvm *vm, kufunc *f) {
  ku_push(vm, OBJ_VAL(f)); // for GC
  kuclosure *cl = KALLOC_OBJ(vm, kuclosure, OBJ_CLOSURE);
  cl->func = f;
  cl->upcount = 0;  // for GC
  cl->upvals = NULL; // for GC
  ku_push(vm, OBJ_VAL(cl));
  kuxobj **upvals = KALLOC(vm, kuxobj*, f->upcount);
  ku_pop(vm);
  ku_pop(vm);
  for (int i = 0; i < f->upcount; i++) {
    upvals[i] = NULL;
  }
  cl->upvals = upvals;
  cl->upcount = f->upcount;
  return cl;
}

// ********************** upvalue **********************
kuxobj *ku_xobjnew(kuvm *vm, kuval *slot) {
  kuxobj *uo = KALLOC_OBJ(vm, kuxobj, OBJ_UPVAL);
  uo->location = slot;
  uo->next = NULL;
  uo->closed = NIL_VAL;
  return uo;
}

// ********************** garbage collection **********************
static void ku_markarray(kuvm *vm, kuarr *array) {
  for (int i = 0; i < array->count; i++) {
    ku_markval(vm, array->values[i]);
  }
}

static void ku_traceobj(kuvm *vm, kuobj *o) {
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p trace ", (void*)o);
    ku_printval(vm, OBJ_VAL(o));
    ku_printf(vm, "\n");
  }

  switch (o->type) {
    case OBJ_STR:
    case OBJ_CFUNC:
      break;

    case OBJ_ARRAY: {
      kuaobj *ao = (kuaobj*)o;
      ku_markarray(vm, &ao->elements);
    }
      break;

    case OBJ_CINST: {
      kunobj *i = (kunobj*)o;
      ku_markobj(vm, (kuobj*)i->klass);
      if (i->klass->imark) {
        i->klass->imark(vm, o);
      }
    }
      break;

    case OBJ_UPVAL:
      ku_markval(vm, ((kuxobj*)o)->closed);
      break;

    case OBJ_FUNC: {
      kufunc *fn = (kufunc*)o;
      ku_markobj(vm, (kuobj*)fn->name);
      ku_markarray(vm, &fn->chunk.constants);
    }
      break;

    case OBJ_CCLASS: {
      kucclass *cc = (kucclass*)o;
      ku_markobj(vm, (kuobj*)cc->name);
      if (cc->smark) {
        cc->smark(vm, o);
      }
    }
      break;

    case OBJ_CLASS: {
      kuclass *c = (kuclass*)o;
      ku_markobj(vm, (kuobj*)c->name);
      ku_marktable(vm, &c->methods);
    }
      break;

    case OBJ_INSTANCE: {
      kuiobj *i = (kuiobj*)o;
      ku_markobj(vm, (kuobj*)i->klass);
      ku_marktable(vm, &i->fields);
    }
      break;

    case OBJ_BOUND_METHOD: {
      kubound *bm = (kubound*)o;
      ku_markval(vm, bm->receiver);
      ku_markobj(vm, (kuobj*)bm->method);
    }
      break;

    case OBJ_CLOSURE: {
      kuclosure *cl = (kuclosure*)o;
      ku_markobj(vm, (kuobj*)cl->func);
      for (int i = 0; i < cl->upcount; i++) {
        ku_markobj(vm, (kuobj*)cl->upvals[i]);
      }
    }
      break;
  }
}

static void ku_tracerefs(kuvm *vm) {
  while (vm->gccount > 0) {
    kuobj *o = vm->gcstack[--vm->gccount];
    ku_traceobj(vm, o);
  }
}

void ku_markobj(kuvm *vm, kuobj *o) {
  if (o == NULL) {
    return;
  }

  if (o->marked) return;
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p mark ", (void*) o);
    ku_printval(vm, OBJ_VAL(o));
    ku_printf(vm, "\n");
  }

  o->marked = true;

  if (vm->gccap < vm->gccount + 1) {
    vm->gccap = CAPACITY_GROW(vm->gccap);
    // for VC++ C6308
    kuobj **temp = (kuobj**)realloc(vm->gcstack, sizeof(kuobj*) * vm->gccap);
    assert(temp != NULL); // TODO: figure out code coverage
    vm->gcstack = temp;
  }
  vm->gcstack[vm->gccount++] = o;
}

static void ku_markval(kuvm *vm, kuval v) {
  if (IS_OBJ(v)) {
    ku_markobj(vm, AS_OBJ(v));
  }
}

static void ku_marktable(kuvm *vm, kutab *tab) {
  for (int i = 0; i < tab->capacity; i++) {
    kuentry *e = &tab->entries[i];
    ku_markobj(vm, (kuobj*)e->key);
    ku_markval(vm, e->value);
  }
}

static void ku_markcomproots(kuvm *vm) {
  kucomp *comp = vm->compiler;
  while (comp != NULL) {
    ku_markobj(vm, (kuobj*)comp->function);
    comp = comp->enclosing;
  }
}

static void ku_markroots(kuvm *vm) {
  for (kuval *pv = vm->stack; pv < vm->sp; pv++) {
    ku_markval(vm, *pv);
  }

  for (int i = 0; i < vm->framecount; i++) {
    ku_markobj(vm, (kuobj*)vm->frames[i].closure);
  }

  for (kuxobj *uo = vm->openupvals; uo != NULL; uo = uo->next) {
    ku_markobj(vm, (kuobj*)uo);
  }

  ku_marktable(vm, &vm->globals);
  ku_markcomproots(vm);
}

void ku_sweep(kuvm *vm) {
  kuobj *prev = NULL;
  kuobj *obj = vm->objects;

  while (obj != NULL) {
    if (obj->marked) {
      obj->marked = false;
      prev = obj;
      obj = obj->next;
    } else {
      kuobj *unreached = obj;
      obj = obj->next;
      if (prev != NULL) {
        prev->next = obj;
      } else {
        vm->objects = obj;
      }
      ku_objfree(vm, unreached);
    }
  }
}

static void ku_freeweak(kuvm *vm, kutab *table) {
  for (int i = 0; i < table->capacity; i++) {
    kuentry *e = &table->entries[i];
    if (e->key != NULL && !e->key->obj.marked) {
      ku_tabdel(vm, table, e->key);
    }
  }
}

#define GC_HEAP_GROW_FACTOR 2

void ku_gc(kuvm *vm) {
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "-- gc start\n");
  }

  size_t bytes = vm->allocated;

  ku_markroots(vm);
  ku_markobj(vm, (kuobj*)vm->initstr);
  ku_markobj(vm, (kuobj*)vm->countstr);
  ku_tracerefs(vm);
  ku_freeweak(vm, &vm->strings);
  ku_sweep(vm);

  vm->gcnext = vm->allocated * GC_HEAP_GROW_FACTOR;

  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "-- gc end\n");
    ku_printf(vm, "collected %zu bytes (%zu -> %zu) next %zu\n",
              bytes - vm->allocated, bytes, vm->allocated, vm->gcnext);
  }
}

// ********************** class **********************
kuclass *ku_classnew(kuvm *vm, kustr *name) {
  kuclass *c = KALLOC_OBJ(vm, kuclass, OBJ_CLASS);
  c->name = name;
  ku_tabinit(vm, &c->methods);
  return c;
}

// ********************** instance **********************
kuiobj *ku_instnew(kuvm *vm, kuclass *klass) {
  kuiobj *i = KALLOC_OBJ(vm, kuiobj, OBJ_INSTANCE);
  i->klass = klass;
  ku_tabinit(vm, &i->fields);
  return i;
}


// ********************** bound method **********************
kubound *ku_boundnew(kuvm *vm, kuval receiver, kuclosure *method) {
  kubound *bm = KALLOC_OBJ(vm, kubound, OBJ_BOUND_METHOD);
  bm->receiver = receiver;
  bm->method = method;
  return bm;
}

// ********************** print **********************
void ku_printf(kuvm *vm, const char *fmt, ...) {
  va_list args;

  if (vm->flags & KVM_F_QUIET) {
    return;
  }

  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void ku_printmem(kuvm *vm) {
  ku_printf(vm, "allocated: %ld\n", vm->allocated);
#ifdef TRACE_OBJ_COUNTS
  ku_printf(vm, "  OBJ_FUNC:          %d\n", vm->alloc_counts[OBJ_FUNC]);
  ku_printf(vm, "  OBJ_DFUNC:         %d\n", vm->alloc_counts[OBJ_CFUNC]);
  ku_printf(vm, "  OBJ_CCLASS:        %d\n", vm->alloc_counts[OBJ_CCLASS]);
  ku_printf(vm, "  OBJ_CLOSURE:       %d\n", vm->alloc_counts[OBJ_CLOSURE]);
  ku_printf(vm, "  OBJ_STR:           %d\n", vm->alloc_counts[OBJ_STR]);
  ku_printf(vm, "  OBJ_UPVAL:         %d\n", vm->alloc_counts[OBJ_UPVAL]);
  ku_printf(vm, "  OBJ_CLASS:         %d\n", vm->alloc_counts[OBJ_CLASS]);
  ku_printf(vm, "  OBJ_FUNC:          %d\n", vm->alloc_counts[OBJ_FUNC]);
  ku_printf(vm, "  OBJ_INSTANCE:      %d\n", vm->alloc_counts[OBJ_INSTANCE]);
  ku_printf(vm, "  OBJ_CINST:         %d\n", vm->alloc_counts[OBJ_CINST]);
  ku_printf(vm, "  OBJ_BOUND_METHOD:  %d\n", vm->alloc_counts[OBJ_BOUND_METHOD]);
#endif // TRACE_OBJ_COUNTS
}

static void ku_printobj(kuvm* vm, kuval val) {
  switch (OBJ_TYPE(val)) {
    case OBJ_FUNC:
      ku_printfunc(vm, AS_FUNC(val));
      break;
    case OBJ_CFUNC:
      ku_printf(vm, "<cfunc>");
      break;
    case OBJ_ARRAY:
      ku_printf(vm, "<array %d>", AS_ARRAY(val)->elements.count);
      break;
    case OBJ_CINST: {
      kunobj *i = AS_CINST(val);
      ku_printf(vm, "<%s instance>", i->klass->name->chars);
    }
      break;
    case OBJ_CCLASS: {
      kucclass *cc = AS_CCLASS(val);
      ku_printf(vm, "<class %s>", cc->name->chars);
    }
      break;
    case OBJ_CLOSURE:
      ku_printfunc(vm, AS_CLOSURE(val)->func);
      break;
    case OBJ_STR:
      ku_printf(vm, "%s", AS_CSTR(val));
      break;
    case OBJ_CLASS:
      ku_printf(vm, "%s", AS_CLASS(val)->name->chars);
      break;
    case OBJ_INSTANCE:
      ku_printf(vm, "%s instance", AS_INSTANCE(val)->klass->name->chars);
      break;
    case OBJ_BOUND_METHOD:
      ku_printfunc(vm, AS_BOUND_METHOD(val)->method->func);
      break;
    case OBJ_UPVAL:
      ku_printf(vm, "upvalue");
      break;
  }
}

void ku_printval(kuvm *vm, kuval value) {

  if (IS_BOOL(value)) {
    ku_printf(vm, AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    ku_printf(vm, "nil");
  } else if (IS_NUM(value)) {
    ku_printf(vm, "%f", AS_NUM(value));
  } else if (IS_OBJ(value)) {
    ku_printobj(vm, value);
  }
}

static void ku_printfunc(kuvm *vm, kufunc *fn) {
  if (fn->name) {
    ku_printf(vm, "<fn %.*s>", fn->name->len, fn->name);
  } else {
    ku_printf(vm, "<fn __main__>");
  }
}

void ku_printstack(kuvm *vm) {
  ku_printf(vm, " [");
  for (kuval* vp = vm->stack; vp < vm->sp; vp++) {
    ku_printval(vm, *vp);
    if (vp < vm->sp - 1) {
      ku_printf(vm, ",");
    }
  }
  ku_printf(vm, "]");
  ku_printf(vm, "\n");
}

// ********************** loop patch **********************
void ku_loopinit(kuvm *vm, kuloop *loop) {
  loop->breakpatch.count = 0;
  loop->continuepatch.count = 0;
}

void ku_emitpatch(kuvm *vm, kupatch *patch, uint8_t op) {
  if (patch->count == vm->max_patches) {
    ku_perr(vm, "max break / continue limit reached");
    return;
  }
  patch->offset[patch->count++] = ku_emitjump(vm, op);
}

void ku_patchall(kuvm *vm, kupatch *patch, uint16_t to, bool rev) {
  for (int i = 0; i < patch->count; i++) {
    kuchunk *c = ku_chunk(vm);
    uint16_t delta = (rev) ? patch->offset[i] - to + 2 : to - patch->offset[i];
    c->code[patch->offset[i]] = (delta >> 8) & 0xff;
    c->code[patch->offset[i]+1] = delta & 0xff;
  }
}

// ********************** native class **********************
kucclass *ku_cclassnew(kuvm *vm, const char *name) {
  int len = (int)strlen(name);
  kustr *sname = ku_strfrom(vm, name, len);

  kucclass *cc = KALLOC_OBJ(vm, kucclass, OBJ_CCLASS);
  cc->name = sname;
  cc->cons = NULL;
  cc->scall = NULL;
  cc->sget = NULL;
  cc->sput = NULL;
  cc->sfree = NULL;
  cc->smark = NULL;
  cc->icall = NULL;
  cc->iget = NULL;
  cc->iput = NULL;
  cc->ifree = NULL;
  cc->imark = NULL;
  return cc;
}

void ku_cclassdef(kuvm *vm, kucclass *cc) {
  ku_push(vm, OBJ_VAL(cc->name));
  ku_push(vm, OBJ_VAL((kuobj*)cc));
  ku_tabset(vm, &vm->globals, AS_STR(vm->stack[0]), vm->stack[1]);
  ku_pop(vm);
  ku_pop(vm);
}

// ********************** array object **********************
kuaobj * ku_arrnew(kuvm* vm, int capacity) {
  kuaobj *arr = KALLOC_OBJ(vm, kuaobj, OBJ_ARRAY);
  ku_push(vm, OBJ_VAL(arr)); // for GC
  arr->elements.count = 0;
  arr->elements.capacity = capacity;
  arr->elements.values = NULL;

  kuarr *e = &arr->elements;
  if (capacity > 0) {
    e->values = (kuval*)ku_alloc(vm, NULL, 0, sizeof(kuval)*capacity);

    for (int i = 0; i < capacity; i++) {
      e->values[i] = NIL_VAL;
    }
  }
  ku_pop(vm); // GC
  return arr;
}

void ku_arrset(kuvm* vm, kuaobj* arr, int index, kuval value) {
  kuarr *e = &arr->elements;

  if (arr == NULL) {
    // TODO: add code coverage
    ku_err(vm, "null array");
    return;
  }
  int oldcount = e->count;

  if (e->capacity <= index) {
    int old = e->capacity;
    e->capacity = CAPACITY_GROW(index);
    e->values = ARRAY_GROW(vm, kuval, e->values, old, e->capacity);
  }

  for (int i = oldcount; i < index; i++) {
    e->values[i] = NIL_VAL;
  }
  e->values[index] = value;
  e->count = (index >= oldcount) ? index + 1 : e->count;
}

kuval ku_arrget(kuvm* vm, kuaobj* arr, int index) {
  if (arr == NULL) {
    // TODO: add code coverage
    ku_err(vm, "null array");
    return NIL_VAL;
  }
  if (index < arr->elements.count) {
    return arr->elements.values[index];
  }
  return NIL_VAL;
}
