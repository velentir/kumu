//  kumu.c

#include "kumu.h"
#include <stdio.h>

// ********************** macros **********************
#define KU_CAPACITY_GROW(cap) ((cap) < 8 ? 8 : (cap) * 2)
#define KU_ARRAY_GROW(k, type, ptr, old, new) \
  (type *)ku_alloc(k, ptr, sizeof(type) * (old), sizeof(type) * (new))
#define KU_ARRAY_FREE(vm, type, ptr, old) ku_free(vm, ptr, sizeof(type) * (old))
#define KU_ALLOC(vm, type, count) \
  (type*)ku_alloc(vm, NULL, 0, sizeof(type) * (count))
#define KU_ALLOC_OBJ(vm, type, objtype) \
  (type*)ku_objalloc(vm, sizeof(type), objtype)
#define KU_FREE(vm, type, ptr) \
  ku_free(vm, ptr, sizeof(type))

#define KU_READ_SHORT(vm) \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define KU_BYTE_READ(vm) (*(frame->ip++))
#define KU_CONST_READ(vm) (frame->closure->func->chunk.constants.values[KU_BYTE_READ(vm)])
#define KU_READ_STR(vm) AS_STR(KU_CONST_READ(vm))

// TODO: add code coverage of ku_err line below
#define KU_BIN_OP(v, vt, op) \
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
kuval ku_peek(kuvm *__nonnull vm, int distance);

static void ku_printobj(kuvm *__nonnull vm, kuval val);
static void ku_printfunc(kuvm *__nonnull vm, kufunc *__nonnull fn);

static void kup_function(kuvm *__nonnull vm, kufunc_t type);
static kuval string_iget(kuvm *__nonnull vm, kuobj *__nonnull obj, kustr *__nonnull prop);
static kuval string_icall(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull m, int argc, kuval *__nullable argv);
static bool array_invoke(kuvm *__nonnull vm, kuval arr, kustr *__nonnull method, int argc, kuval *__nullable argv);
static void ku_markval(kuvm *__nonnull vm, kuval v);
static void ku_marktable(kuvm *__nonnull vm, kutab *__nonnull tab);

// ********************** object **********************
kuobj *__nonnull ku_objalloc(kuvm *__nonnull vm, size_t size, kuobj_t type) {
#ifdef  TRACE_OBJ_COUNTS
  vm->alloc_counts[type]++;
#endif // TRACE_OBJ_COUNTS
  kuobj *__nonnull obj = (kuobj *)ku_alloc(vm, NULL, 0, size);
  obj->type = type;
  obj->marked = false;
  obj->next = (kuobj *)vm->objects;
  vm->objects = obj;
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p alloc %zu for %d\n", (void*)obj, size, type);
  }
  return obj;
}

void ku_objfree(kuvm *__nonnull vm, kuobj *__nonnull obj) {
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p free type %d\n", (void*)obj, obj->type);
  }

#ifdef TRACE_OBJ_COUNTS
  vm->alloc_counts[obj->type]--;
#endif // TRACE_OBJ_COUNTS
  switch (obj->type) {
    case OBJ_FUNC: {
      kufunc *__nonnull fn = (kufunc*)obj;
      kup_chunkfree(vm, &fn->chunk);
      KU_FREE(vm, kufunc, obj);
    }
      break;

    case OBJ_ARRAY: {
      kuaobj *__nonnull ao = (kuaobj*)obj;
      KU_ARRAY_FREE(vm, kuval, ao->elements.values, ao->elements.capacity);
      ao->elements.values = NULL;
      ao->elements.count = 0;
      ao->elements.count = 0;
      KU_FREE(vm, kuaobj, obj);
    }
      break;

    case OBJ_CCLASS: {
      kucclass *__nonnull cc = (kucclass*)obj;
      if (cc->sfree) {
        cc->sfree(vm, obj);
      }
      KU_FREE(vm, kucclass, obj);
    }
      break;

    case OBJ_CINST: {
      kunobj *__nonnull i = (kunobj *)obj;
      if (i->klass->ifree) {
        i->klass->ifree(vm, obj);
      }
    }
      break;

    case OBJ_CFUNC:
      KU_FREE(vm, kucfunc, obj);
      break;

    case OBJ_CLOSURE: {
      kuclosure *__nonnull cl = (kuclosure*)obj;
      KU_ARRAY_FREE(vm, kuxobj*, cl->upvals, cl->upcount);
      KU_FREE(vm, kuclosure, obj);
    }
      break;

    case OBJ_CLASS: {
      kuclass *__nonnull c = (kuclass*)obj;
      ku_tabfree(vm, &c->methods);
      KU_FREE(vm, kuclass, obj);
    }
      break;

    case OBJ_INSTANCE: {
      kuiobj *__nonnull i = (kuiobj*)obj;
      ku_tabfree(vm, &i->fields);
      KU_FREE(vm, kuiobj, obj);
    }
      break;

    case OBJ_BOUND_METHOD:
      KU_FREE(vm, kubound, obj);
      break;

    case OBJ_STR: {
      kustr *__nonnull str = (kustr*)obj;
      KU_ARRAY_FREE(vm, char, str->chars, (size_t)str->len + 1);
      KU_FREE(vm, kustr, obj);
    }
      break;

    case OBJ_UPVAL:
      KU_FREE(vm, kuxobj, obj);
      break;
  }
}

bool ku_objis(kuval v, kuobj_t ot) {
  return IS_OBJ(v) && AS_OBJ(v)->type == ot;
}

// ********************** string **********************
// FNV-1a hashing function
static uint32_t ku_strhash(const char *__nonnull key, int len) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < len; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

static kustr *__nonnull ku_stralloc(kuvm *__nonnull vm, char *__nonnull chars, int len, uint32_t hash) {
  kustr *__nonnull str = KU_ALLOC_OBJ(vm, kustr, OBJ_STR);
  str->len = len;
  str->chars = chars;
  str->hash = hash;
  ku_push(vm, OBJ_VAL(str)); // for GC
  ku_tabset(vm, &vm->strings, str, NULL_VAL);
  ku_pop(vm);
  return str;
}

kustr *__nonnull ku_strfrom(kuvm *__nonnull vm, const char *__nonnull chars, int len) {
  uint32_t hash = ku_strhash(chars, len);

  kustr *__nullable interned = ku_tabfindc(vm, &vm->strings, chars, len, hash);
  if (interned != NULL) {
    return KU_NONNULL(interned); // TODO: figure out code coverage
  }

  char *__nonnull buff = KU_ALLOC(vm, char, (size_t)len + 1);
  memcpy(buff, chars, len);
  buff[len] = '\0';
  return ku_stralloc(vm, buff, len, hash);
}

static kustr *__nonnull ku_strtake(kuvm *__nonnull vm, char *__nonnull buff, int len) {
  uint32_t hash = ku_strhash(buff, len);
  kustr *__nullable interned = ku_tabfindc(vm, &vm->strings, buff, len, hash);
  if (interned != NULL) {
    KU_ARRAY_FREE(vm, char, buff, (size_t)len + 1);
    return KU_NONNULL(interned); // TODO: figure out code coverage
  }

  return ku_stralloc(vm, buff, len, hash);
}

