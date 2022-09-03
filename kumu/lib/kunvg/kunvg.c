//
//  kunvg.c
//  nvgdemo
//
//  Created by Mohsen Agsen on 1/7/22.
//

#include "kunvg.h"

static inline double _N(kuvm *vm, int argc, kuval *args, int index) {
  if (index >= argc || !IS_NUM(args[index])) {
    ku_err(vm, "argument invalid or out of bounds");
    return 0.0;
  }
  return AS_NUM(args[index]);
}

#define N(i) _N(vm, argc, args, i)

static inline const char *_S(kuvm *vm, int argc, kuval *args, int index) {
  if (index >= argc || !IS_STR(args[index])) {
    ku_err(vm, "argument invalid or out of bounds");
    return "";
  }
  return AS_STR(args[index])->chars;
}

#define S(i) _S(vm, argc, args, i)

NVGcontext *nvgContextFor(const char *key, kunvobj *target);

static kuval NVGTextOrBoxBounds(kuvm *vm, NVGcontext *ctx, int argc, kuval *args, bool box) {
  double x = N(0);
  double y = N(1);
  int adj = (box) ? 1 : 0;
  double breakRowWidth = (box) ? N(2) : 0;

  const char *text = S(2 + adj);
  int istart = (int)N(3 + adj);
  int iend = (int)N(4 + adj);
  kuaobj *ao = AS_ARRAY(args[5 + adj]);
  const char *start = text + istart;
  const char *end = text + iend;
  float bounds[4];
  float ret = NULL_VAL;

  if (box) {
    nvgTextBoxBounds(ctx, x, y, breakRowWidth, start, (iend >= 0) ? end : NULL, bounds);
  }
  else {
    ret = nvgTextBounds(ctx, x, y, start, (iend >= 0) ? end: NULL, bounds);
  }

  if (IS_ARRAY(args[5 + adj])) {
    for (int i = 0; i < 4; i++) {
      ku_arrset(vm, ao, i, NUM_VAL(bounds[i]));
    }
  }
  return NUM_VAL(ret);
}

static inline NVGcolor NVGColorFromInt(uint64_t rgba) {
  uint64_t r = (rgba >> 24) & 0xff;
  uint64_t g = (rgba >> 16) & 0xff;
  uint64_t b = (rgba >> 8) & 0xff;
  uint64_t a = rgba & 0xff;
  return nvgRGBA(r, g, b, a);
}

static uint64_t NVGcolorToInt(NVGcolor color) {
  uint64_t r = 255.0 * color.r;
  uint64_t g = 255.0 * color.g;
  uint64_t b = 255.0 * color.b;
  uint64_t a = 255.0 * color.a;
  uint64_t rgba = r << 24 | g << 16 | b << 8 | a;
  return rgba;
}

static void NVGpaintToArray(kuvm *vm, NVGpaint *p, kuaobj *arr) {
  ku_arrset(vm, arr, 0, NUM_VAL(p->xform[0]));
  ku_arrset(vm, arr, 1, NUM_VAL(p->xform[1]));
  ku_arrset(vm, arr, 2, NUM_VAL(p->xform[2]));
  ku_arrset(vm, arr, 3, NUM_VAL(p->xform[3]));
  ku_arrset(vm, arr, 4, NUM_VAL(p->xform[4]));
  ku_arrset(vm, arr, 5, NUM_VAL(p->xform[5]));
  ku_arrset(vm, arr, 6, NUM_VAL(p->extent[0]));
  ku_arrset(vm, arr, 7, NUM_VAL(p->extent[1]));
  ku_arrset(vm, arr, 8, NUM_VAL(p->radius));
  ku_arrset(vm, arr, 9, NUM_VAL(p->feather));
  ku_arrset(vm, arr, 10, NUM_VAL(NVGcolorToInt(p->innerColor)));
  ku_arrset(vm, arr, 11, NUM_VAL(NVGcolorToInt(p->outerColor)));
  ku_arrset(vm, arr, 12, NUM_VAL(p->image));
}

