
#ifndef KUNVG_H
#define KUNVG_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdio.h>
#include "kumu.h"
#include "nanovg.h"

typedef struct {
  kunobj base;
  kuval target;
  NVGcontext *ctx;
  kustr *render;
  kustr *touch;
  kustr *keypress;
} kunvobj;

void kuvg_reg(kuvm *vm);
void kuvg_render(kuvm *vm, kunvobj *obj, double width, double height, double scale);
void kuvg_keydown(kuvm *vm, kunvobj *obj, int code);
void kuvg_keyup(kuvm *vm, kunvobj *obj, int code);
void kuvg_touchesbegan(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1);
void kuvg_touchesmoved(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1);
void kuvg_touchesended(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1);
void kuvg_touchescanceled(kuvm *vm, kunvobj *obj, double x0, double y0, double x1, double y1);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // KUNVG_H