static void ku_strcat(kuvm *__nonnull vm) {
  kustr *__nonnull b = AS_STR(ku_peek(vm,0)); // for GC
  kustr *__nonnull a = AS_STR(ku_peek(vm,1)); // for GC
  int len = a->len + b->len;
  char *__nonnull buff = KU_ALLOC(vm, char, (size_t)len + 1);
  memcpy(buff, a->chars, a->len);
  memcpy(buff + a->len, b->chars, b->len);
  buff[len] = '\0';
  kustr *__nonnull res = ku_strtake(vm, buff, len);
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
void ku_tabinit(KU_UNUSED kuvm *__nonnull vm, kutab *__nonnull map) {
  map->count = 0;
  map->capacity = 0;
  map->entries = NULL;
}

void ku_tabfree(kuvm *__nonnull vm, kutab *__nonnull map) {
  KU_ARRAY_FREE(vm, kuentry, map->entries, map->capacity);
  ku_tabinit(vm, map);
}

#define MAP_MAX_LOAD 0.75

#define MOD2(a,b) ((a) & (b - 1))

static kuentry *__nonnull ku_tabfinds(
    KU_UNUSED kuvm *__nonnull vm, kuentry *__nonnull entries, int capacity, kustr *__nullable key) {
  uint32_t index = MOD2(key->hash, capacity);
  kuentry *__nullable tombstone = NULL;

  for (;;) {
    kuentry *__nonnull e = &entries[index];

    if (e->key == NULL) {
      if (IS_NULL(e->value)) {
        // empty entry and have a tombstone ~> return the tombstone
        // otherwise return this entry.
        // this allows reusing tombstone slots for added efficiency
        return tombstone != NULL ? KU_NONNULL(tombstone) : e; // TODO: figure out code coverage
      } else {
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

static void ku_tabadjust(kuvm *__nonnull vm, kutab *__nonnull map, int capacity) {
  kuentry *__nonnull entries = KU_ALLOC(vm, kuentry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NULL_VAL;
  }

  map->count = 0;
  for (int i = 0; i < map->capacity; i++) {
    kuentry *__nonnull src = &map->entries[i];
    if (src->key == NULL) {
      continue;
    }

    kuentry *__nonnull dest = ku_tabfinds(vm, entries, capacity, src->key);
    dest->key = src->key;
    dest->value = src->value;
    map->count++;
  }

  KU_ARRAY_FREE(vm, kuentry, map->entries, map->capacity);
  map->entries = entries;
  map->capacity = capacity;
}

bool ku_tabset(kuvm *__nonnull vm, kutab *__nonnull map, kustr *__nonnull key, kuval value) {
  if ((size_t)map->count + 1 > map->capacity * MAP_MAX_LOAD) {
    int capacity = KU_CAPACITY_GROW(map->capacity);
    ku_tabadjust(vm, map, capacity);
  }
  kuentry *__nonnull entries = KU_NONNULL(map->entries); // TODO: figure out code coverage

  kuentry *__nonnull e = ku_tabfinds(vm, entries, map->capacity, key);
  bool isnew = e->key == NULL;
  // we don't increase the count if we use a tombstone slot
  if (isnew && IS_NULL(e->value)) {
    map->count++;
  }
  e->key = key;
  e->value = value;
  return isnew;
}

void ku_tabcopy(kuvm *__nonnull vm, kutab *__nonnull from, kutab *__nonnull to) {
  for (int i = 0; i < from->capacity; i++) {
    kuentry *__nonnull e = &from->entries[i];
    if (e->key != NULL) {
      ku_tabset(vm, to, KU_NONNULL(e->key), e->value); // TODO: figure out code coverage
    }
  }
}

bool ku_tabget(kuvm *__nonnull vm, kutab *__nonnull map, kustr *__nullable key, kuval *__nonnull value) {
  if (map->count == 0) {
    return false;
  }
  kuentry *__nonnull entries = KU_NONNULL(map->entries); // TODO: figure out code coverage

  kuentry *__nonnull e = ku_tabfinds(vm, entries, map->capacity, key);
  if (e->key == NULL) {
    return false;
  }

  *value = e->value;
  return true;
}

bool ku_tabdel(kuvm *__nonnull vm, kutab *__nonnull map, kustr *__nullable key) {
  if (map->count == 0) {
    return false;
  }
  kuentry *__nonnull entries = KU_NONNULL(map->entries); // TODO: figure out code coverage

  kuentry *__nonnull e = ku_tabfinds(vm, entries, map->capacity, key);
  if (e->key == NULL) {
    return false;
  }
  e->key = NULL;
  e->value = BOOL_VAL(true);
  return true;
}

kustr *__nullable ku_tabfindc(
    KU_UNUSED kuvm *__nonnull vm, kutab *__nonnull map, const char *__nonnull chars, int len, uint32_t hash) {
  if (map->count == 0) {
    return NULL;
  }

  uint32_t index = MOD2(hash, map->capacity);
  for (;;) {
    kuentry *__nonnull e = &map->entries[index];
    if (e->key == NULL) {
      if (IS_NULL(e->value)) {
        return NULL;    // empty non-tombstone
      }
    } else if (e->key->len == len && e->key->hash == hash &&
      memcmp(e->key->chars, chars, len) == 0) {
      return e->key;
    }
    index = MOD2(index + 1,map->capacity);
  }
}

// ********************** scanner **********************
static char ku_advance(kuvm *__nonnull vm) {
  vm->scanner.curr++;
  return vm->scanner.curr[-1];
}

void ku_lexinit(kuvm *__nonnull vm, const char *__nonnull source) {
  vm->scanner.start = source;
  vm->scanner.curr = source;
  vm->scanner.line = 1;
}

static bool ku_lexend(kuvm *__nonnull vm) {
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

static char ku_lexpeeknext(kuvm *__nonnull vm) {
  if (ku_lexend(vm)) return '\0';
  return vm->scanner.curr[1];
}

static char ku_lexpeek(kuvm *__nonnull vm) {
  return *vm->scanner.curr;
}

static void ku_lexspace(kuvm *__nonnull vm) {
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

static kutok ku_tokmake(kuvm *__nonnull vm, kutok_t type) {
  kutok token;
  token.type = type;
  token.start = vm->scanner.start;
  token.len = (int)(vm->scanner.curr - vm->scanner.start);
  token.line = vm->scanner.line;
  return token;
}

static kutok ku_lexerr(kuvm *__nonnull vm, const char *__nonnull msg) {
  kutok token;
  token.type = TOK_ERR;
  token.start = msg;
  token.len = (int)strlen(msg);
  token.line = vm->scanner.line;
  return token;
}

static bool ku_lexmatch(kuvm *__nonnull vm, char expected) {
  if (ku_lexend(vm)) return false;
  if (*vm->scanner.curr != expected) return false;
  vm->scanner.curr++;
  return true;
}

static kutok ku_hexnum(kuvm *__nonnull vm) {
  ku_advance(vm); // skip 'x'
  while(ku_ishexdigit(ku_lexpeek(vm))) ku_advance(vm);
  return ku_tokmake(vm, TOK_HEX);
}

static kutok ku_lexnum(kuvm *__nonnull vm) {
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

static kutok ku_lexstr(kuvm *__nonnull vm) {
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

static kutok_t ku_lexkey(
    kuvm *__nonnull vm, int start, int len, const char *__nonnull rest, kutok_t type) {
  if (vm->scanner.curr - vm->scanner.start == (long)(start + len) &&
      memcmp(vm->scanner.start + start, rest, len) == 0) {
    return type;
  }
  return TOK_IDENT;
}

static kutok_t ku_keyword(kuvm *__nonnull vm) {
  switch (vm->scanner.start[0]) {
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
    case 'e':
      if (vm->scanner.curr - vm->scanner.start > 1) {
        switch (vm->scanner.start[1]) {
          case 'l': return ku_lexkey(vm, 2,2,"se", TOK_ELSE);
          case 'x': return ku_lexkey(vm, 2,5,"tends", TOK_EXTENDS);
        }
      }
      break;
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
    case 'n': return ku_lexkey(vm, 1,3,"ull", TOK_NULL);
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

static kutok ku_lexid(kuvm *__nonnull vm) {
  while (ku_isalpha(ku_lexpeek(vm)) || ku_isdigit(ku_lexpeek(vm))) {
    ku_advance(vm);
  }
  return ku_tokmake(vm, ku_keyword(vm));
}

kutok ku_scan(kuvm *__nonnull vm) {
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
    case '&':
      if (ku_lexmatch(vm, '&')) {
        return ku_tokmake(vm, TOK_AND);
      }
      return ku_tokmake(vm, TOK_AMP);
    case '|':
      if (ku_lexmatch(vm, '|')) {
        return ku_tokmake(vm, TOK_OR);
      }
      return ku_tokmake(vm, TOK_PIPE);
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

void ku_lexdump(kuvm *__nonnull vm) {
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
static void ku_perrat(kuvm *__nonnull vm, kutok *__nonnull tok, const char *__nonnull msg) {
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

static void ku_perr(kuvm *__nonnull vm, const char *__nonnull msg) {
  ku_perrat(vm, &vm->parser.curr, msg);
}

static void kup_pnext(kuvm *__nonnull vm) {
  vm->parser.prev = vm->parser.curr;

  while (true) {
    vm->parser.curr = ku_scan(vm);
    if (vm->parser.curr.type != TOK_ERR) break;
    ku_perr(vm, vm->parser.curr.start);
  }
}

static void kup_pconsume(kuvm *__nonnull vm, kutok_t type, const char *__nonnull msg) {
  if (vm->parser.curr.type == type) {
    kup_pnext(vm);
    return;
  }
  ku_perr(vm, msg);
}

kuchunk *__nonnull kup_chunk(kuvm *__nonnull vm) {
  return &vm->compiler->function->chunk;
}

static void kup_emitbyte(kuvm *__nonnull vm, uint8_t byte) {
  kup_chunkwrite(vm, kup_chunk(vm), byte, vm->parser.prev.line);
}

static void kup_emitop(kuvm *__nonnull vm, uint8_t byte) {
  kup_emitbyte(vm, byte);
}

static void kup_emitbytes(kuvm *__nonnull vm, uint8_t b1, uint8_t b2);
static void kup_emitop2(kuvm *__nonnull vm, uint8_t b1, uint8_t b2);

static void kup_emitret(kuvm *__nonnull vm, bool lambda) {
  if (lambda) {
    kup_emitop(vm, OP_RET);
    return;
  }
  if (vm->compiler->type == FUNC_INIT) {
    kup_emitop2(vm, OP_GET_LOCAL, 0);
  } else {
    kup_emitop(vm, OP_NULL);
  }
  kup_emitop(vm, OP_RET);
}

static kufunc *__nullable kup_pend(kuvm *__nonnull vm, bool lambda) {
  kup_emitret(vm, lambda);
  kufunc *__nullable fn = vm->compiler->function;
  vm->compiler = vm->compiler->enclosing;
  return fn;
}

static void kup_emitbytes(kuvm *__nonnull vm, uint8_t b1, uint8_t b2) {
  kup_emitbyte(vm, b1);
  kup_emitbyte(vm, b2);
}

static void kup_emitop2(kuvm*__nonnull  vm, uint8_t b1, uint8_t b2) {
  kup_emitbytes(vm, b1, b2);
}

static uint8_t kup_pconst(kuvm *__nonnull vm, kuval val) {
  int cons = kup_chunkconst(vm, kup_chunk(vm), val);
  if (cons > vm->max_const) {
    ku_perr(vm, "out of constant space");
    return 0;
  }
  return (uint8_t)cons;
}

static void kup_emitconst(kuvm *__nonnull vm, kuval val) {
  kup_emitop2(vm, OP_CONST, kup_pconst(vm, val));
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

typedef void (*kupfunc)(kuvm *__nonnull vm, bool lhs);

typedef struct {
  kupfunc prefix;
  kupfunc infix;
  kup_precedence precedence;
} kuprule;

static kuprule *__nonnull kup_getrule(kuvm *__nonnull vm, kutok_t optype);

static bool kup_pcheck(kuvm *__nonnull vm, kutok_t type) {
  return vm->parser.curr.type == type;
}

static bool kup_pmatch(kuvm *__nonnull vm, kutok_t type) {
  if (!kup_pcheck(vm, type)) {
    return false;
  }
  kup_pnext(vm);
  return true;
}

static void kup_prec(kuvm *__nonnull vm, kup_precedence prec) {
  kup_pnext(vm);
  kupfunc __nullable prefix = kup_getrule(vm, vm->parser.prev.type)->prefix;
  if (prefix == NULL) {
    ku_perr(vm, "expected expression");
    return;
  }

  bool lhs = prec <= P_ASSIGN;
  prefix(vm, lhs);

  while (prec <= kup_getrule(vm, vm->parser.curr.type)->precedence) {
    kup_pnext(vm);
    kupfunc infix = kup_getrule(vm, vm->parser.prev.type)->infix;
    infix(vm, lhs);
  }

  if (lhs && kup_pmatch(vm, TOK_EQ)) {
    ku_perr(vm, "invalid assignment target");
  }
}

static void kup_lit(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  switch (vm->parser.prev.type) {
    case TOK_FALSE: kup_emitop(vm, OP_FALSE); break;
    case TOK_TRUE: kup_emitop(vm, OP_TRUE); break;
    case TOK_NULL: kup_emitop(vm, OP_NULL); break;
    default: return; // unreachable - TODO: figure out code coverage
  }
}

static void kup_xstring(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  const char *__nonnull chars = vm->parser.prev.start + 1;
  int len = vm->parser.prev.len - 2;

  // len(encoded) always <= len(chars);
  char *__nonnull encoded = ku_alloc(vm, NULL, 0, len);

  int esclen = len;
  char *__nonnull ep = encoded;
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

  kup_emitconst(vm, OBJ_VAL(ku_strfrom(vm, encoded, esclen)));
  ku_free(vm, encoded, len);
}

static void kup_string(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  const char *__nonnull chars = vm->parser.prev.start + 1;
  int len = vm->parser.prev.len - 2;
  kup_emitconst(vm, OBJ_VAL(ku_strfrom(vm, chars, len)));
}

static void kup_hex(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  const char *__nonnull start = vm->parser.prev.start + 2;  // skip 0x
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
  kup_emitconst(vm, NUM_VAL(val));
}

static void kup_number(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  double val = strtod(vm->parser.prev.start, NULL);
  kup_emitconst(vm, NUM_VAL(val));
}

static void kup_funcexpr(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  kup_function(vm, FUNC_STD);
}

static void kup_expr(kuvm *__nonnull vm) {
  kup_prec(vm, P_ASSIGN);
}

static uint8_t kup_arglist(kuvm *__nonnull vm, kutok_t right) {
  uint8_t argc = 0;
  if (!kup_pcheck(vm, right)) {
    do {
      kup_expr(vm);
      if (argc > vm->max_params) {
        ku_perr(vm, "too many parameters");
      }
      argc++;
    } while (kup_pmatch(vm, TOK_COMMA));
  }

  if (right == TOK_RPAR)
    kup_pconsume(vm, right, "')' expected");
  else
    kup_pconsume(vm, right, "']' expected");
  return argc;
}

static void kup_call(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  uint8_t argc = kup_arglist(vm, TOK_RPAR);
  kup_emitop2(vm, OP_CALL, argc);
}

static void kup_grouping(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  kup_expr(vm);
  kup_pconsume(vm, TOK_RPAR, "')' expected");
}

static bool kup_peek_lambda(kuvm *__nonnull vm) {
  bool ret = false;
  const char *__nonnull start = vm->scanner.start;
  const char *__nonnull curr = vm->scanner.curr;

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

static void kup_params(kuvm *__nonnull vm);
static void kup_lbody(kuvm *__nonnull vm, kucomp *__nonnull compiler);

static void kup_lambda_or_group(kuvm *__nonnull vm, bool lhs) {
  if (kup_peek_lambda(vm)) {
    kucomp compiler;
    kup_compinit(vm, &compiler, FUNC_STD);
    kup_beginscope(vm);
    kup_params(vm);
    kup_pconsume(vm, TOK_RPAR, "')' expected");
    kup_pconsume(vm, TOK_ARROW, "'=>' expected");
    kup_lbody(vm, &compiler);
    return;
  }

  kup_grouping(vm, lhs);
}

static void kup_array(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  int count = kup_arglist(vm, TOK_RBRACKET);
  kup_emitop(vm, OP_ARRAY);
  kup_emitbyte(vm, (count >> 8) & 0xff);
  kup_emitbyte(vm, count & 0xff);
}

static void kup_index(kuvm *__nonnull vm, bool lhs) {
  kup_expr(vm);
  kup_pconsume(vm, TOK_RBRACKET, "']' expected");
  if (lhs && kup_pmatch(vm, TOK_EQ)) {
    kup_expr(vm);
    kup_emitop(vm, OP_ASET);
  } else {
    kup_emitop(vm, OP_AGET);
  }
}

static void kup_unary(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  kutok_t optype = vm->parser.prev.type;

  kup_prec(vm, P_UNARY);

  switch (optype) {
    case TOK_MINUS: kup_emitop(vm, OP_NEG); break;
    case TOK_BANG: kup_emitop(vm, OP_NOT); break;
    default: return; // unreachable - TODO: figure out code coverage
  }
}

static void kup_exprstmt(kuvm *__nonnull vm, KU_UNUSED kuloop *__nullable loop) {
  kup_expr(vm);
  kup_pconsume(vm, TOK_SEMI, "; expected");
  kup_emitop(vm, OP_POP);
}

static void kup_return(kuvm *__nonnull vm, KU_UNUSED kuloop *__nullable loop) {
  if (vm->compiler->type == FUNC_MAIN) {
    ku_perr(vm, "can't return from top-level");
  }

  if (kup_pmatch(vm, TOK_SEMI)) {
    kup_emitret(vm, false);
  } else {

    if (vm->compiler->type == FUNC_INIT) {
      ku_err(vm, "cannot return from initializer");
    }
    kup_expr(vm);
    kup_pconsume(vm, TOK_SEMI, "';' expected");
    kup_emitop(vm, OP_RET);
  }
}

static void kup_break(kuvm *__nonnull vm, kuloop *__nullable loop) {
  if (loop == NULL) {
    ku_perr(vm, "break must be in a loop");
  } else {
    kup_pconsume(vm, TOK_SEMI, "; expected");
    kup_emitpatch(vm, &loop->breakpatch, OP_JUMP);
  }
}

static void kup_continue(kuvm *__nonnull vm, kuloop *__nullable loop) {
  if (loop == NULL) {
    ku_perr(vm, "continue must be in a loop");
  } else {
    kup_pconsume(vm, TOK_SEMI, "; expected");
    kup_emitpatch(vm, &loop->continuepatch, OP_LOOP);
  }
}

static void kup_stmt(kuvm *__nonnull vm, kuloop *__nullable loop) {
  if (kup_pmatch(vm, TOK_IF)) {
    kup_ifstmt(vm, loop);
  } else if (kup_pmatch(vm, TOK_RETURN)) {
    kup_return(vm, loop);
  } else if (kup_pmatch(vm, TOK_WHILE)) {
    kup_whilestmt(vm, loop);
  } else if (kup_pmatch(vm, TOK_FOR)) {
    kup_forstmt(vm, loop);
  } else if (kup_pmatch(vm, TOK_BREAK)) {
    kup_break(vm, loop);
  } else if (kup_pmatch(vm, TOK_CONTINUE)) {
    kup_continue(vm, loop);
  } else if (kup_pmatch(vm, TOK_LBRACE)) {
    kup_beginscope(vm);
    kup_block(vm, loop);
    kup_endscope(vm);
  } else {
    kup_exprstmt(vm, loop);
  }
}

static void kup_pskip(kuvm *__nonnull vm) {
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

    kup_pnext(vm);
  }
}

static uint8_t kup_pidconst(kuvm *__nonnull vm, kutok *__nonnull name) {
  return kup_pconst(vm, OBJ_VAL(ku_strfrom(vm, name->start, name->len)));
}

static uint8_t kup_strtoname(kuvm *__nonnull vm, kutok *__nonnull name) {
  const char *__nonnull chars = name->start + 1;
  int len = name->len - 2;
  return kup_pconst(vm, OBJ_VAL(ku_strfrom(vm, chars, len)));
}

static uint8_t kup_let(kuvm *__nonnull vm, bool isconst, const char *__nonnull msg) {
  kup_pconsume(vm, TOK_IDENT, msg);
  kup_declare_let(vm, isconst);
  if (vm->compiler->depth > 0) {
    return 0;
  }
  return kup_pidconst(vm, &vm->parser.prev);
}

static void kup_vardef(kuvm *__nonnull vm, uint8_t index, bool isconst) {
  if (vm->compiler->depth > 0) {
    kup_markinit(vm);
    return;
  }
  if (isconst) {
    kustr *__nonnull key = AS_STR(kup_chunk(vm)->constants.values[index]);
    kuval cinit;
    if (ku_tabget(vm, &vm->gconst, key, &cinit)) {
        ku_perr(vm, "const already defined");
    } else {
      ku_tabset(vm, &vm->gconst, key, NULL_VAL);
    }
  }
  kup_emitop2(vm, OP_DEF_GLOBAL, index);
}

static void kup_vardecl(kuvm *__nonnull vm, bool isconst) {
  do {
    uint8_t g = kup_let(vm, isconst, "name expected");
    if (kup_pmatch(vm, TOK_EQ)) {
      kup_expr(vm);
    }
    else {
      kup_emitop(vm, OP_NULL);
    }
    kup_vardef(vm, g, isconst);
  } while(kup_pmatch(vm, TOK_COMMA));

  kup_pconsume(vm, TOK_SEMI, "; expected");
}

// common between functions and lambdas
static void kup_functail(kuvm *__nonnull vm, kucomp *__nonnull compiler, bool lambda) {
  kufunc *__nullable fn = kup_pend(vm, lambda);
  kup_emitbytes(vm, OP_CLOSURE, kup_pconst(vm, OBJ_VAL(fn)));
  for (int i = 0; i < fn->upcount; i++) {
    kup_emitbyte(vm, compiler->upvals[i].local ? 1: 0);
    kup_emitbyte(vm, compiler->upvals[i].index);
  }
}

static void kup_params(kuvm *__nonnull vm) {
  do {
    vm->compiler->function->arity++;
    if (vm->compiler->function->arity > vm->max_params) {
      ku_perr(vm, "too many params");
    }

    uint8_t constant = kup_let(vm, false, "expected parameter name");
    kup_vardef(vm, constant, false);
  } while(kup_pmatch(vm, TOK_COMMA));
}

static void kup_lbody(kuvm *__nonnull vm, kucomp *__nonnull compiler) {
  if (kup_pmatch(vm, TOK_LBRACE)) {
    kup_block(vm, NULL);
    kup_functail(vm, compiler, false);
  } else {
    kup_expr(vm);
    kup_functail(vm, compiler, true);
  }
}

static void kup_lblock(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  if (kup_pcheck(vm, TOK_STR)) {
    kup_emitop(vm, OP_TABLE);
    do {
      kup_emitop(vm, OP_DUP);
      kup_pconsume(vm, TOK_STR, "string expected");
      uint8_t name = kup_strtoname(vm, &vm->parser.prev);
      kup_pconsume(vm, TOK_EQ, "table '=' expected");
      kup_expr(vm);
      kup_emitop2(vm, OP_SET_PROP, name);
      kup_emitop(vm, OP_POP); // remove the value
    } while (kup_pmatch(vm, TOK_COMMA));
    kup_pconsume(vm, TOK_RBRACE, "'}' expected");
    // leave table on the stack
    return;
  }
}

static void kup_function(kuvm *__nonnull vm, kufunc_t type) {
  kucomp compiler;
  kup_compinit(vm, &compiler, type);
  kup_beginscope(vm);
  kup_pconsume(vm, TOK_LPAR, "'(' expected after function name");
  if (!kup_pcheck(vm, TOK_RPAR)) {
    kup_params(vm);
  }
  kup_pconsume(vm, TOK_RPAR, "')' expected after parameters");
  kup_pconsume(vm, TOK_LBRACE, "'{' expected before function body");
  kup_block(vm, NULL);
  kup_functail(vm, &compiler, false);
}


static void kup_funcdecl(kuvm *__nonnull vm) {
  uint8_t global = kup_let(vm, false, "function name expected");
  kup_markinit(vm);
  kup_function(vm, FUNC_STD);
  kup_vardef(vm, global, false);
}

static void kup_method(kuvm *__nonnull vm) {
  kup_pconsume(vm, TOK_IDENT, "method name expected");
  uint8_t name = kup_pidconst(vm, &vm->parser.prev);
  kufunc_t type = FUNC_METHOD;

  if (vm->parser.prev.len == 4 && memcmp(vm->parser.prev.start, "init", 4) == 0) {
    type = FUNC_INIT;
  }
  kup_function(vm, type);
  kup_emitop2(vm, OP_METHOD, name);
}

static void kup_namedvar(kuvm *__nonnull vm, kutok name, bool lhs);
static void kup_pvar(kuvm *__nonnull vm, bool lhs);

static kutok kup_maketok(KU_UNUSED kuvm *__nonnull vm, const char *__nonnull text) {
  kutok tok;
  tok.start = text;
  tok.len = (int)strlen(text);
  return tok;
}

static void kup_classdecl(kuvm *__nonnull vm) {
  kup_pconsume(vm, TOK_IDENT, "class name expected");
  kutok cname = vm->parser.prev;
  uint8_t name = kup_pidconst(vm, &vm->parser.prev);
  kup_declare_let(vm, false);
  kup_emitop2(vm, OP_CLASS, name);
  kup_vardef(vm, name, false);
  kuclasscomp cc;
  cc.enclosing = vm->curclass;
  cc.hassuper = false;
  vm->curclass = &cc;

  if (kup_pmatch(vm, TOK_EXTENDS)) {
    kup_pconsume(vm, TOK_IDENT, "class name expected");
    kup_pvar(vm, false); //  [.. GET_GLOBAL <name>;]

    if (kup_identeq(vm, &cname, &vm->parser.prev)) {
      ku_perr(vm, "cannot inherit from self");
    }

    kup_beginscope(vm);
    kup_addlocal(vm, kup_maketok(vm, "super"), false);
    kup_vardef(vm, 0, false);
    kup_namedvar(vm, cname, false);
    kup_emitop(vm, OP_INHERIT);
    cc.hassuper = true;
  }

  kup_namedvar(vm, cname, false);
  kup_pconsume(vm, TOK_LBRACE, "'{' expected");
  while (!kup_pcheck(vm, TOK_RBRACE) && !kup_pcheck(vm, TOK_EOF)) {
    kup_method(vm);
  }
  kup_pconsume(vm, TOK_RBRACE, "'}' expected");
  kup_emitop(vm, OP_POP);

  if (cc.hassuper) {
    kup_endscope(vm);
  }
  vm->curclass = vm->curclass->enclosing;
}

static void kup_decl(kuvm *__nonnull vm, kuloop *__nullable loop) {
  if (kup_pmatch(vm, TOK_CLASS)) {
    kup_classdecl(vm);
  } else if (kup_pmatch(vm, TOK_FUN)) {
    kup_funcdecl(vm);
  } else if (kup_pmatch(vm, TOK_LET)) {
    kup_vardecl(vm, false);
  } else if (kup_pmatch(vm, TOK_CONST)) {
    kup_vardecl(vm, true);
  } else {
    kup_stmt(vm, loop);
  }
  if (vm->parser.panic) {
    kup_pskip(vm);
  }
}

static int kup_xvaladd(kuvm *__nonnull vm, kucomp *__nonnull compiler, uint8_t index, bool local) {
  int upcount = compiler->function->upcount;

  for (int i = 0; i < upcount; i++) {
    kuxval *__nonnull uv = &compiler->upvals[i];
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

static int kup_xvalresolve(kuvm *__nonnull vm, kucomp *__nonnull compiler, kutok *__nonnull name) {
  if (compiler->enclosing == NULL) {
    return -1;
  }
  kucomp *__nonnull enclosing = KU_NONNULL(compiler->enclosing); // TODO: figure out code coverage

  int local = kup_resolvelocal(vm, enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].flags |= KU_LOCAL_CAPTURED;
    return kup_xvaladd(vm, compiler, (uint8_t)local, true);
  }

  int upv = kup_xvalresolve(vm, enclosing, name);
  if (upv != -1) {
    return kup_xvaladd(vm, compiler, (uint8_t)upv, false);
  }
  return -1;
}

static void kup_lambda(kuvm *__nonnull vm, kutok name) {
  kucomp compiler;
  kup_compinit(vm, &compiler, FUNC_STD);
  kup_beginscope(vm);
  kup_addlocal(vm, name, false);
  kup_markinit(vm);
  kup_lbody(vm, &compiler);
  compiler.function->arity = 1;
}


static void kup_namedvar(kuvm *__nonnull vm, kutok name, bool lhs) {
  kucomp *__nonnull compiler = KU_NONNULL(vm->compiler); // TODO: figure out code coverage
  int arg = kup_resolvelocal(vm, compiler, &name);
  uint8_t set, get;
  bool isglobal = false;

  if (arg != -1) {
    get = OP_GET_LOCAL;
    set = OP_SET_LOCAL;
  } else if ((arg = kup_xvalresolve(vm, compiler, &name)) != -1) {
    get = OP_GET_UPVAL;
    set = OP_SET_UPVAL;
  } else {
    arg = kup_pidconst(vm, &name);
    get = OP_GET_GLOBAL;
    set = OP_SET_GLOBAL;
    isglobal = true;
  }
  if (lhs && kup_pmatch(vm, TOK_EQ)) {
    if (isglobal) {
        kustr *__nonnull key = AS_STR(kup_chunk(vm)->constants.values[arg]);
        kuval cinit;
        if (ku_tabget(vm, &vm->gconst, key, &cinit)) {
            ku_perr(vm, "const cannot be assigned");
        }
    }
    else {
      kulocal *__nonnull local = &vm->compiler->locals[arg];
      if (local->flags & KU_LOCAL_CONST && local->flags & KU_LOCAL_INIT) {
        ku_perr(vm, "const cannot be assigned");
      }
    }
    kup_expr(vm);
    kup_emitop2(vm, set, (uint8_t)arg);
  } else if (kup_pmatch(vm, TOK_ARROW)) {
    kup_lambda(vm, name);
  } else {
    kup_emitop2(vm, get, (uint8_t)arg);
  }
}

static void kup_pvar(kuvm *__nonnull vm, bool lhs) {
  kup_namedvar(vm, vm->parser.prev, lhs);
}

static void kup_this(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  if (vm->curclass == NULL) {
    ku_perr(vm, "cannot use this outside a class");
    return;
  }
  kup_pvar(vm, false);
}

static void kup_super(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  if (vm->curclass == NULL) {
    ku_perr(vm, "'super' must be used inside a class");
  } else if (!vm->curclass->hassuper) {
    ku_perr(vm, "cannot use super in a class with no superclass");
  }
  kup_pconsume(vm, TOK_DOT, "'.' expected after super");
  kup_pconsume(vm, TOK_IDENT, "superclass method expected");
  uint8_t name = kup_pidconst(vm, &vm->parser.prev);
  kup_namedvar(vm, kup_maketok(vm, "this"), false); // [ ... GET_LOCAL <0> ]

  if (kup_pmatch(vm, TOK_LPAR)) {
    uint8_t argc = kup_arglist(vm, TOK_RPAR);
    kup_namedvar(vm, kup_maketok(vm, "super"), false);
    kup_emitop2(vm, OP_SUPER_INVOKE, name);
    kup_emitbyte(vm, argc);
  } else {
    kup_namedvar(vm, kup_maketok(vm, "super"), false); // [ ... GET_UPVAL <0> ]
    kup_emitop2(vm, OP_GET_SUPER, name);
  }
}

static void kup_bin(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  kutok_t optype = vm->parser.prev.type;
  kuprule *__nonnull rule = kup_getrule(vm, optype);
  kup_prec(vm, (kup_precedence)(rule->precedence + 1));

  switch (optype) {
    case TOK_LTLT: kup_emitop(vm, OP_SHL); break;
    case TOK_GTGT: kup_emitop(vm, OP_SHR); break;
    case TOK_AMP: kup_emitop(vm, OP_BAND); break;
    case TOK_PIPE: kup_emitop(vm, OP_BOR); break;
    case TOK_PLUS: kup_emitop(vm, OP_ADD); break;
    case TOK_MINUS: kup_emitop(vm, OP_SUB); break;
    case TOK_STAR: kup_emitop(vm, OP_MUL); break;
    case TOK_SLASH: kup_emitop(vm, OP_DIV); break;
    case TOK_NE: kup_emitop2(vm, OP_EQ, OP_NOT); break;
    case TOK_EQUALITY: kup_emitop(vm, OP_EQ); break;
    case TOK_GT: kup_emitop(vm, OP_GT); break;
    case TOK_GE: kup_emitop2(vm, OP_LT, OP_NOT); break;
    case TOK_LT: kup_emitop(vm, OP_LT); break;
    case TOK_LE: kup_emitop2(vm, OP_GT, OP_NOT); break;
    default: return; // unreachable - TODO: figure out code coverage
  }
}

static void kup_dot(kuvm *__nonnull vm, bool lhs) {
  kup_pconsume(vm, TOK_IDENT, "property name expected");
  uint8_t name = kup_pidconst(vm, &vm->parser.prev);
  if (lhs && kup_pmatch(vm, TOK_EQ)) {
    kup_expr(vm);
    kup_emitop2(vm, OP_SET_PROP, name);
  } else if (kup_pmatch(vm, TOK_LPAR)) {
    uint8_t argc = kup_arglist(vm, TOK_RPAR);
    kup_emitop2(vm, OP_INVOKE, name);
    kup_emitbyte(vm, argc);
  } else {
    kup_emitop2(vm, OP_GET_PROP, name);
  }
}

void kup_ifstmt(kuvm *__nonnull vm, kuloop *__nullable loop) {
  kup_pconsume(vm, TOK_LPAR, "'(' expected after 'if'");
  kup_expr(vm);
  kup_pconsume(vm, TOK_RPAR, "'R' expected after condition");
  int then_jump = kup_emitjump(vm, OP_JUMP_IF_FALSE);
  kup_emitop(vm, OP_POP);
  kup_stmt(vm, loop);
  int else_jump = kup_emitjump(vm, OP_JUMP);
  kup_patchjump(vm, then_jump);
  kup_emitop(vm, OP_POP);
  if (kup_pmatch(vm, TOK_ELSE)) {
    kup_stmt(vm, loop);
  }
  kup_patchjump(vm, else_jump);
}

int kup_emitjump(kuvm *__nonnull vm, k_op op) {
  kup_emitop(vm, op);
  kup_emitbyte(vm, 0xff);
  kup_emitbyte(vm, 0xff);
  return kup_chunk(vm)->count - 2;
}

void kup_patchjump(kuvm *__nonnull vm, int offset) {
  int jump = kup_chunk(vm)->count - offset - 2;

  if (jump > vm->max_jump) {
    ku_perr(vm, "too much code to jump over");
  }

  kup_chunk(vm)->code[offset] = (jump >> 8) & 0xff;
  kup_chunk(vm)->code[offset + 1] = jump & 0xff;
}

void kup_emitloop(kuvm *__nonnull vm, int start) {
  kup_emitop(vm, OP_LOOP);
  int offset = kup_chunk(vm)->count - start + 2;
  if (offset > vm->max_body) {
    ku_perr(vm, "loop body too large");
  }
  kup_emitbyte(vm, (offset >> 8) & 0xff);
  kup_emitbyte(vm, offset  & 0xff);
}

void kup_whilestmt(kuvm *__nonnull vm, KU_UNUSED kuloop *__nullable loop) {
  int loop_start = kup_chunk(vm)->count;
  kup_pconsume(vm, TOK_LPAR, "'(' expected after 'while'");
  kup_expr(vm);
  kup_pconsume(vm, TOK_RPAR, "')' expected after 'while'");
  int jump_exit = kup_emitjump(vm, OP_JUMP_IF_FALSE);
  kup_emitop(vm, OP_POP);
  kuloop inner;
  kup_loopinit(vm, &inner);
  kup_stmt(vm, &inner);
  kup_emitloop(vm, loop_start);
  kup_patchjump(vm, jump_exit);

  //       +----------------------+
  //       |                      v See #37
  // POP; JUMP ; .... LOOP; POP; NULL; RET;
  uint16_t loop_end = kup_chunk(vm)->count - 1;
  kup_emitop(vm, OP_POP);
  kup_patchall(vm, &inner.continuepatch, loop_start, true);
  kup_patchall(vm, &inner.breakpatch, loop_end, false);
}

void kup_forstmt(kuvm *__nonnull vm, kuloop *__nullable loop) {
  kup_beginscope(vm);
  kup_pconsume(vm, TOK_LPAR, "'(' expected after 'for'");
  if (kup_pmatch(vm, TOK_SEMI)) {
    // no init
  } else if (kup_pmatch(vm, TOK_LET)) {
    kup_vardecl(vm, false);
  } else {
    kup_exprstmt(vm, loop);
  }
  int loop_start = kup_chunk(vm)->count;
  int exit_jump = -1;

  if (!kup_pmatch(vm, TOK_SEMI)) {
    kup_expr(vm);
    kup_pconsume(vm, TOK_SEMI, "';' expected");
    exit_jump = kup_emitjump(vm, OP_JUMP_IF_FALSE);
    kup_emitop(vm, OP_POP);
  }

  if (!kup_pmatch(vm, TOK_RPAR)) {
    int body_jump = kup_emitjump(vm, OP_JUMP);
    int inc_start = kup_chunk(vm)->count;
    kup_expr(vm);
    kup_emitop(vm, OP_POP);
    kup_pconsume(vm, TOK_RPAR, "')' expected");
    kup_emitloop(vm, loop_start);
    loop_start = inc_start;
    kup_patchjump(vm, body_jump);
  }

  kuloop inner;
  kup_loopinit(vm, &inner);
  kup_stmt(vm, &inner);
  kup_emitloop(vm, loop_start);

  if (exit_jump != -1) {
    kup_patchjump(vm, exit_jump);
    kup_emitop(vm, OP_POP);
  }

  uint16_t loop_end = kup_chunk(vm)->count;
  kup_patchall(vm, &inner.continuepatch, loop_start, true);
  kup_patchall(vm, &inner.breakpatch, loop_end, false);

  kup_endscope(vm);
}

void kup_and(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  int end_jump = kup_emitjump(vm, OP_JUMP_IF_FALSE);
  kup_emitop(vm, OP_POP);
  kup_prec(vm, P_AND);
  kup_patchjump(vm, end_jump);
}

void kup_or(kuvm *__nonnull vm, KU_UNUSED bool lhs) {
  int else_jump = kup_emitjump(vm, OP_JUMP_IF_FALSE);
  int end_jump = kup_emitjump(vm, OP_JUMP);
  kup_patchjump(vm, else_jump);
  kup_emitop(vm, OP_POP);
  kup_prec(vm, P_OR);
  kup_patchjump(vm, end_jump);
}

kuprule kup_rules[] = {
  [TOK_LPAR] =        { kup_lambda_or_group, kup_call,  P_CALL },
  [TOK_RPAR] =        { NULL,         NULL,      P_NONE },
  [TOK_LBRACE] =      { kup_lblock,   NULL,      P_NONE },
  [TOK_RBRACE] =      { NULL,         NULL,      P_NONE },
  [TOK_LBRACKET] =    { kup_array,    kup_index, P_CALL },
  [TOK_RBRACKET] =    { NULL,         NULL,      P_NONE },

  [TOK_COMMA] =       { NULL,         NULL,      P_NONE },
  [TOK_DOT] =         { NULL,         kup_dot,   P_CALL },
  [TOK_MINUS] =       { kup_unary,    kup_bin,   P_TERM },
  [TOK_PLUS] =        { NULL,         kup_bin,   P_TERM },
  [TOK_SEMI] =        { NULL,         NULL,      P_NONE },
  [TOK_SLASH] =       { NULL,         kup_bin,   P_FACTOR },
  [TOK_STAR] =        { NULL,         kup_bin,   P_FACTOR },
  [TOK_BANG] =        { kup_unary,    NULL,      P_NONE },
  [TOK_NE] =          { NULL,         kup_bin,   P_EQ },
  [TOK_EQ] =          { NULL,         NULL,      P_NONE },
  [TOK_EQUALITY] =    { NULL,         kup_bin,   P_EQ },
  [TOK_GT] =          { NULL,         kup_bin,   P_COMP },
  [TOK_GE] =          { NULL,         kup_bin,   P_COMP },
  [TOK_LT] =          { NULL,         kup_bin,   P_COMP },
  [TOK_LE] =          { NULL,         kup_bin,   P_COMP },
  [TOK_IDENT] =       { kup_pvar,     NULL,      P_NONE },
  [TOK_STR] =         { kup_string,   NULL,      P_NONE },
  [TOK_STRESC] =      { kup_xstring,  NULL,      P_NONE },
  [TOK_NUM] =         { kup_number,   NULL,      P_NONE },
  [TOK_HEX] =         { kup_hex,      NULL,      P_NONE },
  [TOK_AND] =         { NULL,         kup_and,   P_AND },
  [TOK_CLASS] =       { NULL,         NULL,      P_NONE },
  [TOK_ELSE] =        { NULL,         NULL,      P_NONE },
  [TOK_EXTENDS] =     { NULL,         NULL,      P_NONE },
  [TOK_FALSE] =       { kup_lit,      NULL,      P_NONE },
  [TOK_FOR] =         { NULL,         NULL,      P_NONE },
  [TOK_FUN] =         { kup_funcexpr, NULL,      P_NONE },
  [TOK_IF] =          { NULL,         NULL,      P_NONE },
  [TOK_NULL] =        { kup_lit,      NULL,      P_NONE },
  [TOK_OR] =          { NULL,         kup_or,    P_OR },
  [TOK_SUPER] =       { kup_super,    NULL,      P_NONE },
  [TOK_THIS] =        { kup_this,     NULL,      P_NONE },
  [TOK_TRUE] =        { kup_lit,      NULL,      P_NONE },
  [TOK_LET] =         { NULL,         NULL,      P_NONE },
  [TOK_WHILE] =       { NULL,         NULL,      P_NONE },
  [TOK_ERR] =         { NULL,         NULL,      P_NONE },
  [TOK_EOF] =         { NULL,         NULL,      P_NONE },
  [TOK_AMP] =         { NULL,         kup_bin,   P_BIT },
  [TOK_PIPE] =        { NULL,         kup_bin,   P_BIT },
  [TOK_LTLT] =        { NULL,         kup_bin,   P_SHIFT },
  [TOK_GTGT] =        { NULL,         kup_bin,   P_SHIFT },
};

static kuprule *__nonnull kup_getrule(KU_UNUSED kuvm *__nonnull vm, kutok_t optype) {
  return &kup_rules[optype];
}

// ********************** virtual machine **********************
void ku_reset(kuvm *__nonnull vm) {
  vm->sp = vm->stack;
  vm->framecount = 0;
#ifdef STACK_CHECK
  vm->underflow = 0;
#endif // STACK_CHECK
}

void ku_push(kuvm *__nonnull vm, kuval val) {
#ifdef STACK_CHECK
  assert(vm->sp < vm->stack + vm->max_stack); // TODO: figure out code coverage
#endif // STACK_CHECK

  *(vm->sp) = val;
  vm->sp++;
}

kuval ku_pop(kuvm *__nonnull vm) {
#ifdef STACK_CHECK
  if (vm->sp <= vm->stack) {
    vm->underflow++;
  }
#endif // STACK_CHECK
  vm->sp--;
  return *(vm->sp);
}

static bool ku_falsy(kuval v) {
  return IS_NULL(v) || (IS_BOOL(v) && !AS_BOOL(v));
}

kuval ku_peek(kuvm *__nonnull vm, int distance) {
  return vm->sp[-1 - distance];
}

static void *__nonnull _kuenv_alloc(size_t size) {
  void *__nullable ptr = malloc(size);
  return KU_NONNULL(ptr); // TODO: figure out code coverage
}

static void *__nonnull _kuenv_realloc(void *__nullable ptr, size_t size) {
  ptr = realloc(ptr, size);
  return KU_NONNULL(ptr); // TODO: figure out code coverage
}

static void _kuenv_free(void *__nullable p) {
  if (p != NULL) {
    free(p);
  }
}

static kuenv g_kuenv_default = {
  .alloc = _kuenv_alloc,
  .realloc = _kuenv_realloc,
  .free = _kuenv_free,
};

kuvm *__nonnull ku_newvm(int stack_max, kuenv *__nullable env) {
  if (env == NULL) {
    env = &g_kuenv_default;
  }

  size_t size = sizeof(kuvm) + stack_max*sizeof(kuval);
  kuvm *__nonnull vm = env->alloc(size);

  *vm = (kuvm){
    .debugger = NULL,
    .print = NULL,
    .env = *env,
    .allocated = sizeof(kuvm),
    .max_params = 255,
    .max_const = UINT8_MAX,
    .max_closures = UPSTACK_MAX,
    .max_jump = UINT16_MAX,
    .max_body = UINT16_MAX,
    .max_frames = FRAMES_MAX,
    .max_locals = LOCALS_MAX,
    .max_patches = PATCH_MAX,
    .max_stack = stack_max,
    .gcnext = 1024*1024,
  };

  ku_tabinit(vm, &vm->strings);
  ku_tabinit(vm, &vm->globals);
  ku_tabinit(vm, &vm->gconst);
  ku_reset(vm);

  vm->initstr = ku_strfrom(vm, "init", 4);
  vm->countstr = ku_strfrom(vm, "count", 5);

  return vm;
}

static void ku_freeobjects(kuvm *__nonnull vm) {
  kuobj *__nullable obj = vm->objects;
  while (obj != NULL) {
    if (vm->flags & KVM_F_GCLOG) {
      ku_printf(vm, "%p dangling ", (void *)obj);
      ku_printval(vm, OBJ_VAL(obj));
      ku_printf(vm, "\n");
    }
    kuobj *__nullable next = (kuobj*)obj->next;
    ku_objfree(vm, KU_NONNULL(obj)); // TODO: figure out code coverage
    obj = next;
  }
}

void ku_freevm(kuvm *__nonnull vm) {
  vm->initstr = NULL; // free_objects will take care of it
  vm->countstr = NULL;

  ku_freeobjects(vm);
  ku_tabfree(vm, &vm->strings);
  ku_tabfree(vm, &vm->globals);
  ku_tabfree(vm, &vm->gconst);
  vm->allocated -= sizeof(kuvm);
  assert(vm->allocated == 0); // TODO: figure out code coverage
  vm->env.free(vm->gcstack);
  vm->env.free(vm);
}

void ku_err(kuvm *__nonnull vm, const char *__nonnull fmt, ...) {
  va_list args;
  char out[1024];

  vm->err = true;
  va_start(args, fmt);
  vsprintf(out, fmt, args);
  va_end(args);

  ku_printf(vm, "error %s\n", out);
  for (int f = vm->framecount - 1; f >= 0; f--) {
    kuframe *__nonnull frame = &vm->frames[f];
    kufunc *__nonnull fn = frame->closure->func;
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

static bool ku_docall(kuvm *__nonnull vm, kuclosure *__nonnull cl, int argc) {
  if (argc != cl->func->arity) {
    ku_err(vm, "%d expected got %d", cl->func->arity, argc);
    return false;
  }

  if (vm->framecount == vm->max_frames) {
    ku_err(vm, "stack overflow");
    return false;
  }

  kuframe *__nonnull frame = &vm->frames[vm->framecount++];
  frame->closure = cl;
  frame->ip = cl->func->chunk.code;
  frame->bp = vm->sp - argc - 1;
  return true;
}

kures ku_nativecall(kuvm *__nonnull vm, kuclosure *__nonnull cl, int argc) {
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

static bool ku_callvalue(kuvm *__nonnull vm, kuval callee, int argc, bool *__nonnull native) {
  *native = false;
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CCLASS: {
        kucclass *__nonnull cc = AS_CCLASS(callee);
        if (cc->cons) {
          kuval res = cc->cons(vm, argc, vm->sp - argc);
          vm->sp -= (size_t)argc + 1;
          ku_push(vm, res);
          *native = true;
          if (!ku_objis(res, OBJ_CINST)) {
            // TODO: add code coverage
            ku_err(vm, "invalid instance");
          } else {
            kunobj *__nonnull i = AS_CINST(res);
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
        kuclass *__nonnull c = AS_CLASS(callee);
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
        kubound *__nonnull bm = AS_BOUND_METHOD(callee);
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
kuchunk *__nonnull ku_chunk_runtime(kuvm *__nonnull vm) {
  // TODO: add code coverage
  return &vm->frames[vm->framecount-1].closure->func->chunk;
}
#endif // TRACE_ENABLED

static kuxobj *__nonnull ku_capture(kuvm *__nonnull vm, kuval *__nonnull local) {
  kuxobj *__nullable prev = NULL;
  kuxobj *__nullable uv = vm->openupvals;

  while (uv != NULL && uv->location > local) {
    // TODO: add code coverage
    prev = uv;
    uv = uv->next;
  }

  if (uv != NULL && uv->location == local) {
    // TODO: add code coverage
    return KU_NONNULL(uv); // TODO: figure out code coverage
  }

  kuxobj *__nonnull created = ku_xobjnew(vm, local);

  created->next = uv;

  if (prev == NULL) {
    vm->openupvals = created;
  } else {
    // TODO: add code coverage
    prev->next = created;
  }
  return created;
}

static void ku_close(kuvm *__nonnull vm, kuval *__nonnull last) {
  while (vm->openupvals != NULL && vm->openupvals->location >= last) {
    kuxobj *__nonnull uo = KU_NONNULL(vm->openupvals); // TODO: figure out code coverage
    uo->closed = *uo->location;
    uo->location = &uo->closed;
    vm->openupvals = uo->next;
  }
}

static void ku_defmethod(kuvm *__nonnull vm, kustr *__nonnull name) {
  kuval method = ku_peek(vm, 0);
  kuclass *__nonnull c = AS_CLASS(ku_peek(vm, 1));
//  ku_printf(vm, ">> class %p method %s\n", (void *)c, name->chars);
  ku_tabset(vm, &c->methods, name, method);
  ku_pop(vm);
}

static bool ku_bindmethod(kuvm *__nonnull vm, kuclass *__nonnull klass, kustr *__nonnull name) {
  kuval method;
  if (!ku_tabget(vm, &klass->methods, name, &method)) {
    ku_err(vm, "undefined property %s", name->chars);
    return false;
  }
  kubound *__nonnull bm = ku_boundnew(vm, ku_peek(vm,0), AS_CLOSURE(method));
  ku_pop(vm);
  ku_push(vm, OBJ_VAL(bm));
  return true;
}

static bool ku_classinvoke(kuvm *__nonnull vm, kuclass *__nonnull klass, kustr *__nonnull name, int argc) {
  kuval method;
  if (!ku_tabget(vm, &klass->methods, name, &method)) {
    ku_err(vm, "undefined property '%s'", name->chars);
    return false;
  }
  return ku_docall(vm, AS_CLOSURE(method), argc);
}

bool ku_invoke(kuvm *__nonnull vm, kustr *__nonnull name, int argc, bool *__nonnull native) {
  kuval receiver = ku_peek(vm, argc);
  *native = false;
  if (IS_CCLASS(receiver)) {
    kucclass *__nonnull cc = AS_CCLASS(receiver);
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
    kunobj *__nonnull no = AS_CINST(receiver);
    if (no->klass->icall) {
      *native = true;
      kuval v = no->klass->icall(vm, (kuobj *)no, name, argc, vm->sp - argc);
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
  kuiobj *__nonnull inst = AS_INSTANCE(receiver);

  kuval val;
  if (ku_tabget(vm, &inst->fields, name, &val)) {
    vm->sp[-argc - 1] = val;
    bool native;
    return ku_callvalue(vm, val, argc, &native);
  }

  return ku_classinvoke(vm, inst->klass, name, argc);
}

kures ku_run(kuvm *__nonnull vm) {
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

    switch (op = KU_BYTE_READ(vm)) {
      case OP_TABLE:
        ku_push(vm, ku_cinstance(vm, "table"));
        break;

      case OP_ARRAY: {
        int count = KU_READ_SHORT(vm);
        kuaobj *__nonnull ao = ku_arrnew(vm, count);
        kuval *__nonnull argv = vm->sp - count;

        for (int i = 0; i < count; i++) {
          ku_arrset(vm, ao, i, argv[i]);
        }

        vm->sp -= count;
        ku_push(vm, OBJ_VAL(ao));
      }
        break;

      case OP_CALL: {
        int argc = KU_BYTE_READ(vm);
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
        ku_push(vm, OBJ_VAL(ku_classnew(vm, KU_READ_STR(vm))));
        break;

      case OP_METHOD:
        ku_defmethod(vm, KU_READ_STR(vm));
        break;

      case OP_INVOKE: {
        bool native;
        kustr *__nonnull method = KU_READ_STR(vm);
        int argc = KU_BYTE_READ(vm);
        if (!ku_invoke(vm, method, argc, &native)) {
          return KVM_ERR_RUNTIME;
        }
        if (!native) {
          frame = &vm->frames[vm->framecount - 1];
        }
      }
        break; // TODO: figure out code coverage

      case OP_SUPER_INVOKE: {
        kustr *__nonnull method = KU_READ_STR(vm);
        int argc = KU_BYTE_READ(vm);
        kuclass *__nonnull superclass = AS_CLASS(ku_pop(vm));
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
        kuclass *__nonnull subclass = AS_CLASS(ku_peek(vm, 0));
        kuclass *__nonnull superclass = AS_CLASS(sc);
//        ku_printf(vm, ">> class %p inherits from %p\n", (void *)subclass, (void *)superclass);
        ku_tabcopy(vm, &superclass->methods, &subclass->methods);
        ku_pop(vm); // subclass
      }
        break; // TODO: figure out code coverage

      case OP_CLOSURE: {
        kufunc *__nonnull fn = AS_FUNC(KU_CONST_READ(vm));
        ku_push(vm, OBJ_VAL(fn));  // for GC
        kuclosure *__nonnull cl = ku_closurenew(vm, fn);
        ku_pop(vm);
        ku_push(vm, OBJ_VAL(cl));
        for (int i = 0; i < cl->upcount; i++) {
          uint8_t local = KU_BYTE_READ(vm);
          uint8_t index = KU_BYTE_READ(vm);
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

      case OP_NULL:
        ku_push(vm, NULL_VAL);
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
        kuval con = KU_CONST_READ(vm);
        ku_push(vm, con);
      }
        break;

      case OP_NEG: {
        if (!IS_NUM(ku_peek(vm, 0))) {
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
        kustr *__nonnull name = KU_READ_STR(vm);
        kuval value;

        if (!ku_tabget(vm, &vm->globals, name, &value)) {
          ku_err(vm, "undefined variable %s", name->chars);
          return KVM_ERR_RUNTIME;
        }
        ku_push(vm, value);
      }
        break; // TODO: figure out code coverage

      case OP_DEF_GLOBAL: {
        kustr *__nonnull name = KU_READ_STR(vm);
        ku_tabset(vm, &vm->globals, name, ku_peek(vm, 0));
        ku_pop(vm);
      }
        break;

      case OP_SET_GLOBAL: {
        kustr *__nonnull name = KU_READ_STR(vm);
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
        kuaobj *__nonnull aobj = AS_ARRAY(aval);
        ku_arrset(vm, aobj, (int)AS_NUM(ival), val);
        ku_pop(vm); // array
        ku_push(vm, val);
      }
        break; // TODO figure out code coverage

      case OP_AGET: {
        kuval ival = ku_pop(vm);
        kuval aval = ku_peek(vm, 0); // for GC

        if (IS_STR(aval) && IS_NUM(ival)) {
          kustr *__nonnull st = AS_STR(aval);
          int idx = (int)AS_NUM(ival);
          if (idx >= 0 && idx < st->len) {
            kustr *__nonnull sr = ku_strfrom(vm, &st->chars[idx], 1);
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
        uint8_t slot = KU_BYTE_READ(vm);
        ku_push(vm, frame->bp[slot]);
      }
        break;

      case OP_SET_LOCAL: {
        uint8_t slot = KU_BYTE_READ(vm);
        frame->bp[slot] = ku_peek(vm, 0);
      }
        break;

      case OP_GET_UPVAL: {
        uint8_t slot = KU_BYTE_READ(vm);
        ku_push(vm, *frame->closure->upvals[slot]->location);
      }
        break;

      case OP_SET_UPVAL: {
        uint8_t slot = KU_BYTE_READ(vm);
        *frame->closure->upvals[slot]->location = ku_peek(vm, 0);
      }
        break;

      case OP_GET_PROP: {
        kustr *__nonnull name = KU_READ_STR(vm);
        kuval target = ku_peek(vm, 0);
        ku_pop(vm);   // pop the target

        if (!IS_OBJ(target)) {
          ku_err(vm, "object expected");
          return KVM_ERR_RUNTIME;
        }

        kuobj *__nonnull obj = AS_OBJ(target);

        if (obj->type == OBJ_ARRAY) {
          kuaobj *__nonnull ao = AS_ARRAY(target);
          if (name == vm->countstr) {
            ku_push(vm, NUM_VAL(ao->elements.count));
          } else if (strcmp(name->chars, "first") == 0) {
            if (ao->elements.count > 0) {
              ku_push(vm, ao->elements.values[0]);
            } else {
              ku_push(vm, NULL_VAL);
            }
          } else if (strcmp(name->chars, "last") == 0) {
            if (ao->elements.count > 0) {
              ku_push(vm, ao->elements.values[ao->elements.count - 1]);
            } else {
              ku_push(vm, NULL_VAL);
            }
          }
          break;
        }
        if (obj->type == OBJ_STR) {
          ku_push(vm, string_iget(vm, obj, name));
          break;
        }

        if (obj->type == OBJ_CCLASS) {
          kucclass *__nonnull cc = (kucclass *)obj;
          if (cc->sget) {
            ku_push(vm, cc->sget(vm, name));
            break;
          }
        }

        if (obj->type == OBJ_CINST) {
          kunobj *__nonnull i = (kunobj *)obj;
          if (i->klass->iget) {
            ku_push(vm, i->klass->iget(vm, (kuobj *)i, name));
            break;
          }
        }

        if (obj->type != OBJ_INSTANCE) {
          ku_err(vm, "instance expected");
          return KVM_ERR_RUNTIME;
        }
        kuiobj *__nonnull i = (kuiobj *)obj;
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
          kucclass *__nonnull cc = AS_CCLASS(ku_peek(vm, 1));
          if (cc->sput) {
            kustr *__nonnull name = KU_READ_STR(vm);
            kuval val = ku_pop(vm);
            ku_pop(vm); // cclass
            val = cc->sput(vm, name, val);
            ku_push(vm, val);
            break;
          }
        }

        if (IS_CINST(ku_peek(vm, 1))) {
          kunobj *__nonnull i = AS_CINST(ku_peek(vm, 1));
          if (i->klass->iput) {
            kustr *__nonnull name = KU_READ_STR(vm);
            kuval val = ku_pop(vm);
            ku_pop(vm); // instance
            val = i->klass->iput(vm, (kuobj *)i, name, val);
            ku_push(vm, val);
            break;
          }
        }

        if (!IS_INSTANCE(ku_peek(vm, 1))) {
          ku_err(vm, "instance expected");
          return KVM_ERR_RUNTIME;
        }
        kuiobj *__nonnull i = AS_INSTANCE(ku_peek(vm, 1));
        ku_tabset(vm, &i->fields, KU_READ_STR(vm), ku_peek(vm, 0));
        kuval val = ku_pop(vm);
        ku_pop(vm); // instance
        ku_push(vm, val);
      }
        break; // TODO: figure out code coverage

      case OP_GET_SUPER: {
        kustr *__nonnull name = KU_READ_STR(vm);
        kuval v = ku_pop(vm);
        kuclass *__nonnull superclass = AS_CLASS(v);
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

      case OP_SUB: KU_BIN_OP(vm,NUM_VAL, -); break;
      case OP_MUL: KU_BIN_OP(vm,NUM_VAL, *); break;
      case OP_DIV: KU_BIN_OP(vm,NUM_VAL, /); break;
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
          kustr *__nonnull b = AS_STR(ku_pop(vm));
          kustr *__nonnull a = AS_STR(ku_pop(vm));
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

//      case OP_LT: KU_BIN_OP(vm,BOOL_VAL, <); break;
      case OP_NOT:
        ku_push(vm, BOOL_VAL(ku_falsy(ku_pop(vm))));
        break;
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = KU_READ_SHORT(vm);
        if (ku_falsy(ku_peek(vm, 0))) {
          frame->ip += offset;
        }
      }
        break;

      case OP_JUMP: {
        uint16_t offset = KU_READ_SHORT(vm);
        frame->ip += offset;
      }
        break;

      case OP_LOOP: {
        uint16_t offset = KU_READ_SHORT(vm);
        frame->ip -= offset;
      }
        break;
    }
  }
  return KVM_OK;
}

kufunc *__nullable ku_compile(kuvm *__nonnull vm, char *__nonnull source) {
  kucomp compiler;
  kup_compinit(vm, &compiler, FUNC_MAIN);
  ku_lexinit(vm, source);
  vm->parser.err = false;
  vm->parser.panic = false;
  kup_pnext(vm);
  while (!kup_pmatch(vm, TOK_EOF)) {
    kup_decl(vm, NULL);
  }

  kufunc *__nullable fn = kup_pend(vm, false);

  return vm->parser.err ? NULL : fn;
}

kures ku_exec(kuvm *__nonnull vm, char *__nonnull source) {
  kufunc *__nullable fn = ku_compile(vm, source);
  if (fn == NULL) {
    return KVM_ERR_SYNTAX;
  }

  if (vm->flags & KVM_F_NOEXEC) {
    return KVM_OK;
  }

  ku_push(vm, OBJ_VAL(fn));
  kuclosure *__nonnull closure = ku_closurenew(vm, KU_NONNULL(fn)); // TODO: figure out code coverage
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
char *__nonnull ku_alloc(kuvm *__nonnull vm, void *__nullable ptr, size_t oldsize, size_t nsize) {
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
  return vm->env.realloc(ptr, nsize);
}

void ku_free(kuvm *__nonnull vm, void *__nullable ptr, size_t oldsize) {
  if (vm->flags & KVM_F_TRACEMEM) {
    ku_printf(vm, "free %d\n", (int)oldsize);
  }

  vm->allocated -= oldsize;
  vm->env.free(ptr);
}

// ********************** chunk **********************
void kup_chunkinit(kuvm *__nonnull vm, kuchunk *__nonnull chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  ku_arrinit(vm, &chunk->constants);
}

void kup_chunkwrite(kuvm *__nonnull vm, kuchunk *__nonnull chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int cap = chunk->capacity;
    chunk->capacity = KU_CAPACITY_GROW(cap);
    chunk->code = KU_ARRAY_GROW(vm, uint8_t, chunk->code, cap, chunk->capacity);
    chunk->lines = KU_ARRAY_GROW(vm, int, chunk->lines, cap, chunk->capacity);
    assert(chunk->code != NULL); // TODO: figure out code coverage
  }
  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

void kup_chunkfree(kuvm *__nonnull vm, kuchunk *__nonnull chunk) {
  KU_ARRAY_FREE(vm, uint8_t, chunk->code, chunk->capacity);
  KU_ARRAY_FREE(vm, int, chunk->lines, chunk->capacity);
  KU_ARRAY_FREE(vm, kuval, chunk->constants.values, chunk->constants.capacity);
}

int kup_chunkconst(kuvm *__nonnull vm, kuchunk *__nonnull chunk, kuval value) {
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
void ku_arrinit(KU_UNUSED kuvm *__nonnull vm, kuarr *__nonnull array) {
  array->values = NULL;
  array->count = 0;
  array->capacity = 0;
}

void ku_arrwrite(kuvm *__nonnull vm, kuarr *__nonnull array, kuval value) {
  if (array->capacity < array->count + 1) {
    int old = array->capacity;
    array->capacity = KU_CAPACITY_GROW(old);
    array->values = KU_ARRAY_GROW(vm, kuval, array->values, old, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}

// ********************** debugging **********************


// ------------------------------------------------------------
// Locals
// ------------------------------------------------------------
void kup_compinit(kuvm *__nonnull vm, kucomp *__nonnull compiler, kufunc_t type) {
  compiler->function = NULL; // for GC
  compiler->enclosing = vm->compiler;
  compiler->count = 0;
  compiler->depth = 0;
  compiler->type = type;
  compiler->function = kup_funcnew(vm);

  vm->compiler = compiler;
  if (type != FUNC_MAIN) {
    compiler->function->name = ku_strfrom(vm, vm->parser.prev.start, vm->parser.prev.len);
  }
  kulocal *__nonnull local = &vm->compiler->locals[vm->compiler->count++];
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

void kup_block(kuvm *__nonnull vm, kuloop *__nullable loop) {
  while (!kup_pcheck(vm, TOK_RBRACE) && !kup_pcheck(vm, TOK_EOF)) {
    kup_decl(vm, loop);
  }
  kup_pconsume(vm, TOK_RBRACE, "'}' expected");
}

void kup_beginscope(kuvm *__nonnull vm) {
  vm->compiler->depth++;
}

void kup_endscope(kuvm *__nonnull vm) {
  vm->compiler->depth--;

  while (vm->compiler->count > 0 &&
         vm->compiler->locals[vm->compiler->count - 1].depth >
         vm->compiler->depth) {

    if (KU_IS_CAPTURED(vm->compiler->locals[vm->compiler->count - 1])) {
      kup_emitop(vm, OP_CLOSE_UPVAL);
    } else {
      kup_emitop(vm, OP_POP);
    }
    vm->compiler->count--;
    }
}

void kup_declare_let(kuvm *__nonnull vm, bool isconst) {
  kutok *__nonnull name = &vm->parser.prev;

  if (vm->compiler->depth == 0) {
    return;
  }
  for (int i = vm->compiler->count - 1; i >= 0; i--) {
    kulocal *__nonnull local = &vm->compiler->locals[i];
    if (local->depth != -1 && local->depth < vm->compiler->depth) {
      break;
    }

    if (kup_identeq(vm, name, &local->name)) {
      // TODO: add code coverage
      ku_perr(vm, "local already defined");
    }
  }
  kup_addlocal(vm, *name, isconst);
}

void kup_addlocal(kuvm *__nonnull vm, kutok name, bool isconst) {
  if (vm->compiler->count == vm->max_locals) {
    ku_perr(vm, "too many locals");
    return;
  }

  kulocal *__nonnull local = &vm->compiler->locals[vm->compiler->count++];
  local->name = name;
  local->depth = -1;
  if ((local->flags & KU_LOCAL_CONST) && (local->flags & KU_LOCAL_INIT)) {
    ku_perr(vm, "const already initialized");
  }

  local->flags = (isconst) ? KU_LOCAL_CONST : KU_LOCAL_NONE;
}

bool kup_identeq(KU_UNUSED kuvm *__nonnull vm, kutok *__nonnull a, kutok *__nonnull b) {
  if (a->len != b->len) {
    return false;
  }

  return memcmp(a->start, b->start, a->len) == 0;
}

int kup_resolvelocal(kuvm *__nonnull vm, kucomp *__nonnull compiler, kutok *__nonnull name) {
  for (int i = compiler->count - 1; i >= 0; i--) {
    kulocal *__nonnull local = &compiler->locals[i];
    if (kup_identeq(vm, name, &local->name)) {
      if (local->depth == -1) {
        ku_perr(vm, "own initialization disallowed");
      }
      return i;
    }
  }
  return -1;
}

void kup_markinit(kuvm *__nonnull vm) {
  // functions can reference themselves unlike global
  // variables which can't use their own value in their
  // initialization
  if (vm->compiler->depth == 0) return;
  kulocal *__nonnull local = &vm->compiler->locals[vm->compiler->count - 1];
  local->depth = vm->compiler->depth;
  local->flags |= KU_LOCAL_INIT;
}

int ku_opslotdis(kuvm *__nonnull vm, const char *__nonnull name, kuchunk *__nonnull chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  ku_printf(vm, "%-16s %4d", name, slot);
  return offset + 2;
}

// ********************** function **********************
kufunc *__nonnull kup_funcnew(kuvm *__nonnull vm) {
  kufunc *__nonnull fn = (kufunc *)KU_ALLOC_OBJ(vm, kufunc, OBJ_FUNC);
  fn->arity = 0;
  fn->upcount = 0;
  fn->name = NULL;
  kup_chunkinit(vm, &fn->chunk);
  return fn;
}

// ********************** native functions **********************
kucfunc *__nonnull ku_cfuncnew(kuvm *__nonnull vm, cfunc __nonnull f) {
  kucfunc *__nonnull cf = KU_ALLOC_OBJ(vm, kucfunc, OBJ_CFUNC);
  cf->fn = f;
  return cf;
}

void ku_cfuncdef(kuvm *__nonnull vm, const char *__nonnull name, cfunc __nonnull f) {
  int len = (int)strlen(name);
  kustr *__nonnull sname = ku_strfrom(vm, name, len);
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
static kuval ku_arraycons(kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  int cap = 0;
  if (argc > 0 && IS_NUM(argv[0])) {
    cap = (int)AS_NUM(argv[0]);
    kuaobj *__nonnull ao = ku_arrnew(vm, cap);
    ku_push(vm, OBJ_VAL(ao)); // for GC
    for (int i = 0; i < cap; i++) {
      ku_arrset(vm, ao, i, NULL_VAL);
    }
    ku_pop(vm); // for GC
    return OBJ_VAL(ao);
  }
  // TODO: add code coverage
  return OBJ_VAL(ku_arrnew(vm, cap));
}

void array_swap(kuval *__nonnull a, kuval *__nonnull b) {
  kuval t = *a;
  *a = *b;
  *b = t;
}

int array_partition(kuvm *__nonnull vm, kuval array[], int low, int high, kuclosure *__nonnull cmp) {
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

void array_qsort(kuvm *__nonnull vm, kuval array[], int low, int high, kuclosure *__nonnull cmp) {
  if (low < high) {
    int pi = array_partition(vm, array, low, high, cmp);
    if (vm->err) return;
    array_qsort(vm, array, low, pi - 1, cmp);
    array_qsort(vm, array, pi + 1, high, cmp);
  }
}

bool array_sort(kuvm *__nonnull vm, kuaobj *__nonnull src, int argc, kuval *__nullable argv) {
  if (argc != 1 || !IS_CLOSURE(argv[0])) {
    return false;
  }

  kuclosure *__nonnull cl = AS_CLOSURE(argv[0]);
  if (cl->func->arity != 2) {
    return false;
  }
  array_qsort(vm, src->elements.values, 0, src->elements.count-1, cl);
  ku_pop(vm);   // closure
  ku_pop(vm);   // receiver
  return !vm->err;
}

bool array_filter(kuvm *__nonnull vm, kuaobj *__nonnull src, int argc, kuval *__nullable argv, kuval *__nonnull ret) {
  if (argc != 1 || !IS_CLOSURE(argv[0])) {
    return false;
  }

  kuclosure *__nonnull cl = AS_CLOSURE(argv[0]);
  if (cl->func->arity != 1) {
    return false;
  }

  kuaobj *__nonnull dest = ku_arrnew(vm, 0);
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

bool array_map(kuvm *__nonnull vm, kuaobj *__nonnull src, int argc, kuval *__nullable argv, kuval *__nonnull ret) {
  if (argc != 1 || !IS_CLOSURE(argv[0])) {
    return false;
  }

  kuclosure *__nonnull cl = AS_CLOSURE(argv[0]);
  if (cl->func->arity != 1) {
    return false;
  }

  kuaobj *__nonnull dest = ku_arrnew(vm, src->elements.capacity);
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

bool array_reduce(kuvm *__nonnull vm, kuaobj *__nonnull src, int argc, kuval *__nullable argv, kuval *__nonnull ret) {
  if (argc != 2 || !IS_CLOSURE(argv[1])) {
    return false;
  }

  kuclosure *__nonnull cl = AS_CLOSURE(argv[1]);
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

bool array_invoke(kuvm *__nonnull vm, kuval obj, kustr *__nonnull method, int argc, kuval *__nullable argv) {
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
      ku_push(vm, NULL_VAL);
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
static int arglen(KU_UNUSED kuvm *__nonnull vm, const char *__nonnull f, kuval arg) {
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

char *__nullable format_core(kuvm *__nonnull vm, int argc, kuval *__nullable argv, int *__nonnull count) {
  if (argc < 1 || !IS_STR(argv[0])) {
    return NULL;
  }

  kustr *__nonnull sfmt = AS_STR(argv[0]);
  const char *__nonnull fmt = sfmt->chars;
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

  char *__nonnull chars = ku_alloc(vm, NULL, 0, (size_t)needed+1);

  iarg = 0;
  char *__nonnull d = chars;

  for (char *__nonnull p = (char *)fmt; *p; p++) {
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
      ku_free(vm, chars, (size_t)needed+1);
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

static kuval ku_intf(KU_UNUSED kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  if (argc > 0 && IS_NUM(argv[0])) {
    double d = AS_NUM(argv[0]);
    int i = (int)d;
    return NUM_VAL((double)i);
  }
  return NULL_VAL;
}

static kuval ku_parseFloat(KU_UNUSED kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  if (argc > 0 && IS_STR(argv[0])) {
    kustr *__nonnull s = AS_STR(argv[0]);
    double d = atof(s->chars);
    return NUM_VAL(d);
  }
  return NULL_VAL;
}

static kuval ku_eval(kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  if (argc > 0 && IS_STR(argv[0])) {
    int stack = (argc > 1 && IS_NUM(argv[1])) ? (int)AS_NUM(argv[1]) : 128;
    kuvm *__nonnull temp = ku_newvm(stack, &vm->env);
    const char *__nonnull line = AS_STR(argv[0])->chars;
    size_t len = strlen(line) + 8;
    char *__nonnull buffer = vm->env.alloc(len);
    sprintf(buffer, "let _=%s;", line);
    kures res = ku_exec(temp, buffer);
    kuval ret = NULL_VAL;
    vm->env.free(buffer);
    if (res == KVM_OK) {
      kustr *__nonnull key = ku_strfrom(temp, "_", 1);
      ku_tabget(temp, &temp->globals, key, &ret);
    }
    ku_freevm(temp);
    return ret;
  }
  return NULL_VAL;
}

static kuval ku_clock(KU_UNUSED kuvm *__nonnull vm, KU_UNUSED int argc, KU_UNUSED kuval *__nullable argv) {
  return NUM_VAL((double)clock() / CLOCKS_PER_SEC);
}

static kuval ku_print(kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  if (argc > 0 && !IS_STR(argv[0])) {
    for (int i = 0; i < argc; i++) {
      ku_printval(vm, argv[i]);
    }
    return NULL_VAL;
  }

  int needed;
  char *__nullable nullable_str = format_core(vm, argc, argv, &needed);
  if (nullable_str == NULL) {
    // TODO: add code coverage
    return NULL_VAL;
  }
  char *__nonnull str = KU_NONNULL(nullable_str); // TODO: figure out code coverage
  ku_printf(vm, str);
  ku_free(vm, str, (size_t)needed+1);
  return NULL_VAL;
}

// ********************** math **********************
#define  _USE_MATH_DEFINES    // for windows
#include <math.h>

kuval math_scall(KU_UNUSED kuvm *__nonnull vm, kustr *__nonnull m, int argc, kuval *__nullable argv) {
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
  return NULL_VAL;
}


// ********************** table **********************
kuval table_cons(kuvm *__nonnull vm, KU_UNUSED int argc, KU_UNUSED kuval *__nullable argv) {
  kutobj *__nonnull to = (kutobj *)ku_objalloc(vm, sizeof(kutobj), OBJ_CINST);
  ku_tabinit(vm, &to->data);
  return OBJ_VAL(to);
}

kuval ku_cinstance(kuvm *__nonnull vm, const char *__nonnull cname) {
  kuval tcv;
  if (!ku_tabget(vm, &vm->globals, ku_strfrom(vm, cname, 5), &tcv)) {
    // TODO: add code coverage
    return NULL_VAL;
  }

  if (!IS_CCLASS(tcv)) {
    // TODO: add code coverage
    return NULL_VAL;
  }

  kucclass *__nonnull tcc = AS_CCLASS(tcv);
  kuval tab = table_cons(vm, 0, NULL);
  kutobj *__nonnull to = (kutobj *)AS_OBJ(tab);
  to->base.klass = tcc;
  return tab;
}

kuval table_icall(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull m, int argc, kuval *__nullable argv) {
  if (M3(m, "get") && argc > 0 && IS_STR(argv[0])) {
    kutobj *__nonnull to = (kutobj *)o;
    kustr *__nonnull s = AS_STR(argv[0]);
    kuval ret;
    if (ku_tabget(vm, &to->data, s, &ret)) {
      return ret;
    }
    return NULL_VAL;
  }

  if (M3(m, "set") && argc > 1 && IS_STR(argv[0])) {
    kutobj *__nonnull to = (kutobj *)o;
    kustr *__nonnull s = AS_STR(argv[0]);
    ku_tabset(vm, &to->data, s, argv[1]);
    return argv[1];
  }

  if (m->len == 4 && strcmp(m->chars, "iter") == 0) {
    if (argc != 1 || !IS_CLOSURE(argv[0])) {
      return NULL_VAL;
    }

    kuclosure *__nonnull cl = AS_CLOSURE(argv[0]);
    if (cl->func->arity != 2) {
      return NULL_VAL;
    }

    kutobj *__nonnull to = (kutobj *)o;
    for (int i = 0; i < to->data.capacity; i++) {
      kuentry *__nonnull e = &to->data.entries[i];
      if (e->key != NULL) {
        ku_push(vm, OBJ_VAL(e->key));
        ku_push(vm, e->value);
        ku_nativecall(vm, cl, 2);
        ku_pop(vm); // call result
        ku_pop(vm); // key
      }
    }
  }
  return NULL_VAL;
}

kuval table_iget(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull p) {
  kutobj *__nonnull to = (kutobj *)o;
  kuval ret;

  if (ku_tabget(vm, &to->data, p, &ret)) {
    return ret;
  }
  return NULL_VAL;
}

kuval table_iput(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull p, kuval v) {
  kutobj *__nonnull to = (kutobj *)o;
  ku_tabset(vm, &to->data, p, v);
  return v;
}

kuval table_ifree(kuvm *__nonnull vm, kuobj *__nonnull o) {
  kutobj *__nonnull to = (kutobj *)o;
  ku_tabfree(vm, &to->data);
  KU_FREE(vm, kutobj, to);
  return NULL_VAL;
}

kuval table_imark(kuvm *__nonnull vm, kuobj *__nonnull o) {
  kutobj *__nonnull to = (kutobj *)o;
  ku_marktable(vm, &to->data);
  return NULL_VAL;
}

// ********************** string **********************
kuval string_format(kuvm *__nonnull vm, int argc, kuval *__nullable argv) {
  int needed;
  char *__nullable nullable_chars = format_core(vm, argc, argv, &needed);
  if (nullable_chars == NULL) {
    return NULL_VAL;
  }
  char *__nonnull chars = KU_NONNULL(nullable_chars); // TODO: figure out code coverage
  return OBJ_VAL(ku_strtake(vm, chars, needed));
}

static kuval string_frombytes(kuvm *__nonnull vm, kuaobj *__nonnull arr) {
  int len = arr->elements.count;
  char *__nonnull buff = KU_ALLOC(vm, char, (size_t)len + 1);
  for (int i = 0; i < len; i++) {
    buff[i] = (int)AS_NUM(ku_arrget(vm, arr, i));
  }
  buff[len] = '\0';
  kustr *__nonnull res = ku_strtake(vm, buff, len);
  return OBJ_VAL(res);
}

kuval string_scall(kuvm *__nonnull vm, kustr *__nonnull m, int argc, kuval *__nullable argv) {
  if (strcmp(m->chars, "format") == 0) {
    return string_format(vm, argc, argv);
  } else if (strcmp(m->chars, "frombytes") == 0 && argc == 1 && IS_ARRAY(argv[0])) {
    return string_frombytes(vm, AS_ARRAY(argv[0]));
  }
  return NULL_VAL;
}

kuval string_iget(kuvm *__nonnull vm, kuobj *__nonnull obj, kustr *__nonnull prop) {
  if (prop == vm->countstr) {
    kustr *__nonnull str = (kustr *)obj;
    return NUM_VAL(str->len);
  }
  return NULL_VAL;
}

kuval string_icall(kuvm *__nonnull vm, kuobj *__nonnull o, kustr *__nonnull m, int argc, kuval *__nullable argv) {
  if (m->len == 6 && strcmp(m->chars, "substr") == 0) {
    kustr *__nonnull s = (kustr *)o;
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
    char *__nonnull buff = KU_ALLOC(vm, char, (size_t)len + 1);
    for (int i = 0; i < len; i++) {
      buff[i] = s->chars[start + i];
    }
    buff[len] = '\0';
    kustr *__nonnull res = ku_strtake(vm, buff, len);
    return OBJ_VAL(res);

  }
  return NULL_VAL;
}

kuval math_sget(KU_UNUSED kuvm *__nonnull vm, kustr *__nonnull p) {
  if (M2(p, "pi")) {
    return NUM_VAL(M_PI);
  }
  return NULL_VAL;
}

void ku_reglibs(kuvm *__nonnull vm) {
  ku_cfuncdef(vm, "clock", ku_clock);
  ku_cfuncdef(vm, "int", ku_intf);
  ku_cfuncdef(vm, "parseFloat", ku_parseFloat);
  ku_cfuncdef(vm, "printf", ku_print);
  ku_cfuncdef(vm, "array", ku_arraycons);
  ku_cfuncdef(vm, "eval", ku_eval);

  kucclass *__nonnull math = ku_cclassnew(vm, "math");
  math->sget = math_sget;
  math->scall = math_scall;
  ku_cclassdef(vm, math);

  kucclass *__nonnull string = ku_cclassnew(vm, "string");
  string->scall = string_scall;
  ku_cclassdef(vm, string);

  kucclass *__nonnull table = ku_cclassnew(vm, "table");
  table->cons = table_cons;
  table->icall = table_icall;
  table->iget = table_iget;
  table->iput = table_iput;
  table->ifree = table_ifree;
  table->imark = table_imark;
  ku_cclassdef(vm, table);
}

// ********************** closure **********************
kuclosure *__nonnull ku_closurenew(kuvm *__nonnull vm, kufunc *__nonnull f) {
  ku_push(vm, OBJ_VAL(f)); // for GC
  kuclosure *__nonnull cl = KU_ALLOC_OBJ(vm, kuclosure, OBJ_CLOSURE);
  cl->func = f;
  cl->upcount = 0;  // for GC
  cl->upvals = NULL; // for GC
  ku_push(vm, OBJ_VAL(cl));
  kuxobj *__nullable *__nonnull upvals = KU_ALLOC(vm, kuxobj*, f->upcount);
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
kuxobj *__nonnull ku_xobjnew(kuvm *__nonnull vm, kuval *__nonnull slot) {
  kuxobj *__nonnull uo = KU_ALLOC_OBJ(vm, kuxobj, OBJ_UPVAL);
  uo->location = slot;
  uo->next = NULL;
  uo->closed = NULL_VAL;
  return uo;
}

// ********************** garbage collection **********************
static void ku_markarray(kuvm *__nonnull vm, kuarr *__nonnull array) {
  for (int i = 0; i < array->count; i++) {
    ku_markval(vm, array->values[i]);
  }
}

static void ku_traceobj(kuvm *__nonnull vm, kuobj *__nonnull o) {
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p trace ", (void *)o);
    ku_printval(vm, OBJ_VAL(o));
    ku_printf(vm, "\n");
  }

  switch (o->type) {
    case OBJ_STR:
    case OBJ_CFUNC:
      break;

    case OBJ_ARRAY: {
      kuaobj *__nonnull ao = (kuaobj *)o;
      ku_markarray(vm, &ao->elements);
    }
      break;

    case OBJ_CINST: {
      kunobj *__nonnull i = (kunobj *)o;
      ku_markobj(vm, (kuobj *)i->klass);
      if (i->klass->imark) {
        i->klass->imark(vm, o);
      }
    }
      break;

    case OBJ_UPVAL:
      ku_markval(vm, ((kuxobj *)o)->closed);
      break;

    case OBJ_FUNC: {
      kufunc *__nonnull fn = (kufunc *)o;
      ku_markobj(vm, (kuobj *)fn->name);
      ku_markarray(vm, &fn->chunk.constants);
    }
      break;

    case OBJ_CCLASS: {
      kucclass *__nonnull cc = (kucclass *)o;
      ku_markobj(vm, (kuobj *)cc->name);
      if (cc->smark) {
        cc->smark(vm, o);
      }
    }
      break;

    case OBJ_CLASS: {
      kuclass *__nonnull c = (kuclass *)o;
      ku_markobj(vm, (kuobj *)c->name);
      ku_marktable(vm, &c->methods);
    }
      break;

    case OBJ_INSTANCE: {
      kuiobj *__nonnull i = (kuiobj *)o;
      ku_markobj(vm, (kuobj *)i->klass);
      ku_marktable(vm, &i->fields);
    }
      break;

    case OBJ_BOUND_METHOD: {
      kubound *__nonnull bm = (kubound *)o;
      ku_markval(vm, bm->receiver);
      ku_markobj(vm, (kuobj *)bm->method);
    }
      break;

    case OBJ_CLOSURE: {
      kuclosure *__nonnull cl = (kuclosure *)o;
      ku_markobj(vm, (kuobj *)cl->func);
      for (int i = 0; i < cl->upcount; i++) {
        ku_markobj(vm, (kuobj *)cl->upvals[i]);
      }
    }
      break;
  }
}

static void ku_tracerefs(kuvm *__nonnull vm) {
  while (vm->gccount > 0) {
    kuobj *__nonnull o = vm->gcstack[--vm->gccount];
    ku_traceobj(vm, o);
  }
}

void ku_markobj(kuvm *__nonnull vm, kuobj *__nullable o) {
  if (o == NULL) {
    return;
  }

  if (o->marked) return;
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "%p mark ", (void *) o);
    ku_printval(vm, OBJ_VAL(o));
    ku_printf(vm, "\n");
  }

  o->marked = true;

  if (vm->gccap < vm->gccount + 1) {
    vm->gccap = KU_CAPACITY_GROW(vm->gccap);
    // for VC++ C6308
    vm->gcstack = (kuobj **)vm->env.realloc(vm->gcstack, sizeof(kuobj *) * vm->gccap);
  }
  vm->gcstack[vm->gccount++] = KU_NONNULL(o); // TODO: figure out code coverage
}

static void ku_markval(kuvm *__nonnull vm, kuval v) {
  if (IS_OBJ(v)) {
    ku_markobj(vm, AS_OBJ(v));
  }
}

static void ku_marktable(kuvm *__nonnull vm, kutab *__nonnull tab) {
  for (int i = 0; i < tab->capacity; i++) {
    kuentry *__nonnull e = &tab->entries[i];
    ku_markobj(vm, (kuobj *)e->key);
    ku_markval(vm, e->value);
  }
}

static void ku_markcomproots(kuvm *__nonnull vm) {
  kucomp *__nullable comp = vm->compiler;
  while (comp != NULL) {
    ku_markobj(vm, (kuobj *)comp->function);
    comp = comp->enclosing;
  }
}

static void ku_markroots(kuvm *__nonnull vm) {
  for (kuval *__nonnull pv = vm->stack; pv < vm->sp; pv++) {
    ku_markval(vm, *pv);
  }

  for (int i = 0; i < vm->framecount; i++) {
    ku_markobj(vm, (kuobj *)vm->frames[i].closure);
  }

  for (kuxobj *__nullable uo = vm->openupvals; uo != NULL; uo = uo->next) {
    ku_markobj(vm, (kuobj *)uo);
  }

  ku_marktable(vm, &vm->globals);
  ku_markcomproots(vm);
}

void ku_sweep(kuvm *__nonnull vm) {
  kuobj *__nullable prev = NULL;
  kuobj *__nullable obj = vm->objects;

  while (obj != NULL) {
    if (obj->marked) {
      obj->marked = false;
      prev = obj;
      obj = obj->next;
    } else {
      kuobj *__nonnull unreached = KU_NONNULL(obj); // TODO: figure out code coverage
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

static void ku_freeweak(kuvm *__nonnull vm, kutab *__nonnull table) {
  for (int i = 0; i < table->capacity; i++) {
    kuentry *__nonnull e = &table->entries[i];
    if (e->key != NULL && !e->key->obj.marked) {
      ku_tabdel(vm, table, e->key);
    }
  }
}

#define GC_HEAP_GROW_FACTOR 2

void ku_gc(kuvm *__nonnull vm) {
  if (vm->flags & KVM_F_GCLOG) {
    ku_printf(vm, "-- gc start\n");
  }

  size_t bytes = vm->allocated;

  ku_markroots(vm);
  ku_markobj(vm, (kuobj *)vm->initstr);
  ku_markobj(vm, (kuobj *)vm->countstr);
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
kuclass *__nonnull ku_classnew(kuvm *__nonnull vm, kustr *__nonnull name) {
  kuclass *__nonnull c = KU_ALLOC_OBJ(vm, kuclass, OBJ_CLASS);
  c->name = name;
  ku_tabinit(vm, &c->methods);
  return c;
}

// ********************** instance **********************
kuiobj *__nonnull ku_instnew(kuvm *__nonnull vm, kuclass *__nonnull klass) {
  kuiobj *__nonnull i = KU_ALLOC_OBJ(vm, kuiobj, OBJ_INSTANCE);
  i->klass = klass;
  ku_tabinit(vm, &i->fields);
  return i;
}


// ********************** bound method **********************
kubound *__nonnull ku_boundnew(kuvm *__nonnull vm, kuval receiver, kuclosure *__nonnull method) {
  kubound *__nonnull bm = KU_ALLOC_OBJ(vm, kubound, OBJ_BOUND_METHOD);
  bm->receiver = receiver;
  bm->method = method;
  return bm;
}

// ********************** print **********************
void ku_printf(kuvm *__nonnull vm, const char *__nonnull fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (vm->print) {
    vm->print(fmt, args);
  }
  va_end(args);
}

void ku_printmem(kuvm *__nonnull vm) {
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

static void ku_printobj(kuvm *__nonnull vm, kuval val) {
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
      kunobj *__nonnull i = AS_CINST(val);
      ku_printf(vm, "<%s instance>", i->klass->name->chars);
    }
      break;
    case OBJ_CCLASS: {
      kucclass *__nonnull cc = AS_CCLASS(val);
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

void ku_printval(kuvm *__nonnull vm, kuval value) {

  if (IS_BOOL(value)) {
    ku_printf(vm, AS_BOOL(value) ? "true" : "false");
  } else if (IS_NULL(value)) {
    ku_printf(vm, "null");
  } else if (IS_NUM(value)) {
    ku_printf(vm, "%f", AS_NUM(value));
  } else if (IS_OBJ(value)) {
    ku_printobj(vm, value);
  }
}

static void ku_printfunc(kuvm *__nonnull vm, kufunc *__nonnull fn) {
  if (fn->name) {
    ku_printf(vm, "<fn %.*s>", fn->name->len, fn->name);
  } else {
    ku_printf(vm, "<fn __main__>");
  }
}

void ku_printstack(kuvm *__nonnull vm) {
  ku_printf(vm, " [");
  for (kuval *__nonnull vp = vm->stack; vp < vm->sp; vp++) {
    ku_printval(vm, *vp);
    if (vp < vm->sp - 1) {
      ku_printf(vm, ",");
    }
  }
  ku_printf(vm, "]");
  ku_printf(vm, "\n");
}

// ********************** loop patch **********************
void kup_loopinit(KU_UNUSED kuvm *__nonnull vm, kuloop *__nonnull loop) {
  loop->breakpatch.count = 0;
  loop->continuepatch.count = 0;
}

void kup_emitpatch(kuvm *__nonnull vm, kupatch *__nonnull patch, uint8_t op) {
  if (patch->count == vm->max_patches) {
    ku_perr(vm, "max break / continue limit reached");
    return;
  }
  patch->offset[patch->count++] = kup_emitjump(vm, op);
}

void kup_patchall(kuvm *__nonnull vm, kupatch *__nonnull patch, uint16_t to, bool rev) {
  for (int i = 0; i < patch->count; i++) {
    kuchunk *__nonnull c = kup_chunk(vm);
    uint16_t delta = rev ? patch->offset[i] - to + 2 : to - patch->offset[i];
    c->code[patch->offset[i]] = (delta >> 8) & 0xff;
    c->code[patch->offset[i]+1] = delta & 0xff;
  }
}

// ********************** native class **********************
kucclass *__nonnull ku_cclassnew(kuvm *__nonnull vm, const char *__nonnull name) {
  int len = (int)strlen(name);
  kustr *__nonnull sname = ku_strfrom(vm, name, len);

  kucclass *__nonnull cc = KU_ALLOC_OBJ(vm, kucclass, OBJ_CCLASS);
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

void ku_cclassdef(kuvm *__nonnull vm, kucclass *__nonnull cc) {
  ku_push(vm, OBJ_VAL(cc->name));
  ku_push(vm, OBJ_VAL((kuobj *)cc));
  ku_tabset(vm, &vm->globals, AS_STR(vm->stack[0]), vm->stack[1]);
  ku_pop(vm);
  ku_pop(vm);
}

// ********************** array object **********************
kuaobj *__nonnull ku_arrnew(kuvm *__nonnull vm, int capacity) {
  kuaobj *__nonnull arr = KU_ALLOC_OBJ(vm, kuaobj, OBJ_ARRAY);
  ku_push(vm, OBJ_VAL(arr)); // for GC
  arr->elements.count = 0;
  arr->elements.capacity = capacity;
  arr->elements.values = NULL;

  kuarr *__nonnull e = &arr->elements;
  if (capacity > 0) {
    e->values = (kuval *)ku_alloc(vm, NULL, 0, sizeof(kuval)*capacity);

    for (int i = 0; i < capacity; i++) {
      e->values[i] = NULL_VAL;
    }
  }
  ku_pop(vm); // GC
  return arr;
}

void ku_arrset(kuvm *__nonnull vm, kuaobj *__nullable arr, int index, kuval value) {
  if (arr == NULL) {
    // TODO: add code coverage
    ku_err(vm, "null array");
    return;
  }

  kuarr *__nonnull e = &arr->elements;
  int oldcount = e->count;

  if (e->capacity <= index) {
    int old = e->capacity;
    e->capacity = KU_CAPACITY_GROW(index);
    e->values = KU_ARRAY_GROW(vm, kuval, e->values, old, e->capacity);
  }

  for (int i = oldcount; i < index; i++) {
    e->values[i] = NULL_VAL;
  }
  e->values[index] = value;
  e->count = (index >= oldcount) ? index + 1 : e->count;
}

kuval ku_arrget(kuvm *__nonnull vm, kuaobj *__nullable arr, int index) {
  if (arr == NULL) {
    // TODO: add code coverage
    ku_err(vm, "null array");
    return NULL_VAL;
  }
  if (index < arr->elements.count) {
    return arr->elements.values[index];
  }
  return NULL_VAL;
}