static void NVGpaintFromArray(kuvm *vm, NVGpaint *p, kuaobj *arr) {
  p->xform[0] = AS_NUM(ku_arrget(vm, arr, 0));
  p->xform[1] = AS_NUM(ku_arrget(vm, arr, 1));
  p->xform[2] = AS_NUM(ku_arrget(vm, arr, 2));
  p->xform[3] = AS_NUM(ku_arrget(vm, arr, 3));
  p->xform[4] = AS_NUM(ku_arrget(vm, arr, 4));
  p->xform[5] = AS_NUM(ku_arrget(vm, arr, 5));

  p->extent[0] = AS_NUM(ku_arrget(vm, arr, 6));
  p->extent[1] = AS_NUM(ku_arrget(vm, arr, 7));

  p->radius = AS_NUM(ku_arrget(vm, arr, 8));
  p->feather = AS_NUM(ku_arrget(vm, arr, 9));

  p->innerColor = NVGColorFromInt(AS_NUM(ku_arrget(vm, arr, 10)));
  p->outerColor = NVGColorFromInt(AS_NUM(ku_arrget(vm, arr, 11)));
  p->image = (int)AS_NUM(ku_arrget(vm, arr, 12));
}

kuval nanovg_cons(kuvm *vm, int argc, kuval *args) {
  if (argc < 2 || !IS_INSTANCE(args[0]) || !IS_STR(args[1])) {
    ku_err(vm, "expected nanovg(object, key)");
    return NULL_VAL;
  }
  kunvobj *no = (kunvobj*)ku_objalloc(vm, sizeof(kunvobj), OBJ_CINST);
  ku_push(vm, OBJ_VAL(no)); // for GC
  no->target = args[0];
  no->ctx = nvgContextFor(AS_STR(args[1])->chars, no);
  no->render = ku_strfrom(vm, "render", 6);
  no->touch = ku_strfrom(vm, "touch", 5);
  no->keypress = ku_strfrom(vm, "keypress", 8);
  ku_pop(vm);
  return OBJ_VAL(no);
}

static inline uint64_t nanovg_args2intcolor(kuval *args) {
  uint64_t r = (uint64_t)AS_NUM(args[0]);
  uint64_t g = (uint64_t)AS_NUM(args[1]);
  uint64_t b = (uint64_t)AS_NUM(args[2]);
  uint64_t a = (uint64_t)AS_NUM(args[3]);
  uint64_t rgba = r << 24 | g << 16 | b << 8 | a;
  return rgba;
}



kuval nanovg_icall(kuvm *vm, kuobj *o, kustr *m, int argc, kuval *args) {
  kunvobj *no = (kunvobj*)o;

  if (strcmp(m->chars, "beginFrame") == 0 && argc == 3) {
    nvgBeginFrame(no->ctx, N(0), N(1), N(2));
  } else if (strcmp(m->chars, "endFrame") == 0) {
    nvgEndFrame(no->ctx);
  } else if (strcmp(m->chars, "beginPath") == 0) {
    nvgBeginPath(no->ctx);
  } else if (strcmp(m->chars, "rect") == 0 && argc == 4) {
    nvgRect(no->ctx, N(0), N(1), N(2), N(3));
  } else if (strcmp(m->chars, "fillColor") == 0 && argc == 1) {
    nvgFillColor(no->ctx, NVGColorFromInt((uint64_t)AS_NUM(args[0])));
  } else if (strcmp(m->chars, "RGBA") == 0 && argc == 4) {
    return NUM_VAL(nanovg_args2intcolor(args));
  } else if (strcmp(m->chars, "HSLA") == 0 && argc == 4) {
    NVGcolor color = nvgHSLA(N(0), N(1), N(2), N(3));
    return NUM_VAL(NVGcolorToInt(color));
  } else if (strcmp(m->chars, "fill") == 0) {
    nvgFill(no->ctx);
  } else if (strcmp(m->chars, "translate") == 0 && argc == 2) {
    nvgTranslate(no->ctx, N(0), N(1));
  } else if (strcmp(m->chars, "rotate") == 0 && argc == 1) {
    nvgRotate(no->ctx, N(0));
  } else if (strcmp(m->chars, "resetTransform") == 0 && argc == 0) {
    nvgResetTransform(no->ctx);
  } else if (strcmp(m->chars, "strokeColor") == 0 && argc == 1) {
    nvgStrokeColor(no->ctx, NVGColorFromInt((uint64_t)N(0)));
  } else if (strcmp(m->chars, "strokeWidth") == 0 && argc == 1) {
    nvgStrokeWidth(no->ctx, AS_NUM(args[0]));
  } else if (strcmp(m->chars, "moveTo") == 0 && argc == 2) {
    nvgMoveTo(no->ctx, N(0), N(1));
  } else if (strcmp(m->chars, "lineTo") == 0 && argc == 2) {
    nvgLineTo(no->ctx, N(0), N(1));
  } else if (strcmp(m->chars, "arc") == 0 && argc == 6) {
    nvgArc(no->ctx, N(0), N(1), N(2), N(3), N(4), (int)N(5));
  } else if (strcmp(m->chars, "stroke") == 0 && argc == 0) {
    nvgStroke(no->ctx);
  } else if (strcmp(m->chars, "circle") == 0 && argc == 3) {
    nvgCircle(no->ctx, N(0), N(1), N(2));
  } else if (strcmp(m->chars, "createFont") == 0 && argc == 2) {
    int i = nvgCreateFont(no->ctx, S(0), S(1));
    return NUM_VAL(i);
  }  else if (strcmp(m->chars, "fontFaceId") == 0 && argc == 1) {
    nvgFontFaceId(no->ctx, (int)N(0));
  } else if (strcmp(m->chars, "textAlign") == 0 && argc == 1) {
    nvgTextAlign(no->ctx, (int)N(0));
  } else if (strcmp(m->chars, "currentTransform") == 0 && argc == 1) {
    if (IS_ARRAY(args[0])) {
      float xform[6];
      nvgCurrentTransform(no->ctx, xform);
      kuaobj *ao = AS_ARRAY(args[0]);
      for (int i = 0; i < 6; i++) {
        ku_arrset(vm, ao, i, NUM_VAL(xform[i]));
      }
    } else {
      ku_err(vm, "array expected");
    }
  } else if (strcmp(m->chars, "transform") == 0 && argc == 6) {
    nvgTransform(no->ctx, N(0), N(1), N(2), N(3), N(4), N(5));
  } else if (strcmp(m->chars, "fontSize") == 0 && argc == 1) {
    nvgFontSize(no->ctx, AS_NUM(args[0]));
  } else if (strcmp(m->chars, "text") == 0 && argc == 3) {
    nvgText(no->ctx, N(0), N(1), S(2), NULL);
  } else if (strcmp(m->chars, "text") == 0 && argc == 5) {
    nvgText(no->ctx, N(0), N(1), S(2) + (int)N(3), S(2)+(int)N(4));
  } else if (strcmp(m->chars, "closePath") == 0 && argc == 0) {
    nvgClosePath(no->ctx);
  } else if (strcmp(m->chars, "ellipse") == 0 && argc == 4) {
    nvgEllipse(no->ctx, N(0), N(1), N(2), N(3));
  }  else if (strcmp(m->chars, "scale") == 0 && argc == 2) {
    nvgScale(no->ctx, N(0), N(1));
  } else if (strcmp(m->chars, "linearGradient") == 0 && argc == 7 && IS_ARRAY(args[6])) {
    double sx = N(0), sy = N(1), ex = N(2), ey = N(3);
    NVGcolor icolor = NVGColorFromInt((uint64_t)N(4));
    NVGcolor ocolor = NVGColorFromInt((uint64_t)N(5));
    kuaobj *arr = AS_ARRAY(args[6]);
    NVGpaint p = nvgLinearGradient(no->ctx, sx, sy, ex, ey, icolor, ocolor);
    NVGpaintToArray(vm, &p, arr);
  } else if (strcmp(m->chars, "fillPaint") == 0 && argc == 1 && IS_ARRAY(args[0])) {
    NVGpaint paint;
    NVGpaintFromArray(vm, &paint, AS_ARRAY(args[0]));
    nvgFillPaint(no->ctx, paint);
  } else if (strcmp(m->chars, "radialGradient") == 0 && argc == 7 && IS_ARRAY(args[6])) {
    double cx = N(0), cy = N(1), inr = N(2), outr = N(3);
    NVGcolor icol = NVGColorFromInt((uint64_t)N(4));
    NVGcolor ocol = NVGColorFromInt((uint64_t)N(5));
    kuaobj *arr = AS_ARRAY(args[6]);
    NVGpaint p = nvgRadialGradient(no->ctx, cx, cy, inr, outr, icol, ocol);
    NVGpaintToArray(vm, &p, arr);
  } else if (strcmp(m->chars, "save") == 0) {
    nvgSave(no->ctx);
  } else if (strcmp(m->chars, "fontFace") == 0 && argc == 1 && IS_STR(args[0])) {
    nvgFontFace(no->ctx, S(0));
  } else if (strcmp(m->chars, "textMetrics") == 0 && argc == 0) {
    float asc, desc, lineh;
    nvgTextMetrics(no->ctx, &asc, &desc, &lineh);
    kuval tab = ku_cinstance(vm, "table");
    ku_push(vm, tab); // for GC
    kuobj *o = AS_OBJ(tab);
    table_iput(vm, o, ku_strfrom(vm, "ascender", 8), NUM_VAL(asc));
    table_iput(vm, o, ku_strfrom(vm, "descender", 9), NUM_VAL(desc));
    table_iput(vm, o, ku_strfrom(vm, "lineh", 5), NUM_VAL(lineh));
    ku_pop(vm);
    return tab;
  } else if (strcmp(m->chars, "textBreakLines") == 0 && argc == 5) {
    const char *text = S(0);
    int start = (int)N(1);
    int end = (int)N(2);
    float breakRowWidth = (float)N(3);
    kuaobj *rows = AS_ARRAY(args[4]);
    int maxRows = rows->elements.count;
    NVGtextRow *trows = (NVGtextRow*)malloc(maxRows * sizeof(NVGtextRow));
    int nrows = nvgTextBreakLines(no->ctx, text + start, text + end, breakRowWidth, trows, maxRows);
    for (int r = 0; r < nrows; r++) {
      NVGtextRow *tr = &trows[r];
      kuval tab = ku_cinstance(vm, "table");
      ku_push(vm, tab);   // for GC
      kuobj *o = AS_OBJ(tab);
      table_iput(vm, o, ku_strfrom(vm, "start", 5), NUM_VAL(tr->start - text));
      table_iput(vm, o, ku_strfrom(vm, "end", 3), NUM_VAL(tr->end - text));
      table_iput(vm, o, ku_strfrom(vm, "next", 4), NUM_VAL(tr->next - text));
      table_iput(vm, o, ku_strfrom(vm, "width", 5), NUM_VAL(tr->width));
      table_iput(vm, o, ku_strfrom(vm, "minx", 4), NUM_VAL(tr->minx));
      table_iput(vm, o, ku_strfrom(vm, "maxx", 4), NUM_VAL(tr->maxx));
      ku_arrset(vm, rows, r, tab);
      ku_pop(vm);
    }
    free(trows);
    return NUM_VAL(nrows);
  } else if (strcmp(m->chars, "textBounds") == 0 && argc == 6) {
    return NVGTextOrBoxBounds(vm, no->ctx, argc, args, false);
  } else if (strcmp(m->chars, "textLineHeight") == 0 && argc == 1) {
    nvgTextLineHeight(no->ctx, N(0));
  } else if (strcmp(m->chars, "textBoxBounds") == 0 && argc == 7) {
    return NVGTextOrBoxBounds(vm, no->ctx, argc, args, true);
  } else if (strcmp(m->chars, "globalAlpha") == 0 && argc == 1) {
    nvgGlobalAlpha(no->ctx, N(0));
  } else if (strcmp(m->chars, "roundedRect") == 0 && argc == 5) {
    nvgRoundedRect(no->ctx, N(0), N(1), N(2), N(3), N(4));
  } else if (strcmp(m->chars, "textBox") == 0) {
    int iend = (int)N(5);
    nvgTextBox(no->ctx, N(0), N(1), N(2), S(3) + (int)N(4), (iend >= 0) ? S(3)+(int)N(5) : NULL);
  } else if (strcmp(m->chars, "restore") == 0 && argc == 0) {
    nvgRestore(no->ctx);
  } else if (strcmp(m->chars, "bezierTo") == 0 && argc == 6) {
    nvgBezierTo(no->ctx, N(0), N(1), N(2), N(3), N(4), N(5));
  } else if (strcmp(m->chars, "pathWinding") == 0 && argc == 1) {
    nvgPathWinding(no->ctx, (int)N(0));
  } else if (strcmp(m->chars, "boxGradient") == 0 && argc == 9) {
    NVGcolor icolor = NVGColorFromInt((uint64_t)N(6));
    NVGcolor ocolor = NVGColorFromInt((uint64_t)N(7));
    kuaobj *arr = AS_ARRAY(args[8]);
    NVGpaint p = nvgBoxGradient(no->ctx, N(0), N(1), N(2), N(3), N(4), N(5), icolor, ocolor);
    NVGpaintToArray(vm, &p, arr);
  } else if (strcmp(m->chars, "lineCap") == 0 && argc == 1) {
    nvgLineCap(no->ctx, (int)N(0));
  } else if (strcmp(m->chars, "lineJoin") == 0 && argc == 1) {
    nvgLineJoin(no->ctx, (int)N(0));
  } else if (strcmp(m->chars, "degToRad") == 0 && argc == 1) {
    return NUM_VAL(nvgDegToRad(N(0)));
  } else if (strcmp(m->chars, "scissor") == 0 && argc == 4) {
    nvgScissor(no->ctx, N(0), N(1), N(2), N(3));
  } else if (strcmp(m->chars, "resetScissor") == 0 && argc == 0) {
    nvgResetScissor(no->ctx);
  } else if (strcmp(m->chars, "intersectScissor") == 0 && argc == 4) {
    nvgIntersectScissor(no->ctx, N(0), N(1), N(2), N(3));
  } else if (strcmp(m->chars, "fontBlur") == 0 && argc == 1) {
    nvgFontBlur(no->ctx, N(0));
  } else if (strcmp(m->chars, "createImage") == 0 && argc == 2) {
    return NUM_VAL(nvgCreateImage(no->ctx, S(0), (int)N(1)));
  } else if (strcmp(m->chars, "imageSize") == 0 && argc == 2 && IS_ARRAY(args[1])) {
    int w, h;
    nvgImageSize(no->ctx, (int)N(0), &w, &h);
    kuaobj *ao = AS_ARRAY(args[1]);
    ku_arrset(vm, ao, 0, NUM_VAL(w));
    ku_arrset(vm, ao, 1, NUM_VAL(h));
  } else if (strcmp(m->chars, "imagePattern") == 0 && argc == 8 && IS_ARRAY(args[7])) {
    NVGpaint paint = nvgImagePattern(no->ctx, N(0), N(1), N(2), N(3), N(4), (int)N(5), N(6));
    kuaobj *ao = AS_ARRAY(args[7]);
    NVGpaintToArray(vm, &paint, ao);
  } else if (strcmp(m->chars, "textGlyphPositions") == 0 && argc == 6) {
    double x = N(0);
    double y = N(1);
    const char *text = S(2);
    int start = (int)N(3);
    int end = (int)N(4);
    kuaobj *ao = AS_ARRAY(args[5]);

    int maxpos = ao->elements.count;
    NVGglyphPosition *positions = (NVGglyphPosition*)malloc(sizeof(NVGglyphPosition)*maxpos);
    int nglypes = nvgTextGlyphPositions(no->ctx, x, y, text + start, text + end, positions, maxpos);
    for (int i = 0; i < nglypes; i++) {
      NVGglyphPosition p = positions[i];
      kuval tab = ku_cinstance(vm, "table");
      ku_push(vm, tab); // for GC
      kuobj *o = AS_OBJ(tab);
      table_iput(vm, o, ku_strfrom(vm, "start", 5), p.str - text);
      table_iput(vm, o, ku_strfrom(vm, "minx", 4), NUM_VAL(p.minx));
      table_iput(vm, o, ku_strfrom(vm, "maxx", 4), NUM_VAL(p.maxx));
      table_iput(vm, o, ku_strfrom(vm, "x", 1), NUM_VAL(p.x));
      ku_arrset(vm, ao, i, OBJ_VAL(tab));
      ku_pop(vm);
    }
    return NUM_VAL(nglypes);
  }
  else {
    ku_err(vm, "unexpected method %s\n", m->chars);
  }
  return NULL_VAL;
}

kuval nanovg_sget(kuvm *vm, kustr *p) {
  if (strcmp(p->chars, "CCW") == 0) {
    return NUM_VAL(NVG_CCW);
  } else if (strcmp(p->chars, "CW") == 0) {
    return NUM_VAL(NVG_CW);
  } else if (strcmp(p->chars, "ALIGN_CENTER") == 0) {
    return NUM_VAL(NVG_ALIGN_CENTER);
  } else if (strcmp(p->chars, "ALIGN_LEFT") == 0) {
    return NUM_VAL(NVG_ALIGN_LEFT);
  } else if (strcmp(p->chars, "ALIGN_RIGHT") == 0) {
    return NUM_VAL(NVG_ALIGN_RIGHT);
  } else if (strcmp(p->chars, "ALIGN_MIDDLE") == 0) {
    return NUM_VAL(NVG_ALIGN_MIDDLE);
  } else if (strcmp(p->chars, "ALIGN_TOP") == 0) {
    return NUM_VAL(NVG_ALIGN_TOP);
  } else if (strcmp(p->chars, "ALIGN_BOTTOM") == 0) {
    return NUM_VAL(NVG_ALIGN_BOTTOM);
  } else if (strcmp(p->chars, "HOLE") == 0) {
    return NUM_VAL(NVG_HOLE);
  } else if (strcmp(p->chars, "MITER") == 0) {
    return NUM_VAL(NVG_MITER);
  } else if (strcmp(p->chars, "ROUND") == 0) {
    return NUM_VAL(NVG_ROUND);
  }  else if (strcmp(p->chars, "SQUARE") == 0) {
    return NUM_VAL(NVG_SQUARE);
  } else if (strcmp(p->chars, "BEVEL") == 0) {
    return NUM_VAL(NVG_BEVEL);
  } else if (strcmp(p->chars, "BUTT") == 0) {
    return NUM_VAL(NVG_BUTT);
  }
  return NULL_VAL;
}

kuval nanovg_ifree(kuvm *vm, kuobj *o) {
  kunvobj *no = (kunvobj*)o;
  nvgDeleteInternal(no->ctx);
  no->ctx = NULL;
  ku_alloc(vm, no, sizeof(kunvobj), 0);
  return NULL_VAL;
}

kuval nanovg_imark(kuvm *vm, kuobj *o) {
  kunvobj *no = (kunvobj*)o;
  ku_markobj(vm, AS_OBJ(no->target));
  return NULL_VAL;
}

void kuvg_reg(kuvm *vm) {
  kucclass *cc = ku_cclassnew(vm, "nanovg");
  cc->cons = nanovg_cons;
  cc->sget = nanovg_sget;
  cc->icall = nanovg_icall;
  cc->ifree = nanovg_ifree;
  cc->imark = nanovg_imark;
  ku_cclassdef(vm, cc);
}


void kuvg_keypress(kuvm *vm, kunvobj *obj, int event, int code) {
  if (obj == NULL) {
    return;
  }
  bool native;
  kuval *oldsp = vm->sp;
  ku_push(vm, obj->target);
  ku_push(vm, NUM_VAL(event));
  ku_push(vm, NUM_VAL(code));
  int oldbase = vm->baseframe;
  vm->baseframe = vm->framecount;
  if (ku_invoke(vm, obj->keypress, 2, &native)) {
    ku_run(vm);
  }
  vm->baseframe = oldbase;
  vm->sp = oldsp;
}

void kuvg_keydown(kuvm *vm, kunvobj *obj, int code) {
  kuvg_keypress(vm, obj, 0, code);
}

void kuvg_keyup(kuvm *vm, kunvobj *obj, int code) {
  kuvg_keypress(vm, obj, 1, code);
}

void kuvg_render(kuvm *vm, kunvobj *obj, double width, double height, double scale) {
  if (obj == NULL) {
    return;
  }
  bool native;
  kuval *oldsp = vm->sp;
  ku_push(vm, obj->target);
  ku_push(vm, OBJ_VAL(obj));
  ku_push(vm, NUM_VAL(width));
  ku_push(vm, NUM_VAL(height));
  ku_push(vm, NUM_VAL(scale));
  int oldbase = vm->baseframe;
  vm->baseframe = vm->framecount;
  if (ku_invoke(vm, obj->render, 4, &native)) {
    ku_run(vm);
  }
  vm->baseframe = oldbase;
  vm->sp = oldsp;
}

void kuvg_touch(kuvm *vm, kunvobj *o, int event, double x0, double y0, double x1, double y1) {
  if (o == NULL) {
    return;
  }
  bool native;
  kuval *oldsp = vm->sp;
  ku_push(vm, o->target);
  ku_push(vm, OBJ_VAL(o));
  ku_push(vm, NUM_VAL(event));
  ku_push(vm, NUM_VAL(x0));
  ku_push(vm, NUM_VAL(y0));
  ku_push(vm, NUM_VAL(x1));
  ku_push(vm, NUM_VAL(y1));
  int oldbase = vm->baseframe;
  vm->baseframe = vm->framecount;
  if (ku_invoke(vm, o->touch, 6, &native)) {
    ku_run(vm);
  }
  vm->baseframe = oldbase;
  vm->sp = oldsp;
}

void kuvg_touchesbegan(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1) {
  kuvg_touch(vm, obj, 0, x0, y0, x1, y1);
}

void kuvg_touchesmoved(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1) {
  kuvg_touch(vm, obj, 1, x0, y0, x1, y1);
}

void kuvg_touchesended(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1) {
  kuvg_touch(vm, obj, 2, x0, y0, x1, y1);
}

void kuvg_touchescanceled(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1) {
  kuvg_touch(vm, obj, 3, x0, y0, x1, y1);
}
