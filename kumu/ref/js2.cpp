//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// ECMAScript implementation file
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#if 0
#include "stdafx.h"
#endif

#include "js2.h"
#include <cassert>
#include <algorithm>
#define _USE_MATH_DEFINES
#include <math.h>
#include <limits>
#include <sys/time.h>
#if 0
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
 

#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif
#endif

#include <stdlib.h>
#include <wchar.h>

#include "jsregex.h"
#include "dbgutil.h"

namespace js {

node_ptr nullNode = NULL;
str_ptr nullStr;
obj_ptr nullObj;
token_ptr nullToken = NULL;
const unsigned int iterationLimit = 1000;
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// TODO: This should be on the root context
// TODO: Collect all unique root context state into a separate struct and
//       alloc / free on demand
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
obj_ptr Object_prototype;
unsigned int object::__next_id__ = 0;

int wstlprintf(wstring& dest, const jschar* format, ...);
number numberFromHex(wstring& word);
number numberFromWord(wstring& word);
// node_ptr new_node(token_ptr t);
void update_length(oarray* pa);
bool check_call_direct(context& ec, node_ptr n, obj_ptr othis, var& ret);
bool check_breakpoints(debugger* dbg, breakpoint current);
void update_length(string_object* so);
void list(debugger* dbg, int line);
bool is_child_context(context* cc, context* cp); 
void debug(context* ctx, node_ptr n, debugger* dbg);
void debug_command(context* ctx, breakpoint curr, debugger* dbg);
bool debug_pif_check(debugger* dbg, int file, int line);
bool debug_pib_check(debugger* dbg, int file, int line);
void debug_checkbow(debugger* dbg, prop* pr);

breakpoint get_node_current(debugger* dbg, node_ptr n);
number _wtof(const jschar* s);

bool basic_debugger(context* ec);
bool basic_debugger(context* ec) {
    if(ec->root->attached_debugger && ec->root->attached_debugger->is_basic())
        return true;
        
    return false;
}

bool advanced_debugger(context* ec);
bool advanced_debugger(context* ec) {
    if(ec->root->attached_debugger && !ec->root->attached_debugger->is_basic())
        return true;
        
    return false;
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Internal helper functions
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
int _wtoi(const jschar* s) {
    int d;
    swscanf(s, L"%d", &d);
    return d;
}

number _wtof(const jschar* s) {
    char c[128];
    int i;
    for(i=0; s[i] != '\0'; i++)
        c[i] = s[i];
    
    c[i] = '\0';
    
    return atof(c);
}

    
int wstlprintf(wstring& dest, const jschar* format, ...)
{
    va_list args;
    va_start(args, format);
    
#if 0
    unsigned int len = _vscwprintf(format, args) + 1;
    vector<jschar> buffer(len);
    jschar* buf = &(buffer[0]);
    int result = _vsnwprintf(buf, len, _TRUNCATE, format, args);
#endif
    
    jschar buf[255];
    unsigned int result = vswprintf(buf, 255, format, args);    
    va_end(args);
    dest = buf;
    return result;
}

number numberFromHex(wstring& word)
{
    unsigned int n;

    char_ptr p = word.c_str();
    jschar c;

    n = 0;

    while(*p != 0)
    {
        c = *p;

        if(c >= 'a' && c <= 'f')
        {
            n = 16*n + (c - 'a' + 10);
        }

        if(c >= 'A' && c <= 'F')
        {
            n = 16*n + (c - 'A' + 10);
        }

        if(c >= '0' && c <= '9')
        {
            n = 16*n + (c - '0');
        }
        p++;

    }

    return (number) n;
}


number numberFromWord(wstring& word)
{
    number mult = 10.0;
    number res = 0.0;
    char_ptr p = word.c_str();

    while(*p != 0)
    {
        jschar c = *p;

        // handle nnn.xxxe+zzz
        if(c == 'e' || c == 'E') {
            number ten = 0;
            number ns = 1;
            
            p++;
            
            mult = 1;
            if(*p == '+')
                p++;
            else if(*p == '-') {
                ns = -1;
                p++;
            }
            
            while(*p != 0) {
                c = *p;
                int xxx = c;
                ten = ten*mult + (number) (xxx - '0');
                mult *= 10;
                p++;
            }
            
            ten *= ns;
            number n =  res * pow(10, ten);
            return n;
        }

        if(c == '.')
        {
            number frac = 0;
            mult = 1;
            p++;
            while(*p != 0 && *p != 'e' && *p != 'E')
            {
                c = *p;
                frac = frac * 10 + (number) (c - '0');
                mult *= 10;
                p++;
            }

            number n = res + frac / mult;
            if(*p == 0)
                return n;
            
            res = n;        // must be have exponent
            continue;
        }

        if(c >= 0 && c <= '9')
        {
            res = res * mult + (number) (c - '0');
        }
        p++;
    }
    return res;
}

//node_ptr new_node(token_ptr t)
//{
//    node* n = new node(t);
//    return node_ptr(n);
//}


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// exception class for handling errors
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
exception::exception(std::wstring& e, int l, int c) {
    this->error = e;
    this->line = l;
    this->column = c;
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Variant (var) class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
var::var(number n) : ctl(ctlnone)
{
    vt = tnum;
    nval = n;
}

bool var::isPrimitive() {
    return (vt != tobj);
}

var::var(const var& other): ctl(ctlnone)
{
    vt = tundef;
    copy(other);
}

var::var(int n): ctl(ctlnone)
{
    vt = tnum;
    nval = (number) n;
}

var::var(unsigned int n): ctl(ctlnone)
{
    vt = tnum;
    nval = (number) n;
}

var::var(char_ptr s): ctl(ctlnone)
{
    vt = tstr;
    sval = new wstring(s);
}

var::var(wstring& s): ctl(ctlnone)
{
    vt = tstr;
    sval = new wstring(s);
}

var::var(): ctl(ctlnone)
{
    vt = tundef;
}

var::var(bool b): ctl(ctlnone)
{
    vt = tbool;
    bval = b;
}

var::var(obj_ptr o): ctl(ctlnone)
{
    vt = tobj;
    oval = o;
}

wstring var::type_name()
{
    switch(vt)
    {
    case tobj:
        return wstring(L"object");
    case tnum:
        return wstring(L"number");
    case tstr:
        return wstring(L"string");
    case tnull:
        return wstring(L"null");
    }
    return wstring(L"undefined");
}

void var::clear()
{
    if(vt == tstr)
    {
        if(sval)
            delete sval;
        sval = nullStr;
    }

    ctl = ctlnone;
    oval = nullObj;
    vt = tundef;
}

void var::copy(const var& other)
{
    clear();
    vt = other.vt;
    ctl = other.ctl;
    switch(vt)
    {
        case tnum:
        {
            nval = other.nval;
        }
        break;
        case tbool:
        {
            bval = other.bval;
        }
        break;

        case tstr:
        {
            sval = new wstring(*(other.sval));
        }
        break;

        case tobj:
        {
            oval = other.oval;
        }
        break;
    }
}


void var::operator=(const var& other)
{
    return copy(other);
}


#ifdef ES_11_9_6
bool strictEqualityCompare(context& ec, var* x, var* y);

bool strictEqualityCompare(context& ec, var* x, var* y) {
    if(x->vt != y->vt)
        return false;
    if(x->vt == var::tundef)
        return true;
    if(x->vt == var::tnull)
        return true;
    if(x->vt == var::tnum) {
        if(isnan(x->nval)) return false;
        if(isnan(y->nval)) return false;
        if(x->nval == y->nval) return true;
        return false;
    }
    if(x->vt == var::tstr) {
        if(*x->sval == *y->sval) return true;
        return false;
    }
    if(x->vt == var::tbool) {
        return (x->bval && y->bval);
    }
    return (x->oval == y->oval);
}

#endif

#ifdef ES_11_9_3
var toPrimitive(context&ec, var* x);
bool abstractEqualityCompare(context& ec, var* x, var* y);
bool abstractEqualityCompare(number x, number y);

var toPrimitive(context& ec, var* x) {
    if(x->vt != var::tobj)
        return x;
    
    return x->oval->defaultValue(ec, object::DVHINT_NONE);
}


bool abstractEqualityCompare(number x, number y) {
    if(isnan(x)) return false;
    if(isnan(y)) return false;
    if(x== y) return true;
    return false;
}

// 2013-01-26 shark@anui.com - match 11.9.3 in ECMA spec
bool abstractEqualityCompare(context& ec, var* x, var* y) {
    if(x->vt == y->vt) {
        if(x->vt == var::tundef)
            return true;
        
        if(x->vt == var::tnull)
            return true;
        
        if(x->vt == var::tnum) {
            return abstractEqualityCompare(x->nval, y->nval);
        }
        
        if(x->vt == var::tstr) {
            return (*x->sval == *y->sval);
        }
        
        if(x->vt == var::tbool) {
            if(x->bval == true && y->bval == true)
                return true;
            else
                return false;
        }
        if(x->oval == y->oval)
            return true;
        return false;    
    }
    
    if(x->vt == var::tnull && y->vt == var::tundef) return true;
    if(x->vt == var::tundef && y->vt == var::tnull) return true;
    if(x->vt == var::tnum && y->vt == var::tstr) {
        number n = y->num();
        return (x->nval == n);
    }
    if(x->vt == var::tstr && y->vt == var::tnum) {
        number n = x->num();
        return abstractEqualityCompare(n, y->nval);
    }
    if(x->vt == var::tbool) {
        number n = x->num();
        var x1 = var(n);
        return abstractEqualityCompare(ec, &x1,y);
    }
    if(y->vt == var::tbool) {
        number n = y->num();
        var y1 = var(n);
        return abstractEqualityCompare(ec, x, &y1);
    }
    if((x->vt == var::tstr || x->vt == var::tnum) && y->vt == var::tobj) {
        var py = toPrimitive(ec, y);
        return abstractEqualityCompare(ec, x, &py);
    }
    
    if(x->vt == var::tobj && (y->vt == var::tstr || y->vt == var::tnum)) {
        var px = toPrimitive(ec, x);
        return abstractEqualityCompare(ec, &px, y);
    }
    return false;
}

#endif


bool var::operator==(const var& other)
{
    // 2011-11-21 shark@anui.com - undefined == undefined
    if(vt == tundef && other.vt == tundef)
        return true;
        
    // spcial handling for null check on undefined
    if(vt == tundef && other.vt == tnull)
       return true;

    if(vt == tnull && other.vt == tundef)
       return true;

    if(vt != other.vt)
        return false;

    switch(vt)
    {
        case    tnull:
            return true;

        case    tnum:
        {
            return (nval == other.nval);
        }
        break;

        case    tstr:
        {
            return (*sval == *other.sval);
        }
        break;

        case    tbool:
        {
            return (bval == other.bval);
        }
        break;

        case    tobj:
        {
            object* t = oval.get();
            object* o = other.oval.get();
            
            if(t->is_equal(o))
                return true;
            if(o->is_equal(t))
                return true;
            return false;
        }
        break;
    }

    return false;
}

var var::operator+(var& other)
{
    // 2011-11-26 shark@anui.com - handle issue #4 writeln(x + "," + y)
    if(other.vt == tstr) {
        wstring s = this->wstr();
        wstring ret = s + other.wstr();
        return var(ret);
    }
    
    if(vt == tnum)
    {
        number n = other.num();
        return var(nval + n);
    }

    if(vt == tstr)
    {
        wstring s = *sval;
        wstring o = other.wstr();
        wstring ret = s +o;
        return var(ret);
    }

    return var();
}

var var::operator-(var& other)
{
    number n = num() - other.num();
    return var(n);
}

var var::operator*(var& other)
{
    number n = num() * other.num();
    return var(n);
}

var var::operator/(var& other)
{
    number n = num() / other.num();
    return var(n);
}

void var::operator+=(var& other)
{
   if(vt == tnum && other.vt == tnum)
        nval += other.nval;

   if(vt == tstr)
   {
        wstring s = *sval;
        delete sval;
        wstring o = other.wstr();
        sval = new wstring(s + o);
   }
}

wstring numberToString(number m, int radix);
wstring numberToString(number m, int radix) {
    number r = (number) radix;
    jschar chars[] = L"0123456789abcdefghijklmnopqrstuvwxyz";
    
    // shark@anui.com 2011-11-12 - fixed so that negative toString works correctly
    
    wstring prefix = L"";
    
     if(m < 0) {
        prefix = L"-";
        m = fabs(m);
     }

    if(isnan(m))
        return wstring(L"Nan");

    if(!isfinite(m))
        return prefix + wstring(L"Infinity");
        
    
    if(m == 0)
        return wstring(L"0");
    
    
    jschar numstr[1076];
    jschar intpartrev[311];
    int i, cc = 0;
    
    double fpint, fpfrac;
    
    fpfrac = modf(m, &fpint);       // split int and fraction part
    
    if(fpint == 0) {
        numstr[0] = '0';
        numstr[1] = 0;
        cc = 1;
    }
    else {
        while(fpint > 0) {
            int i = (int) fmod(fpint, r);
            intpartrev[cc++] = chars[i]; 
            fpint = floor(fpint / r);
        }
        
        for(i=0; i < cc; i++) 
            numstr[i] = intpartrev[cc-i-1];
    }

    if(radix != 10) {
        numstr[cc] = 0;
        return wstring(numstr);
    }
    
    int fracZeroCount = 0;
    bool startZeroCount = true;

    // Has no fraction
    if(fpfrac == 0) {
        if(cc >= 22) {
            jschar sexp[1076];
            sexp[0] = numstr[0];
            sexp[1] = '.';
            int i;
            for(i=1; i <= 16; i++) {
                sexp[1+i] = numstr[i];
            }
            sexp[i++] = 'e';
            sexp[i++] = '+';
            sexp[i] = 0;
            wstring spow = numberToString(cc-1, 10);
            return prefix + wstring(sexp) + spow;
        }
    }
    
    // Has fraction
    bool countTrailing = false;     // For rounding
    int trailingZeroCount = 0;      // For rounding
    int trailingNineCount = 0;      // For rounding
    int trailingBaseIndex = 0;      // For rounding
    int trailingPrecision = 7;      // For rounding
    
    if(fpfrac > 0) {
        numstr[cc++] = '.';
        while(fpfrac > 0) {
            fpfrac *= r;
            fpfrac = modf(fpfrac, &fpint);
            int i = (int) fpint;
            
            if(countTrailing) {
                if(i == 0)
                    trailingZeroCount++;
                if(i == 9)
                    trailingNineCount++;
                    
                if(trailingZeroCount > trailingPrecision) {
                    cc = trailingBaseIndex+1;
                    break;
                }
                
                if(trailingNineCount > trailingPrecision) {
                    numstr[trailingBaseIndex] = numstr[trailingBaseIndex] + 1;
                    cc = trailingBaseIndex + 1;
                    break;
                }
            }
            
            if(i != 0 && i != 9) {
                countTrailing = true;
                trailingBaseIndex = cc;
            }
                
            if(i != 0)
                startZeroCount = false;
            
            if(startZeroCount)
                fracZeroCount++;
            numstr[cc++] = chars[i];
        }
    }

    numstr[cc] = 0;
    
    if(m < 1) {
        if(fracZeroCount > 5) {
            jschar s2[1071];
            s2[0] = numstr[2 + fracZeroCount];
            s2[1] = '.';
            int i;
            for(i = 2; (i + fracZeroCount) < cc; i++) {
                s2[i] = numstr[i + fracZeroCount+1];
            }
            s2[i-1] = 'e';
            s2[i] = '-';
            s2[i+2] = 0;
            wstring spow = numberToString(fracZeroCount+1,10);
            return prefix + wstring(s2) + spow;
        }
    }

    if(cc > 21)
        numstr[21] = 0;
    return prefix + wstring(numstr);
}

wstring var::wstr()
{
    switch(vt)
    {
        case tnum:
        {
            return numberToString(nval, 10);
        }
        break;

        case tstr:
        {
            return *sval;
        }
        break;

        case tbool:
        {
            if(bval)
                return wstring(L"true");
            return wstring(L"false");
        }
        break;

        case tobj:
        {
            wstring res;
            if(this->oval->is_array()) {
                oarray* oa = (oarray*) this->oval.get();
                int len = oa->length();
                wstring temp;
                wstlprintf(temp, L"array[%d]", len);
                res += temp;
                int limit = 16;
                for(int i=0;  i < len && i < limit; i++) {
                    if(i > 16)
                        break;
                    wstlprintf(temp, L"%ls,", oa->getat(i).wstr().c_str());
                    res += temp;
                }
                return res;
            }
            else { 
                wstring res = L"{";
                int i = 0;

                for(map<wstring,prop>::iterator p = oval->properties.begin(); p != oval->properties.end(); p++) {
                    wstring name = p->first;
                    wstring val = p->second.value.wstr();
                    if(p->second.value.vt == var::tstr) 
                        val = L"\"" + val + L"\"";
                    wstring temp;
                    wstlprintf(temp, L"%ls \"%ls\":%ls", (i==0) ? L"" : L",", name.c_str(), val.c_str());
                    i++;
                    res += temp;
                }
                res += L" }";
                return res;
            }
            // todo: call toString()
        }
        break;

        case tnull:
        {
            return wstring(L"null");
        }
        break;

        case tundef:
        {
        }
        break;
    }
    return wstring(L"undefined");

}

void var::operator-=(var& other)
{
   if(vt == tnum)
        nval -= other.num();
}

void var::operator*=(var& other)
{
   if(vt == tnum)
        nval *= other.num();
}

void var::operator/=(var& other)
{
    if(vt == tnum)
        nval /= other.num();
}

char_ptr var::wchar()
{
    if(vt == tstr)
        return sval->c_str();
    else
        return NULL;
}

int var::c_int()
{
    if(vt == tbool) {
        if(bval) 
            return 1;
        else
            return 0;
    }
    return (int) num();
}

unsigned int var::c_unsigned() {
    unsigned int u = (unsigned int) num();
    return u;
}

float var::c_float() {
    return (float) num();
}

number var::num()
{
    switch(vt)
    {
        case tnum:
        {
            return nval;
        }
        break;

        case tstr:
        {
            return _wtof( wcharFromVar(*this)); //  this->c_str());
        }
        break;
    }

    // todo: other types
    return 0;
}

bool var::c_bool()
{
    if(vt == tbool)
        return bval;

    if(vt == tnum) {
        return (nval != 0);
    }

    if(vt == tobj) {
        return (oval != nullObj);
    }
    
    if(vt == tstr && sval->length() > 0)
        return true;
        
    // todo: none boolean conversion rules
    return false;
}

obj_ptr var::obj()
{
    if(vt == tobj)
        return oval;
    else
        return nullObj;
}

var::~var()
{
    clear();
}

bool var::operator<(var& other)
{
    if(vt == tnum)
        return nval < other.num();
    if(vt == tstr)
        return *sval < other.wstr();
    return false;
}

bool var::operator>(var& other)
{
    if(vt == tnum)
        return nval > other.num();
    if(vt == tstr)
        return *sval > other.wstr();
    return false;
}

bool var::operator<=(var& other)
{
    if(vt == tnum)
        return nval <= other.num();
    if(vt == tstr)
        return *sval <= other.wstr();
    return false;
}

bool var::operator>=(var& other)
{
    if(vt == tnum)
        return nval >= other.num();
    if(vt == tstr)
        return *sval >= other.wstr();
    return false;
}
bool var::operator!=(var& other)
{
    return !(*this == other);
}

unsigned int var::operator&(var& other)
{
    unsigned int i1 = c_unsigned();
    unsigned int i2 = other.c_unsigned();
    return (i1 & i2);
}

unsigned int var::operator|(var& other)
{
    number no = other.num();
    if(vt == tnum)
        return (unsigned int) nval | (unsigned int) no;
    return 0;
}

unsigned int var::operator^(var& other)
{
    number no = other.num();
    if(vt == tnum)
        return (unsigned int) nval ^ (unsigned int) no;
    return 0;
}

unsigned int var::operator~()
{
    number n = this->num();
    unsigned int u = (unsigned int) n;
    return ~u;
}

var var::error()
{
    var v;
    v.vt = tnull;
    v.ctl = ctlerror;
    return v;
}

void var::test()
{
    var n1(10.3);
    var n2(2.1);
    var n3 = n1 + n2;
    assert(n3.num() == 12.4);

    var s1(L"hello");
    var s2(L"world");
    var s3 = s1 + s2;
    assert(s3.wstr() == L"helloworld");

    n1 = var(9);
    var _n1 = var(8);
    n1 += _n1;
    assert(n1.num() == 17);

    var b1(true);
    var b2(false);
    var b3 = (b1 == b2);
    assert(b3.c_bool() == false);

    n1 = var(100);
    n2 = var(50);
    n3 = n1 / n2;
    assert(n3.num() == 2);

    s1 = var(L"abc");
    n1 = var(100);
    s1 += n1;
    assert(s1.wstr() == L"abc100");

    b1 = var(true);
    assert(b1.wstr() == L"true");
    b2 = var(false);
    assert(b2.wstr() == L"false");

    s1 = var(L"hello");
    s2 = s1;
    assert(s2.wstr() == L"hello");

    char_ptr cp1 = wcharFromVar(s1); // s1.c_str();
    assert(wcscmp(cp1, L"hello") == 0);

    n1 = var(3);
    cp1 = wcharFromVar(n1); // n1.c_str();
    assert(cp1 == NULL);

    var n = 99;
    assert(n.num() == 99);

    var s = L"omg";
    assert(s.wstr() == L"omg");

    s = var(L"123.456");
    assert(s.num() == 123.456);

    n1 = var(7);
    n2 = var(9);
    assert(n1 < n2);

    n1 = var(9);
    n2 = var(10);
    assert(n1 <= n2);

    n1 = var(4);
    n2 = var(7);
    assert(n1 != n2);

    n1 = var(L"abc");
    n2 = var(L"bcd");
    assert(n1 < n2);

    n1 = var(L"xyz");
    n2 = var(L"bcd");
    assert(n1 >= n2);

    n1 = var(0xffff);
    n2 = var(0x0f0f);
    assert( (n1 & n2) == 0x0f0f);

    n1 = var(0x0f0f);
    n2 = var(0xf0f0);
    assert( (n1 | n2) == 0xffff);
    n1 = var(0xffff0000);
    assert(~n1 == 0x0000ffff);


    // stress / leak test
    var _v1 = var(1.0);
    var _vx = var(L"x");
    for(int i=0; i < 1000; i++)
    {
        var v1(0.0);
        var v2(L"");
        v1 += _v1;
        v2 += _vx;
    }
}


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// property class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
prop::prop(const var& v, bool ro, bool de) 
{
    value = v;
    read_only = ro;
    dont_enum = de;
    configurable = true;
}

prop::prop() 
{
    value = var();
    read_only = false;
    dont_enum = false;
    configurable = true;
}

void prop::operator=(const prop& other)
{
    value = other.value;
    read_only = other.read_only;
    dont_enum = other.dont_enum;
}

void prop::test()
{
#if 0
    prop p(var(L"testing"), false, false);
#endif
}


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// object class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
object::object() : node(nullNode)
{
    ALLOC("OB",1);
    __id = __next_id__++;
    type = NULL;
    flags = 0;
    flags = SET_FLAG(flags, OBJ_EXTENSIBLE);
    flags = CLEAR_FLAG(flags, OBJ_PINNED);
//    is_extensible = true;
//    pinned = false;
    scope = NULL;
    prototype.value = var(Object_prototype);
}

#ifdef ES_8_12_8
// If hintString is false then assume hintNumber
var object::defaultValue(context& ec, dv hint) {
    if(hint == DVHINT_STR) {
        // get "toString" method and call it
        // if result is a primitivereturn it
        var mem = this->get(L"toString");
        if(mem.vt == var::tobj) {
            oarray args(0);
            var res = mem.oval->call(ec, args);
            if(res.isPrimitive()) {
                return res;
            }
        }
        // try call "valueOf"
        // if result is primitive return it
        mem = this->get(L"valueOf");
        if(mem.vt == var::tobj) {
            oarray args(0);
            var res = mem.oval->call(ec, args);
            if(res.isPrimitive()) {
                return res;
            }
        }
        
        // return TypeError exception
        return ec.setError(L"TypeError", NULL);
        
    }
 
    // for all objects DVHINT_NUM and DVHINT_NONE are the same
    // except for date object. It will override this method and
    // treat DVHINT_NONE as DVHINT_STR

    // try call valueOf
    // if result is primitive return it
    var mem = this->get(L"valueOf");
    if(mem.vt == var::tobj) {
        oarray args(0);
        var res = mem.oval->call(ec, args);
        if(res.isPrimitive()) {
            return res;
        }
    }
    
    // try call toString
    // if result is primitive return it
    mem = this->get(L"toString");
    if(mem.vt == var::tobj) {
        oarray args(0);
        var res = mem.oval->call(ec, args);
        if(res.isPrimitive()) {
            return res;
        }
    }
    
    // return TypeError
    return ec.setError(L"TypeError", NULL);
}
#endif

bool object::is_equal(js::object *other) {
    return (this == other);
}


object::~object() {
    FREE("OB",1);
}

bool object::isNative()
{
    return false;
}

bool object::is_array()
{
    return false;
}

bool object::can_call_direct()
{
    return false;
}

var object::call_direct(context& ec, char_ptr method, oarray& args)
{
    wstring es;
    wstlprintf(es, L"%ls not found - native object", method);
    return ec.setError(es.c_str(), NULL);
}


var object::call(context& ec, oarray& args)
{
    return ec.setError(L"not a function", NULL);
}

var object::getat(int i)
{
    return var();
}

void object::putat(int i, const var& v)
{
}

void object::put(char_ptr p, const var& val)
{
    wstring s = p;

    if(s == L"prototype")
    {
        prototype.value = val;
        return;
    }

    prop pv = prop(val, false, false);
    properties[s] = pv;
}


var object::get(char_ptr p)
{
   wstring s = p;

    map<wstring,prop>::iterator i = properties.find(s);
    if(i != properties.end())
    {
        return (*i).second.value;
    }

   if(s == L"prototype")
       return prototype.value;

    return var();
}

prop* object::getprop(char_ptr n, bool followPrototype)
{
    wstring s = n;


    map<wstring, prop>::iterator p = properties.find(s);
    if(p != properties.end())
        return &((*p).second);

    if(s == L"prototype")
        return &prototype;

    
    if(followPrototype) 
    {
        prop* pp = &prototype; // getprop(L"prototype", false);
        obj_ptr po;
        if(pp != NULL)
        {
            po = pp->value.obj();
        }
        if(po != NULL) {
            pp =  po->getprop(n, true);
            if(pp != NULL)
                return pp;
        }
    }

    if(scope != NULL) {
        prop* sp = scope->getprop(n, followPrototype);
        if(sp != NULL)
            return sp;
    }

    return NULL;
}

prop* object::addprop(char_ptr n)
{
    if(! IS_FLAG(flags, OBJ_EXTENSIBLE)) { //  is_extensible) {
        return NULL;
    }
    var v;
    put(n, v);
    wstring s = n;
    map<wstring, prop>::iterator p = properties.find(s);
    return &((*p).second);
}

void object::delprop(char_ptr n)
{
    wstring s = n;
    properties.erase(s);
}

void object::test()
{
#if 0
    obj_ptr o(new object());
    o->put(L"name", var(L"mohsen"));
    o->put(L"x", var(20.1));
    o->put(L"y", var(10.2));

    var x = o->get(L"x");
    assert(x.num() == 20.1);
    number y = o->get(L"y").num();
    assert(y == 10.2);
    o->put(L"y", var(999));
    y = o->get(L"y").num();
    assert(y == 999);

    var name = o->get(L"name");
    wstring s = name.wstr();
    assert(s == L"mohsen");

    var r = o->get(L"foo");
    assert(r.vt == var::tundef);

    prop* p = o->getprop(L"name",false);
    assert(p != nullptr);
    o->delprop(L"name");
    p = o->getprop(L"name", true);
    assert(p == nullptr);
    p = o->addprop(L"foo");
    assert(o->getprop(L"foo", false) != nullptr);

    obj_ptr pr(new object());
    pr->put(L"abc", var(456.78));
    o->put(L"prototype", var(pr));
    p = o->getprop(L"abc", false);
    assert(p == nullptr);
    p = o->getprop(L"abc", true);
    assert(p != nullptr);
    assert(p->value.num() == 456.78);
#endif
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// native_object class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
bool native_object::isNative()
{
    return true;
}

bool native_object::can_call_direct()
{
    return true;
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// array class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void update_length(oarray* pa)
{
    int length = pa->elements.size();
    pa->put(L"length", var(length));
}

void oarray::reg_cons(obj_ptr vars)
{
    obj_ptr o(new oarray(0));
    vars->put(L"Array", var(o));
}

var oarray::defaultValue(js::context &ec, js::object::dv hint) {
    return var(0);
}

var oarray::call_direct(context& ec, char_ptr m, oarray& args)
{
    wstring s = m;
    int argc = args.length();

    if(s == L"") {      // constructor
        int count = 0;
        if(argc > 0) {
            count = args.getat(0).c_int();
        }
        obj_ptr o(new oarray(count));
        return var(o);
    }
    else if(s == L"isArray" && argc > 0) {
        if(args.getat(0).vt != var::tobj) {
            return var(false);
        }
        
        obj_ptr o = args.getat(0).oval;
        if(o->is_array())
            return var(true);
        return var(false);
    }
	else if(s == L"toString") {
        wstring p = L"";
        wstring s = this->join(p);
        return var(s);
	}
	else if(s == L"join") 	{
        wstring s;
        wstring sep = args.getat(0).wstr(); 
        s = this->join(sep);
        return var(s);
	}
	else if(s == L"concat") {
        var v0 = args.getat(0);
        oarray* other;
        int ol;
        int l;
        
        if(v0.vt == var::tobj && v0.oval->is_array()) {
            other = (oarray*) v0.oval.get();   
            ol = other->length();         
        }
        else {
            other = &args;
            ol = argc;
        }
        l = length() + ol;
        oarray* ret = new oarray(l);
        int a,j;

        for(a=0; a < length(); a++) {
            ret->putat(a, getat(a));
        }

        for(j=0; j < ol; j++) {
            ret->putat(a+j, other->getat(j));
        }
        return var(obj_ptr(ret));
	}
	else if(s == L"pop") {
        return pop();
	}
	else if(s == L"push") {
        int len = length();
        insert(len, argc);
        for(int i=0;  i < argc; i++) {
            putat(len+i, args.getat(i));
        }
        update_length(this);
        return var(length());
	}
	else if(s == L"reverse") {
        reverse();
        return var();
	}
	else if(s == L"shift") {
        return shift();
	}
	else if(s == L"slice") {
        if(argc > 1) {
            int start = args.getat(0).c_int();
            int end = args.getat(1).c_int();
            int len = this->length();
            if(start < 0)
                start = len + start;
            if(end < 0)
                end = len + end;
            
            return slice(start, end);
        }
        return var();
    }
	else if(s == L"splice") {
        int start = args.getat(0).c_int();
        int del = args.getat(1).c_int();
        int lt = length();
        if(start < 0) {
            start = lt + start;
            if(start < 0) start = 0;
        }
        else {
            if(start > lt)
                start = lt;
        }

        oarray* ret = new oarray(del);
        for(int a=0; a < del; a++) {
            ret->putat(a, getat(start+a));				
        }

        if(del >0)
            remove(start, del);

        int ln = argc - 2;
        if(ln > 0) {
            insert(start,ln);
            for(int a=0; a < ln; a++) {
                putat(a+start, args.getat(a+2));
            }
        }
        return var(obj_ptr(ret));
	}
	else if(s == L"unshift") {
        if(argc <= 0)
            return var(length());

        insert(0, argc);
        for(int a=0; a < argc; a++) {
            putat(a, args.getat(a));
        }
        return length();
	}
	else if(s == L"sort") {
        obj_ptr fo = nullObj;
        if(argc > 0)
            fo = args.getat(0).obj();

        int i,j;
        for(i=0; i < length(); i++) {
            for(j=i+1; j < length(); j++) {
                var v1 = getat(i);
                var v2 = getat(j);
                bool b;
                if(fo == nullObj) {
                    b = v1 > v2;
                }
                else {
                    oarray a (2);
                    a.putat(0, v1);
                    a.putat(1, v2);
                    var v = fo->call(ec, a);
                    b = v.c_bool();
                }
                if(b) {
                    putat(j, v1);
                    putat(i, v2);
                }
            }
        }
        return var();
	}
    else if(s == L"map" && argc > 0) {
        var vf = args.getat(0);
        if(vf.vt != var::tobj) 
            return ec.setError(L"Function expected - Array.map", nullToken);
        obj_ptr  of = vf.oval;
        
        obj_ptr othis;
        if(argc > 1) {
            othis = args.getat(1).obj();
        }
        
        context e2(ec);
        if(othis != nullObj)
            e2.othis.value = var(othis);
            
        int len = this->length();
        oarray* res = new oarray(len);
        oarray a2(2);
        for(int i=0; i < this->length(); i++) {
            a2.putat(0, this->getat(i));
            a2.putat(1, var(i));
            var ret = of->call(e2, a2);
            res->putat(i, ret);
        }
        
        return var(obj_ptr(res));
    }
    else if(s == L"forEach" && argc > 0) {
        var vf = args.getat(0);
        if(vf.vt != var::tobj) 
            return ec.setError(L"Function expected - Array.forEach", nullToken);
        obj_ptr  of = vf.oval;
        
        obj_ptr othis;
        if(argc > 1) {
            othis = args.getat(1).obj();
        }
        
        context e2(ec);
        if(othis != nullObj)
            e2.othis.value = var(othis);
            
        oarray a2(2);
        for(int i=0; i < this->length(); i++) {
            a2.putat(0, this->getat(i));
            a2.putat(1, var(i));
            var ret = of->call(e2, a2);
        }
        
        return var();
    }
    else if(s == L"reduce" && argc > 0) {
        var vf = args.getat(0);
        if(vf.vt != var::tobj) 
            return ec.setError(L"Function expected - Array.reduce", nullToken);
        obj_ptr  of = vf.oval;

        int len = this->length();
        if(len == 0 && argc == 1)
            return ec.setError(L"Unexpected - Array.reduce", nullToken);

        int i =0 ;
        var rv;
        
        if(argc >= 2) {
            rv = args.getat(1);
        }
        else {
            do {
                if(i < len) {
                    rv = this->getat(i++);
                    break;
                }
                
                if(++i >= len) {
                    return ec.setError(L"Unexpected - Array.reduce", nullToken);
                }
                
            } while(true);
        }
        
        oarray a2(2);
        context e2(ec);
        for(; i < len; i++) {
            a2.putat(0, rv);
            a2.putat(1, this->getat(i));
            rv = of->call(e2, a2);
        }
        
        return rv;
    }
    else if(s == L"reduceRight" && argc > 0) {
        var vf = args.getat(0);
        if(vf.vt != var::tobj) 
            return ec.setError(L"Function expected - Array.reduce", nullToken);
        obj_ptr  of = vf.oval;

        int len = this->length();
        if(len == 0 && argc == 1)
            return ec.setError(L"Unexpected - Array.reduceRight", nullToken);

        int i = len - 1;
        var rv;
        
        if(argc >= 2) {
            rv = args.getat(1);
        }
        else {
            do {
                if(i < len) {
                    rv = this->getat(i--);
                    break;
                }
                
                if(--i < 0) {
                    return ec.setError(L"Unexpected - Array.reduceRight", nullToken);
                }
                
            } while(true);
        }
        
        oarray a2(2);
        context e2(ec);
        for(; i >= 0; i--) {
            if(i < len) {
                a2.putat(0, rv);
                a2.putat(1, this->getat(i));
                rv = of->call(e2, a2);
            }
        }
        
        return rv;
    }
    wstring err = L"Array method not found: ";
    err += m;
    return ec.setError(err.c_str(), NULL);

}

oarray::oarray(int count)
{
    elements.insert(elements.begin(), count, prop());
    update_length(this);
}

bool oarray::is_array()
{
    return true;
}

var oarray::getat(int i)
{
    if(i < 0 || i >= elements.size()) {
        return var();
    }
    
    return elements[i].value;
}

var& oarray::getref(int i)
{
    return elements[i].value;
}

void oarray::putat(int a, const var& v)
{
    // 2011-12-08 shark@anui.com auto-expand arrays 
    int len = elements.size();
    if( a < 0 ) return;
    
    // TODO: Need to send an error to the console
    
    if(a >= len ) {
        elements.insert(elements.end(), a - len + 1, prop());
        update_length(this);
    }
    
    elements[a].value = v;
}

prop* oarray::refat(int i)
{
    return &elements[i];
}

var oarray::pop()
{
    if( elements.size() == 0 ) return var();
    
    var ret = elements[elements.size()-1].value;
    elements.pop_back();
    update_length(this);    
    return ret;
}

void oarray::reverse()
{
    std::reverse(elements.begin(), elements.end());    
}

var oarray::shift()
{
    var v = getat(0);
    elements.erase(elements.begin());
    update_length(this);    
    return v;
}

void oarray::remove(int s, int c)
{
    elements.erase(elements.begin() + s, elements.begin() + s + c);
    update_length(this);    
}

void oarray::insert(int s, int c)
{
    elements.insert(elements.begin()+s, c, prop());
    update_length(this);    
}

var oarray::slice(int s, int e)
{
    int count = e - s;
    if (count < 0 || e >= (int) elements.size())
        return var::error();

    obj_ptr o(new oarray(count));
    vector<prop>::iterator start = elements.begin() + s;
    vector<prop>::iterator end = elements.begin() + e;
    for(int i =0; start != end; start++, i++)
    {
        o->putat(i, (*start).value);
    }

    return var(o);
}

wstring oarray::join(wstring& sep)
{
    wstring ret;
    int count = (int) elements.size();
    for(int i=0; i < count; i++)
    {
        ret += elements[i].value.wstr();
        if(i < (count-1))
        {
            ret += sep;
        }
    }
   
    return ret;
}

int oarray::length()
{
    return (int) elements.size();
}

void oarray::test()
{
#if 0
    oarray a(10);

    for(int i=0; i < 10; i++)
    {
        a.putat(i, var(i+1));
    }
    assert(a.getat(9).num() == 10);

    a.reverse();
    assert(a.getat(0).num() == 10);
    a.reverse();

    number n = a.getat(2).num();
    var& r = a.getref(2);
    r.copy(var(L"hello"));
    assert(a.getat(2).wstr() == L"hello");
    r.copy(var(n));
    
    a.shift();
    assert(a.getat(0).num() == 2);
    a.insert(0, 1);
    assert(a.length() == 10);
    a.putat(0, var(1));
    var v = a.pop();
    assert(v.num() == 10);
    wstring sep = L",";
    wstring s = a.join(sep);
    assert(s == L"1,2,3,4,5,6,7,8,9");
    prop* pr = a.refat(5);
    pr->value = var(111.222);
    assert(a.getat(5).num() == 111.222);

    tokenizer l;
    l.tokenize(L"a=[1,2,3]; x=a.length;");
    parser p(l);
    node_ptr np = p.parse();
    context ctx(nullptr, obj_ptr(new object()));
    oarray::reg_cons(ctx.vars);
    v = ctx.run(ctx, np);
    assert(v.num() == 3);

    l.tokenize(L"m=new Array(19); y=m.length;");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.num() == 19);

    l.tokenize(L"m=[1,2,3]; y=m.toString();");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.wstr() == L"123");

    l.tokenize(L"m=[1,2,3]; y=m.join('_');");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.wstr() == L"1_2_3");

    l.tokenize(L"m=[1,2,3]; z=m.concat('a','b','c'); y=z.toString();");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.wstr() == L"123abc");

    l.tokenize(L"m=new Array(); m.push(1); m.push(2); m.push(3); y=m.pop();");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.num() == 3);

    l.tokenize(L"m=[9,8,7]; y=m.shift();");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.num() == 9);

    l.tokenize(L"m=[0,1,2,3,4,5]; j=m.slice(2,4);y=j[1];");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.num() == 3);

    l.tokenize(L"m=[9,8,7]; m.reverse();y=m[0];");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.num() == 7);

    l.tokenize(L"m=[9,8,7]; m.sort();y=m[0];");
    p.lex = l;
    np = p.parse();
    v = ctx.run(ctx, np);
    assert(v.num() == 7);
#endif

}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// token class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
token::token(number n, int l, int c, int so, int eo) : sval(nullStr), line(l)
{
    type = tnumlit;
    nval = n;
    column = c;
    start = so;
    end = eo;
}

token::token(int t, str_ptr s, int l, int c, int so, int eo) : type(t), sval(s), line(l)
{
    column = c;
    start = so;
    end = eo;
}

token::token(int t, str_ptr s, int l, int c, int so, int eo, int fo) : type(t), sval(s), line(l)
{
    column = c;
    start = so;
    end = eo;
    flagOffset = fo;
}


token::token(int k, int l, int c, int so, int eo) : sval(nullStr), type(tkeyword), line(l)
{
    kid = k;
    column = c;
    start = so;
    end = eo;
}

token::token(int t, int l, int c, int so, int eo, bool other) {
    type = t;
    line = l;
    column = c;
    start = so;
    end = eo;
}

token::token(int t, int l, int c, int so, int eo, bool other, int fo) {
    type = t;
    line = l;
    column = c;
    start = so;
    end = eo;
    flagOffset = fo;
}

bool token::is_numop() {
    if(type == tlineterm)
        return true;
        
    if(type != tkeyword)
        return false;
        
    switch(kid) {
    	 case token::kidGe:
    	 case token::kidEqEq:
    	 case token::kidNe:
    	 case token::kidEqEqEq:
    	 case token::kidNEqEq:

    	 case token::kidPlus:
    	 case token::kidMinus:
    	 case token::kidMult:
    	 case token::kidMod:

    	 case token::kidLShift:
    	 case token::kidRShift:
    	 case token::kidRShiftA:
    	 case token::kidAnd:
    	 case token::kidOr:
    	 case token::kidCaret:

    	 case token::kidNot:
    	 case token::kidTilde:
    	 case token::kidAndAnd:
    	 case token::kidOrOr:

    	 case token::kidEqual:
    	 case token::kidPlusEq:
    	 case token::kidMinusEq:
    	 case token::kidMultEq:
    	 case token::kidModEq:
    	 case token::kidLShiftEq:

    	 case token::kidRShiftEq:
    	 case token::kidRShiftAEq:
    	 case token::kidAndEq:
    	 case token::kidOrEq:
    	 case token::kidCaretEq:

    	 case token::kidDiv:
    	 case token::kidDivEq:
         case token::kidComma:
         case token::kidLBracket:
            return true;        
    }
    return false;
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// tokenizer class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
tokenizer::tokenizer() {
    lineTokens = NULL;
    finalTokens = NULL;
    finalOffsets = NULL;
}

tokenizer::~tokenizer() {
    if(lineTokens)
        delete lineTokens;
    if(finalTokens)
        delete finalTokens;        
    if(finalOffsets)
        delete finalOffsets;
}

void tokenizer::advance(int n) {
    current += n;
    column += n;
}

void tokenizer::nextLine() {
    if(column > maxWidth)
        maxWidth = column;
    line++;
    lineOffsets.push_back(current);
    column = 1;
}


void tokenizer::newWord() {
    word.clear();
    wordColumn = column;
    offset = current;
}

bool tokenizer::hasError()
{
    if(error == L"")
        return false;
    else
        return true;
}

bool tokenizer::setError(char_ptr sz, int line, int column)
{
    wstlprintf(error, L"%d:%d (%d) %ls", script_index,line,column,sz);
    if(!colorizer)
        throw new exception(error, line, column);
    
    return true;
}

void tokenizer::initKeywords()
{
    keywords[L"break"] = token::kidBreak;
    keywords[L"else"]=	token::kidElse;
    keywords[L"new"]=		 token::kidNew;
    keywords[L"var"]=		 token::kidVar;
    keywords[L"case"]=		 token::kidCase;
    keywords[L"finally"]=	 token::kidFinally;
    keywords[L"return"]=	 token::kidReturn;
    keywords[L"void"]=		 token::kidVoid;
    keywords[L"catch"]=		 token::kidCatch;
    keywords[L"for"]=		 token::kidFor;
    keywords[L"switch"]=	 token::kidSwitch;
    keywords[L"while"]=		 token::kidWhile;
    keywords[L"continue"]=	 token::kidContinue;
    keywords[L"function"]=	 token::kidFunction;
    keywords[L"this"]=		 token::kidThis;
    keywords[L"with"]=		 token::kidWith;

    keywords[L"default"]=	 token::kidDefault;
    keywords[L"if"]=		 token::kidIf;
    keywords[L"throw"]=		 token::kidThrow;

    keywords[L"delete"]=	 token::kidDelete;
    keywords[L"in"]=		 token::kidIn;
    keywords[L"try"]=		 token::kidTry;

    keywords[L"do"]=		 token::kidDo;
    keywords[L"instanceof"]= token::kidInstanceOf;
    keywords[L"typeof"]=	 token::kidTypeOf;

    keywords[L"{"]=			 token::kidLBrace;
    keywords[L"}"]=			 token::kidRBrace;
    keywords[L"("]=			 token::kidLPar;
    keywords[L")"]=			 token::kidRPar;
    keywords[L"["]=			 token::kidLBracket;
    keywords[L"]"]=			 token::kidRBracket;

    keywords[L"."]=			 token::kidDot;
    keywords[L";"]=			 token::kidSemi;
    keywords[L","]=			 token::kidComma;
    keywords[L"<"]=			 token::kidLt;
    keywords[L">"]=			 token::kidGt;
    keywords[L"<="]=		 token::kidLe;

    keywords[L">="]=		 token::kidGe;
    keywords[L"=="]=		 token::kidEqEq;
    keywords[L"!="]=		 token::kidNe;
    keywords[L"==="]=		 token::kidEqEqEq;
    keywords[L"!=="]=		 token::kidNEqEq;

    keywords[L"+"]=			 token::kidPlus;
    keywords[L"-"]=			 token::kidMinus;
    keywords[L"*"]=			 token::kidMult;
    keywords[L"%"]=			 token::kidMod;
    keywords[L"++"]=		 token::kidPlusPlus;
    keywords[L"--"]=		 token::kidMinusMinus;

    keywords[L"<<"]=		 token::kidLShift;
    keywords[L">>"]=		 token::kidRShift;
    keywords[L">>>"]=		 token::kidRShiftA;
    keywords[L"&"]=			 token::kidAnd;
    keywords[L"|"]=			 token::kidOr;
    keywords[L"^"]=			 token::kidCaret;

    keywords[L"!"]=			 token::kidNot;
    keywords[L"~"]=			 token::kidTilde;
    keywords[L"&&"]=		 token::kidAndAnd;
    keywords[L"||"]=		 token::kidOrOr;
    keywords[L"?"]=			 token::kidQuestion;
    keywords[L":"]=			 token::kidColon;

    keywords[L"="]=			 token::kidEqual;
    keywords[L"+="]=		 token::kidPlusEq;
    keywords[L"-="]=		 token::kidMinusEq;
    keywords[L"*="]=		 token::kidMultEq;
    keywords[L"%="]=		 token::kidModEq;
    keywords[L"<<="]=		 token::kidLShiftEq;

    keywords[L">>="]=		 token::kidRShiftEq;
    keywords[L">>>="]=		 token::kidRShiftAEq;
    keywords[L"&="]=		 token::kidAndEq;
    keywords[L"|="]=		 token::kidOrEq;
    keywords[L"^="]=		 token::kidCaretEq;

    keywords[L"/"]=			 token::kidDiv;
    keywords[L"/="]=		 token::kidDivEq;

    keywords[L"abstract"]=	 token::kidReserved;
    keywords[L"enum"]=		 token::kidReserved;
    keywords[L"int"]=		 token::kidReserved;
    keywords[L"short"]=		 token::kidReserved;

    keywords[L"boolean"]=	 token::kidReserved;
    keywords[L"export"]=	 token::kidReserved;
    keywords[L"interface"]=	 token::kidReserved;
    keywords[L"static"]=	 token::kidReserved;

    keywords[L"byte"]=		 token::kidReserved;
    keywords[L"extends"]=	 token::kidReserved;
    keywords[L"long"]=		 token::kidReserved;
    keywords[L"super"]=		 token::kidReserved;

    keywords[L"char"]=		 token::kidReserved;
    keywords[L"final"]=		 token::kidReserved;
    keywords[L"native"]=	 token::kidReserved;
    keywords[L"synchronized"]= token::kidReserved;

    keywords[L"class"]=		 token::kidReserved;
    keywords[L"float"]=		 token::kidReserved;
    keywords[L"package"]=	 token::kidReserved;
    keywords[L"throws"]=	 token::kidReserved;

    keywords[L"const"]=		 token::kidReserved;
    keywords[L"goto"]=		 token::kidReserved;
    keywords[L"private"]=	 token::kidReserved;
    keywords[L"transient"]=	 token::kidReserved;

    keywords[L"debugger"]=	 token::kidDebugger;
    keywords[L"implements"]= token::kidReserved;
    keywords[L"protected"]=	 token::kidReserved;
    keywords[L"volatile"]=	 token::kidReserved;

    keywords[L"double"]=	 token::kidReserved;
    keywords[L"import"]=	 token::kidReserved;
    keywords[L"public"]=	 token::kidReserved;

    keywords[L"true"]=		 token::kidTrue;
    keywords[L"false"]=		 token::kidFalse;
    keywords[L"null"]=		 token::kidNull;
    keywords[L"undefined"]=	 token::kidUndefined;

}

void tokenizer::tokenizeBegin(char_ptr inp, int index, bool c) {
    if(finalTokens) delete finalTokens;
    if(lineTokens) delete lineTokens;
    if(finalOffsets) delete finalOffsets;
    
    finalTokens = NULL;
    lineTokens = NULL;
    finalOffsets = NULL;
    lineOffsets.clear();
    lineOffsets.push_back(0);
    script_index = index;
    this->maxWidth = -1;
    colorizer = c;
    tokens.clear();
    error.clear();
    strings.clear();
    identifiers.clear();

    if(keywords.size() == 0)
    {
        initKeywords();
    }

    input = inp;

    current = 0;
    column = 1;
    line = 1;
    newWord();
    max = input.size();
}

bool tokenizer::tokenizeNext() {
    if(current >= max)
        return false;
        
    tokenizeSpace();
    if(current >= max) {
        return false;
    }

    int last_current = current;
    int last_column = column;

    if(tokenizeComment()) {
        if(!hasError()) 
            return true;
    }

    if(tokenizeLineTerm()) {
        if(!hasError())
            return true;
    }

    if(tokenizeNumber()) {
        if(!hasError())
            return true;
    }

    if(tokenizeString()) {
        if(!hasError())
            return true;
    }
        
    if(tokenizeRegEx()) {
        if(!hasError())
            return true;
    }
        
    if(tokenizeWord()) {
        if(!hasError())
            return true;
    }
        
    if(colorizer) {
        current = last_current;
        column = last_column;
        tokenizeAny();
        return true;
    }
    return false;
}

void tokenizer::tokenizeEnd() {
    // If we have no source, don't add anything here
    if(input == L"") {
        lineCount = 0;
        return;
    }

    // special case - add the newLine token at the end of the file to overlap
    // with the last actual token so we never index out of range in the text
    // of the line
    offset = current -1;
    // add a line term token in case we need it to complete parsing for auto-semi
    token t(token::tlineterm, line, wordColumn, offset, offset+1, true);         
    add_token(t);
    
    lineCount = line;
    
    // create the finalToken stream
    finalTokens = new token_ptr[tokens.size()];

    // setup all the lineTokens data
    lineTokens = new int[lineCount];
    
    // setup all line offset starts
    finalOffsets = new int[lineCount];
    

    int index = 0;
    for(vector<int>::iterator i = lineOffsets.begin(); i != lineOffsets.end(); i++) {
        finalOffsets[index] = *i;
        index++;
    }
    
    // clear all lineTokens data
    for(int i=0; i < lineCount; i++)
        lineTokens[i] = -1;

    index = 0;
    for(vector<token>::iterator i = tokens.begin(); i != tokens.end(); i++) {
        token_ptr p = &(*i);
        // set all the token pointers to speed up getToken() calls
        finalTokens[index] = p;
        
        // update the line to point to the associated start token index
        int l = p->line;
        if(lineTokens[l-1] == -1)
            lineTokens[l-1] = index;
        
        index++;
    }
}

int tokenizer::lineEndOffset(int line) {
    if(line < lineCount-1) {
        return lineStartOffset(line+1) -1;
    }
    
    return input.length();
}

int tokenizer::lineFromOffset(int offset) {
    if(offset >= input.length())
        return lineCount-1;
        
    for(int l=0; l < lineCount; l++) {
        int start = lineStartOffset(l);
        int end = lineEndOffset(l);
        if(offset >= start && offset <= end)
            return l;
    }
    return (lineCount-1);
}

int tokenizer::tokenIndexFromLine(int line) {
    return lineTokens[line];
}

int tokenizer::getLineCount() {
    return lineCount;
}

int tokenizer::lineStartOffset(int line) {
    assert(finalOffsets);
    if(line >= lineCount-1)
        line = lineCount - 1;
    return finalOffsets[line];
}

bool tokenizer::tokenize(char_ptr buffer, int index, bool c)
{
    tokenizeBegin(buffer, index, c);
    while(tokenizeNext()) {
        if(hasError())
            break;
        error = L"";
    }

    tokenizeEnd();
    return true;
}

bool tokenizer::tokenizeSpace()
{
    jschar c;
    c = input[current];
    if(!isSpace(c))
        return false;

    while(isSpace(c) && current < max)
    {
        advance(1);
        c = input[current];
    }
    return true;
}

bool tokenizer::tokenizeComment()
{
    jschar c = input[current];

    if(c == '/' && input[current+1] == '/')
    {
        newWord();
        advance(2);

        while(current < max)
        {
            c = input[current];
            advance(1);
            if(isLineTerm(c))
            {
                if(colorizer) {
                    add_token(token(token::tcomment, line, wordColumn, offset, current, true));                
                }
                nextLine();
                return true;
            }
        }

        if(colorizer) {
            add_token(token(token::tcomment, line, wordColumn, offset, current, true));                
        }

        return true;
    }

    if(c == '/' && input[current+1] == '*')
    {
        newWord();
        advance(2);
        while(current < max)
        {
            c = input[current];
            if(c == '\n')
            {
                if(colorizer) {
                    // add 1 to current to cover the \n character
                    add_token(token(token::tcomment, line, wordColumn, offset, current+1, true));
                    advance(1);
                    nextLine();
                    newWord();
                    continue;
                }
                nextLine();
            }

            if(c == '*' && input[current+1] == '/')
            {
                advance(2);
                if(colorizer) {
                    add_token(token(token::tcomment, line, wordColumn, offset, current, true));
                }
                return true;
            }
            advance(1);
        }
        
        if(colorizer) {
            add_token(token(token::tcomment, line, wordColumn, offset, current, true));  
            return true;              
        }
        
        return setError(L"Unterminated comment", line, column);		
    }

    return false;
}

bool tokenizer::tokenizeLineTerm()
{
    jschar c;
    c = input[current];

    if(!isLineTerm(c))
        return false;

    newWord();                  // Must advance offset
    while(isLineTerm(c) && current < max)
    {
        token t(token::tlineterm, line, wordColumn, offset, offset+1, true);         
        add_token(t);

        advance(1);                 // advance first so that column resets back to 1
        
        // increment the offset here so we don't put the line term token at the wrong offset
        offset++;
        nextLine();
        if(current >= max)
            break;
        c = input[current];
    }
    return true;
}

bool tokenizer::isNumConstPrefix(jschar c) {
    return (c == '-' || c == '+');
}

bool tokenizer::tokenizeNumber()
{
    jschar c;
    c = input[current];
    bool hasPrefix = false;             // true if prefix '+' or '-' present

    // Hex number
    if(c=='0')
    {
        jschar c2 = input[current+1];
        if(c2 == 'x' || c2 == 'X')
        {
            newWord();
            advance(2);
            c = input[current];
            while(isHexDigit(c) && current < max)
            {
                word.push_back(c); 
                advance(1);
                c = input[current];
            }
            
            number n = numberFromHex(word);
            add_token(token(n, line, wordColumn, offset, offset+word.length()));
            return true;
        }
    }

    // mohsena 20110120 - allow for '-' prefix for numlit
    // mohsena 20110306 - allow for '-<literal>' not to be confused with numlit
    // shark@anui.com - 2011-11-13 - support '+' prefix in addition to '-'
    number mult = 1.0;
    if(isNumConstPrefix(c) && current < max && isNum(input[current+1]))
    {
        // shark@anui.com 2011-11-12 - only treat negative numbers as constants
        // if preceeded by *, +, -, /, %
        int tcount = tokens.size();
        if(tcount > 0) {
            token last = tokens[tcount - 1];
            if(last.is_numop()) {
                hasPrefix = true;
                advance(1);
                mult = (c == '-') ? -1.0 : 1.0;
                c = input[current];
            }
        }
    }

    bool hasDot = false;
    bool hasExp = false;
    if(c == '.') {
        if(current < max-1) {
            if(!isNum(input[current+1]))
                return false;
            hasDot = true;
        }
    }
    
    // Is Number
    if(isNum(c) || c == '.')
    {
        newWord();

        if(c == '.')
            hasDot = true;

        while(current < max)
        {
            word.push_back(c);
            advance(1);
            c = input[current];
            if(c == 'e' || c == 'E')
            {
                if(hasExp)
                    return setError(L"Invalid number format", line, column);		
                hasExp = true;
                word.push_back(c);
                advance(1);
                c = input[current];
                if(c == '+' || c == '-')
                {
                    word.push_back(c);
                    advance(1);
                }
                c = input[current];
                continue;
            }

            if(c == '.')
            {
                if(hasExp)
                {
                    return setError(L"Invalid number format", line, column);		
                }

                if(hasDot)
                {
                    return setError(L"Invalid number format", line, column);		
                }

                hasDot = true;
                continue;
            }
            if(isNum(c))
                continue;
            else
            {
                number n = mult * numberFromWord(word);
                
                // fix shark@anui.com 2011-11-12 - if the multiplier is negative, adjust the starting
                // column to account for the minus sign
                int adjust = 0;
                if(hasPrefix) {
                    wordColumn--;
                    offset--;
                    adjust = 1;
                }
                    
                add_token(token(n, line, wordColumn, offset, offset+word.length()+adjust));
                break;
            }
        }
        return true;
    }
    else
        return false;
}

bool tokenizer::isWordStart(jschar c)
{
    if(isAlpha(c))
        return true;

    if(c == '_' || c == '$')
        return true;

    return false;
}

bool tokenizer::isWordChar(jschar c)
{
    if(isWordStart(c) || isNum(c))
        return true;

    return false;
}

bool tokenizer::isPuncSolo(jschar c)
{
    switch(c)
    {
        // 2012-01-21 shark@anui.com added '(' and ')' to handle (v>10)&&(x<20) 
        case '(':
        case ')':

        case	'[':
        case	']':
        //			case	'^':
        //			case	'%':
        case	'.':
            return true;            
    }
    return false;
}

bool tokenizer::isPuncTerm(jschar c)
{
    switch(c)
    {
    case	'=':
    case	'>':
    case	'<':
    case	'+':
    case	'-':
    case	'|':
    case	'&':
        return false;
    }
    return true;
}

bool tokenizer::isSpace(jschar c)
{
    switch(c)
    {
    case	0xfeff:
    case	0x9:
    case	0xb:
    case	0xc:
    case	0x20:
    case	0xa0:
    case	0xd:
    case	0x2028:
    case	0x2029:
        return true;
    }
    return false;
}

bool tokenizer::isLineTerm(jschar c)
{
    switch(c)
    {
    case	0xa:
        return true;
    }
    return false;
}

bool tokenizer::isHexDigit(jschar c)
{
    if(isNum(c))
        return true;

    if(c >= 'A' && c <= 'F')
        return true;

    if(c >= 'a' && c <= 'f')
        return true;

    return false;
}

bool tokenizer::isAlpha(jschar c)
{
    if( (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z'))
        return true;
    return false;
}

bool tokenizer::isStringDelim(jschar c)
{
    if(c == '\'' || c == '\"')
        return true;
    return false;
}

bool tokenizer::isNum(jschar ch)
{
    if(ch >= '0' && ch <= '9')
        return true;
    return false;
}

bool tokenizer::isAlNum(jschar c)
{
    if(isNum(c) || isAlpha(c))
        return true;
    return false;
}


jschar tokenizer::hexVal(jschar c)
{
    if(c >= 'a' && c <= 'f')
        c = 10 + (c - 'a');
    else if(c >= 'A' && c <= 'F')
        c = 10 + (c - 'A');
    else if(c >= '0' && c <= '9')
        c = c - '0';
    return c;
}

bool tokenizer::isIdentifier(wstring& s)
{
    jschar c = s[0];

    if(!isWordStart(c))
        return false;

    for(unsigned int l=1 ; l < s.size(); l++)
    {
        c = s[l];
        if(!isWordChar(c))
            return false;
    }
    return true;
}


bool regularExpressionFlags(tokenizer* tk);
bool regularExpressionBody(tokenizer* tk);
bool regularExpressionChars(tokenizer* tk);
bool regularExpressionChar(tokenizer* tk);
bool regularExpressionNonTerminator(tokenizer* tk);
bool regularExpressionBackslashSequence(tokenizer* tk);
bool regularExpressionClass(tokenizer* tk);
bool regularExpressionLiteral(tokenizer* tk, int* fo);
bool regularExpressionClassChar(tokenizer* tk);
bool regularExpressionFirstChar(tokenizer* tk);
bool regularExpressionClassChars(tokenizer* tk);

bool identifierPart(tokenizer* tk);
bool sourceCharacter(tokenizer* tk);
bool lineTerminator(jschar c);
bool unicodeEscapeSequence(tokenizer* tk);

bool lineTerminator(jschar c) {
    if(c == '\r' || c == '\n')
        return true;
    return false;
}

bool sourceCharacter(tokenizer* tk) {
    jschar c = tk->input[tk->current];
    if(c != 0 && tk->current < tk->max-1) {
        tk->advance(1);
        return true;
    }
    
    return false;
}

bool identifierPart(tokenizer* tk) {    
    while(tk->current < tk->max) {
        jschar c = tk->input[tk->current];
        if(c == '$' || c == '_')
            tk->advance(1);
        else if(tk->isAlNum(c))
            tk->advance(1);            
        else if(!unicodeEscapeSequence(tk)) {
            break;
        }
    }
    return true;
}

bool unicodeEscapeSequence(tokenizer* tk) {
    jschar c = tk->input[tk->current];
    if(c != '\\')
        return false;
    int temp = tk->current;
    
    tk->advance(1);
    if(tk->current >= tk->max || tk->input[tk->current] != 'u') {
        tk->current = temp;
        return false;
    }

    for(int i = 0; i < 4; i ++) {
        tk->advance(1);
        if(tk->current >= tk->max || !tk->isHexDigit(tk->input[tk->current])) {
            tk->current = temp;
            return false;
        }
    }
    tk->advance(1);
    return true;
}


bool regularExpressionLiteral(tokenizer* tk, int* fo) {

    //
    // although this is not in the spec, we must distinguish between regular
    // expression literals and mathematical experssions that contain two
    // forward slashes in them (e.g. x = 5 / 3*2 / 1;
    //        
    if(tk->current > 0) {
        int s = tk->tokens.size();
        if(s < 1)
            return false;
        token_ptr last = tk->getToken(s-1);
        if(last->type == token::tidentifier || last->type == token::tstrlit ||
           last->type == token::tnumlit)
            return false;
           
        // 20120906 shark@anui.com - only support RegEx literals if the previous
        // token is not a right parenthesis
        if(last->type == token::tkeyword) {
            if(last->kid == token::kidRPar)
                return false;
            
            // 20120907 shark@anui.com we also have to account for a[0]/2, b=4/3;
            // so disallow RegEx literal if the previous token is a ']'
            if(last->kid == token::kidRBracket)
                return false;
        }
    }
    int temp = tk->current;
    jschar c = tk->input[tk->current];
    if(c != '/') {
        tk->current = temp;
        return false;
    }
        
    tk->advance(1);
    bool res = regularExpressionBody(tk);
    if(!res) {
        tk->current = temp;
        return false;
    }

    c = tk->input[tk->current];
    if(c != '/') {
        tk->current = temp;
        return false;
    }
    
    *fo = tk->current - temp;
    tk->advance(1);
    return regularExpressionFlags(tk);
}

bool regularExpressionBody(tokenizer* tk) {
    int temp = tk->current;
    bool res = regularExpressionFirstChar(tk);
    
    if(!res) {
        tk->current = temp;
        return false;
    }
        
    return regularExpressionChars(tk);
}

bool regularExpressionChars(tokenizer* tk) {
    while(regularExpressionChar(tk));    
    return true;
}

bool regularExpressionFirstChar(tokenizer* tk) {
    int temp = tk->current;
    jschar c = tk->input[tk->current];
    bool res = regularExpressionNonTerminator(tk);
    
    if(res && c != '*' && c != '\\' && c != '/' && c != '[')
        return true;
        
    tk->current = temp;
    res = regularExpressionBackslashSequence(tk);
    if(res)
        return res;
        
    tk->current = temp;
    return regularExpressionClass(tk);
}

bool regularExpressionChar(tokenizer* tk) {
    int temp = tk->current;
    jschar c = tk->input[tk->current];
    bool res = regularExpressionNonTerminator(tk);
    if(res && c != '\\' && c != '/' && c != '[')
        return true;
        
    tk->current = temp;
    res = regularExpressionBackslashSequence(tk);
    if(res)
        return res;
        
    tk->current = temp;
    return regularExpressionClass(tk);
}

bool regularExpressionBackslashSequence(tokenizer* tk) {
    jschar c = tk->input[tk->current];
    if(c != '\\')
        return false;
        
    tk->advance(1);
    return regularExpressionNonTerminator(tk);
}

bool regularExpressionNonTerminator(tokenizer* tk) {
    int temp = tk->current;
    jschar c = tk->input[tk->current];
     
    bool res = sourceCharacter(tk);
    if(res && ! lineTerminator(c))
        return true;
    tk->current = temp;
    return false;
}

bool regularExpressionClass(tokenizer* tk) {
    int temp = tk->current;
    jschar c = tk->input[tk->current];
    
    if(c != '[') {
        tk->current = temp;
        return false;
    }
        
    tk->advance(1);
    bool res = regularExpressionClassChars(tk);
    if(!res) {
        tk->current = temp;
        return false;
    }
    
    c = tk->input[tk->current];
    if(c != ']') {
        tk->current = temp;
        return false;
    }
        
    tk->advance(1);
    return true;
}

bool regularExpressionClassChars(tokenizer* tk) {
    while(regularExpressionClassChar(tk));
    return true;
}

bool regularExpressionClassChar(tokenizer* tk) {
    int temp = tk->current;
    jschar c = tk->input[tk->current];
    bool res = regularExpressionNonTerminator(tk);
    if(res && c != ']' && c != '\\')
        return true;
    
    tk->current = temp;
    return regularExpressionBackslashSequence(tk);
}

bool regularExpressionFlags(tokenizer* tk) {
    identifierPart(tk);
    return true;
}


bool tokenizer::tokenizeRegEx() {
    int start = current;
    int flagOffset;
    bool res = regularExpressionLiteral(this, &flagOffset);
    if(res) {
        // capture string between start and current as a token::tregex
        newWord();
        
        for(int i=start; i < current; i++) {
            word.push_back(input[i]);
        }
                
        if(!colorizer) {
            std::pair<set<wstring>::iterator, bool> pi = strings.insert(word);
            set<wstring>::iterator i = pi.first;
            str_ptr sp = &(*i);
            add_token(token(token::tregex, sp, line, wordColumn, start, current+1, flagOffset));
        }
        else
            add_token(token(token::tregex, line, wordColumn, start, current+1, false, flagOffset));
                
        return true;
    }
    current = start;
    return false;
}


bool tokenizer::tokenizeString()
{
    jschar c = input[current];

    if(c == '\'' || c == '\"')
    {
        jschar term = c;						// Terminator

        newWord();
        while(true)
        {
            advance(1);
            if(current >= max)
            {
                return setError(L"Unterminated string", line, column);		
            }

            c = input[current];
            if(isLineTerm(c))
            {
                return setError(L"Unterminated string", line, column);		
            }

            if(c == '\\')
            {
                jschar c2 = input[current+1];	
                switch(c2)
                {
                case	'\'':
                case	'"':
                case	'\\':
                    word.push_back(c2);
                    advance(1);
                    break;
                case	'b':
                    word.push_back(0x0008);
                    advance(1);
                    break;
                case	'f':
                    word.push_back(0x000c);
                    advance(1);
                    break;
                case	'n':
                    word.push_back(0x000a);
                    advance(1);
                    break;
                case	'r':
                    word.push_back(0x000d);
                    advance(1);
                    break;
                case	't':
                    word.push_back(0x0009);
                    advance(1);
                    break;
                case	'v':
                    word.push_back(0x0008);
                    advance(1);
                    break;
                case	'x':
                    jschar d1, d2;
                    advance(2);
                    d1 = input[current];
                    d2 = input[current+1];
                    word.push_back((jschar) (hexVal(d1)*16 + hexVal(d2)));
                    advance(1);
                    break;
                case	'u':
                    jschar d3, d4;
                    advance(2);
                    d1 = input[current];
                    d2 = input[current+1];
                    d3 = input[current+2];
                    d4 = input[current+3];
                    word.push_back((jschar) (hexVal(d1)*4096+hexVal(d2)*256+hexVal(d3)*16+hexVal(d4)));
                    current += 3;
                    break;
                default:
                    return setError(L"Unrecognized string escape sequence", line, column);		
                }
//                c = input[current];
                continue;
            }

            if(c == term)
            {
                // find or add the word
                if(!colorizer) {
                    std::pair<set<wstring>::iterator, bool> pi = strings.insert(word);
                    set<wstring>::iterator i = pi.first;
                    str_ptr sp = &(*i);
                    add_token(token(token::tstrlit, sp, line, wordColumn, offset, current+1));
                }
                else
                    add_token(token(token::tstrlit, line, wordColumn, offset, current+1, false));
                
                advance(1);
                break;
            }

            word.push_back(c);
        }
        return true;
    }
    else
        return false;
}

void tokenizer::tokenizeAny() {
    jschar c = input[current];
    newWord();

    while(true)
    {
        word.push_back(c);
        if(isPuncSolo(c))
        {
            advance(1);
            break;
        }

        advance(1);
        if(current >= max)
            break;

        c = input[current];

        // Special case -N change mohsen@mobicore.com 20060928
        if(c == '-' && current > 0 && input[current-1] != '-')
            break;

        if(current >= max)
            break;
        if(isLineTerm(c)) {
            break;
        }
    }
    
    add_token(token(token::tword, line, wordColumn, offset, offset+ word.length(), false));    
}

bool tokenizer::tokenizeWord()
{
    jschar c = input[current];

    // Get word
    if(isWordStart(c))
    {
        newWord();

        while(true)
        {
            word.push_back(c);
            advance(1);
            if(current >= max)
                break;

            c = input[current];
            if(!isWordChar(c))
                break;
        }
    }
    else
    {
        newWord();

        while(true)
        {
            word.push_back(c);
            if(isPuncSolo(c))
            {
                advance(1);
                break;
            }

            advance(1);
            if(current >= max)
                break;

            c = input[current];

            // Special case -N change mohsen@mobicore.com 20060928
            if(c == '-' && current > 0 && input[current-1] != '-')
                break;

            // 2011-11-17 - shark@anui.com Special case +N change
            if(c == '+' && current > 0 && input[current-1] != '+')
                break;

            if(current >= max || isPuncTerm(c))
                break;
        }
    }

    // Is Keyword or punctuator
    map<wstring, int>::iterator i = keywords.find(word);
    if(i != keywords.end())
    {
        int kid = (*i).second;
        add_token(token(kid, line, wordColumn, offset, offset +word.length()));
    }
    else
    {
        if(!isIdentifier(word))
        {
            return setError(L"Expected identifier",line, column);
        }

        if(colorizer) 
        {
            identifiers.insert(word);   // Add to identifiers list so intelisense can use it
            add_token(token(token::tidentifier, line, wordColumn, offset, offset+ word.length(), false));
        }
        else {
            std::pair<set<wstring>::iterator, bool> pi = identifiers.insert(word);
            set<wstring>::iterator i = pi.first;
            str_ptr sp = &(*i);
            add_token(token(token::tidentifier, sp, line, wordColumn, offset, offset+ word.length()));
        }
    }
    return true;
}

bool tokenizer::tokenize(wstring& inp, int index, bool ie)
{
    return tokenize(inp.c_str(), index, ie);
}

token_ptr tokenizer::getToken(int index)
{
    if(index >= (int) tokens.size())
        return nullToken;

    if(finalTokens)
        return finalTokens[index];
        
    return &tokens[index];
}

void tokenizer::add_token(js::token t) {
    t.script_index = script_index;
    t.pnot_flags = 0;
    tokens.push_back(t);
}

vector<wstring> tokenizer::match(int index, wstring substring) {
    vector<wstring> result;
    
    for(set<wstring>::iterator i = identifiers.begin(); i != identifiers.end(); i++) {
        wstring s = *i;
        if(s.find(substring) != string::npos)
            result.push_back(s);
    }
    
    for(map<wstring, int>::iterator i = keywords.begin(); i != keywords.end(); i++) {
        wstring s = (*i).first;
    if(s.find(substring) != string::npos)
            result.push_back(s);        
    }
    
    return result;
}

void tokenizer::test()
{
    tokenizer lex;

    lex.tokenize(L"var x=20.1;");
    assert(lex.tokens.size() == 5);
    assert(lex.tokens[0].kid == token::kidVar);
    assert(*(lex.tokens[1].sval) == L"x");
    assert(lex.tokens[2].kid == token::kidEqual);
    assert(lex.tokens[3].nval == 20.1);
    assert(lex.tokens[4].kid == token::kidSemi);    

    lex.tokenize(L"hello='world");
    assert(lex.error != L"");

    lex.tokenize(L"s='hello world'; z='hello world'");
    assert(lex.tokens[0].type == token::tidentifier);
    assert(lex.tokens[1].type == token::tkeyword);
    assert(lex.tokens[2].type == token::tstrlit);
    assert(*(lex.tokens[2].sval) == L"hello world");
    assert(lex.tokens[6].sval == lex.tokens[2].sval);

    lex.tokenize(L"i=-2.4;");
    assert(lex.tokens[2].nval == -2.4);
}


class arguments_object : public object {
public:
    oarray* inner;
    arguments_object(object* f, oarray* oa) :inner(oa) {
        this->put(L"length", var(oa->length()));
        weak_proxy* wp = new weak_proxy(f);
        obj_ptr c = obj_ptr(wp);
        var vc = var(c);
        this->put(L"callee", vc);
    }
    virtual bool is_array() { return true; }
    virtual var getat(int i) {
        return inner->getat(i);
    }
    virtual void putat(int i, const var& v) {
        inner->putat(i, v);
    }
    
    virtual var get(char_ptr p) {
        return inner->get(p);
    }
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Parse node class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
node::node(token_ptr t) : token(t), left(nullNode), right(nullNode)
{
    type = None;
    ALLOC("ND",1);
}

node::~node() {
    FREE("ND", 1);
}

var node::eval(js::context &ec) {
    context* ec_found = &ec;
    debugger* dbg = ec.root->attached_debugger;
    bool adv = (dbg) ? !dbg->is_basic() : false;

    int opt = ec.root->optimization;

    if((opt & context::Opt_DynamicProfiler) != 0) {
        if(adv) {
            dbg->profiler_update_node(ec, this);
        }
    }

    if(type == GlobalPtr)
        return *var_ptr;
    else if(type == LocalPtr && match_object == ec.vars->__id)
        return *var_ptr;
        
    char_ptr cp = token->sval->c_str();
    prop* p = ec.vars->getprop(cp, true);
    if(p == NULL)
    {
        // 2012-02-17 shark@anui.com - check for 'arguments' as a special case
        if(wcscmp(cp, L"arguments") == 0) {
            if(ec.func_args != NULL) {
                if(ec.arguments == nullObj) {
                    arguments_object* ao = new arguments_object(ec.func, ec.func_args);
                    ec.arguments = obj_ptr(ao);
                }
                return var(ec.arguments);
            }
        }
        if(! ec.isroot()) {
            p = ec.root->vars->getprop(token->sval->c_str(), true);
            ec_found = ec.root;
        }
        
        if(p == NULL)		
            return var();
    }
    
    if(ec_found->isroot()) {
        this->type = GlobalPtr;
        this->var_ptr = &p->value;
    }
    else {
        type = LocalPtr;
        var_ptr = &p->value;
        match_object = ec.vars->__id;
    }
    return p->value;
}


prop* node::ref(js::context &ec, bool local) {
    if(type == GlobalPtr) {
        return prop_ptr;
    }
    else if(type == LocalPtr && match_object == ec.vars->__id) {
        return prop_ptr;
    }
        
    wstring s = *(token->sval);
    prop* p = ec.vars->getprop(s.c_str(), true);
    if(p == NULL)
    {
        // 2012-04-04 shark@anui.com special case "arguments" to allow for arguments[0].call support
        if(s == L"arguments") {
            if(ec.func_args != NULL) {
                if(ec.arguments == nullObj) {
                    arguments_object* ao = new arguments_object(ec.func, ec.func_args);
                    ec.arguments = obj_ptr(ao);
                }
                ec.vars->put(L"arguments", var(ec.arguments));
            }
            return ec.vars->getprop(L"arguments", false);
        }
        if(local || ec.isroot())
        {
            p = ec.vars->addprop(s.c_str());
            return p;
        }
        
        if(! ec.isroot()) 
            return ec.ref(*(ec.root), this, false);

        return ec.vars->addprop(s.c_str());
    }
        
    if(ec.isroot()) {
        type = GlobalPtr;
        prop_ptr = p;
    }
    else {
        type = LocalPtr;
        prop_ptr = p;
        match_object = ec.vars->__id;
    }
    return p;
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Parser class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
parser::parser(tokenizer& t) : lex(t)
{
    node_count = 0;
#ifdef JS_OPT
    current_block = 0;
    next_block = 0;
    parse_hits = 0;
    alloc_count = 0;
#endif    
}


#ifdef JS_OPT
bool parser::check(int start, unsigned int flag) {
    token_ptr t = getToken(&start);
    if(t == nullToken)
        return true;
        
    if(t->pnot_flags & flag) {
        parse_hits++;
        return true;
    }
    return false;
}
#endif

node_ptr parser::new_node(token_ptr t) {
    node_count++;
    node_ptr p = new node(t);
#ifdef JS_OPT
    p->block = this->current_block;
    alloc_count++;
#endif    
    return p;
}

void parser::free_nodes(node_ptr n) {
    if(n == nullNode)
        return;
        
    vector<node_ptr> queue;
    queue.push_back(n);
    
    while(!queue.empty()) {
        node_ptr c = queue.back();
        queue.pop_back();
        if(c->left) 
            queue.push_back(c->left);
        if(c->right)
            queue.push_back(c->right);
        delete c;
        node_count--;
    }
            
//    if(n) {
//        if(n->left)
//            free_nodes(n->left);
//        if(n->right)
//            free_nodes(n->right);
//        delete n;
//        node_count--;
//    }
}

node_ptr parser::parse()
{
    current = 0;
    next = -1;

    error.clear();
    return program(current);
}

node_ptr parser::setError(char_ptr sz, int pos)
{
    if(error != L"")
        return nullNode;

    int s = (int) lex.tokens.size();
    if(pos >= s)
        pos = s-1;
        
    token& tk = lex.tokens[pos];
    int l = tk.line;
    int c = tk.column;
    int i = tk.script_index;
    
    wstlprintf(error, L"%d:%d (%d) syntax error - %ls", i,l,c, sz);
    throw new exception(error, l, c);
    return nullNode;
}

node_ptr parser::program(int start)
{
    node_ptr p = sourceElements(start, true);
    if(p == nullNode) {
        if(this->lex.tokens.size() > 0) {
            for(int i=0; i < this->lex.tokens.size(); i++) {
                token& tk = this->lex.tokens[i];
                if(tk.type != token::tlineterm)
                    return setError(L"Source elements expected",start);
            }
        }
        return nullNode;
    }
    return p;
}

node_ptr parser::sourceElements(int start, bool checkEnd)
{
    token_ptr t = getToken(&start);
    if(check(start, NotSourceElements))
        return nullNode;
        
    node_ptr p = sourceElement(start);
    if(p == nullNode) {
        t->pnot_flags |= NotSourceElements;
        return nullNode;
    }

    p->right = sourceElements(next, checkEnd);
    
    // skip trailing lineterm tokens
    getToken(&next);
        
    // 2011-11-26 shark@anui.com - we should make sure that if there are no more 
    //                             source elements, that we have exhausted all 
    //                             tokens
    if(checkEnd && p->right == nullNode && next < lex.tokens.size()) {
        return setError(L"Source element expected", next);
    }
    return p;
}

node_ptr parser::sourceElement(int start)
{
    token_ptr t = getToken(&start);
    if(check(start, NotSourceElement))
        return nullNode;
        
    node_ptr p = statement(start);
    if(p != nullNode)
        return p;

    p = funcDecl(start,false);
    if(p == nullNode) {
        t->pnot_flags |= NotSourceElement;
        return nullNode; 
    }
    return p;
}

node_ptr parser::statement(int start)
{
    token_ptr t = getToken(&start);
    if(t == nullToken)
        return nullNode;
        
    if(check(start, NotStatement))
        return nullNode;
        
    node_ptr p = new_node(nullToken);

    p->left = block(start);
    if(p->left != nullNode)         
        return p;


    p->left = varStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = emptyStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = expressionStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = ifStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = iterationStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = continueStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = breakStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = debuggerStatement(start);
    if(p->left != nullNode)
        return p;
        
    p->left = returnStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = withStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = labelledStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = switchStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = throwStatement(start);
    if(p->left != nullNode)
        return p;

    p->left = tryStatement(start);
    if(p->left != nullNode)
        return p;

    free_nodes(p);
    t->pnot_flags |= NotStatement;
    return nullNode;
}

node_ptr parser::funcDecl(int start,bool allowAnonymous)
{
    token_ptr t = getToken(&start); 
    if(t == nullToken || t->kid != token::kidFunction)
        return nullNode;

    start++;
    node_ptr n = new_node(t);

    n->left = new_node(nullToken);

    t = getToken(&start); 
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Function definition expected", start);
    }

    if(t->type == token::tidentifier)
    {
        n->left->left = new_node(t);
        start++;
    }
    else
    {
        if(!allowAnonymous)
        {
            free_nodes(n);
            return setError(L"Expected identifier",next);
        }
    }

    t = getToken(&start);
    if(t == nullToken || t->kid != token::kidLPar)
    {
        free_nodes(n);
        return setError(L"Expected (",start);
    }

    start++;
    next = start;

    n->left->right = new_node(nullToken);

    n->left->right->left = formalParamList(start);

    start = next;

    t = getToken(&start);

    if(t == nullToken || t->kid != token::kidRPar)
    {
        free_nodes(n);
        return setError(L"Expected )",start);
    }

    start++;

    t = getToken(&start);
    if(t == nullToken || t->kid != token::kidLBrace)
    {
        free_nodes(n);
        return setError(L"Expected {",start);
    }

    int temp_block = this->current_block;
    this->current_block = ++this->next_block;
    
    start++;

    t = getToken(&start);
    {
        if(t != nullToken && t->kid == token::kidRBrace)
        {
            this->current_block = temp_block;
            next = start+1;
            if(!allowAnonymous)		// handle statement case
            {
                node_ptr nf = new_node(nullToken);
                nf->left = n;
                return nf;
            }

            return n;
        }
    }

    n->left->right->right = sourceElements(start, false);
    start = next;

    this->current_block = temp_block;
    t = getToken(&start);
    if(t == nullToken || t->kid != token::kidRBrace)
    {
        free_nodes(n);
        return setError(L"Expected }",start);
    }

    next = start+1;

    if(!allowAnonymous)		// handle statement case
    {
        node_ptr nf = new_node(nullToken);
        nf->left = n;
        return nf;
    }
    return n;

}

node_ptr parser::block(int start)
{
    token_ptr tk = getToken(&start);

    if(tk == nullToken)
        return nullNode;

    if(tk->kid != token::kidLBrace)
        return nullNode;

    int temp_block = this->current_block;
    this->current_block = ++this->next_block;
    
    start++;
    node_ptr n = statementList(start);

    if(n != nullNode)
        start = next;

    tk = getToken(&start);
    
    if(tk != nullToken) {
    
        if(tk->kid == token::kidRBrace)
        {
            this->current_block = temp_block;
            next = start+1;
            // 2011-11-26 shark@anui.com - support empty statement block
            if(n != nullNode) 
                return n;
            return new_node(nullToken);
        }
    }
    
    free_nodes(n);
    return setError(L"Expected }",start);
}

bool parser::checkSemi(int* start) {
    int i = *start;
    token_ptr t = checkToken(&i, token::kidSemi);
    if(t != nullToken)
        return true;

    if(i <= 0)
        return false;
        
    // If the previous token is a line term, then treat it as a semi
    t = &lex.tokens[i - 1];
    if(t->type == token::tlineterm) {
        *start = i-1;
        return true;
    }
    
    t = checkToken(&i, token::kidRBrace);
    
    // we must decrement the start position to make sure that the parser
    // can still see the right brace token. Otherwise we get syntax errors
    if(t != nullToken) {
        *start = i - 1;
        return true;
    }
        
    return false;
}

node_ptr parser::varStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidVar);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start+1;
    n->left = varDeclList(next, false);
    if(n->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected variable declaration list",next);
    }

    if(!checkSemi(&next))    {
        free_nodes(n);
        return setError(L"Expected ;", next);
    }
    next++;
    return n; 
}

node_ptr parser::varDeclList(int start, bool notIn)
{
    node_ptr n = varDecl(start, notIn);
    if(n == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next,token::kidComma);
    if(t == nullToken) {
        return n;
    }

    next++;
    n->right = varDeclList(next,notIn);
    if(n->right == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected variable declaration",next);
    }

    return n;
}

node_ptr parser::varDecl(int start, bool notIn)
{
    token_ptr t = getToken(&start);
    if(t == nullToken || t->type != token::tidentifier)
        return nullNode;

    node_ptr n = new_node(t);
    next = start+1;
    n->left = initializer(next, notIn);
    return n;
}

node_ptr parser::initializer(int start, bool notIn)
{
    token_ptr t = checkToken(&start,token::kidEqual);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start+1;
    n->left = assignmentExpression(next, notIn);
    if(n->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected assignment expression",next);
    }

    return n;
}

node_ptr parser::emptyStatement(int start)
{
    token_ptr t = getToken(&start);

    if(t==nullToken)
        return nullNode;

    if(t->kid == token::kidSemi)
    {
        node_ptr n = new_node(t);
        next = start+1;
        return n;
    }
    return nullNode;
}
node_ptr parser::expressionStatement(int start)
{
    token_ptr t = getToken(&start);

    if(t == nullToken || t->kid == token::kidFunction || t->kid == token::kidLBrace)
        return nullNode;

    if(check(start, NotExpressionStatement))
        return nullNode;
        
    node_ptr n = expression(start, false);
    if(n == nullNode)
    {
        t->pnot_flags |= NotExpressionStatement;
        return nullNode;
    }

    t = getToken(&next);

    // Special case in expressions is a function expression
    // a = function(b,c) { .... }
    if(n->right != nullNode && n->right->token != nullToken && n->right->token->kid == token::kidFunction)
    {
        return n;
    }

    // end of file is treated as a semicolon
    if(t == nullToken)
        return n;
    
    if(!checkSemi(&next))		
    {
        free_nodes(n);
        return nullNode; // setError(t("Expected ;"),next);
    }
    next++;
    return n;
}

node_ptr parser::ifStatement(int start)
{
    token_ptr t = checkToken(&start,token::kidIf);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    start++;

    t = checkToken(&start,token::kidLPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected (",start);
    }

    n->left = new_node(nullToken);
    start++;
    next = start;

    n->left->left = expression(start, false);
    if(n->left->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected expression",start);
    }

    start = next;
    t = checkToken(&start,token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )",start);
    }

    start++;
    n->left->right = new_node(nullToken);

    n->left->right->left = statement(start);
    if(n->left->right->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected statement",start);
    }

    start = next;
    t = checkToken(&start,token::kidElse);
    if(t == nullToken)
        return n;

    n->left->right->right = new_node(t);

    start++;
    n->left->right->right->left = statement(start);
    if(n->left->right->right->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected sttement",start);
    }

    return n;

}


token_ptr parser::getToken(int* pi) {
    int i = *pi;
    token_ptr t = lex.getToken(i);
        
    while(t != nullToken && t->type == token::tlineterm) {
        i++;
        t = lex.getToken(i);
    }
    
    *pi = i;
    return t;
}


token_ptr parser::checkToken(int* pi, int k)
{
    int i = *pi;
    token_ptr t = lex.getToken(i);
        
    while(t != nullToken && t->type == token::tlineterm) {
        i++;
        t = lex.getToken(i);
    }
    
    if(t == nullToken || t->kid != k)
        return nullToken;
    
    *pi = i;
    return t; 
}

node_ptr parser::shiftExpression(int start)
{
    node_ptr n = additiveExpression(start);
    if(n == nullNode)
        return nullNode;

    token_ptr t = getToken(&next);
    if(t == nullToken)
        return n;

    if(t->kid == token::kidLShift || t->kid == token::kidRShift||
        t->kid == token::kidRShiftA)
    {
        next++;
        node_ptr n2 = new_node(t);
        n2->left = n;
        n2->right = shiftExpression(next);
        if(n2->right == nullNode)
        {
            free_nodes(n2);
            return setError(L"Expected shift expression",next);
        }
        return n2;
    }
    return n;
}

node_ptr parser::additiveExpression(int start)
{
    node_ptr m1 = multiplicativeExpression(start);

    if(m1 == nullNode)
        return nullNode;

    token_ptr t = getToken(&next);
    node_ptr op = m1;
    while(t != nullToken && (t->kid == token::kidPlus || t->kid == token::kidMinus)) {
        next++;
        op = new_node(t);
        node_ptr m2 = multiplicativeExpression(next);
        if(m2 == nullNode) {
            return setError(L"Expected multiplicative expression", start);
        }
        t = getToken(&next);
        op->left = m1;
        op->right = m2;
        m1 = op;
    }
    
    return op;
}

node_ptr parser::multiplicativeExpression(int start)
{
    node_ptr u1 = unaryExpression(start);

    if(u1 == nullNode)
        return nullNode;

    token_ptr t = getToken(&next);
    node_ptr op = u1;
    while(t != nullToken && (t->kid == token::kidMult || t->kid == token::kidDiv ||
                             t->kid == token::kidMod)) {
        next++;
        op = new_node(t);
        node_ptr u2 = unaryExpression(next);
        if(u2 == nullNode) {
            return setError(L"Expected unary expression", start);
        }
        t = getToken(&next);
        op->left = u1;
        op->right = u2;
        u1 = op;
    }
    
    return op;
}

node_ptr parser::unaryExpression(int start)
{
    node_ptr n = postfixExpression(start);

    if(n != nullNode)
        return n;

    // 2012-02-09 shark@anui.com - this used to be getToken(&next) but next might not be set properly
    token_ptr t = getToken(&start);
    if(t == nullToken) 
        return n;

    switch(t->kid)
    {
    case	token::kidDelete:
    case	token::kidVoid:
    case	token::kidTypeOf:
    case	token::kidPlusPlus:
    case	token::kidMinusMinus:
    case	token::kidPlus:
    case	token::kidMinus:
    case	token::kidCaret:
    case	token::kidNot:
    case	token::kidTilde:
        n = new_node(t);        
        next = start+1;         // this used to be next++
        n->left = unaryExpression(next);
        if(n->left == nullNode)
        {
            free_nodes(n);
            return setError(L"Expected unary expression",next);
        }
        return n;
    }
    return nullNode;
}

node_ptr parser::postfixExpression(int start)
{
    node_ptr n = leftHandSideExpression(start);
    if(n == nullNode)
        return nullNode;

    token_ptr t = getToken(&next);
    if(t == nullToken) 
        return n;

    if(t->kid == token::kidPlusPlus || t->kid == token::kidMinusMinus)
    {
        // changed 20120729 shark@anui.com - We special case postfix to produce
        // a different parse tree than prefix ++ and -- operations so that
        // at runtime we can return the value prior to change
        node_ptr n3 = new_node(nullToken);
        n3->left = n;
        node_ptr n2 = new_node(t);
        n2->left = n3;
        next++;
        return n2;
    }

    return n;
}


node_ptr parser::conditionalExpression(int start, bool notIn)
{
    token_ptr ts = getToken(&start);
    if(check(start, NotConditionalExpression))
        return nullNode;
        
    node_ptr n = logicalOrExpression(start, notIn);
    if(n == nullNode) {
        ts->pnot_flags |= NotConditionalExpression;
        return nullNode;
    }

    token_ptr t = checkToken(&next,token::kidQuestion);
    if(t == nullToken)  {
        if(n == nullNode)
            ts->pnot_flags |= NotConditionalExpression;
        return n;
    }

        
    node_ptr n2 = new_node(t);
    n2->left = n;
    n2->right = new_node(nullToken);
    next++;
    n2->right->left = assignmentExpression(next, notIn);
    if(n2->right->left == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected assignment expression",next);
    }
    t = checkToken(&next,token::kidColon);
    if(t == nullToken)
    {
        free_nodes(n2);
        return setError(L"Expected :",next);
    }
    n2->right->right = new_node(t);
    next++;
    n2->right->right->left = assignmentExpression(next, notIn);
    if(n2->right->right->left == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected assignment expression",next);
    }
    return n2;
}

node_ptr parser::logicalOrExpression(int start, bool notIn)
{
    node_ptr n = logicalAndExpression(start, notIn);
    if(n == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next,token::kidOrOr);
    if(t == nullToken) 
        return n;

    node_ptr n2 = new_node(t);
    next++;

    n2->left = n;
    n2->right = logicalOrExpression(next, notIn);
    if(n2->right == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected logical AND expression",next);
    }
    return n2;
}

node_ptr parser::logicalAndExpression(int start, bool notIn)
{
    node_ptr n = bitwiseOrExpression(start, notIn);
    if(n == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next, token::kidAndAnd);
    if(t == nullToken) 
        return n;

    node_ptr n2 = new_node(t);
    next++;
    n2->left = n;

    n2->right = logicalAndExpression(next, notIn);
    if(n2->right == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected bitwise OR expression",next);
    }
    return n2;
}

node_ptr parser::bitwiseOrExpression(int start, bool notIn)
{
    node_ptr n = bitwiseXorExpression(start, notIn);

    if(n == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next,token::kidOr);
    if(t == nullToken) 
        return n;

    next++;
    node_ptr n2 = new_node(t);
    n2->left = n;

    n2->right = bitwiseOrExpression(next, notIn);
    if(n2->right == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected bitwise XOR expression",next);
    }
    return n2;
}

node_ptr parser::bitwiseXorExpression(int start, bool notIn)
{
    node_ptr n = bitwiseAndExpression(start, notIn);
    if(n == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next,token::kidCaret);

    if(t == nullToken) 
        return n;

    next++;
    node_ptr n2 = new_node(t);
    n2->left = n;

    n2->right = bitwiseXorExpression(next, notIn);
    if(n2->right == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected bitwise AND expression",next);
    }

    return n2;
}

node_ptr parser::bitwiseAndExpression(int start, bool notIn)
{
    node_ptr n = equalityExpression(start, notIn);
    if(n == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next,token::kidAnd);
    if(t == nullToken) 
        return n;

    node_ptr n2 = new_node(t);
    next++;
    n2->left = n;
    n2->right = bitwiseAndExpression(next, notIn);
    if(n2->right == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected equality expression",next);
    }

    return n2;
}

node_ptr parser::equalityExpression(int start, bool notIn)
{
    node_ptr n = relationalExpression(start, notIn);
    if(n == nullNode)
        return n;

    token_ptr t = getToken(&next);
    if(t==nullToken) 
        return n;

    if(t->kid == token::kidEqEq || t->kid == token::kidNe || 
        t->kid == token::kidEqEqEq || t->kid == token::kidNEqEq)
    {
        node_ptr n2 = new_node(t);
        n2->left = n;
        next++;
        n2->right = relationalExpression(next, notIn);
        if(n2->right == nullNode)
        {
            free_nodes(n2);
            return setError(L"Expected relational expression",next);
        }
        return n2;
    }
    return n;
}

node_ptr parser::relationalExpression(int start, bool notIn)
{
    node_ptr n = shiftExpression(start);
    if(n == nullNode)
        return nullNode;

    token_ptr t = getToken(&next);
    if(t == nullToken) 
        return n;

    switch(t->kid)
    {
    case	token::kidIn:
        if(notIn)
            break;
    case	token::kidLt:
    case	token::kidGt:
    case	token::kidLe:
    case	token::kidGe:
    case	token::kidInstanceOf:
        node_ptr n2 = new_node(t);
        n2->left = n;
        next++;
        n2->right = shiftExpression(next);
        if(n2->right == nullNode)
        {
            free_nodes(n2);
            return setError(L"Expected shift expression",next);
        }
        return n2;
    }
    return n;
}

node_ptr parser::leftHandSideExpression(int start)
{
//    node_ptr n = newExpression(start);
//    if(n != nullNode) {
//        return n;
//    }

    token_ptr t = getToken(&start);
    if(check(start, NotLeftHandSideExpression))
        return nullNode;
        
    node_ptr n = callExpression(start);
    if(n != nullNode)
        return n;
        
    n = memberExpression(start);
    if(n == nullNode)
        t->pnot_flags |= NotLeftHandSideExpression;
    return n;
}



node_ptr parser::arguments(int start)
{
    node_ptr n;
    token_ptr t = checkToken(&start,token::kidLPar);
    if(t == nullToken)
        return nullNode;

    n = new_node(t);
    next = start+1;
    t = checkToken(&next, token::kidRPar);
    if(t != nullToken)
    {
        next++;
        return n;
    }

    node_ptr n2 = argumentList(next);
    t = checkToken(&next, token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        free_nodes(n2);
        return setError(L"Expected )",next);
    }
    next++;
    n->right = n2;
    return n;
}

node_ptr parser::argumentList(int start)
{
    // change mohsen@mobicore.com 20060925 allow for member
    // and call expressions as arguments not to trash right node
    node_ptr na = new_node(nullToken);

    node_ptr n = assignmentExpression(start,false);
    if(n == nullNode)
    {
        return n;
    }

    na->left = n;

    token_ptr t = checkToken(&next, token::kidComma);
    if(t != nullToken)
    {
        next++;
        na->right = argumentList(next);
        if(na->right == nullNode)
        {
            free_nodes(na);
            return setError(L"Expected assignment expression",next);
        }
    }
    return na;
}

node_ptr parser::primaryExpression(int start)
{
    token_ptr t = getToken(&start);

    if(t == nullToken)
        return nullNode;

    if(check(start, NotPrimaryExpression))
        return nullNode;
        
    if(t->kid == token::kidThis || t->kid == token::kidTrue || t->kid == token::kidFalse 
        || t->kid == token::kidNull || t->kid == token::kidUndefined)
    {
        next = start+1;
        return new_node(t);
    }

    if(t->type == token::tidentifier)
    {
        next = start+1;
        return new_node(t);
    }

    if(t->type == token::tnumlit || t->type == token::tstrlit || t->type == token::tregex)
    {
        next = start + 1;
        return new_node(t);
    }

    node_ptr n = arrayLiteral(start);
    if(n != nullNode)
        return n;

    n = objectLiteral(start);
    if(n != nullNode)
        return n;

    t = checkToken(&start,token::kidLPar);
    if(t == nullToken)
        return nullNode;

    next = start + 1;
    n = expression(next, false);
    if(n == nullNode)
        return setError(L"Expected expression",next);

    t = checkToken(&next,token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )",next);
    }

    next++;
    if(n == nullNode)
        t->pnot_flags |= NotPrimaryExpression;
        
    return n;
}


node_ptr parser::arrayLiteral(int start)
{
    token_ptr t = checkToken(&start,token::kidLBracket);

    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);

    next = start + 1;
    
    t = checkToken(&next, token::kidRBracket);
    if(t != nullToken) {
        next++;
        n->token = t;
        return n;
    }
    
    n->left = elementList(next);

    
    if(n->left != nullNode)
    {
        t = checkToken(&next,token::kidRBracket);
        if(t != nullToken)
        {
            next++;
            n->token = t;			// array literals have ']' token
            return n;
        }

        next++;
        t = checkToken(&next,token::kidComma);
        if(t == nullToken)
        {
            free_nodes(n);
            return setError(L"Expected ,",next);
        }

        next++;
        n->right = elision(next);
        return n;
    }
    
    n->left = elision(next);
    next++;
    t = checkToken(&next,token::kidRBracket);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected ]",next);
    }

    next++;
    return n;
}

node_ptr parser::elision(int start)
{
    token_ptr t = checkToken(&start,token::kidComma);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next++;

    n->right = elision(next);
    return n;
}

obj_type::obj_type(context& ec) {
    context* root = ec.root;
    
    if(!root->types) {
        root->types = new vector<obj_type*>;
    }
    
    root->types->push_back(this);
}

obj_type::~obj_type() {

}

void obj_type::add_method(const jschar *name, method m) {
    wstring s = name;
    methods[s] = m;
}

node_ptr parser::objectLiteral(int start)
{
    token_ptr t = checkToken(&start,token::kidLBrace);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start+1;
    n->right = propNameValueList(next);

    t = checkToken(&next,token::kidRBrace);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected }",next);
    }
    next++;
    return n;
}

node_ptr parser::propNameValueList(int start)
{
    node_ptr n = propertyName(start);
    if(n == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next,token::kidColon);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected :",next);
    }

    node_ptr n2 = new_node(t);
    n2->left = n;
    next++;

    n2->left->right = assignmentExpression(next, false);
    if(n2->left->right == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected assignment expression",next);
    }

    t = checkToken(&next, token::kidComma);
    if(t != nullToken)
    {
        next++;
        n2->right = propNameValueList(next);
        if(n2->right == nullNode)
        {
            free_nodes(n2);
            return setError(L"Expected property name and value list",next);
        }
    }
    return n2;
}


node_ptr parser::propertyName(int start)
{
    token_ptr t = getToken(&start);
    
    // 2011-11-16 shark@anui.com - need to special case var o = {}; supoprt emptry list
    if(t == nullToken || t->kid == token::kidRBrace)
        return nullNode;

    if(t->type == token::tidentifier || t->type == token::tstrlit 
        || t->type == token::tnumlit)
    {
        next = start + 1;
        return new_node(t);
    }
    return setError(L"Expected identifier or string or number literal",next);
}


bool isUnaryNode(node_ptr n);
bool isUnaryNode(node_ptr n) {
    if(n->token == nullToken)
        return false;
    if(n->token->type != token::tkeyword)
        return false;
    if(n->token->kid == token::kidMinus || n->token->kid == token::kidPlus)
        return true;
    return false;
}

node_ptr parser::elementList(int start)
{
    node_ptr n = elision(start);

    node_ptr n2 = assignmentExpression(next, false);

    if(n2 == nullNode) {
        free_nodes(n);
        return nullNode;
    }

    // 2012-01-27 shark@anui.com. The logic below was only being applied if the element 
    // had more elements. This was causing expressions like [ 1, {x:2, y:3} ] to generate
    // incorrect parsing where as [{x:2,y:3}, 1] was generating the proper parse trees
    // we move the handling of complex expressions here to be independent of whether
    // additional elements follow or not
    
    // 2012-01-22 shark@anui.com if the element was a complex expression, we are trashing the
    // right pointer here. We fix by adding an extra null token node
    if(n2->right != nullNode) {
        node_ptr n3 = new_node(nullToken);
        n3->left = n2;
        n2 = n3;
    }

    // 2012-04-06 shark@anui.com we need to account for unargy expressions here also. they are
    // complex expressions that don't have a right hand pointer
    if(isUnaryNode(n2)) {
        node_ptr n3 = new_node(nullToken);
        n3->left = n2;
        n2 = n3;
    }
    
    // 2011-12-09 shark@anui.com - the expression below used to be: n2->left = n
    // this was trashing the left handside of any assignment expression 
    // and was making nested array literals (and who knows what else) fail
    // silently. todo: invesitgate what the proper usage of elision in this
    // context but for now, only override left if elision is non-null
    if(n != nullNode)
        n2->left = n;

    
    token_ptr t = checkToken(&next,token::kidComma);
    if(t != nullToken)
    {
        // 2011-12-09 shark@anui.com handle terminate bracket here. If allowed, we terminate if 
        // next is a right bracket
        next++;
        if(checkToken(&next, token::kidRBracket)) {
            return n2;
        }
        
        n2->right = elementList(next);
        if(n2->right == nullNode)
        {
            free_nodes(n2);
            return setError(L"Expected element list",next);
        }
    }
    return n2;
}

node_ptr parser::memberExpression2(node_ptr l, int start) {
    token_ptr t;
    
    t = getToken(&start);
    if(t == nullToken || t->type != token::tkeyword) {
        return l;
    }
    
    if(t->kid == token::kidDot) {
        node_ptr n2 = new_node(t);
        next++;
        n2->left = l;
        t = getToken(&next);
        if(t->type != token::tidentifier) {
            free_nodes(n2);
            return setError(L"identifier expected", next);
        }
        next++;
        n2->right = new_node(t);
        return memberExpression2(n2, next);
    }
    
    if(t->kid == token::kidLBracket) {
        node_ptr n2 = new_node(t);
        next++;
        n2->left = l;
        n2->right = expression(next, true);
        if(n2->right == nullNode) {
            free_nodes(n2);
            return setError(L"array index expression expected", next);
        }
        t = checkToken(&next, token::kidRBracket);
        if(t == nullToken) {
            free_nodes(n2);
            return setError(L"']' expected", next);
        }
        next++;
        return memberExpression2(n2, next);
    }
    return l;
}

node_ptr parser::memberExpression(int start) {
    token_ptr t = checkToken(&start, token::kidNew);
    if(t != nullToken) {
        next = start+1;
        node_ptr n = new_node(t);
        node_ptr n2 = memberExpression(next);
        if(n2 == nullNode) {
            free_nodes(n);
            return setError(L"new memberExpression expected", next);
        }
        node_ptr n3 = arguments(next);
        if(n3 == nullNode) {
            // 2012-02-27 shark@anui.com - we don't error here to support a = new function() { body }
            n->left = n2;
            return n;
//            free_nodes(n);
//            free_nodes(n2);
//            return setError(L"new memberExpression(args) expected", next);
        }
        
        n3->left = n2;
        n->left = n3;
        return memberExpression2(n, next);
    }
    
    node_ptr n = functionExpression(start);
    
    // 2012-02-27 shark@anui.com - support x = new function() { body }
    if(n != nullNode)
        return n;
        
    if(n == nullNode) 
        n = primaryExpression(start);
    if(n == nullNode)
        return nullNode;
    
    return memberExpression2(n, next);
}    

node_ptr parser::newExpression(int start)
{
    token_ptr t = getToken(&start);
    if(t == nullToken || t->type != token::tkeyword || t->kid != token::kidNew)
        return nullNode;

    node_ptr n = new_node(t);
//    n = new_node(t);
    next = start+1;
    n->left = memberExpression(next);
    if(n->left == nullNode) {
        free_nodes(n);
        return setError(L"new <expression> expected", next);
    }
    return memberExpression2(n, next);
}


node_ptr parser::callExpression2(node_ptr l, int start) {
    token_ptr t = getToken(&start);
    if(t == nullToken || t->type != token::tkeyword)
        return l;
    
    node_ptr n = arguments(next);
    if(n != nullNode) {
        n->left = l;
        return callExpression2(n, next);
    }
    
    if(t->kid == token::kidLBracket) {
        next = start+1;
        node_ptr n = new_node(t);
        n->left = l;
        n->right = expression(next, true);
        if(n->right == nullNode) {
            free_nodes(n);
            return setError(L"expression expected", next);
        }
        
        t = checkToken(&next, token::kidRBracket);
        if(t == nullToken) {
            free_nodes(n);
            return setError(L"']' expected", next);
        }
        next++;
        return callExpression2(n, next);
    }
    if(t->kid == token::kidDot) {
        next = start+1;
        node_ptr n = new_node(t);
        n->left = l;
        t = getToken(&next);
        if(t->type != token::tidentifier) {
            free_nodes(n);
            return setError(L"identifier expected", next);
        }
        
        next++;
        // 2012-03-21 shark@anui.com issue #282 - right hand side of dot was not being set here
        n->right = new_node(t);
        return callExpression2(n, next);        
    }
    return l;
}

node_ptr parser::callExpression(int start)
{
    token_ptr t = getToken(&start);
    
    if(check(start, NotCallExpression))
        return nullNode;
        
    node_ptr n = memberExpression(start);
    if(n == nullNode) {
        t->pnot_flags |= NotCallExpression;
        return nullNode;
    }
    
    node_ptr n2 = arguments(next);
    if(n2 == nullNode) {
        free_nodes(n);
        t->pnot_flags |= NotCallExpression;
        return nullNode;
    }
    
    n2->left = n;

    return callExpression2(n2, next);
}

node_ptr parser::functionExpression(int start)
{
    return funcDecl(start,true);
}

node_ptr parser::expression(int start, bool notIn)
{
    token_ptr t = getToken(&start);
    if(check(start, NotExpression))
        return nullNode;
        
    node_ptr n = assignmentExpression(start, notIn);

    if(n != nullNode)
    {
        while(true) {
            token_ptr t = checkToken(&next,token::kidComma);
            if(t == nullToken)
                break;
                
            // 2012-02-08 shark@anui.com - n already has a left and right members
            // we need to introduce a new model for how to deal with composite expressions
            node_ptr n2 = new_node(nullToken);
            n2->left = n;
            n2->right = assignmentExpression(next+1, notIn);
            if(n2->right == nullNode)
            {
                free_nodes(n2);
                return setError(L"Expected assignment expression",next);
            }
            n = n2;
        }
        return n;
    }
    
    t->pnot_flags |= NotExpression;
    return nullNode;
}

node_ptr parser::assignmentExpression(int start, bool notIn)
{
    node_ptr n;
    token_ptr t = getToken(&start);
    
    if(check(start, NotAssignmentExpression))
        return nullNode;

    node_ptr nl = leftHandSideExpression(start);
    if(nl == nullNode)
    {
        n = conditionalExpression(start, notIn);
        if(n == nullNode)
            t->pnot_flags |= NotAssignmentExpression;
        return n;
        
    }

    n = assignmentOperator(next);
    if(n == nullNode)
    {
        free_nodes(nl);
        n = conditionalExpression(start, notIn);
        if(n == nullNode)
            t->pnot_flags |= NotAssignmentExpression;
        return n;
    }

    n->left = nl;

    node_ptr nr = assignmentExpression(next, notIn);
    if(nr == nullNode)
    {
        free_nodes(n);
        n = conditionalExpression(start, notIn);
        if(n == nullNode)
            t->pnot_flags |= NotAssignmentExpression;
        return n;
    }

    n->right = nr;

    return n;
}

node_ptr parser::assignmentOperator(int start)
{
    token_ptr t = getToken(&start);
    if(t==nullToken)
        return nullNode;

    switch(t->kid)
    {
    case	token::kidEqual:
    case	token::kidMultEq:
    case	token::kidDivEq:
    case	token::kidModEq:
    case	token::kidPlusEq:
    case	token::kidMinusEq:
    case	token::kidLShiftEq:
    case	token::kidRShiftEq:
    case	token::kidRShiftAEq:
    case	token::kidAndEq:
    case	token::kidCaretEq:
    case	token::kidOrEq:
        next = start + 1;
        return new_node(t);
    }

    return nullNode;
}


node_ptr parser::iterationStatement(int start)
{
    node_ptr n = doStatement(start);
    if(n != nullNode)
        return n;

    n = whileStatement(start);
    if(n != nullNode)
        return n;

    n = forStatement(start);
    return n;
}

node_ptr parser::doStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidDo);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start + 1;
    n->left = new_node(nullToken);
    n->left->left = statement(next);
    if(n->left->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected statement", next);
    }

    t = checkToken(&next, token::kidWhile);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected while",next);
    }

    n->left->right = new_node(t);

    next++;
    t = checkToken(&next, token::kidLPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected (",next);
    }


    next++;
    n->left->right->left = expression(next, false);
    if(n->left->right->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected expression", next);
    }

    t = checkToken(&next, token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )",next);
    }

    next++;
    if(!checkSemi(&next))    {
        free_nodes(n);
        return setError(L"Expected ;", next);
    }
    next++;
    return n;
}

node_ptr parser::whileStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidWhile);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start + 1;

    t = checkToken(&next, token::kidLPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected (",next);
    }

    next++;

    n->left = new_node(nullToken);
    n->left->left = expression(next, false);
    if(n->left->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected expression", next);
    }
    t = checkToken(&next, token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )", next);
    }

    next++;
    n->left->right = statement(next);
    if(n->left->right == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected statement", next);
    }
    return n;
}

node_ptr parser::forStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidFor);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start + 1;
    n->left = new_node(nullToken);

    t = checkToken(&next, token::kidLPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected (", next);
    }

    next++;
    int i = next;
    n->left->left = forVarIn(i);
    if(n->left->left == nullNode)
        n->left->left = forIn(i);
    if(n->left->left == nullNode)
        n->left->left = forVar(i);
    if(n->left->left == nullNode)
        n->left->left = forSimple(i);
    if(n->left->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected for expression", next);
    }

    t = checkToken(&next, token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )", next);
    }

    next++;
    n->left->right = statement(next);
    if(n->left->right == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected statement", next);
    }

    return n;
}

node_ptr parser::forIn(int start)
{
    node_ptr nl = leftHandSideExpression(start);
    if(nl == nullNode)
        return nullNode;

    token_ptr t = checkToken(&next, token::kidIn);
    if(t == nullToken)
    {
        free_nodes(nl);
        return nullNode;
    }

    node_ptr n = new_node(t);
    n->left = nl;
    next++;
    n->right = expression(next, false);
    if(n->right == nullNode)
    {
        free_nodes(n);
        return nullNode;
    }

    return n;
}

node_ptr parser::forVar(int start)
{
    token_ptr t = checkToken(&start, token::kidVar);
    if(t == nullToken)
        return nullNode;

    node_ptr n3 = new_node(t);
    next = start + 1;

    n3->left = varDeclList(next, true);
    if(n3->left == nullNode)
    {
        free_nodes(n3);
        return nullNode;
    }

    t = checkToken(&next, token::kidSemi);
    if(t == nullToken)
    {
        free_nodes(n3);
        return nullNode;
    }


    node_ptr n2 = new_node(t); 
    next++;
    n2->right = expression(next, false);

    t = checkToken(&next, token::kidSemi);
    if(t == nullToken)
    {
        free_nodes(n3);
        free_nodes(n2);
        return nullNode;
    }

    node_ptr n1 = new_node(t);
    next++;
    n1->right = expression(next, false);

    node_ptr n = new_node(nullToken);
    n->left = n1;
    n1->left = n2;
    n2->left = n3;
    return n;

}

node_ptr parser::forSimple(int start)
{
    node_ptr n3 = new_node(nullToken);
    n3->right = expression(start, true);

    token_ptr t = checkToken(&next, token::kidSemi);
    if(t == nullToken)
    {
        free_nodes(n3);
        return nullNode;
    }

    next++;
    node_ptr n2 = new_node(t);
    n2->right = expression(next, false);
    t = checkToken(&next, token::kidSemi);
    if(t == nullToken)
    {
        free_nodes(n3);
        free_nodes(n2);
        return nullNode;
    }

    next++;
    node_ptr n1 = new_node(t);
    n1->right = expression(next, false);

    node_ptr n = new_node(nullToken);
    n->left = n1;
    n1->left = n2;
    n2->left = n3;
    return n;
}


node_ptr parser::forVarIn(int start)
{
    token_ptr t = checkToken(&start, token::kidVar);
    if(t == nullToken)
        return nullNode;

    node_ptr nv = new_node(t);

    next++;

    node_ptr nl = varDeclList(next, true);
    if(nl == nullNode)
    {
        free_nodes(nv);
        free_nodes(nl);
        return nullNode;
    }

    nv->right = nl;

    t = checkToken(&next, token::kidIn);
    if(t == nullToken)
    {
        free_nodes(nv);
        return nullNode;
    }

    next++;
    node_ptr ni = new_node(t);
    ni->left = nv;

    ni->right = expression(next, false);
    if(ni->right == nullNode)
    {
        free_nodes(ni);
        return nullNode;
    }

    return ni;
}


node_ptr parser::labelledStatement(int start)
{
    token_ptr t = getToken(&start);
    if(t == nullToken || t->type != token::tidentifier)
        return nullNode;

    node_ptr n = new_node(t);
    int s2 = start+1;
    t = checkToken(&s2, token::kidColon);
    if(t == nullToken)
    {
        free_nodes(n);
        return nullNode;
    }
    node_ptr n2 = new_node(nullToken);
    n2->left = new_node(t);
    n2->left->left = n;
    next = s2 + 2;
    n2->left->right = statement(next);
    if(n2->left->right == nullNode)
    {
        free_nodes(n2);
        return setError(L"Expected statement",next);
    }
    return n2;
}

node_ptr parser::continueStatement(int start)
{
    return contBreakStatement(start, token::kidContinue);
}

node_ptr parser::contBreakStatement(int start,int k)
{
    token_ptr t = getToken(&start);
    if(t == nullToken || t->kid != k)
        return nullNode;

    start++;
    node_ptr n =  new_node(t);

    t = getToken(&start);
    if(t != nullToken && t->type == token::tidentifier)
    {
        n->left = new_node(t);
        start++;
    }

    t = getToken(&start);
    if(t == nullToken || t->kid != token::kidSemi)
    {
        free_nodes(n);
        return setError(L"Expected ;",start);
    }

    next = start+1;
    return n;
}
node_ptr parser::breakStatement(int start)
{
    return contBreakStatement(start, token::kidBreak);
}

node_ptr parser::debuggerStatement(int start) {
    token_ptr t = getToken(&start);
    if(t == nullToken || t->kid != token::kidDebugger) 
        return nullNode;
    node_ptr n = new_node(t);
    next = start+1;
    if(checkSemi(&next)) {
        next++;
        return n;
    }
    
    free_nodes(n);
    return nullNode;
}

node_ptr parser::returnStatement(int start)
{
    token_ptr t = checkToken(&start,token::kidReturn);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    start++;
    next = start;

    n->left = expression(start, false);

    start = next;
    if(checkSemi(&next)) {
        next++;
        return n;
    }
//    t = checkToken(&start,token::kidSemi);
//    if(t != nullToken)
//    {
//        next = start+1;
//        return n;
//    }
    
    free_nodes(n);
    return nullNode;
}

node_ptr parser::withStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidWith);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start + 1;
    t = checkToken(&next, token::kidLPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected (",next);
    }

    next++;
    n->left = new_node(nullToken);
    n->left->left = expression(next, false);
    if(n->left->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected expression",next);
    }

    t = checkToken(&next, token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )",next);
    }

    next++;
    n->left->right = statement(next);
    if(n->left->right == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected expression",next);
    }

    return n;
}

node_ptr parser::throwStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidThrow);
    if(t == nullToken)
        return nullNode;

    next = start+1;
    node_ptr n = new_node(t);
    n->left = expression(next, false);
    t = checkToken(&next, token::kidSemi);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected ;",next);
    }

    next++;
    return n;
}

node_ptr parser::tryStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidTry);
    if(t == nullToken)
        return nullNode;

    next = start + 1;
    node_ptr n = new_node(t);

    n->left = new_node(nullToken);
    n->left->right = block(next);

    if(n->left->right == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected block",next);
    }

    n->left->left = new_node(nullToken);
    n->left->left->left = catchBlock(next);
    n->left->left->right = finallyBlock(next);

    return n;
}

node_ptr parser::catchBlock(int start)
{
    token_ptr t = checkToken(&start, token::kidCatch);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start + 1;

    t = checkToken(&next, token::kidLPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected (",next);
    }

    next++;
    t = getToken(&next);
    if(t == nullToken || t->type != token::tidentifier)
    {
        free_nodes(n);
        return setError(L"Expected identifier",next);
    }

    n->left = new_node(t);
    next++;
    t = checkToken(&next, token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )",next);
    }
    next++;
    n->right = block(next);
    if(n->right == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected block",next);
    }

    return n;
}


node_ptr parser::finallyBlock(int start)
{
    token_ptr t = checkToken(&start, token::kidFinally);
    if(t == nullToken)
        return nullNode;

    next = start+1;
    node_ptr n = new_node(t);
    n->left = block(next);
    if(n->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected block",next);
    }
    return n;
}

node_ptr parser::switchStatement(int start)
{
    token_ptr t = checkToken(&start, token::kidSwitch);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start+1;

    t = checkToken(&next, token::kidLPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected (",next);
    }
    next++;

    n->left = new_node(nullToken);
    n->left->left = expression(next, false);
    if(n->left->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected expression", next);
    }
    t = checkToken(&next, token::kidRPar);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected )",next);
    }

    next++;
    n->left->right = caseBlock(next);
    if(n->left->right == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected case block",next);
    }

    return n;
}

node_ptr parser::caseBlock(int start)
{
    token_ptr t = checkToken(&start, token::kidLBrace);
    if(t == nullToken)
    {
        return setError(L"Expected {",start);
    }

    next = start + 1;
    t = checkToken(&next, token::kidRBrace);
    if(t != nullToken)
    {
        next++;
        return new_node(nullToken);		// empty block
    }

    node_ptr n = caseClauses(next);
    if(n == nullNode)
    {
        return setError(L"Expected case or default",next);
    }

    t = checkToken(&next, token::kidRBrace);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected }",next);
    }
    next++;
    return n;
}

node_ptr parser::caseClauses(int start)
{
    node_ptr n = caseDefClause(start);
    if(n == nullNode)
        return nullNode;
    n->right = caseClauses(next);
    return n;
}

node_ptr parser::caseDefClause(int start)
{
    node_ptr n = caseClause(start);
    if(n != nullNode)
        return n;

    n = defaultClause(start);
    return n;
}

node_ptr parser::caseClause(int start)
{
    token_ptr t = checkToken(&start, token::kidCase);
    if(t == nullToken)
        return nullNode;

    next = start+1;
    node_ptr n = new_node(t);
    n->left = new_node(nullToken);
    n->left->left = expression(next, false);
    if(n->left->left == nullNode)
    {
        free_nodes(n);
        return setError(L"Expected expression", next);
    }

    t = checkToken(&next, token::kidColon);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected :", next);
    }

    next++;
    n->left->right = statementList(next);
    return n;
}


node_ptr parser::defaultClause(int start)
{
    token_ptr t = checkToken(&start, token::kidDefault);
    if(t == nullToken)
        return nullNode;

    node_ptr n = new_node(t);
    next = start + 1;
    t = checkToken(&next, token::kidColon);
    if(t == nullToken)
    {
        free_nodes(n);
        return setError(L"Expected :", next);
    }
    next++;
    n->left = statementList(next);
    return n;

}

node_ptr parser::formalParamList(int start)
{
    token_ptr t = getToken(&start);

    if(t == nullToken || t->type != token::tidentifier)
    {
        next = start;
        return nullNode;
    }

    node_ptr n = new_node(t);
    start++;
    next = start;

    t = getToken(&start);

    if(t == nullToken)
    {
        free_nodes(n);
        setError(L"Expected , or )",start);
        return nullNode;
    }

    if(t->kid == token::kidRPar)
    {
        return n;
    }

    if(t->kid != token::kidComma)
    {
        free_nodes(n);
        setError(L"Expected ,",start);
        return nullNode;
    }
    start++;
    n->left = formalParamList(start);
    return n;
}

node_ptr parser::statementList(int start)
{
    node_ptr n = statement(start);

    if(n != nullNode)
    {
        n->right = statementList(next);
    }

    return n;
}


node_ptr parser::jsonObjectLiteral(int start) {
    token_ptr t = getToken(&start);
    if(t->kid != token::kidLBrace) 
        return nullNode;
        
    node_ptr n = new_node(t);
    next = start+1;
    // 2013-03-02 shark@anui.com - ({)->right should be left in case this is
    // placed in an array or another object value. Instead use ({)->left
    // to store the list
    n->left = jsonPropNameValueList(next);
    t = getToken(&next);
    if(t->kid != token::kidRBrace) {
        free_nodes(n);
        return nullNode;
    }
    next++;    
    return n;
}

node_ptr parser::jsonPropNameValueList(int start) {
    node_ptr np = propertyName(start);

    if(np == nullNode)
        return nullNode;

    token_ptr t = getToken(&next);

    if(t->kid != token::kidColon) {
        free_nodes(np);
        return nullNode;
    }

    next++;
    node_ptr nc = new_node(t);
    nc->left = np;
    
    // 2013-03-02 shark@anui.com we used to use (:)->left = name and (:)->right = value
    // this is incorrect and caused multiple property values to trash each other
    // Instead, we should match what the JS parser does and use (:)->left = name and
    // (:)->left->right = value
    // np->right = jsonValue(next);
    nc->left->right = jsonValue(next);

    node_ptr nr = nc;
    while(next < lex.max) {            
        t = getToken(&next);
        if(t == nullToken || t->kid != token::kidComma) 
            break;
        
        next++;

        node_ptr np2 = propertyName(next);

        if(np2 == nullNode)
            break;

        t = getToken(&next);

        if(t->kid != token::kidColon) 
            break;

        next++;
        node_ptr nc2 = new_node(t);
        nc2->left = np2;
        np2->right = jsonValue(next);
        nr->right = nc2;
        nr = nc2;
    }
    return nc;
}

node_ptr parser::jsonValue(int start) {
    token_ptr t = getToken(&start);

    if(t == nullToken)
        return nullNode;

    if(t->type == token::tkeyword && (
        t->kid == token::kidTrue || t->kid == token::kidFalse
        || t->kid == token::kidNull || t->kid == token::kidUndefined) ) {
        next = start+1;
        return new_node(t);
    }

    if(t->type == token::tidentifier)
    {
        next = start+1;
        return new_node(t);
    }

    // 2013-03-03 shark@anui.com support null values
    if(t->type == token::tkeyword && t->kid == token::kidNull) {
        next = start + 1;
        return new_node(t);
    }
    if(t->type == token::tnumlit || t->type == token::tstrlit)
    {
        next = start + 1;
        return new_node(t);
    }

    node_ptr n = jsonArrayLiteral(start);
    if(n != nullNode)
        return n;

    n = jsonObjectLiteral(start);
    if(n != nullNode)
        return n;

    return nullNode;
}

node_ptr parser::jsonArrayLiteral(int start) {
    token_ptr t = getToken(&start);
    
    if(t == nullToken)
        return nullNode;
        
    node_ptr n = new_node(t);
    if(t->kid != token::kidLBracket) {
        free_nodes(n);
        return nullNode;
    }
        
    next = start + 1;
    t = getToken(&next);
    if(t->type == token::tkeyword && t->kid == token::kidRBracket) {
        next++;
        return n;
    }
    
    node_ptr v = jsonValue(next);
    if(v == nullNode) {
        free_nodes(n);
        return nullNode;
    }
    
    n->left = v;
    
    while(next < lex.max) {
        t = getToken(&next);
        if(t->kid != token::kidComma)
            break;
        
        next++;
        node_ptr vr = jsonValue(next);
        if(vr == nullNode)
            break;
        v->right = vr;
        v = vr;
    }
    
    t = getToken(&next);
    if(t->kid != token::kidRBracket) {
        free_nodes(n);
        return nullNode;
    }
    
    next++;
    return n;
}

void parser::test()
{
    tokenizer l;

    l.tokenize(L"x=2+3;");
    parser p(l);
    node_ptr n = p.parse();

    assert(n->token == nullToken);
    assert(n->left->token->kid == token::kidEqual);
    assert(*(n->left->left->token->sval) == L"x");
    assert(n->left->right->token->kid == token::kidPlus);
    assert(n->left->right->left->token->nval== 2.0);
    assert(n->left->right->right->token->nval== 3.0);


}
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// execution context clas
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void context::attach(debugger* dbg) 
{
    if(dbg)
        dbg->suspend_exit_check = false;
    this->root->attached_debugger = dbg;
}

context::context() {
    attached_debugger = NULL;
    lastToken = NULL;
    vars = nullObj;
    error = L"";
    func_args = NULL;
    func = NULL;
    has_error = false;
    root = NULL;
    call_depth = 0;
    types = NULL;
    
#ifdef JS_OPT
    optimization = 0;
#endif
}

context::context(context* par, obj_ptr pvars) 		// non-root context
{
    attached_debugger = NULL;
    lastToken = NULL;
    vars = pvars;
    parent = par;
    error = L"";
    func_args = NULL;
    types = NULL;
    func = NULL;
#ifdef JS_OPT
    optimization = 0;
#endif
    
    
    has_error = false;
    if(par == NULL) {
        root = this;
        call_depth = 0;
    }
    else {
        root = par->root;
        call_depth = par->call_depth + 1;
    }
}


#ifdef JS_OPT
context::~context() {
    if(types) {
        for(vector<obj_type*>::iterator i = types->begin(); i != types->end(); i++) {
            obj_type* t = *i;
            delete t;
        }
        delete types;
        Object_prototype = nullObj;
    }
}
#endif

obj_ptr context::toObject(var& v)
{
    int t = v.vt;

    switch(t)
    {
    case	var::tstr:
        {
            return obj_ptr(new string_object(v));
        }

    case	var::tobj:
        {
            return v.obj();
        }

    case	var::tnum:
        {
            number_object* n = new number_object(v.nval);
            obj_ptr op(n);
            return op;
        }
        break;
    case	var::tbool:
        {
            boolean_object* n = new boolean_object(v.bval);
            obj_ptr op(n);
            return op;
        }
        break;

    case	var::tnull:
    case	var::tundef:
        {
            return obj_ptr(new object());
        }
    }
    return nullObj;
}

bool context::isroot()
{
    if(root == this)
        return true;
    else
        return false;
}

bool context::isIdent(var& v1, var& v2)
{
    int t1 = v1.vt;
    int t2 = v2.vt;

    if(t1 != t2)
        return false;

    switch(t1)
    {

    case	var::tundef:
    case	var::tnull:
        return true;

    case	var::tnum:
        return(v1.num() == v2.num());

    case	var::tstr:
        return (*(v1.sval) == *(v2.sval));

    case	var::tobj:
        return (v1.obj() == v2.obj());

    case	var::tbool:
        return (v1.bval == v2.bval);
    }

    return false;
}

void context::setclass(obj_ptr o, char_ptr c)
{
    prop* p = root->vars->getprop(c,false);
    obj_ptr op = p->value.obj();
    var vop(op);
    o->put(L"prototype", vop);
}

var context::execCatch(context& ec, node_ptr n, var& e)
{
    // @todo: add to scope chain a new object
    // obj_ptr o = new xobject()
    // p = o->addProp(n->left)...
    prop* p = ref(ec, n->left,true);
    if(!p)
        return var();
    
    p->value = e; 
    var v = run(ec, n->right);
    
    // 2012-03-13 shark@anui.com - ensure that running catch() allows execution to continue
    ec.has_error = false;           // clear error flag to resume execution
    
    // @todo: remove object from scope chain
    return v;
}

bool check_call_direct(context& ec, node_ptr n, obj_ptr othis, var& ret)
{
    obj_ptr direct_obj;
    char_ptr direct_method;
    method fast = NULL;

    if(n->left->token == nullToken)
        return false;

    // handle direct constructor calls
    if(othis != nullObj)
    {
        if(n->left->token->type != token::tidentifier)
            return false;
        prop* p = ec.ref(ec,n->left,false);
        if(p == NULL || p->value.vt != var::tobj)
            return false;
        if(!p->value.oval->can_call_direct())
            return false;
        direct_obj = p->value.oval;
        direct_method = L"";
    }
    else if(n->left->token->kid == token::kidDot)
    {
        if(othis != nullObj)
            return false;

        if(n->left->right == nullNode)
            return false;
        
        if(n->left->right->token->type != token::tidentifier)
            return false;


        prop* p = ec.ref(ec,n->left->left,false); 
        // TODO: Need to make sure this optimizes since we can't use the above code for fluent calls!!!
        if(p != NULL)
            direct_obj  = ec.toObject(p->value);
        if(direct_obj == nullObj)
            return false;

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // 2012-06-22 shark@anui.com - fast native call support
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        object* dobj = direct_obj.get();
        if(dobj->type != NULL) {
            // we have already patched this node, make the fast call
            if(n->left->right->match_type == direct_obj->type) {
                fast = n->left->right->func_ptr;
            }
            else {            
                // Look for the method in the type and patch the node if found
                map<wstring, method>& ms = direct_obj->type->methods;
                map<wstring,method>::iterator i = ms.find(*n->left->right->token->sval);
                if(i != ms.end())
                {
                    fast = (*i).second;
                    n->left->right->match_type = direct_obj->type;
                    n->left->right->func_ptr = fast;
                    n->left->right->type = node::MethodPtr;
                }
            }
        }        
        
        if(!fast) {
            direct_method = n->left->right->token->sval->c_str();

            if(!direct_obj->can_call_direct())
                return false;
        }
    }
    else
    {
        if(n->left->token->type != token::tidentifier)
            return false;
        prop* p = ec.ref(ec,n->left,false);
        if(p != NULL && p->value.vt == var::tobj)
        direct_obj  = p->value.oval;
        direct_method = L"";
        if(direct_obj == nullObj || !direct_obj->can_call_direct())
            return false;
    }

    int ac = 0;
    node_ptr n2 = n->right;		// below change 20060925 to support new argument node layout
    while(n2 != nullNode && (n2->token != nullToken || n2->left != nullNode))	// to handle empty call
    {
        n2 = n2->right;
        ac++;
    }
    oarray aa(ac);

    n2 = n->right;
    for(int a=0; a < ac; a++)
    {
        var v = ec.eval(ec,n2->left);		// changed 20060925 to allow complex args
        aa.putat(a,v);
        n2 = n2->right;
    }


    var v;
    if(fast) {
        // set othis so that called methods can return safe references to themselves
        // without needing weak_proxy objects
        obj_ptr op;
        context e2(&ec,op);
        e2.othis.value.oval = direct_obj;
        v = fast(e2, direct_obj.get(), aa);
    }
    else {
        v = direct_obj->call_direct(ec, direct_method, aa);
    }
    ret = v;
    return true;
}

var fast_call(context& ec, node_ptr nmember, node_ptr ncall, obj_ptr obj);
var fast_call(context& ec, node_ptr nmember, node_ptr ncall, obj_ptr obj) {
    method fast = NULL;
    node_ptr n = nmember;
    obj_ptr op;
    context e2(&ec, op);
    
    // we have already patched this node, make the fast call
    if(n->match_type == obj->type) {
        fast = n->func_ptr;
    }
    else {            
        // Look for the method in the type and patch the node if found
        map<wstring, method>& ms = obj->type->methods;
        map<wstring,method>::iterator i = ms.find(*n->token->sval);
        if(i != ms.end())
        {
            fast = (*i).second;
            n->match_type = obj->type;
            n->func_ptr = fast;
            n->type = node::MethodPtr;
        }
    }
    int ac = 0;
    n = ncall;
    node_ptr n2 = n->right;		// below change 20060925 to support new argument node layout
    while(n2 != nullNode && (n2->token != nullToken || n2->left != nullNode))	// to handle empty call
    {
        n2 = n2->right;
        ac++;
    }
    oarray aa(ac);

    n2 = n->right;
    for(int a=0; a < ac; a++)
    {
        var v = ec.eval(ec,n2->left);		// changed 20060925 to allow complex args
        aa.putat(a,v);
        n2 = n2->right;
    }


    var v;

    e2.othis.value.oval = obj;
    if(obj && fast)
        v = fast(e2, obj.get(), aa);
    return v;
}

var context::callFunc(context& ec, node_ptr n, obj_ptr othis)
{
    var v;
    
    // mohsen - 20110218 
    // handle direct calls that don't require ec setup or property lookup
    if(check_call_direct(ec, n, othis, v))
    {
        return v;
    }

    // create a new execution context
    obj_ptr po(new object());
    context e(&ec, po);
    
    if(e.call_depth > 20) {
        return ec.setError(L"Call depth reached", nullToken);
    }
    
    var vfunc;
    obj_ptr ofunc;
    char_ptr cmember = NULL;
    obj_ptr oleft;
    
    // setup this ptr if necessary
    if(n->left->token != nullToken && n->left->token->kid == token::kidDot)
    {        
        if(othis != nullObj)
            return setError(L"Cannot new <obj>.<method>", n->token);

        // 2011-11-23 shark@anui.com - use eval() instead of ref to support nested calls
        var vleft = eval(ec, n->left->left);
        oleft = toObject(vleft);
        
        // Set the this pointer
        e.othis.value = var(oleft);
        
        if(oleft->type != NULL) {
            return fast_call(ec, n->left->right, n, oleft);
        }
        
        // Get the member identifier
        node_ptr nmember = n->left->right;
        if(nmember->token == nullToken || nmember->token->type != token::tidentifier) {
            return setError(L"Identifier expected", nmember->token);
        }
        
        // Get the member value
        cmember = nmember->token->sval->c_str();
        
        // Direct call will be handled below
        if(!oleft->can_call_direct()) {
            // get the member 
            prop* p = oleft->getprop(cmember, true);
            if(p != NULL) {
                vfunc = p->value;
            }
//            vfunc = oleft->get(cmember);
            
            // Ensure member is an object (function)
            ofunc = vfunc.obj();
        }
    }

    if(othis != nullObj)
    {
        e.othis.value = othis;
    }

    // 2012-02-27 shark@anui.com - handle new function() case (use this ptr as a function)
    if(n->token != nullToken && n->token->type == token::tkeyword && n->token->kid == token::kidFunction) {
        ofunc = othis->prototype.value.oval;
    }
    else {
        if(oleft == nullObj) {
            v = eval(ec,n->left);

            if(v.vt != var::tobj)
            {
                wstring err;
                wstlprintf(err, L"Object expected line: %d column: %d", n->token->line, n->token->column);
                return setError(err.c_str(), n->token);
            }
            ofunc = v.oval;
        }
    }


    if(oleft != nullObj && !oleft->can_call_direct() && ofunc == nullObj) {
        return setError(L"Function expected", n->left->token);
    }

    // get argument count
    int ac = 0;
    node_ptr n2 = n->right;		// below change 20060925 to support new argument node layout
    while(n2 != nullNode && (n2->token != nullToken || n2->left != nullNode))	// to handle empty call
    {
        n2 = n2->right;
        ac++;
    }

    // create arguments array
    oarray aa(ac);

    n2 = n->right;
    for(int a=0; a < ac; a++)
    {
        v = eval(ec,n2->left);		// changed 20060925 to allow complex args
        aa.putat(a,v);
        n2 = n2->right;
    }

    e.func_args = &aa;              // setup the arguments on the context so we can use 'arguments' 
    // call
    if(oleft != nullObj && oleft->can_call_direct()) {
        v = oleft->call_direct(ec, cmember, aa);
    }
    else {
        // handle the scope chain if the function has a scope property, then
        // add it to the resolution scope chain
        function* fn = (function*) ofunc.get();
        if(fn->scope != NULL) {
            object* vo = (object*) e.vars.get();
            vo->scope = fn->scope;
        }
        e.func = ofunc.get();;
        v = ofunc->call(e,aa);
    }

    return v;
}



var context::setError(char_ptr s, token_ptr t)
{
    int lineNum = 0;
    int colNum  = 0; // = t != nullToken ? t->column : -1;
    
    if(!has_error)
    {
    
        if (t) {
            lineNum = t->line;
            colNum = t->column;
        }
        else {
            t = this->root->lastToken;
            if(t) {
                lineNum = t->line;
                colNum = t->column;
            }
        }
        if(t)
            wstlprintf(error, L"%d:%d (%d) runtime error - %ls", t->script_index, lineNum, colNum, s);
    }
    
    if(this->root->attached_debugger != NULL) {
        this->root->attached_debugger->output_error(error.c_str());
    }

    has_error = true;
    var v(error);
    v.ctl = var::ctlerror;
    return v;    
//    throw new exception(error, lineNum, colNum);

//    return var::error();
}

var context::run(context& ec, node_ptr n)
{
    var v;
    debugger* dbg = this->root->attached_debugger ;
        
    while(true)
    {
        if(n == nullNode)
            return v;

        token_ptr t = n->token;

        if(t == nullToken)
        {
            if(n->left)
            {
                v = run(ec, n->left);
                if(has_error || v.ctl == var::ctlerror)
                {
                    return v;
                }

                if(v.ctl != var::ctlnone)
                    return v;
            }
            n = n->right;
        }
        else
        {
            return eval(ec, n);
        }

#ifdef ENABLE_DEBUGGER        
    if(dbg != NULL) {
        if(!dbg->suspend_exit_check && dbg->should_exit())
            return var();
    }
#endif        
    }
}


var context::eval(context& ec, node_ptr n)
{
    if(n == nullNode)
        return var();

    token_ptr t = n->token;
    if(!t)
        return var();

    ec.root->lastToken = t;
    
    switch(t->type)
    {
    case	token::tidentifier:
        {        
            return n->eval(ec);
        }
        break;

    case	token::tnumlit:
        return var(t->nval);

    case	token::tstrlit:
        return var(t->sval->c_str());

    case    token::tregex: {        
        wstring input = t->sval->substr(1, t->flagOffset-1);
        
        // encode all backslash regex expressions correctly
        wstring pattern;
        int len = input.size();
        for(int i=0; i < len; i++) {
            if(input[i] == '\\') {
                if(i < (len-1)) {
                    i++;
                    jschar c = input[i];
                    switch(c) {
                        case '/':
                            pattern.push_back(c);
                            break;
                        case 'n':
                            pattern.push_back('\n');
                            break;
                        case 't':
                            pattern.push_back('\t');
                            break;
                        case 'r':
                            pattern.push_back('\r');
                            break;
                        case 'v':
                            pattern.push_back('\v');
                            break;
                        case 'f':
                            pattern.push_back('\f');
                            break;
                        case '\\':
                            pattern.push_back('\\');
                            break;
                        default:
                            pattern.push_back('\\');
                            pattern.push_back(c);
                            break;
                    }
                }
            }
            else {
                pattern.push_back(input[i]);
            }
        }
        wstring flags = t->sval->substr(t->flagOffset+1);        
        obj_ptr o(regex_obj::create(pattern, flags));
        return var(o);        
    }
    
    case	token::tkeyword: {
            return interpretKeyword(ec, n, t->kid);
        }
    }

    return var();;
}


void context::propDel(context& ec, node_ptr n)
{
    token_ptr t = n->token;

    if(t->type == token::tidentifier)
    {
        ec.vars->delprop(t->sval->c_str());
        return;
    }

    if(t->type == token::tkeyword && t->kid == token::kidDot)
    {
        // get object reference
        prop* p = ref(ec, n->left,false);
        // todo: mohsena 20110724 - middle check is not correct?
//        if(p == NULL || p->value == nullptr || p->value.vt != var::tobj)
        if(p == NULL || p->value.vt != var::tobj)
            return;	

        obj_ptr o = p->value.oval;
        if(o == nullObj)
            return; 

        o->delprop(n->right->token->sval->c_str());
    }

    if(t->type == token::tkeyword && t->kid == token::kidLBracket)
    {
        prop* p = ref(ec, n->left,false);
        if(p) {
            obj_ptr o = p->value.obj();
            var v = eval(ec, n->right);

            if(v.vt == var::tstr)
            {
                o->delprop(wcharFromVar(v)); //  v.c_str());
            }
        }
    }
}

void debug_checkbow(debugger* dbg, prop* pr) {
    if(!pr) return;
    for(vector<bow>::iterator i = dbg->bow_list.begin(); i != dbg->bow_list.end(); i++) {
        if((*i).ref == pr) {
            wstring s;
            wstlprintf(s, L"%ls break on write hit", (*i).expression.c_str());
            dbg->output(s.c_str());
            dbg->set_state(debugger::pause,NULL,0);
            return;
        }
    }
}



prop* context::ref(context& ec, node_ptr n, bool local)
{
    token_ptr t = n->token;
    debugger* dbg = ec.root->attached_debugger;
    if(dbg && ! dbg->is_basic() && dbg->bow_list.size() > 0) {
    }
    else 
        dbg = NULL;
    
    if(t->type == token::tidentifier)
    {
        prop* p = n->ref(ec, local);
        if(dbg) debug_checkbow(dbg, p);
        return p;
    }

    if(t->type == token::tkeyword && t->kid == token::kidDot)
    {
        // get object reference
        prop* p = ref(ec, n->left,local);
        obj_ptr o;
        
        // 20130103 shark@anui.com - support for expr().prop = value
        if(p == NULL) {
            // evaluate the left side and see if it's an object
            var v = eval(ec, n->left);
            if(v.vt == var::tobj) {
                o = v.oval;
                wstring s = *(n->right->token->sval);
                p = o->getprop(s.c_str(),true);
                if(p == NULL)
                    return o->addprop(s.c_str());
                if(dbg) debug_checkbow(dbg, p);            
                return p;                
            }
        }
        if(p == NULL || p->value.vt != var::tobj)
            o = obj_ptr(new object());
        else
            o = toObject(p->value);

        // 20130106 shark@anui.com - fix crash in func().prop = value where func()
        //                           returns null
        if(!p)
            return NULL;

        // shark@anui.com - 2011-11-15 - the 'o' object above is going to 
        // be cleared on exit. It must be saved as the property value
        p->value = var(o);
        wstring s = *(n->right->token->sval);
        p = o->getprop(s.c_str(),true);
        if(p == NULL)
            return o->addprop(s.c_str());
        if(dbg) debug_checkbow(dbg, p);            
        return p;
    }

    if(t->type == token::tkeyword && t->kid == token::kidLBracket)
    {
        prop* p = ref(ec, n->left,false);
        if(!p)
            return NULL;
        obj_ptr o = p->value.obj();
        
        if(o == nullObj) {
            return NULL;
        }
        var v = eval(ec, n->right);

        if(v.vt == var::tstr)
        {
            p = o->getprop(wcharFromVar(v), true); //  v.c_str(), true);
            if(p == NULL)
                p = o->addprop(wcharFromVar(v)); //  v.c_str());
            return p;
        }

        int i = (int) v.num();
        if(!o->is_array())
            return NULL;

        object* po = o.get();
        oarray* a = (oarray*) po;
        p = a->refat(i);
        if(p == NULL)
        {
            a->putat(i,var());
            p = a->refat(i);
        }
        if(dbg) debug_checkbow(dbg, p);
        return p;
    }

    if(t->type == token::tkeyword && t->kid == token::kidThis)
    {
        return &ec.othis;
    }
    
    
    // 2011-12-08 shark@anui.com - support for(var x in y) requires
    // that ref support evaluate var statement
    if(t->type == token::tkeyword && t->kid == token::kidVar) {
        return ref(ec, n->right, true);
    }
    return NULL;
}

bool check_breakpoints(debugger* dbg, breakpoint current) {
    if(!dbg->breakpoints_enabled())
        return false;
    if(current == dbg->temp_breakpoint) {
        dbg->temp_breakpoint.line = debugger::no_line;
        return true;
    }
    for(unsigned int i=0; i < dbg->breakpoints.size(); i++) {
        if(current == dbg->breakpoints[i])
            return true;
    }
    return false;
}

void list(debugger* dbg, int line) {
    int current_script = dbg->call_stack[0].script_index;
    breakpoint current(current_script, line);
    bool is_bp = check_breakpoints(dbg, current);
    script* s = dbg->get_script(current_script);
    wstring sline = s->get_line(line);
    wstring text;
    wstlprintf(text, L"%4d%ls%ls", line, (is_bp) ? L"*" : L" ",sline.c_str());
    dbg->output(text.c_str());
}

bool is_child_context(context* cc, context* cp) {
    if(cc == cp)
        return false;

    for(context* ccp=cc->parent; ccp != NULL; ccp=ccp->parent) {
        if(ccp == cp)
            return true;
    }

    return false;
}

bool is_parent_context(context* cc, context* cp);
bool is_parent_context(context* cc, context* cp) {
    if(cc == cp)
        return false;

    for(context* ccp=cp->parent; ccp != NULL; ccp=ccp->parent) {
        if(ccp == cc)
            return true;
    }

    return false;
}

wstring debug_short_desc(debugger* dbg, var& v, int start);
wstring debug_array_short_desc(debugger* dbg, var& v, int start);
wstring debug_obj_short_desc(debugger* dbg, var& v, int start);
wstring debug_obj_short_desc(debugger* dbg, var& v, int start) {
    obj_ptr oval = v.oval;
        
    wstring res;
    
    if(start == 0)
        wstlprintf(res, L"(%d){}%ls", oval->__id, (start < 0) ? L"" : L"\n");
    int i = 0;
    
    for(map<wstring,prop>::iterator p = oval->properties.begin(); p != oval->properties.end();) {
        if(i < start) {
            i++;
            p++;
            continue;
        }
        
        wstring name = p->first;
        wstring temp;
        if(p->second.value.vt == var::tobj) {
            if(p->second.value.oval->is_array())
                wstlprintf(temp, L"%ls = []%ls", name.c_str(), (start < 0) ? L"," : L"\n");
            else
                wstlprintf(temp, L"%ls = {}%ls", name.c_str(), (start < 0) ? L"," : L"\n");
        }
        else {
            wstring val = debug_short_desc(dbg, p->second.value,0);
            wstlprintf(temp, L"%ls:%ls%ls", name.c_str(), val.c_str(), (start < 0) ? L"," : L"\n");
        }
        i++;
        if((i - start) >= debugger::next_print_count) {
            res += L"...";
            break;            
        }
        
        res += temp;
        p++;
        if(p == oval->properties.end()) {
            dbg->last_print_start = 0;
            dbg->last_print_expr = L"";
            if(start > 0)
                res += L"done";
        }
    }
    
//    res += L"}";
    return res;
}
wstring debug_array_short_desc(debugger* dbg, var& v, int start) {

    wstring res;
    if(start == 0)
        res = L"[]\n";
    
    if(start < 0)
        res = L"[";
    obj_ptr oval = v.oval;
    oarray* oa = (oarray*) oval.get();
    int len = oa->length();
    wstring temp;
    int st = (start <0) ? 0 : start;
    
    for(int i=st; i < len; i++) {
        var iv = oa->getat(i);
        if(iv.vt == var::tobj) {
            if(iv.oval->is_array())
                wstlprintf(temp, L"%d = []%ls", i, (start <0) ? L"," : L"\n");
            else
                wstlprintf(temp, L"%d = {}%ls", i, (start <0) ? L"," : L"\n");
        }
        else {
            wstring val = debug_short_desc(dbg, iv,0);
            wstlprintf(temp, L"%d:%ls%ls", i,val.c_str(), (start<0) ? L"," : L"\n");
        }
        
        if((i-start) >= debugger::next_print_count) {
            res += L"...";
            return res;
        }
        
        res += temp;
    }
    res += L"]";
    return res;
}

wstring debug_short_desc(debugger* dbg, var& v, int start) {
    if(v.vt == var::tobj && !v.oval->is_array()) {
        return debug_obj_short_desc(dbg, v, start);
    }
    if(v.vt == var::tobj && v.oval->is_array()) {
        return debug_array_short_desc(dbg, v, start);
    }
    if(v.vt == var::tnum) {
        wstring res;
        wstlprintf(res, L"%g", v.nval);
        return res;
    }
    if(v.vt == var::tstr) {
        wstring res;
        wstlprintf(res, L"\"%ls\"", v.sval->c_str());
        return res;
    }
    else {
        return v.wstr();
    }
}

wstring debug_eval(debugger* dbg, context* ctx, wstring expr, int start);
wstring debug_expr(debugger* dbg, context* ctx, wstring expr, int start);
wstring debug_expr(debugger* dbg, context* ctx, wstring expr, int start) {
    wstring buff;
    
    // special case for global
    if(expr == L"_global") {
        var r(ctx->vars);
        return debug_short_desc(dbg, r,0);
    }
    
    wstlprintf(buff, L"%ls", expr.c_str());
    script temp(buff.c_str(), 0, L"");
    
    // We have to save the environment created for this script so that
    // when it's destructed, it doesn't nuke types and other global
    // state (issue #442)
    context c = temp.env;
    
    temp.env = *ctx;
    ctx->root->attached_debugger = NULL; // avoid reentering debugger
    try {
        var r = temp.run(NULL);
        temp.env = c;
        ctx->root->attached_debugger = dbg; // reset debugger
        return debug_short_desc(dbg, r, start);
    }
    catch(js::exception* e) {
        temp.env = c;
        ctx->root->attached_debugger = dbg; // reset debugger
        return e->error;
    }
}

wstring debug_eval(debugger* dbg, context* ctx, wstring expr, int start) {
    wstring buff;
     
    // special case for global
    if(expr == L"_global") {
        var r(ctx->vars);
        return debug_short_desc(dbg, r,0);
    }
    
    if(expr == dbg->last_print_expr) {
        start = dbg->last_print_start;
    }
    else {
        if(dbg->last_print_expr != L"")
            dbg->last_print_start = 0;
    }
    
    if(expr == L"") {
        expr = dbg->last_print_expr;
    }
    
    wstlprintf(buff, L"___res=%ls;", expr.c_str());
    script temp(buff.c_str(), 0, L"");
    
    // We have to save the environment created for this script so that
    // when it's destructed, it doesn't nuke types and other global
    // state (issue #442)
    context c = temp.env;
    
    temp.env = *ctx;
    ctx->root->attached_debugger = NULL; // avoid reentering debugger
    try {
        var r = temp.run(NULL);
        temp.env = c;
        ctx->root->attached_debugger = dbg; // reset debugger
        dbg->last_print_start += debugger::next_print_count;        
        return debug_short_desc(dbg, r, start);
    }
    catch(js::exception* e) {
        temp.env = c;
        ctx->root->attached_debugger = dbg; // reset debugger
        return e->error;
    }
}

wstring debug_backtrack(debugger* dbg, int count);
wstring debug_get_line(debugger* dbg, int file, int line);

wstring debug_get_line(debugger* dbg, int si, int line) {
    script*  s = dbg->get_script(si);
    wstring sline = s->get_line(line);
    wstring text;
    wstring& file = s->file;
    wstlprintf(text, L"%ls:%4d %ls", file.c_str(), line, sline.c_str());
    return text;

}
wstring debug_backtrack(debugger* dbg, int count) {
    if(count == 0)
        count = dbg->call_stack.size();
    
    wstring res = L"";
    
    for(int c = 0; c < count; c++) {
        wstring f;
        stack_frame sf = dbg->call_stack[c];
        wstring s = debug_get_line(dbg, sf.script_index, sf.line);
        wstlprintf(f, L"#%d %ls\n", c, s.c_str());
        res += f;
    }
    return res;
}

void debug_display(debugger* dbg, context* ctx);

void debug_display(debugger* dbg, context* ctx) {
    for(int i=0; i < dbg->display_list.size(); i++) {
        wstring out;
        wstring expr = dbg->display_list[i];
        wstring val = debug_eval(dbg, ctx, expr,-1);
        wstlprintf(out, L"%d %ls = %ls", i+1, expr.c_str(), val.c_str());
        dbg->output(out.c_str());
    }
}

bool debug_islocal(context& ec, char_ptr v, node* n, var val);
bool debug_islocal(context& ec, char_ptr v, node* n, var val) {
    if(ec.isroot() && val.vt == var::tobj && val.oval->isNative())
        return false;
    
    // special case _global so that we don't end up with the circular infinite recursion
    // for require()
    if(wcscmp(v, L"_global") == 0)
        return false;
        
    while(n != nullNode) {
        char_ptr s = n->token->sval->c_str();
        if(wcscmp(v, s) == 0)
            return false;
        n = n->left;
    }
    return true;
}


void debug_command(context* ctx, breakpoint curr, debugger* dbg) {

    debug_display(dbg, ctx);
    // list(dbg, line);
    while(true) {
        wstring cmd = dbg->get_command();
        
        if(cmd == L"" && dbg->last_command != L"") {
            // dbg->output(dbg->last_command.c_str());
            cmd = dbg->last_command;
        }
        
        dbg->last_command = cmd;
        
        if(!dbg->handle_command(cmd, ctx, curr))
            break;
    }
}

bool context::refarray(context& ec, node_ptr n, oarray** array, int* index) {
    token_ptr tp = n->token;
    if(tp->type == token::tkeyword && tp->kid == token::kidLBracket) {
        var obj = eval(ec, n->left);
        if(obj.vt != var::tobj) 
            return false;
        
        if(!obj.oval->is_array())
            return false;
            
        var vindex = eval(ec, n->right);
        *index = vindex.c_int();
        *array = (oarray*) obj.oval.get();
        return true;
    }
    return false;
}

breakpoint get_node_current(debugger* dbg, node_ptr n) {
    breakpoint bp;
    
    if(n == nullNode)
        return bp;

    if(n->token != nullToken) {
        return breakpoint(n->token->script_index, n->token->line);
    }

    breakpoint l = get_node_current(dbg, n->left);
    if(l == bp) {
        return get_node_current(dbg, n->right);
    }
    return l;
}

void debug_update_frame(debugger* dbg, int fi, int line);
void debug_update_frame(debugger* dbg, int fi, int line) {

#ifdef DEBUG_NOTIFICATIONS
    // We use the debugger's last_script and last_line members instead of relying
    // on the call_stack[0] since that could have changed during a function call
    // entry which means that the node will match the call_stack and no change
    // notification would be sent
    bool change = (dbg->last_script != fi) ||
                  (dbg->last_line != line);
    dbg->last_script = fi;
    dbg->last_line = line;
#endif

    dbg->call_stack[0].line = line;
    dbg->call_stack[0].script_index = fi;
    dbg->current_frame = 0;

#ifdef DEBUG_NOTIFICATIONS
    if(change)
        dbg->location_changed(line);
#endif
}

void debug(context* ctx, node_ptr n, debugger* dbg) {
    if(dbg->is_basic())
        return;
    
    breakpoint curr = get_node_current(dbg, n);
        
    switch(dbg->get_state()) {
        case debugger::running:
            // 20130119 shark@anui.com issue #509 ensure that file & line is always updated
            dbg->call_stack[0].script_index = curr.file_index;
            dbg->call_stack[0].line = curr.line;
          
            if(!check_breakpoints(dbg, curr)) {
                // the line must have changed here, so we need to reset skip_line so that
                // we can break (issues #283 and #323)
                dbg->skip_line = breakpoint(-1, -1);
                return;
            }
            
            // 2012-05-11 shark@anui.com issue #283. if the breakpoint line has
            // complex expressions, we set the skip line to be this line so that we don't hit
            // it multiple times until the line changes
            if(curr == dbg->skip_line) {
                return;
            }
            dbg->skip_line = curr;
            
            debug_update_frame(dbg,curr.file_index, curr.line);

#ifdef DEBUG_NOTIFICATIONS
            // 2012-05-11 shark@anui.com - added this to fix issue #323 to handle
            // case where continue is hit on a breakpoint
            dbg->location_changed(curr.line);
#endif

            // force a break
            debug_command(ctx, curr, dbg);
            break;

        case debugger::pause: 
            debug_update_frame(dbg, curr.file_index, curr.line);
            debug_command(ctx, curr, dbg);
            break;
        
        case debugger::step:
            if(curr != dbg->skip_line) {
                debug_update_frame(dbg, curr.file_index, curr.line);
                debug_command(ctx, curr, dbg);
            }
            break;

        case debugger::next:
            if(curr != dbg->skip_line && !is_child_context(ctx, dbg->skip_context)) {
                debug_update_frame(dbg,curr.file_index, curr.line);        
                debug_command(ctx, curr, dbg);
            }
            break;

        case debugger::step_out:
            if(curr != dbg->skip_line && (dbg->skip_context == ctx || ctx->parent == NULL)) {
                debug_update_frame(dbg,curr.file_index, curr.line);        
                debug_command(ctx, curr, dbg);
            }
            break;
        
        case debugger::stopped:
                debug_command(ctx, curr, dbg);
            break;
    }
}

var context::interpretKeyword(context& ec, node_ptr n, int k)
{
    var v;
    prop* p = NULL;
    var v2;
    var v3;

#ifdef ENABLE_DEBUGGER
    debugger* dbg = this->root->attached_debugger ;
    if(dbg != NULL)
    {
        if(!dbg->suspend_exit_check && dbg->should_exit())
            return var();
        debug(this, n, dbg);
    }
#endif

#ifdef JS_OPT
    bool adv = (dbg) ? !dbg->is_basic() : false;
    
    int opt = ec.root->optimization;
    
    if((opt & Opt_DynamicProfiler) != 0) {
        if(adv) {
            dbg->profiler_update_node(ec, n);
        }
    }
#endif

    switch(k)
    {
    case	token::kidEqual:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            arr->putat(idx, v);
            return v;
        }
        
//        // 2011-12-02 shark@anui.com - special case handling for typed arrays
//        node_ptr nl = n->left;
//        token_ptr tp = nl->token;
//        if(tp->type == token::tkeyword && tp->kid == token::kidLBracket) {
//            node_ptr nid = nl->left;
//            token_ptr tid = nid->token;
//            if(tid->type == token::tidentifier) {
//                wstring s = *(tid->sval);
//                prop* p = ec.vars->getprop(s.c_str(), true);
//                if(p != NULL) {
//                    if(p->value.vt == var::tobj) {
//                        obj_ptr op = p->value.oval;
//                        if(op->is_array()) {
//                            oarray* oa = (oarray*) op.get();
//                            var vindex = eval(ec, nl->right);
//                            int index = vindex.c_int();
//                            v = eval(ec, n->right);
//                            oa->putat(index, v);
//                            return v;
//                        }
//                    }
//                }
//            }
//        }
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }
        v = eval(ec, n->right);
        if(v.ctl == var::ctlerror)
            return v;
        p->value = v;
        return v; 
    }
    case	token::kidMinus:
        v = eval(ec, n->left);
        if(n->right)
        {
            v2 = eval(ec, n->right);
            return (v - v2);
        }
        else
        {
            // Unary minus change: mohsen@mobicore.com 20060928
            return var(-v.num());
        }

    case	token::kidDiv:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return (v / v2);

    case	token::kidPlus:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return (v + v2);

    case	token::kidMult:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return (v * v2);

    case	token::kidMod:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(fmod(v.num(), v2.num()));

    case	token::kidTrue:
        return var(true);

    case	token::kidFalse:
        return var(false);

    case	token::kidIf:
        {
            node_ptr pcond = n->left->left;
            node_ptr ptrue = n->left->right->left;
            node_ptr pelse = n->left->right->right;
            v = eval(ec,pcond);
            if(v.c_bool())
            {
                return run(ec, ptrue);
            }
            else
            {
                if(pelse)
                    return run(ec,pelse->left);
            }
            return var();
        }

    case	token::kidAndAnd:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v.c_bool() && v2.c_bool());

    case	token::kidOrOr:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        if(v.c_bool())
            return v;
        if(v2.c_bool())
            return v2;
        return var(false);

    case	token::kidNull:
        {
            v.vt = var::tnull;
            return v;
        }

    case	token::kidPlusPlus:
        // Handle post increment differently
        if(n->left->token == nullToken) {
            p = ref(ec, n->left->left,false);
            if(p == NULL) {
                return setError(L"Expected variable", n->token);
            }
            v = eval(ec, n->left->left);
            p->value = v.num() + 1;
            return v;
        }
                
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }
        v = eval(ec, n->left);
        p->value = v.num() + 1;
        return p->value; 

    case	token::kidMinusMinus:
        // Handle post decrement differently
        if(n->left->token == nullToken) {
            p = ref(ec, n->left->left,false);
            if(p == NULL) {
                return setError(L"Expected variable", n->token);
            }
            v = eval(ec, n->left->left);
            p->value = v.num() - 1;
            return v;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }
        v = eval(ec, n->left);
        p->value = v.num() - 1;
        return p->value;

    case	token::kidAnd:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v & v2);

    case	token::kidOr:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v | v2);

    case	token::kidCaret:
        v = eval(ec, n->left);

        v2 = eval(ec, n->right);
        return var(v ^ v2);

    case	token::kidLShift:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v.c_int() << v2.c_int());

    case	token::kidRShift:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v.c_int() >> v2.c_int());
        return v3;

    case	token::kidRShiftA:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var( ((unsigned int) v.c_int()) >> ((unsigned int) v2.c_int()));

    case	token::kidNot:
        v = eval(ec, n->left);
        return var(! v.c_bool());

    case	token::kidTilde:
        v = eval(ec,n->left);
        return var( ~v.c_int() );

    case	token::kidPlusEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = va + v;
            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        p->value = v + v2;
        return p->value; 

    }
    case	token::kidMinusEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = va - v;
            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        p->value = v - v2;
        return p->value;
    }
    case	token::kidMultEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = v * va;
            arr->putat(idx, v2);
            return v2;
        }

        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        p->value = v * v2;
        return p->value;
    }
    case	token::kidDivEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = va / v;
            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        p->value = v / v2;
        return p->value;
    }
    case	token::kidModEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = var( fmod(va.num(), v.num()));
            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        p->value = var( fmod(v.num(), v2.num()));
        return p->value; 
    }
    case	token::kidLShiftEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = var( va.c_int() << v.c_int());
            arr->putat(idx, v2);
            return v2;
        }

        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        p->value = var( v.c_int() << v2.c_int());
        return p->value; 
    }
    case	token::kidRShiftEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = var( va.c_int() >> v.c_int());
            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        p->value = var( v.c_int() >> v2.c_int());
        return p->value; 
    }
    case	token::kidRShiftAEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = var( (unsigned int) va.c_int() >> (unsigned int) v.c_int());
            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);

        p->value = var( (unsigned int) v.c_int() >> (unsigned int) v2.c_int());
        return p->value; 
    }
    case	token::kidAndEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = var( va.c_int() & v.c_int());
            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);

        p->value = var( v.c_int() & v2.c_int());
        return p->value; 
    }
    case	token::kidOrEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = var( va.c_int() | v.c_int());
            arr->putat(idx, v2);
            return v2;
        }
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);

        p->value = var( v.c_int() | v2.c_int());
        return p->value; 
    }
    case	token::kidCaretEq:
    {
        oarray* arr;
        int idx;
        bool isarray = refarray(ec, n->left, &arr, &idx);
        if(isarray) {            
            v = eval(ec, n->right);
            var va = arr->getat(idx);
            var v2 = var( va.c_int() ^ v.c_int());

            arr->putat(idx, v2);
            return v2;
        }
    
        p = ref(ec, n->left,false);
        if(p == NULL)
        {
            return setError(L"Expected variable", n->token);
        }

        v = eval(ec, n->left);
        v2 = eval(ec, n->right);

        p->value = var( v.c_int() ^ v2.c_int());
        return p->value; 
    }
    case	token::kidLt:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v < v2);

    case	token::kidGt:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v > v2);

    case	token::kidLe:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v <= v2);

    case	token::kidGe:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(v >= v2);

    case	token::kidEqEq:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
#ifdef ES_8_12_8
        return var(abstractEqualityCompare(ec, &v,&v2));
#endif
        
        return var(v == v2);

    case	token::kidNe:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
#ifdef ES_8_12_8
        return var(!abstractEqualityCompare(ec, &v,&v2));
#endif

        return var(v != v2);

    case	token::kidEqEqEq:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
#ifdef ES_11_9_6
        return var( strictEqualityCompare(ec, &v, &v2));
#else
        return var( isIdent(v,v2) );
#endif

    case    token::kidNEqEq:
        v = eval(ec, n->left);
        v2 = eval(ec, n->right);
        return var(! isIdent(v, v2));
    
    case	token::kidDot:
        {
            v = eval(ec, n->left);
            obj_ptr o = toObject(v);
            
            unsigned int oi = o->__id;
                
            // If it's the same object pointer and we're calling the same
            // property identifier, we return the cached property 
            // the danger here is that if we have a situation where
            // the same parse node is called for one object (O1) and
            // then the object is freed and the same node is called with
            // another object (O2) which happens to have been allocated
            // at the same memory address, we could crash
            if(n->type == node::ObjProp && oi ==  n->match_object) {
                return *(n->var_ptr);
            }
     
            // shark@anui.com 2011-11-12 - check for non-identifier dot expressions
            node_ptr nr = n->right;
            if(nr == nullNode)
                return setError(L"expected method", n->token);
            token_ptr tp = nr->token;
            if(tp == nullToken || tp->type != token::tidentifier)
                return setError(L"expected method", n->token);
                
                
            p = o->getprop(n->right->token->sval->c_str(),true);
            if(p == NULL) {
            
#ifdef STRICT
                // shark@anui.com 2011-11-12 - return runtime error on invalid method
                wstring m = L"method not found: ";
                m += n->right->token->sval->c_str();
                return setError(m.c_str(), n->right->token);
#else
                return var();
#endif
            }
            
            // remember the last object and the last property value reference
            n->type = node::ObjProp;
            n->var_ptr = &p->value;
            n->match_object = oi;
    
            return p->value;
        }

    case	token::kidRBracket:						// array literal
        {
            int l=0;
            node_ptr ne = n->left;
            while(ne)
            {
                l++;
                ne = ne->right;
            }
            oarray* a = new oarray(l);

            ne = n->left;
            for(int i=0; i < l; i++)
            {
                // 2012-01-22 shark@nui.com. If the node was a complex expression, eval it's left.
                // see elementList() note for details
                if(ne->token == nullToken) {
                    a->putat(i, eval(ec,ne->left));
                }
                else {
                    a->putat(i, eval(ec,ne));
                }
                ne = ne->right;
            }
            return var(obj_ptr(a));
        }

    case	token::kidLBracket:						// array index
        {
            v = eval(ec, n->left);
            if(v.vt != var::tobj)
                return var();

            v2 = eval(ec, n->right);
            if(v2.vt == var::tstr)
            {
                char_ptr s = v2.sval->c_str();
                obj_ptr o = v.oval;
                prop* p = o->getprop(s, true);
                if(p == NULL)
                    return var();
                return p->value;
            }
            else
            {
                int i = v2.c_int();
                if(!v.oval->is_array())
                    return var();

                object* o = v.oval.get();
                oarray* a = (oarray*) o;
                return a->getat(i);
            }
        }

    case	token::kidWhile:
        {
            unsigned int iterationCount = 0;
            while(true)
            {
                iterationCount++;
                if((iterationCount % iterationLimit) == 0) {
                    if(ec.root->attached_debugger) {
                        if(ec.root->attached_debugger->peek_exit()) {
                            break;
                        }
                    }
                }

                v = eval(ec, n->left->left);
                if(v.c_bool() == false)
                    break;
                v = run(ec, n->left->right);
                if(v.ctl == var::ctlbreak)
                {
                    break;
                }

                if(v.ctl == var::ctlreturn || v.ctl == var::ctlerror)
                {
                    return v;
                }
            }
            return var();			
        }

    case token::kidDebugger:
    {
        debugger* dbg = ec.root->attached_debugger ;
        if(dbg != NULL && !dbg->is_basic())
        {
//            int current_script = dbg->call_stack[0].script_index;
//            int current_file = dbg->_script->file_from_include(current_script);
            breakpoint bp(-1,-1);
            dbg->skip_line = bp;
            dbg->set_state(debugger::step,NULL,0);
            return var();
        }
    }
    break;
    
    case	token::kidBreak:
        {
            v.ctl = var::ctlbreak;
            return v;
        }

        case	token::kidContinue:
        {
            v.ctl = var::ctlcontinue;
            return v;
        }
            
    case	token::kidReturn:
        if(n->left)
            v = eval(ec,n->left);
        else
            v = var();
        v.ctl = var::ctlreturn;
        return v;

    case	token::kidDo:
    {
        unsigned int iterationCount = 0;
        while(true)
        {
            iterationCount++;
            if((iterationCount % iterationLimit) == 0) {
                if(ec.root->attached_debugger) {
                    if(ec.root->attached_debugger->peek_exit()) {
                        break;
                    }
                }
            }

            v = run(ec, n->left->left);
            if(v.ctl == var::ctlbreak)
            {
                break;
            }

            if(v.ctl == var::ctlreturn || v.ctl == var::ctlerror)
            {
                return v;
            }

            v = eval(ec, n->left->right->left);
            if(v.c_bool() == false)
                break;
        }
    }
    return var();

    case	token::kidLPar:
        {
            v = callFunc(ec, n, nullObj);
            
            // pass exceptions down but not other controls
            if(v.ctl == var::ctlerror)
                return v;
            v.ctl = var::ctlnone;	// Avoid cascading down
            return v;
        }

    case	token::kidFunction:
        {
            function* fn = new function();

            // 2012-03-12 shark@anui.com - support for closures. If the function
            // is being defined inside another function and not the root, then
            // we must capture the vars as a scope for this function
            if(ec.parent != NULL) {
                fn->scope = ec.vars.get();
                ec.root->attached_debugger->gcpush(ec.vars);
            }
            
            obj_ptr o(fn);    
            o->node = n->left->right;
            v = var(o);
            // If the function is not anonymuos (expression)
            if(n->left->left != nullNode)
            {
                token_ptr ti = n->left->left->token;
                char_ptr s = ti->sval->c_str();
                prop* p = ec.vars->addprop(s);
                p->value = v;
            }
            return v;
        }

    case    token::kidUndefined:
        return var();
        
    case	token::kidThis:
        return ec.othis.value;

    case	token::kidFor:
        {
            // for( ... in ...) loop
            if(n->left->left->token != nullToken && n->left->left->token->kid == token::kidIn)
            {
                node_ptr ni = n->left->left->left;
                p = ref(ec, ni,false);
                v2 = eval(ec, n->left->left->right);
                if(v2.vt != var::tobj)
                {
                    return var();
                }
                obj_ptr o = v2.oval;
                
                if(o->is_array()) {
                    oarray* oa = (oarray*) o.get();
                    for(int i=0; i < oa->length(); i++) {
                        p->value = var(i);
                        v = run(ec, n->left->right);
                        if(v.ctl == var::ctlbreak)
                        {
                            break;
                        }

                        if(v.ctl == var::ctlreturn || v.ctl == var::ctlerror)
                        {
                            return v;
                        }
                    }
                    return var();
                }
                
                map<wstring, prop>::iterator pi = o->properties.begin();
                for(; pi != o->properties.end(); pi++)
                {
                    char_ptr s = (*pi).first.c_str();
                    p->value = var(s);
                    v = run(ec, n->left->right);
                    if(v.ctl == var::ctlbreak)
                    {
                        break;
                    }

                    if(v.ctl == var::ctlreturn || v.ctl == var::ctlerror)
                    {
                        return v;
                    }
                }
                return var();
            }
            else
            {
                node_ptr n1;
                node_ptr n2;
                node_ptr n3;
                node_ptr nb;

                n1 = n->left->left->left->left->left;
                n2 = n->left->left->left->left->right;
                n3 = n->left->left->left->right;
                nb = n->left->right;

                // 2011-12-06 shark@anui.com - Handle for(var x ...; .. ;) statements
                if(n1->token == nullToken) {
                    n1 = n1->right;
                }
                v = run(ec, n1);
                unsigned int iterationCount = 0;
                while(true)
                {
                    iterationCount++;
                    if((iterationCount % iterationLimit) == 0) {
                        if(ec.root->attached_debugger) {
                            if(ec.root->attached_debugger->peek_exit()) {
                                break;
                            }
                        }
                    }
                    v = eval(ec, n2);
                    if(n2 != nullNode && v.c_bool() == false)
                    {
                        break;
                    }

                    v = run(ec, nb);
                    if(v.ctl == var::ctlbreak)
                    {
                        break;
                    }

                    if(v.ctl == var::ctlreturn || v.ctl == var::ctlerror)
                    {
                        return v;
                    }

                    v = run(ec, n3);
                }
                return var();
            }
        }

    case	token::kidQuestion:
        {
            v = eval(ec, n->left);
            if(v.c_bool() == true)
            {
                return eval(ec, n->right->left);
            }
            else
            {
                if(n->right->right != nullNode)
                {
                    return eval(ec, n->right->right->left);
                }
            }
        }

    case	token::kidVar:
        {
            n = n->left;

            while(n != nullNode)
            {
                p = ref(ec, n, true);
                if(n->left)
                    v = eval(ec,n->left->left);
                
                if(v.ctl == var::ctlerror)
                    return v;
                p->value = v;
                n = n->right;
            }
            return var();
        }

    case	token::kidNew:
        {
            // 2012-02-27 shark@anui.com - handle o = new function() syntax
            if(n->left->token != nullToken && n->left->token->type == token::tkeyword && 
               n->left->token->kid == token::kidFunction) {
                v2 = eval(ec, n->left);
            }
            else {
                // get the function object
                v2 = eval(ec, n->left->left);
            }
            if(v2.vt != var::tobj)
            {
                return setError(L"Function expected", n->token);
            }

            // get the prototype
            obj_ptr of = v2.oval;
            
            object* o2 = of.get();
            
            if(o2 && of->isNative())
            {
                v = callFunc(ec, n->left, nullObj);
                return v;
            }
            else
            {
                obj_ptr o(new object());
                
                // Always set the prototype to be the function's prototype
                if(of->prototype.value.vt == var::tobj)
                {
                    o->put(L"prototype", var(of->prototype.value.oval));
                }

                v = callFunc(ec, n->left, o);
                if(v.ctl == var::ctlerror)
                    return v;
                return var(o);
            }
        }

    case	token::kidLBrace:
        {
            obj_ptr o(new object());
            n = n->right;
            while(n != nullNode)
            {
                // 20130105 shark@anui.com - ensure token is a string otherwise error
                int t = n->left->token->type;
                if(t != token::tstrlit && t != token::tidentifier) {
                    return ec.setError(L"Expected string or identifier", n->left->token);
                }
                char_ptr s = n->left->token->sval->c_str();
                v = eval(ec, n->left->right);
                p = o->addprop(s);
                p->value = v;
                n = n->right;
            }
            return var(o);
        }

    case	token::kidVoid: 
        v = eval(ec, n->left);
        return var();	

    case	token::kidTypeOf:
        {
            v = eval(ec, n->left);
            wstring tn = v.type_name();
            return var(tn);
        }

    case	token::kidWith:
        {
            v = eval(ec, n->left->left);
            if(v.vt != var::tobj)
            {
                // @todo: convert to object
                return setError(L"Object expected", n->token);
            }

            obj_ptr pvold = ec.vars;
            ec.vars = v.oval;
            v = run(ec, n->left->right);
            ec.vars = pvold;
            return v;
        }
    case	token::kidSwitch:
        {
            v = eval(ec, n->left->left);
            n = n->left->right;
            bool match = false;
            while(n)
            {
                if(!match)
                {
                    if(n->token->kid == token::kidCase)
                    {
                        v2 = eval(ec, n->left->left);
                        match = (v == v2);
                    }
                    else	// Must be default node
                    {
                        match = true;
                    }
                }

                if(match)
                {
                    v3 = run(ec, n->left->right);
                    if(v3.ctl == var::ctlbreak)
                    {
                        break;
                    }					// @todo: return or throw support
                }
                n = n->right;
            }
            return var();
        }

    case	token::kidThrow:
        if(n->left)
            v = eval(ec,n->left);
        else
            v = var();
        v.ctl = var::ctlerror;
        return v;

    case	token::kidTry:
        {
            node_ptr ptryblock = n->left->right;
            node_ptr pcatchblock = n->left->left->left;
            node_ptr pfinallyblock = n->left->left->right;
            if(pfinallyblock == nullNode)
            {
                v = run(ec, ptryblock);
                if(v.ctl != var::ctlerror)
                    return v;
                v2 = execCatch(ec, pcatchblock, v);
                return v2;
            }

            if(pcatchblock == nullNode)
            {
                v = run(ec, ptryblock);
                v2 = run(ec, pfinallyblock->left);
                if(v2.ctl != var::ctlerror)
                {
                    return v;
                }
                return v2;
            }

            v = run(ec, ptryblock);
            v2 = var();
            if(v.ctl == var::ctlerror)
            {
                v2 = execCatch(ec, pcatchblock, v);
            }
            v3 = run(ec, pfinallyblock->left);
            if(v2.ctl == var::ctlerror)
            {
                return v2;
            }
            return v3;
        }

    case	token::kidDelete:
        {
            propDel(ec, n->left);
            return var();
        }


    case	token::kidInstanceOf:
        {
            v = eval(ec, n->left);
            if(v.vt != var::tobj)
            {
                return var(false);
            }

            v2 = eval(ec, n->right);
            if(v2.vt != var::tobj)
            {
                return var(false);
            }

            obj_ptr o = v.oval;
            obj_ptr of = v2.oval;
            object* oo = o.get();
            object* ofo = of.get();
            object* op = NULL;
            object* ofp = NULL;
            
            if(oo->prototype.value.vt == var::tobj)
                op = oo->prototype.value.oval.get();
            
            if(ofo->prototype.value.vt == var::tobj)
                ofp = ofo->prototype.value.oval.get();
            
            
            // Loop through prototype chain looking for a match
            
            for(object* p = op; p != NULL; p = p->prototype.value.oval.get()) {
                if(ofp && ofp->is_equal(p))
                    return var(true);
                if(p->is_equal(ofp))
                    return var(true);
            }
            
            return var(false);
        }

    // 2011-11-16 shark@anui.com - added support for empty statement (kidSemi)
    case    token::kidSemi:
        return var();
        
    case	token::kidCase:
    case	token::kidCatch:
    case	token::kidDefault:
    case	token::kidElse:
    case	token::kidFinally:
    case	token::kidIn:
    case	token::kidRBrace:
    case	token::kidRPar:
//    case	token::kidSemi:
    case	token::kidComma:
    case	token::kidColon:
    default:
        return setError(L"Not implemented", n->token);
    }
    return var();
}

void context::test()
{
    tokenizer l;
    l.tokenize(L"x=2+3;");
    parser p(l);
    node_ptr n = p.parse();
    context ctx(NULL, obj_ptr(new object()));
    var v = ctx.run(ctx, n);
    assert(v.num() == 5);
    l.tokenize(L"function mul(x,y) { return x*y; } z = mul(2,3);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 6);
    l.tokenize(L"function K() { this.x=9; } k = new K(); m = k.x;");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() ==9);
    l.tokenize(L"function K() { this.x= function() { return 8; } } k = new K(); m = k.x();");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() ==8);
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// function class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
function::function()
{
    // Always have a prototype
    prototype.value = obj_ptr(new object());
}

bool function::isNative()
{
    return false;
}

token_ptr get_token(node_ptr n);
token_ptr get_token(node_ptr n) {
    if(n == nullNode)
        return nullToken;
    if(n->token != nullToken)
        return n->token;
    token_ptr t = get_token(n->left);
    if(t != nullToken)
        return t;
    return get_token(n->right);
}

var function::call(context& ec, oarray& args)
{
    debugger* dbg = ec.root->attached_debugger;
    bool isdbg = (dbg != NULL && !dbg->is_basic());
    
#ifdef JS_OPT    
    bool tracktime = false;
#endif
    
    var v;
    node_ptr n = node->left;
    if(isdbg) {
        token_ptr ft = get_token(node);
        if(ft)
            dbg->call_enter(ft->script_index, ft->line, n, &ec);
#ifdef JS_OPT        
        if(ec.root->optimization & context::Opt_FuncTimeProfiler) {
            if(ft && !debug_pif_check(dbg, ft->script_index, ft->line)) {
                tracktime = true;
                gettimeofday(&this->profile_start, NULL);
            }
        }
#endif
    }
    
    for(int a=0; a < args.length() && n != nullNode; a++)
    {
        char_ptr s = n->token->sval->c_str();
        v = args.getat(a);
        prop* p = ec.vars->addprop(s);
        p->value = v;
        n = n->left;
    }

    // 2011-12-07 shark@anui.com - All remaining named arguments must be
    // added and set to null. This avoids creating accidental globals
    // when a function is called with less arguments than it expects
    while(n != nullNode) {
        char_ptr s = n->token->sval->c_str();
        prop* p = ec.vars->addprop(s);
        p->value = var();
        n = n->left;
    }
    var vret =  ec.run(ec, node->right);
    if(isdbg) {
        dbg->call_exit();
        
#ifdef JS_OPT        
        if(tracktime) {
            struct timeval current;
            gettimeofday(&current, NULL);
            int ms2 = current.tv_sec * 1000 + current.tv_usec / 1000;
            int ms1 = profile_start.tv_sec * 1000 + profile_start.tv_usec / 1000;
            profile_duration = ms2 - ms1;
            vector<func_info>& tf = dbg->_script->top_functions;
            token_ptr tp = get_token(this->node);
            func_info fi(tp->script_index, tp->line, this->profile_duration);
            
            // if we exist in the list, then simply update existing value
            for(vector<func_info>::iterator i=tf.begin(); i != tf.end(); i++) {
                func_info o = *i;
                if(o.file == fi.file && o.line == fi.line) {
                    *i = fi;
                    return vret;
                }
            }
            
            if(tf.size() < 5) {
                tf.push_back(fi);
            }
            else {
                for(vector<func_info>::iterator i=tf.begin(); i != tf.end(); i++) {
                    func_info o = *i;
                    if(o.duration < this->profile_duration) {
                        tf.insert(i, fi);
                        break;
                    }
                }
                if(tf.size() > 5) {
                    tf.pop_back();
                }
            }
        }
#endif        
    }
    return vret;
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// math native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
bool isNumChar(wchar_t c);
bool isNumChar(wchar_t c) {
    if(c == ' ' || c == '-' || c == '+')
        return true;
    if(c >= '0' && c <= '9')
        return true;
    
    if(c >= 'a' && c <= 'f')
        return true;
        
    if(c >= 'A' && c <= 'F')
        return true;
    
    if(c == 'e' || c == 'E' || c == '.' || c == 'x' || c == 'X')
        return true;
        
    return false;
}

class parseFloat_object : public native_object
{
    virtual var call_direct(context& ec, char_ptr method, oarray& args)
    {
        wstring s = args.getat(0).wstr();
        double nan = sqrt(-1.0);
        int len = s.length();
        
        for(int i=0; i < len; i++) {
            if(!isNumChar(s[i]))
                return var(nan);
        }

        return var(_wtof(s.c_str()));
    }
};

class isNaN_object : public native_object
{
    virtual var call_direct(context& ec, char_ptr method, oarray& args)
    {
        int argc = args.length();
        if(argc ==0)
            return true;
           
        if(args.getat(0).vt == var::tnum) {
            float f = args.getat(0).c_float();
            if(isnan(f))
                return var(true);
            return var(false);
        }
        
        return var(false);
    }
};
// @todo: support 0x, 0, and radix
class parseInt_object : public native_object {
    virtual var call_direct(context& ec, char_ptr method, oarray& args) {
        wstring s = args.getat(0).wstr();
        double nan = sqrt(-1.0);
        int len = s.length();
        
        for(int i=0; i < len; i++) {
            if(!isNumChar(s[i]))
                return var(nan);
        }
        
        return var(_wtoi(s.c_str()));
    }
};

var mmAbs(context& ec, object* o, oarray& args);
var mmAcos(context& ec, object* o, oarray& args);
var mmAsin(context& ec, object* o, oarray& args);
var mmAtan(context& ec, object* o, oarray& args);
var mmAtan2(context& ec, object* o, oarray& args);
var mmCeil(context& ec, object* o, oarray& args);
var mmCos(context& ec, object* o, oarray& args);
var mmExp(context& ec, object* o, oarray& args);
var mmFloor(context& ec, object* o, oarray& args);
var mmLog(context& ec, object* o, oarray& args);
var mmMax(context& ec, object* o, oarray& args);
var mmMin(context& ec, object* o, oarray& args);
var mmPow(context& ec, object* o, oarray& args);
var mmRandom(context& ec, object* o, oarray& args);
var mmRound(context& ec, object* o, oarray& args);
var mmSin(context& ec, object* o, oarray& args);
var mmSqrt(context& ec, object* o, oarray& args);
var mmTan(context& ec, object* o, oarray& args);

var mmAbs(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var((number) ::fabs(a0));
}

var mmAcos(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(acos(a0));
}

var mmAsin(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(asin(a0));
}

var mmAtan(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(atan(a0));
}

var mmAtan2(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(atan2(a0, args.getat(1).num()));
}

var mmCeil(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(ceil(a0));
}

var mmCos(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(cos(a0));
}

var mmExp(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(exp(a0));
}

var mmFloor(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(floor(a0));

}

var mmLog(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(log(a0));
}

var mmMax(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    number m = a0;
    for(int i=0; i < args.length(); i++)
    {
        number a = args.getat(i).num();
        if(a > m)
            m = a;
    }
    return var(m);
}

var mmMin(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    number m = a0;
    for(int i=0; i < args.length(); i++)
    {
        number a = args.getat(i).num();
        if(a < m)
            m = a;
    }
    return var(m);
} 

var mmPow(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(pow(a0,args.getat(1).num()));
}

var mmRandom(context& ec, object* o, oarray& args) {
    return var((double) rand() / (double)RAND_MAX);
}

var mmRound(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(floor(a0));  // TODO: fix this
}

var mmSin(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(sin(a0));
}

var mmSqrt(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(sqrt(a0));
}

var mmTan(context& ec, object* o, oarray& args) {
    int argc = args.length();
    number a0 = (argc > 0) ? args.getat(0).num() : 0;
    return var(tan(a0));
}

void math_object::reg_cons(context& ec, obj_ptr vars)
{
    obj_type* t = new obj_type(ec);
    t->add_method(L"abs", mmAbs);
    t->add_method(L"acos", mmAcos);
    t->add_method(L"asin", mmAsin);
    t->add_method(L"atan", mmAtan);
    t->add_method(L"atan2", mmAtan2);
    t->add_method(L"ceil", mmCeil);
    t->add_method(L"cos", mmCos);
    t->add_method(L"exp", mmExp);
    t->add_method(L"floor", mmFloor);
    t->add_method(L"log", mmLog);
    t->add_method(L"max", mmMax);
    t->add_method(L"min", mmMin);
    t->add_method(L"pow", mmPow);
    t->add_method(L"random", mmRandom);
    t->add_method(L"round", mmRound);
    t->add_method(L"sin", mmSin);
    t->add_method(L"sqrt", mmSqrt);
    t->add_method(L"tan", mmTan);
        
    obj_ptr m(new math_object());
    m->type = t;
    vars->put(L"Math", var(m));
    obj_ptr o(new parseFloat_object());
    vars->put(L"parseFloat", var(o));
    obj_ptr o2(new parseInt_object());
    vars->put(L"parseInt", var(o2));
    obj_ptr o3(new isNaN_object());
    vars->put(L"isNaN", var(o3));
    m->put(L"E", var(M_E));
    m->put(L"LN10", var(M_LN10));
    m->put(L"LN2", var(M_LN2));
    m->put(L"LOG2E", var(M_LOG2E));
    m->put(L"LOG10E", var(M_LOG10E));
    m->put(L"PI", var(M_PI));
    m->put(L"SQRT1_2", var(M_SQRT1_2));
    m->put(L"SQRT2", var(M_SQRT2));
}

var math_object::call_direct(context& ec, char_ptr method, oarray& args)
{
    wstring s;
    wstlprintf(s, L"Math unexpected method call %ls", method);
    return ec.setError(s.c_str(), NULL);
}

void math_object::test()
{
    tokenizer l;
    l.tokenize(L"x=Math.PI;");
    parser p(l);
    node_ptr n = p.parse();
    context ctx(NULL, obj_ptr(new object()));

    math_object::reg_cons(ctx, ctx.vars);
    var v = ctx.run(ctx, n);
    assert(v.num() == M_PI);

    l.tokenize(L"x=Math.sin(2.4);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == sin(2.4));

    l.tokenize(L"x=parseFloat('12.1');");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 12.1);

    l.tokenize(L"x=Math.min(8,7,9);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 7);

    l.tokenize(L"x=Math.max(8,7,9);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 9);

}



//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// js object native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void js_object::reg_cons(obj_ptr vars)
{
    wstring s;
    obj_ptr o(new js_object());
    vars->put(L"Object", var(o));
    object* op = new object();
    Object_prototype = obj_ptr(op);
    o->prototype.value = var(Object_prototype);
}

var Object_defineProperties(context& ec, object* op, object* props);
var Object_defineProperties(context& ec, object* op, object* props) {
    map<wstring, prop>::iterator pi = props->properties.begin();
    for(; pi != props->properties.end(); pi++)
    {            
        wstring pname = (*pi).first;
        prop* pp = &(*pi).second;
        var adesc = pp->value;
        if(adesc.vt != var::tobj) {
            return ec.setError(L"Object.defineProperties(obj,prop, props) object expected", nullToken);        
        }
        
        obj_ptr desc = adesc.oval;
        prop* p = op->getprop(pname.c_str(), false);
        if(p == NULL) {
            p = op->addprop(pname.c_str());
            if( p == NULL)
                return ec.setError(L"Cannot add property", nullToken);
        }
        p->value = desc->get(L"value");
        p->read_only = !desc->get(L"writeable").c_bool();
        p->dont_enum = !desc->get(L"enumerable").c_bool();
        p->configurable = desc->get(L"configurable").c_bool();            
        
    }  
    return var();
}

var js_object::call_direct(context& ec, char_ptr method, oarray& args)
{
    wstring m = method;
    int argc = args.length();
    
    // constructor
    if(m == L"")
    {
        object* o = new object();
        return var(obj_ptr(o));
    }
    else if(m == L"isExtensible" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.isExtensible(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        if(IS_FLAG(op->flags, OBJ_EXTENSIBLE)) // is_extensible)
            return var(true);
        else   
            return var(false);
    }
    else if(m == L"preventExtensions" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.preventExtensions(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        op->flags = CLEAR_FLAG(op->flags, OBJ_EXTENSIBLE);
//        op->is_extensible = false;
    }
    else if(m == L"getOwnPropertyDescriptor" && argc > 1) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.getOwnPropertyDescriptor(obj,prop) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        wstring pname = args.getat(1).wstr();
        prop* p = op->getprop(pname.c_str(), false);
        object* oret = new object();
        if(p == NULL)
            return var(obj_ptr(oret));
        oret->put(L"configurable", var(p->configurable));
        oret->put(L"writeable", var(!p->read_only));
        oret->put(L"enumerable", var(!p->dont_enum));
        oret->put(L"value", p->value);
        return obj_ptr(oret);        
    }
    else if(m == L"defineProperty" && argc > 2) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.defineProperty(obj,prop, desc) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        wstring pname = args.getat(1).wstr();
        var a2 = args.getat(2);
        if(a2.vt != var::tobj) {
            return ec.setError(L"Object.defineProperty(obj,prop, desc) desc object expected", nullToken);        
        }
        obj_ptr desc = a2.oval;
        prop* p = op->getprop(pname.c_str(), false);
        if(p == NULL) {
            p = op->addprop(pname.c_str());
            if( p == NULL)
                return ec.setError(L"Cannot add property", nullToken);
        }
        p->value = desc->get(L"value");
        p->read_only = !desc->get(L"writeable").c_bool();
        p->dont_enum = !desc->get(L"enumerable").c_bool();
        p->configurable = desc->get(L"configurable").c_bool();
    }
    else if(m == L"defineProperties" && argc > 1) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.defineProperties(obj,prop, desc) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        var a2 = args.getat(1);
        if(a2.vt != var::tobj) {
            return ec.setError(L"Object.defineProperties(obj,prop, props) object expected", nullToken);        
        }
        obj_ptr props = a2.oval;
        
        return Object_defineProperties(ec, op.get(), props.get());                          
    }
    else if(m == L"keys" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.keys(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        map<wstring, prop>::iterator pi = op->properties.begin();
        int count = 0;
        for(; pi != op->properties.end(); pi++)
        {            
            prop* pp = &(*pi).second;
            if(pp->dont_enum)
                continue;
            count++;
        }
        
        oarray* oa = new oarray(count);
        int index = 0;
        pi = op->properties.begin();
        for(; pi != op->properties.end(); pi++)
        {            
            prop* pp = &(*pi).second;
            if(pp->dont_enum)
                continue;
            wstring pname = (*pi).first;
            
            var adesc = pp->value;
            oa->putat(index++, var(pname));
        }
        return var(obj_ptr(oa));
    }
    else if(m == L"getOwnPropertyNames" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.getOwnPropertyNames(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        map<wstring, prop>::iterator pi = op->properties.begin();
        int count = 0;
        for(; pi != op->properties.end(); pi++)
        {            
            count++;
        }
        
        oarray* oa = new oarray(count);
        int index = 0;
        pi = op->properties.begin();
        for(; pi != op->properties.end(); pi++)
        {            
            prop* pp = &(*pi).second;
            wstring pname = (*pi).first;
            
            var adesc = pp->value;
            oa->putat(index++, var(pname));
        }
        return var(obj_ptr(oa));
    }
    else if(m == L"create" && argc > 1) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.create(proto, props) object expected", nullToken);
        }
        obj_ptr proto = a0.oval;
        var a2 = args.getat(1);
        if(a2.vt != var::tobj) {
            return ec.setError(L"Object.create(proto, props) object expected", nullToken);        
        }
        obj_ptr props = a2.oval;
    
        object* op = new object();
        op->put(L"prototype", a0);
        var e = Object_defineProperties(ec, op, props.get());
        if(e.ctl == var::ctlerror)
            return e;
        return var(obj_ptr(op));
    }
    else if(m == L"seal" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.seal(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        map<wstring, prop>::iterator pi = op->properties.begin();
        pi = op->properties.begin();
        for(; pi != op->properties.end(); pi++)
        {            
            prop* pp = &(*pi).second;
            pp->configurable = false;
        }
        op->flags = CLEAR_FLAG(op->flags, OBJ_EXTENSIBLE);
//        op->is_extensible = false;    
    }
    else if(m == L"isSealed" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.isSealed(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        if(IS_FLAG(op->flags, OBJ_EXTENSIBLE)) //  op->is_extensible)
            return var(false);
        map<wstring, prop>::iterator pi = op->properties.begin();
        pi = op->properties.begin();
        for(; pi != op->properties.end(); pi++)
        {            
            prop* pp = &(*pi).second;
            if(pp->configurable)
                return var(false);
        }
        return var(true);
    }
    else if(m == L"freeze" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.freeze(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        map<wstring, prop>::iterator pi = op->properties.begin();
        pi = op->properties.begin();
        for(; pi != op->properties.end(); pi++)
        {            
            prop* pp = &(*pi).second;
            pp->configurable = false;
            pp->read_only = true;
        }
//        op->is_extensible = false;
        op->flags = CLEAR_FLAG(op->flags, OBJ_EXTENSIBLE);
    }
    else if(m == L"isFrozen" && argc > 0) {
        var a0 = args.getat(0);
        if(a0.vt != var::tobj) {
            return ec.setError(L"Object.isFrozen(obj) object expected", nullToken);
        }
        obj_ptr op = a0.oval;
        if(IS_FLAG(op->flags, OBJ_EXTENSIBLE)) // op->is_extensible)
            return var(false);
        map<wstring, prop>::iterator pi = op->properties.begin();
        pi = op->properties.begin();
        for(; pi != op->properties.end(); pi++)
        {            
            prop* pp = &(*pi).second;
            if(pp->configurable)
                return var(false);
            if(!pp->read_only)
                return var(false);
        }
        return var(true);
    }    
    return var();
}



//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// number native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void number_object::reg_cons(obj_ptr vars) {
    obj_ptr o(new number_object(0));
    o->put(L"MAX_VALUE", var(1.7976931348623157e308));
    o->put(L"MIN_VALUE", var(5e-324));
    o->put(L"POSITIVE_INFINITY", var(INFINITY));
    o->put(L"NEGATIVE_INFINITY", var(-INFINITY));
    vars->put(L"Number", var(o));
}

var number_object::defaultValue(js::context &ec, js::object::dv hint) {
    if(hint == object::DVHINT_STR)
        return var(this->value.wstr().c_str());
    return var(this->value.nval);
}

number_object::number_object(const var& initial) {
    value = initial;
}

number_object::number_object(const number initial) {
    value = var(initial);
}

var number_object::call_direct(context& ec, char_ptr method, oarray& args) {
    wstring m = method;
    int argc = args.length();
    
    // constructor
    if(m == L"")
    {
        if(argc > 0)
        {
            number n = args.getat(0).num();
            obj_ptr o(new number_object(n));
            return var(o);
        }
    }
    
    if(m == L"toString") {
        return var(this->value.wstr().c_str());
    }
    return object::call_direct(ec, method, args);
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// boolean native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void boolean_object::reg_cons(obj_ptr vars) {
    obj_ptr o(new boolean_object(false));
    vars->put(L"Boolean", var(o));
}

var boolean_object::defaultValue(js::context &ec, js::object::dv hint) {
    if(hint == object::DVHINT_STR) {
        return var(this->value.wstr().c_str());
    }
    return var(this->value.num());
}

boolean_object::boolean_object(const var& initial) {
    value = initial;
}

boolean_object::boolean_object(const bool initial) {
    value = var(initial);
}

var boolean_object::call_direct(context& ec, char_ptr method, oarray& args) {
    wstring m = method;

    int argc = args.length();
    
    // constructor
    if(m == L"")
    {
        if(argc > 0)
        {
            bool b = args.getat(0).c_bool();
            obj_ptr o(new boolean_object(b));
            return var(o);
        }
    }
    
    if(m == L"toString") {
        return var(this->value.wstr().c_str());
    }
    return object::call_direct(ec, method, args);
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// string native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void update_length(string_object* so)
{
    int length = so->value.wstr().length();
    so->put(L"length", var(length));
}

var string_object::defaultValue(js::context &ec, js::object::dv hint) {
    if(hint == object::DVHINT_STR)
        return var(this->value.sval);
    return var(this->value.num());
}

void string_object::reg_cons(obj_ptr vars)
{
    wstring s;
    obj_ptr o(new string_object(s));
    vars->put(L"String", var(o));
}

string_object::string_object(const wstring& initial) 
{
    value = var(initial.c_str());
    update_length(this);
}

string_object::string_object(const var& initial) : value(initial)
{
    update_length(this);
}

match_state string_object::splitMatch(wstring& S, int q, var R) {
    if(R.vt == var::tobj) {
        regex_obj* ro = (regex_obj*) R.oval.get();
        return ro->match(S, q);
    }
    
    wstring RS = R.wstr();
    int r = RS.size();
    int s = S.size();
    if(q+r > s)
        return match_state();
    for(int i=0; i < r; i++) {
        if(S[q+i] != RS[i])
            return match_state();
    }
    return match_state(q+r);
}


wstring string_object::replace(context& ec, wstring &S, wstring &match, int start, int end, js::oarray *cap, js::var rep) {
    int m = 0;
    if(cap != NULL) 
        m = cap->length();
        
    if(rep.vt == var::tobj) {   // function case
        oarray args(m+3);
        args.putat(0, var(match));
        for(int i=0; i < m; i++) {
            args.putat(i+1, cap->getat(i));
        }
        args.putat(m+1, var(start));
        args.putat(m+2, var(S));
        
        obj_ptr f = rep.oval;
        var vr = f->call(ec, args);
        return vr.wstr();
    }
    else {  // string template case
        wstring T = rep.wstr();
        int t = T.length();
        wstring R = L"";
        for(int i=0; i < t; i++) {
            jschar c = T[i];
            if(c == '$' && i < (t-1)) {
                jschar c2 = T[i+1];
                if(c2 == '$')
                    R += '$';
                else if(c2 == '&')
                    R += match;
                else if(c2 == '`') 
                    R += S.substr(0, start);
                else if(c2 == '\'') 
                    R += S.substr(end);
                else if(isnumber(c2)) {
                    int n = c2 - '0';
                    if(i < (t - 2)) {
                        jschar c3 = T[i+2];
                        if(isnumber(c3)) {
                            n = n*10 + c3 - '0';
                            i++;
                        }
                    }
                    
                    if(cap != NULL && n < cap->length()) {
                        var v = cap->getat(n);
                        if(v.vt != var::tundef)
                            R += v.wstr();
                    }
                }
                i++;
            }
            else {
                R.push_back(c);
            }
        }
        return R;
    }
}

var string_object::call_direct(context& ec, char_ptr method, oarray& args)
{
    wstring m = method;
    int argc = args.length();

    // constructor
    if(m == L"")
    {
        if(argc > 0)
        {
            wstring s = args.getat(0).wstr();
            obj_ptr o(new string_object(var(s)));
            return var(o);
        }
    }
	else if(m == L"fromCharCode")
	{
        wstring s = L"";
        s.reserve(argc);
        for(int a=0; a < argc; a++)
        {
            jschar c = (jschar) args.getat(a).c_int();
            s += c;
        }

        return var(s.c_str());
	}
	else if(m == L"toString")
	{
        wstring s = value.wstr();
        return var(s);
	}
	else if(m == L"charAt")
	{
        wstring s;
        if(argc > 0)
        {
            int i = args.getat(0).c_int();
            if(i < (int) value.sval->length())
            {
                wstring v = value.wstr();
                jschar c = v.c_str()[i];
                s += c;
                return var(s);
            }
        }
        return var();

	}
	else if(m == L"charCodeAt")
	{
        if(argc > 0)
        {
            int i = args.getat(0).c_int();
            if(i < (int) value.sval->length())
            {
                wstring v = value.wstr();
                jschar c = v.c_str()[i];
                return var((int) c);
            }
        }
        return var();
	}
	else if(m == L"concat")
	{
        wstring s = value.wstr();
        for(int a=0; a < argc; a++)
            s+= args.getat(a).wstr();
        return var(s);
	}
	else if(m == L"indexOf")
	{
        if(argc > 0)
        {
            int pos = 0;
            if(argc > 1)
                pos = args.getat(1).c_int();

            wstring s = args.getat(0).wstr();
            size_t i = value.sval->find(s, pos);
            return var((int) i);
        }
        return var();
	}
	else if(m == L"lastIndexOf")
	{
        if(argc > 0)
        {
            int pos = 0;
            if(argc > 1)
                pos = args.getat(1).c_int();

            wstring s = args.getat(0).wstr();
            size_t i = value.sval->find_last_of(s, pos);
            return var((int) i);
        }
        return var();
	}
	else if(m == L"substring")
	{
        int start = 0;
        int end = value.sval->length();

        if(argc > 0)
            start = args.getat(0).c_int();

        if(argc > 1)
            end = args.getat(1).c_int();

        wstring s = value.sval->substr(start, end);
        return var(s);
	}
	else if(m == L"toLowerCase")
	{
        int len = (int) value.sval->length();
        wstring s = *(value.sval);
        for(int i=0; i < len; i++)
        {
            s[i] = tolower(s[i]);
        }

        return var(s.c_str());
	}
	else if(m == L"toUpperCase")
	{
        int len = (int) value.sval->length();
        wstring s = *(value.sval);
        for(int i=0; i < len; i++)
        {
            s[i] = toupper(s[i]);
        }

        return var(s.c_str());
	}
    else if(m == L"match" && argc > 0) {
        regex_obj* ro;
        bool del = false;
        
        if(args.getat(0).vt == var::tobj) {
            ro = (regex_obj*) args.getat(0).oval.get();
        }
        else {
            wstring p = args.getat(0).wstr();
            wstring f = L"";
            ro = regex_obj::create(p, f);
            del = true;
        }
        
        wstring s= *(value.sval);
        oarray* oa = ro->exec(s);
        if(del)
            delete ro;

        if(oa == NULL) {   
            var v;
            v.vt = var::tnull;
            return v;
        }
        
        obj_ptr o(oa);
        return var(o);
    }
    else if(m == L"search" && argc > 0) {
        var vreg = args.getat(0);
        if(vreg.vt != var::tobj) {
            wstring flags = L"";
            wstring pattern = vreg.wstr();
            regex_obj* ro = regex_obj::create(pattern, flags);
            obj_ptr pro(ro);            
            vreg = var(pro);
        }
        int len = this->value.sval->size();
        wstring* S = (wstring*) this->value.sval;
        for(int q=0; q < len; q++) {
            match_state z = splitMatch(*S, q, vreg);
            if(!z.failed())
                return var(q);
        }
        return var(-1);
    }
    else if(m == L"split" && argc > 0) {
        var separator = args.getat(0);
        unsigned int limit;
        if(argc > 1) 
            limit = (unsigned int) args.getat(1).c_int();
        else
            limit = (unsigned int) (pow(2, 32) - 1);
            
        wstring S = this->value.wstr();
        int s = S.size();
        int p = 0;
        oarray* A = new oarray(0);
        int lengthA = 0;
        var R = separator;

        if(limit == 0)
            return var(obj_ptr(A));
        
        if(separator.vt == var::tundef) {
            A->putat(0, var(S));
            return var(obj_ptr(A));
        }
        
        if(s == 0) {
            match_state State = splitMatch(S, 0, R);
            if(!State.failed())
                return var(obj_ptr(A));
            A->putat(0, var(S));
            return var(obj_ptr(A));
        }
        
        int q = p;
        while(q != s) {
            match_state z = splitMatch(S, q, R);
            if(z.failed())
                q = q + 1;
            else {
                int e = z.endIndex;
                oarray* cap = z.captures;
                if(e == p)
                    q = q + 1;
                else {
                    wstring T = S.substr(p, q-p);
                    A->putat(lengthA, var(T));
                    lengthA++;
                    if(lengthA == limit)
                        return var(obj_ptr(A));
                    p = e;
                    int i = 1;
                    while(cap && i != cap->length()) {
                        wstring v = cap->getat(i).wstr();
                        A->putat(lengthA, cap->getat(i));
                        i = i + 1;
                        lengthA++;
                        if(lengthA == limit)
                            return var(obj_ptr(A));
                    }
                    q = p;
                }
            }
        
        }
        wstring T = S.substr(p, s-p);
        A->putat(lengthA, var(T));
        return var(obj_ptr(A));        
    }
    else if(m == L"replace" && argc > 1) {
        var searchValue = args.getat(0);
        var replaceValue = args.getat(1);
        wstring* S = (wstring*) this->value.sval;
        wstring R;
        int s = S->length();
        
        // RegEx case
        if(searchValue.vt == var::tobj) {  
            regex_obj* rx = (regex_obj*) searchValue.oval.get();
            bool global = rx->global;
            rx->put(L"startIndex", var(0));
            int start = 0;
            int offset;
            
            while(start < s) {
                match_state z = rx->firstMatch(*S, start, &offset);
                if(z.failed())
                    break;
                
                R += S->substr(start, offset-start);                                
                wstring match = S->substr(offset, z.endIndex - offset);
                start = z.endIndex;
                wstring r = replace(ec, *S, match, offset, z.endIndex, z.captures, replaceValue);
                R += r;
                if(!global)
                    break;
            }
            R += S->substr(start);
            return var(R);
        }
        else { // string case
            wstring M = searchValue.wstr();
            int m = M.length();
            int start = 0;
            int c;
            for(c=0; c < (s-m); c++) {
                wstring match = S->substr(c, m);
                if(match == M) {
                    R += S->substr(start, c - start);
                    start = c + m;
                    wstring r = replace(ec, *S, match, c, start, NULL, replaceValue);
                    R += r;
                    c += m-1;   // account for the for loop c++ above
                }
            }
            R += S->substr(start);
            return var(R);
        }
    }
    return var();
}

void string_object::test()
{
    tokenizer l;
    l.tokenize(L"x=new String('hello'); z=x.length;");
    parser p(l);
    node_ptr n = p.parse();
    context ctx(NULL, obj_ptr(new object()));

    string_object::reg_cons(ctx.vars);
    var v = ctx.run(ctx, n);
    assert(v.num() == 5);

    l.tokenize(L"m=String.fromCharCode(65,66,67);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.wstr() == L"ABC");

    l.tokenize(L"m='XY';z=m.length;");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 2);

    l.tokenize(L"m='XYZ';z=m.charAt(2);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.wstr() == L"Z");

    l.tokenize(L"m='ABC';z=m.charCodeAt(1);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 66);

    l.tokenize(L"m='ABC';y=m.concat('XYZ');");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.wstr() == L"ABCXYZ");

    l.tokenize(L"m='0123abc789';y=m.indexOf('abc');");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 4);

    l.tokenize(L"m='0123abc789';y=m.indexOf('wbc');");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == -1);

    l.tokenize(L"m='0123abc789abcwww';y=m.indexOf('abc',5);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 10);

/*
    l.tokenize(L"m='0123abc789abcwww';y=m.lastIndexOf('abc',0);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == 10);

    l.tokenize(L"m='0123abc789abcwww';y=m.lastIndexOf('abc',9);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.num() == -1);
*/

    l.tokenize(L"m='0123abc789abcwww';y=m.substring(4,3);");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.wstr() == L"abc");

    l.tokenize(L"m='abc123';y=m.toUpperCase();");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.wstr() == L"ABC123");

    l.tokenize(L"m='ABC123';y=m.toLowerCase();");
    p.lex = l;
    n = p.parse();
    v = ctx.run(ctx, n);
    assert(v.wstr() == L"abc123");

}


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// date native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#define MsPerDay            86400000.0
#define HoursPerDay         24.0
#define MinutesPerHour      60.0
#define SecondsPerMinute    60.0
#define MsPerSecond         1000.0
#define MsPerMinute         (MsPerSecond * SecondsPerMinute)
#define MsPerHour           (MsPerMinute * MinutesPerHour)
#define MsPerYear           (MsPerHour * HoursPerDay * 365.0)

int Day(number t);
int Day(number t) {
    return floor(t / MsPerDay);
}

number TimeWithinDay(number t);
number TimeWithinDay(number t) {
    return fmod(t, MsPerDay);
}

int DaysInYear(int y);
int DaysInYear(int y) {
    if((y % 4) != 0.0)
        return 365;
    if((y % 4) == 0 && (y % 100) != 0)
        return 366;
    if((y % 100) == 0 && (y % 400) != 0)
        return 365;
    if((y % 400) == 0)
        return 366;
    return 365;
}

int DayFromYear(number y);
int DayFromYear(number y) {
    return 365 * (y - 1970.0) + floor((y - 1969.0)/4.0) - 
           floor((y - 1901.0)/100) + floor((y - 1601)/400.0);
}

number TimeFromYear(number y);
number TimeFromYear(number y) {
    return MsPerDay * DayFromYear(y);
}

int YearFromTime(int s, int e, int i, number t);
int YearFromTime(int s, int e, int i, number t) {
    if(i < 1) 
        return s;
    // 2013-03-04 shark@anui.com - must account for the last year
    // since this didn't work for 1999
    for(int y = s; y <= e; y += i) {
        number yt = TimeFromYear(y);
        if(yt > t) {
            if(i == 1)
                return (y-1);
            return YearFromTime(y-i, y, i/10, t);
        }
    }
    return e + 1;
}
int YearFromTime(number t);      
int YearFromTime(number t) {
    int y = YearFromTime(1000, 3000, 100, t);
    if(y <= 3000)
        return y;
    
    return YearFromTime(-10000, 10000, 100, t);
}

int InLeapYear(number t);
int InLeapYear(number t) {
    if(DaysInYear(YearFromTime(t)) == 365.0)
        return 0;
    return 1;
}

number DayWithinYear(number t);
number DayWithinYear(number t) {
    int day = Day(t);
    int yt = YearFromTime(t);
    int dy = DayFromYear(yt);
    return day - dy;
}

int MonthFromTime(number t);
int MonthFromTime(number t) {
    int d = DayWithinYear(t);
    bool l = InLeapYear(t);
    if(0 <= d && d < 31)
        return 0;
        
    if(31 <= d && d < (59 + l))
        return 1;
    
    if((59+l) <= d && d < (90+l))
        return 2;
    
    if((90+l) <= d && d < (120+l))
        return 3;
    
    if((120+l) <= d && d < (151+l))
        return 4;
        
    if((151+l) <= d && d < (181+l))
        return 5;
        
    if((181+l) <= d && d < (212+l))
        return 6;
 
    if((212+l) <= d && d < (243+l))
        return 7;
    
    if((243+l) <= d && d < (273+l))
        return 8;
        
    if((273+l) <= d && d < (304+l))
        return 9;
        
    if((304+l) <= d && d < (334+l))
        return 10;
        
    if((334+l) <= d && d < (365+l))
        return 11;
    
    return 11;
}

int DateFromTime(number t);
int DateFromTime(number t) {
    int d = DayWithinYear(t);
    int m = MonthFromTime(t);
    int l = InLeapYear(t);
    
    switch(m) {
        case 0:
            return d + 1;
        case 1:
            return d - 30;
        case 2:
            return d - 58 - l;
        case 3:
            return d - 89 - l;
        case 4:
            return d - 119 - l;
        case 5:
            return d - 150 - l;
        case 6:
            return d - 180 - l;
        case 7:
            return d - 211 - l;
        case 8:
            return d - 242 - l;
        case 9:
            return d - 272 - l;
        case 10:
            return d - 303 - l;
        case 11:
            return d - 333 - l;    
    }
    return d - 333 - l;
}

int WeekDay(number t);
int WeekDay(number t) {
    return (Day(t) + 4) % 7;
}


number LocalTZA();
number LocalTZA() {
    time_t l;    
    time(&l);
    struct tm *m = gmtime(&l);
    time_t g = mktime(m);
    
    double delta = difftime(l, g);
    return delta * MsPerSecond;
}
number DaylightSavingTA(number t);
number DaylightSavingTA(number t) {
    return 0.0;
}

number LocalTime(date_object*d, number t);
number LocalTime(date_object*d, number t) {
    return t + d->_localTZA + DaylightSavingTA(t);
}

number UTC(date_object*d, number t);
number UTC(date_object*d,number t) {
    return t - d->_localTZA - DaylightSavingTA(t - d->_localTZA);
}


number HourFromTime(number t);
number HourFromTime(number t) {
    number h=  fmod(floor(t/MsPerHour), HoursPerDay);
    if(h < 0)
        return HoursPerDay + h;
    return h;
}

number MinFromTime(number t);
number MinFromTime(number t) {
    number mn =  fmod(floor(t/MsPerMinute), MinutesPerHour);
    if(mn < 0)
        return 60.0 + mn;
    return mn;
}

number SecFromTime(number t);
number SecFromTime(number t) {
    number s = fmod(floor(t/MsPerSecond), SecondsPerMinute);
    if(s < 0)
        return 60.0 + s;
    return s;
}

number MsFromTime(number t);
number MsFromTime(number t) {
    number ms = fmod(t, MsPerSecond);
    if(ms < 0)
        return 1000 + ms;
    return ms;
}

number MakeTime(number h, number m, number s, number ms);
number MakeTime(number  h,number m, number s, number ms) {
    return  h * MsPerHour + m * MsPerMinute + s * MsPerSecond + ms;
}

number MakeDay(number y, number m, number d);
number MakeDay(number y, number m, number d) {
    if(isinf(y) || isinf(m) || isinf(d))
        return std::numeric_limits<double>::quiet_NaN();
        
    int yi = (int) y;
    int mi = (int) m;
    int di = (int) d;
    int ym = yi +  floor(m/12);
    int mn = mi % 12;
    
    number t = TimeFromYear(ym);
    int day;
    for(day = 0; day < 366; day++) {
        number t2 = t + day*MsPerDay;
        int m2 = MonthFromTime(t2);
        if(m2 == mn) {
            return Day(t2) + di - 1;
            
        }
    }
    
    return std::numeric_limits<double>::quiet_NaN();
}

number MakeDate(number day, number time);
number MakeDate(number day, number time) {
    return day * MsPerDay + time;
}

number TimeClip(number t);
number TimeClip(number t) {
    if(isinf(t))
        return std::numeric_limits<double>::quiet_NaN();
    if(fabs(t) > 8.64e15)
        return std::numeric_limits<double>::quiet_NaN();
    return t;
}


//////////////////////////////////////////////////////////////////////// 
// ISO 8601 Date Time Format
//
// YYYY-MM-DDTHH:mm:ss.sssZ
// YYYY-MM-DDTHH:mm:ss.sss+HH:mm
// YYYY-MM-DDTHH:mm:ss.sss-HH:mm
// +YYYYYY-MM-DDTHH:mm:ss.sssZ
// -YYYYYY-MM-DDTHH:mm:ss.sssZ
//////////////////////////////////////////////////////////////////////// 
date_object* DateTimeParseIso(const wchar_t* ds);
date_object* DateTimeParseIso(const wchar_t* ds) {
    int index = 0, len = wcslen(ds);
    int YYYY = 0, MM = 1, DD = 1, hh = 0, mm = 0, ss = 0, sss = 0;
    int zh = 0, zm = 0;
    number tza = 0;
    
    if(len < 4)
        return 0;
    // get year
    YYYY = _wtoi(ds + index);
    index += 4;
    // check for '-'
    if(index < len && ds[index] == '-') {
        index++;
        MM = _wtoi(ds+index);
        index += 2;
        if(index < len && ds[index] == '-') {
            index++;
            DD = _wtoi(ds+index);
            index += 2;
        }
    }
    if(index < len && ds[index] == 'T') {
        index++;
        hh = _wtoi(ds + index);
        index += 2;
        if(index < len) {
            if(ds[index] == ':') {
                index++;
                mm = _wtoi(ds + index);
                index += 2;
                if(index < len && ds[index] == ':') {
                    index++;
                    ss = _wtoi(ds + index);
                    index += 2;
                    if(index < len && ds[index] == '.') {
                        index++;
                        sss = _wtoi(ds + index);
                        index += 3;
                    }                    
                }
            }
        }
    }
    int mult = 1;
    
    if(index < len && ds[index] == 'Z') {
        zh = 0;
        zm = 0;
        mult = 0;
        index++;
    }
    
    if(index < len && ds[index] == '+') {
        mult = 1;
        index++;
        zh = _wtoi(ds+index);
        index += 2;
        if(index < len && ds[index] == ':') {
            index++;
            zm = _wtoi(ds+index);
        }
    }
    if(index < len && ds[index] == '-') {
        mult = -1;
        index++;
        zh = _wtoi(ds+index);
        index += 2;
        if(index < len && ds[index] == ':') {
            index++;
            zm = _wtoi(ds+index);
        }
    }
    
    tza = mult* (zh * MsPerHour + zm * MsPerMinute);
    
    number md = MakeDay(YYYY, MM-1, DD);
    number mt = MakeTime(hh, mm, ss, sss);
    number final = MakeDate(md, mt);
    
    date_object*  d = new date_object(final);
    d->_localTZA = tza;
    return d;
}


void date_object::reg_cons(obj_ptr vars)
{
    obj_ptr o(new date_object(0));
    vars->put(L"Date", var(o));
}

var date_object::defaultValue(js::context &ec, js::object::dv hint) {
    if(hint == object::DVHINT_NONE) hint = object::DVHINT_STR;
    oarray args(0);
    if(hint == object::DVHINT_STR) {
        return this->call_direct(ec, L"toString", args);
    }
    return this->call_direct(ec, L"valueOf", args);
}

date_object::date_object(number initial)
{
    _value = initial;
    _localTZA = LocalTZA();
}

// @todo: support calling Date() not as a constructor

var date_object::call_direct(context& ec, char_ptr method, oarray& args)
{
    wstring m = method;
    int argc = args.length();

    // constructor
    if(m == L"") {
        if(argc > 1) {
            number y, m, d=1, h=0, mn=0, s=0, ms=0;
            y = args.getat(0).num();
            if(y <= 99.0)
                y += 1900.0;
                
            m = args.getat(1).num();
            if(argc > 2) 
                d = args.getat(2).num();
            if(argc > 3)
                h = args.getat(3).num();
            if(argc > 4)
                mn = args.getat(4).num();
            if(argc > 5)
                s = args.getat(5).num();
            if(argc > 6)
                ms = args.getat(6).num();
            
            number md = MakeDay(y, m, d);
            number mt = MakeTime(h, mn, s, ms);
            number final = MakeDate(md, mt);
            number value = TimeClip(UTC(this, final));
            obj_ptr o(new date_object(value));
            return  var(o);
                
        }
        else if(argc > 0) {
            if(args.getat(0).vt == var::tstr) {
                date_object* d = DateTimeParseIso(args.getat(0).sval->c_str());
                obj_ptr o(d);
                return var(o);
            }
            number value = args.getat(0).num();
            obj_ptr o(new date_object(value));
            return  var(o);
        }
        else {
            struct timeval current;
            gettimeofday(&current, NULL);
            number ms = current.tv_usec / 1000.0;
            time_t rawtime;
            struct tm * ptm;
            time ( &rawtime );
            ptm = gmtime ( &rawtime );
            number value = MakeDate(MakeDay(ptm->tm_year+1900.0, ptm->tm_mon, ptm->tm_mday),
                           MakeTime(ptm->tm_hour, ptm->tm_min, ptm->tm_sec, ms));          
            obj_ptr o(new date_object(value));
            return  var(o);
        }
    }
	else if(m == L"parse" && argc > 0) {

	}
    else if(m == L"now") {
            struct timeval current;
            gettimeofday(&current, NULL);
            number ms = current.tv_usec / 1000.0;
            time_t rawtime;
            struct tm * ptm;
            time ( &rawtime );
            ptm = gmtime ( &rawtime );
            number value = MakeDate(MakeDay(ptm->tm_year+1900.0, ptm->tm_mon, ptm->tm_mday),
                           MakeTime(ptm->tm_hour, ptm->tm_min, ptm->tm_sec, ms));          
            return var(value);
    }
	else if(m == L"toString" || m == L"toLocaleString") {
        wstring res;
        number v = LocalTime(this, _value);
        int h = (int) HourFromTime(v);
        int mn = (int) MinFromTime(v);
        int s = (int) SecFromTime(v);
        int y = (int) YearFromTime(v);
        int mo = (int) MonthFromTime(v)+1;
        int d = (int) DateFromTime(v);
        wstlprintf(res, L"%04d/%02d/%02d %02d:%02d:%02d", y, mo, d, h, mn, s);
        return var(res);
	}
	else if(m == L"toDateString" || m == L"toLocaleDateString") {
        wstring res;
        number v = LocalTime(this, _value);
        int y = (int) YearFromTime(v);
        int mo = (int) MonthFromTime(v)+1;
        int d = (int) DateFromTime(v);
        wstlprintf(res, L"%02d-%02d-%02d", y, mo, d);
        return var(res);
	}
	else if(m == L"toTimeString" || m == L"toLocaleTimeString") {
        wstring res;
        number v = LocalTime(this, _value);
        int h = (int) HourFromTime(v);
        int mn = (int) MinFromTime(v);
        int s = (int) SecFromTime(v);
        wstlprintf(res, L"%02d:%02d:%02d", h, mn, s);
        return var(res);
	}
	else if(m == L"valueOf") {
        return var(this->_value);
	}
	else if(m == L"getTime") {
        return var(this->_value);
	}
	else if(m == L"getFullYear") {
        int y = YearFromTime(LocalTime(this, this->_value));
        return var(y);
	}
	else if(m == L"getUTCFullYear") {
        int y = YearFromTime(this->_value);
        return var(y);
	}
	else if(m == L"getMonth") {
        int m =  MonthFromTime(LocalTime(this, _value));
        return var(m);
	}
	else if(m == L"getUTCMonth") {
        int m = MonthFromTime(_value);
        return var(m);
	}
	else if(m == L"getDate") {
        int d = DateFromTime(LocalTime(this, _value));
        return var(d);
	}
	else if(m == L"getUTCDate") {
        int d = DateFromTime(_value);
        return var(d);
	}
	else if(m == L"getDay") {
        int d = WeekDay(LocalTime(this, _value));
        return var(d);
	}
	else if(m == L"getUTCDay") {
        int d = WeekDay(_value);
        return var(d);
	}
	else if(m == L"getHours") {
        int h = HourFromTime(LocalTime(this, _value));
        return var(h);
	}
	else if(m == L"getUTCHours") {
        int h = HourFromTime(_value);
        return var(h);
	}
	else if(m == L"getMinutes") {
        int mn = MinFromTime(LocalTime(this,_value));
        return var(mn);
	}
	else if(m == L"getUTCMinutes") {
        int mn = MinFromTime(_value);
        return var(mn);
	}
	else if(m == L"getSeconds") {
        int sec = SecFromTime(LocalTime(this,_value));
        return var(sec);
	}
	else if(m == L"getUTCSeconds") {
        int sec = SecFromTime(_value);
        return var(sec);
	}
	else if(m == L"getMilliseconds") {
        int ms = MsFromTime(LocalTime(this, _value));
        return var(ms);
	}
	else if(m == L"getUTCMilliseconds") {
        int ms = MsFromTime(_value);
        return var(ms);
	}
	else if(m == L"getTimezoneOffset") {
        number n = (_value - LocalTime(this, _value)) / MsPerMinute;
        return var(n);
	}
	else if(m == L"setTime" && argc > 0) {
        number t = args.getat(0).num();
        _value = t;
        return var();
	}
	else if(m == L"setMilliseconds" && argc > 0) {
        number ms = args.getat(0).num();
        number t = LocalTime(this, _value);
        number h = HourFromTime(t);
        number m = MinFromTime(t);
        number s = SecFromTime(t);
        number tm = MakeTime(h, m, s, ms);
        number u = TimeClip((MakeDate(Day(t), tm)));
        _value = u;
        return  var(u);
    }
	else if(m == L"setUTCMilliseconds" && argc > 0) {
        number ms = args.getat(0).num();
        number t = _value;
        number h = HourFromTime(t);
        number m = MinFromTime(t);
        number s = SecFromTime(t);
        number tm = MakeTime(h, m, s, ms);
        number u = TimeClip((MakeDate(Day(t), tm)));
        _value = u;
        return  var(u);
	}
	else if(m == L"setSeconds" && argc > 0) {
        number t = LocalTime(this, _value);
        number s = args.getat(0).num();
        number ms = MsFromTime(t);
        if(argc > 1)
            ms = args.getat(1).num();
        number d = MakeDate(Day(t), MakeTime(HourFromTime(t), MinFromTime(t), s, ms));
        number u = TimeClip(UTC(this, d));
        _value = u;
        return var(u);
	}
	else if(m == L"setUTCMilliseconds") {
        number t = _value;
        number s = args.getat(0).num();
        number ms = MsFromTime(t);
        if(argc > 1)
            ms = args.getat(1).num();
        number d = MakeDate(Day(t), MakeTime(HourFromTime(t), MinFromTime(t), s, ms));
        number u = TimeClip((d));
        _value = u;
        return var(u);
	}
	else if(m == L"setMinutes" && argc > 0) {
        number t = LocalTime(this, _value);
        number mn = args.getat(0).num();
        number sec = SecFromTime(t);
        if(argc > 1)
            sec = args.getat(1).num();
        number ms = MsFromTime(t);
        if(argc > 2)
            ms = args.getat(2).num();
        number d = MakeDate(Day(t), MakeTime(HourFromTime(t), mn, sec, ms));
        number u = TimeClip(UTC(this, d));
        _value = u;
        return var(u);
	}
	else if(m == L"setUTCMinutes" && argc > 0) {
        number t = _value;
        number mn = args.getat(0).num();
        number sec = SecFromTime(t);
        if(argc > 1)
            sec = args.getat(1).num();
        number ms = MsFromTime(t);
        if(argc > 2)
            ms = args.getat(2).num();
        number d = MakeDate(Day(t), MakeTime(HourFromTime(t), mn, sec, ms));
        number u = TimeClip((d));
        _value = u;
        return var(u);
	}
	else if(m == L"setHours" && argc > 0) {
        number t = LocalTime(this, _value);
        number h = args.getat(0).num();
        number mn = MinFromTime(t);
        number sec = SecFromTime(t);
        number ms = MsFromTime(t);
        if(argc > 1) mn = args.getat(1).num();
        if(argc > 2) sec = args.getat(2).num();
        if(argc > 3) ms = args.getat(3).num();
        number d = MakeDate(Day(t), MakeTime(h, mn, sec, ms));
        number u = TimeClip(UTC(this, d));
        _value = u;
        return var(u);
	}
	else if(m == L"setUTCHours" && argc > 0) {
        number t = _value;
        number h = args.getat(0).num();
        number mn = MinFromTime(t);
        number sec = SecFromTime(t);
        number ms = MsFromTime(t);
        if(argc > 1) h = args.getat(1).num();
        if(argc > 2) mn = args.getat(2).num();
        if(argc > 3) sec = args.getat(3).num();
        if(argc > 4) ms = args.getat(4).num();
        number d = MakeDate(Day(t), MakeTime(h, mn, sec, ms));
        number u = TimeClip((d));
        _value = u;
        return var(u);
	}
	else if(m == L"setDate" && argc > 0) {
        number t = LocalTime(this, _value);
        number d = args.getat(0).num();
        number d2 = MakeDate(MakeDay(YearFromTime(t), MonthFromTime(t), d), TimeWithinDay(t));
        number u = TimeClip(UTC(this, d2));
        _value = u;
        return var(u);
	}
	else if(m == L"setUTCDate" && argc > 0) {
        number t = _value;
        number d = args.getat(0).num();
        number d2 = MakeDate(MakeDay(YearFromTime(t), MonthFromTime(t), d), TimeWithinDay(t));
        number u = TimeClip((d2));
        _value = u;
        return var(u);
	}
	else if(m == L"setMonth" && argc > 0) {
        number t = LocalTime(this, _value);
        number mo = args.getat(0).num();
        number d = DateFromTime(t);
        if(argc > 1) d = args.getat(1).num();
        number d2 = MakeDate(MakeDay(YearFromTime(t), mo, d), TimeWithinDay(t));
        number u = TimeClip(UTC(this, d2));
        _value = u;
        return var(u);            
	}
	else if(m == L"setUTCMonth" && argc > 0) {
        number t = (_value);
        number mo = args.getat(0).num();
        number d = DateFromTime(t);
        if(argc > 1) d = args.getat(1).num();
        number d2 = MakeDate(MakeDay(YearFromTime(t), mo, d), TimeWithinDay(t));
        number u = TimeClip((d2));
        _value = u;
        return var(u);            
	}
	else if(m == L"setFullYear" && argc > 0) {
        number t = LocalTime(this, _value);
        number y = args.getat(0).num();
        number month = MonthFromTime(t);
        number d = DateFromTime(t);
        if(argc > 1) month = args.getat(1).num();
        if(argc > 2) d = args.getat(2).num();
        number d2 = MakeDate(MakeDay(y, month, d), TimeWithinDay(t));
        number u = TimeClip(UTC(this, d2));
        _value = u;
        return var(u);
	}
	else if(m == L"setUTCFullYear" && argc > 0) {
        number t = (_value);
        number y = args.getat(0).num();
        number month = MonthFromTime(t);
        number d = DateFromTime(t);
        if(argc > 1) month = args.getat(1).num();
        if(argc > 2) d = args.getat(2).num();
        number d2 = MakeDate(MakeDay(y, month, d), TimeWithinDay(t));
        number u = TimeClip((d2));
        _value = u;
        return var(u);
	}
	else if(m == L"toUTCString") {
        wstring res;
        number v = _value;
        int h = (int) HourFromTime(v);
        int mn = (int) MinFromTime(v);
        int s = (int) SecFromTime(v);
        int y = (int) YearFromTime(v);
        int mo = (int) MonthFromTime(v)+1;
        int d = (int) DateFromTime(v);
        wstlprintf(res, L"%04d/%02d/%02d %02d:%02d:%02d", y, mo, d, h, mn, s);
        return var(res);
	}
	else if(m == L"toISOString") {
        wstring res;
        number v = _value;
        int h = (int) HourFromTime(v);
        int mn = (int) MinFromTime(v);
        int s = (int) SecFromTime(v);
        int y = (int) YearFromTime(v);
        int mo = (int) MonthFromTime(v)+1;
        int d = (int) DateFromTime(v);
        int ms = (int) MsFromTime(v);
        wstlprintf(res, L"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", y, mo, d, h, mn, s, ms);
        return var(res);
	}
	else if(m == L"toJSON") {

	}
    

    return var();
}

void date_object::test() {

}
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// JSON native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void json_object::reg_cons(obj_ptr vars) {
    json_object* jo = new json_object();
    obj_ptr ojo(jo);
    vars->put(L"JSON", var(ojo));
}

var json_object::defaultValue(js::context &ec, js::object::dv hint) {
    return var(0);
}

var json_object::getvalue(node_ptr n) {
    token* t = n->token;
    if(t == nullToken)
        return var();
        
    if(t->type == token::tnumlit) {
        return var(t->nval);
    }
    
    if(t->type == token::tstrlit) {
        wstring s = *(t->sval);
        return var(s);
    }
    
    if(t->type == token::tkeyword) {
        switch(t->kid) {
            case token::kidNull:
                return var();
            case token::kidTrue:
                return var(true);
            case token::kidFalse:
                return var(false);
        }
    }
    if(t->type == token::tkeyword && t->kid == token::kidLBrace) {
        obj_ptr o(new object());
        return eval(o, n);
    }
    
    if(t->type == token::tkeyword && t->kid == token::kidLBracket) {
        // get number of elements
        int count = 0;
        for(node_ptr e = n->left; e != nullNode; e = e->right) 
            count++;
        
        oarray* oa = new oarray(count);
        int i = 0;
        for(node_ptr e = n->left; e != nullNode; e = e->right) { 
            var v = getvalue(e);
            oa->putat(i, v);
            i++;
        }
        obj_ptr o(oa);
        return var(o);
    }
    return var();
}

var json_object::eval(obj_ptr o, node_ptr root) {
    // 2013-03-02 shark@anui.com - support for global arrays in JSON
    token_ptr tp = root->token;
    if(tp && tp->type == token::tkeyword && tp->kid == token::kidLBracket) {
        var val = getvalue(root);
        return val;
    }

    node_ptr n = root->left;
    while(n != nullNode)
    {
        char_ptr pname = n->left->token->sval->c_str();
        var pval = getvalue(n->left->right);
        prop* p = o->addprop(pname);
        p->value = pval;
        n = n->right;
    }

    return var(o);
}

var json_object::call_direct(js::context &ec, char_ptr method, oarray& args) {
    int argc = args.length();
    wstring m = method;
    
    if(m == L"parse" && argc > 0) {
        wstring source = args.getat(0).wstr();
        tokenizer lex;
        lex.tokenize(source.c_str());
        if(lex.hasError())
            return var();
        parser parse(lex);
        node_ptr p = parse.jsonObjectLiteral(0);
        if(p == nullNode) {
            // 2013-03-02 - fix for #770 Support for value literal where a JSON file is
            //              composed of an array of objects
            p = parse.jsonValue(0);
            if(!p)
                return var();
        }
        obj_ptr o(new object());
        var v = eval(o, p);
        parse.free_nodes(p);
        return v;
    }
    else if(m == L"stringify" && argc > 0) {
        if(args.getat(0).vt != var::tobj)
            return var(L"");
        var o = args.getat(0);
        wstring s = o.wstr();
        return var(s);
    }
    
    return var();
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// script class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
script::script(char_ptr text, int index, char_ptr filename) : env(NULL, obj_ptr(new object())) {
    ALLOC("SC",1);
    syntax = NULL;
    lexer = NULL;
    source = text;
    obj_ptr o(new object());
    env.vars = o;
    env.othis.value = var(o);
    this->index = index;
    path = filename;
    parse_time = 0;
    
    // create short file name
    size_t pos = path.find_last_of(L"/");
    if(pos == path.npos) 
        file = path;
    else
        file = path.substr(pos+1);
    
}


void script::init_lines() {
    source_lines.clear();
    int start = 0;
    wstring delim = L"\n";
    
    while(true) {
        int end = source.find_first_of(L'\n', start);
        if(end >= source.size())
            break;
        source_lines.push_back(source.substr(start, end-start));
        start = end + 1;
    }
    
    
//    source_lines = tokenize(source, L"\n");
#ifdef JS_OPT
    int size = source_lines.size();
    line_calls.resize(size);
    line_blocks.resize(size);
    for(int i=0; i < size; i++) {
        line_calls[i] = 0;
        line_blocks[i] = 0;
    }
#endif
}

wstring script::get_line(int l) {
    if(l < source_lines.size())
        return source_lines[l-1];
    return wstring(L"");
}

script::~script() {
    // always start with index 1 since index 0 refers to self and will
    // end up in infinite recursion crash
    FREE("SC",1);
    delete syntax;
    delete lexer;
}

var script::run(debugger* dbg) {
    lexer = new tokenizer();
    message = L"";
    lexer->tokenize(source, index);
    if(lexer->hasError()) {
        message = lexer->error;
        return var();
    }
    
    if(lexer->tokens.size() == 0) {
        return var();
    }
        
    #ifdef JS_OPT
    float ms1, ms2;
    struct timeval current;
    gettimeofday(&current, NULL);
    ms1 = current.tv_sec*1000 + current.tv_usec / 1000;
    #endif
    
    syntax = new parser(*lexer);
    root = syntax->parse();
    
    #ifdef JS_OPT
    gettimeofday(&current, NULL);
    ms2 = current.tv_sec * 1000 + current.tv_usec / 1000;
    if(dbg != NULL) {
        wstring s;
        wstlprintf(s, L"parse time:%.2fms", (ms2 - ms1));
        dbg->trace(5, s);
    }
    #endif
    
    if(syntax->error != L"") {
        message =  syntax->error;
        return var();
    }
    
    env.attach(dbg);
    
    // changed 20120730 shark@anui.com - we should not register constructors
    // again if they are already registered. This was causing issues in the debugger
    // when using the "p"rint command (issue #442)
    if(env.vars->get(L"RegEx").vt == var::tundef) {
        js::oarray::reg_cons(env.vars);
        js_object::reg_cons(env.vars);
        js::string_object::reg_cons(env.vars);
        js::number_object::reg_cons(env.vars);
        js::boolean_object::reg_cons(env.vars);
        js::date_object::reg_cons(env.vars);
        js::math_object::reg_cons(env, env.vars);
        js::json_object::reg_cons(env.vars);
        js::regex_obj::reg_cons(env.vars);
    }
    
    if(dbg != NULL && !dbg->is_basic()) {
        dbg->call_enter(index, -1, NULL, &env);
    }
    
#ifdef JS_OPT
    gettimeofday(&current, NULL);
    ms1 = current.tv_sec*1000 + current.tv_usec / 1000;
#endif

    var v = env.run(env, root);
#ifdef JS_OPT
    gettimeofday(&current, NULL);
    ms2 = current.tv_sec * 1000 + current.tv_usec / 1000;
    if(dbg != NULL) {
        wstring s;
        wstlprintf(s, L"run time:%.2fms", (ms2 - ms1));
        dbg->trace(5, s);
    }

#endif
    

#ifdef JS_OPT
    if(dbg) {
        dbg->profiler_list(5);
    }
#endif        
    
    syntax->free_nodes(root);
    if(dbg != NULL && !dbg->is_basic()) {
        dbg->call_exit();
    }
    
    // This was causing a duplicate in printing runtime errors
//    if(v.ctl == var::ctlerror && dbg != NULL) {
//        wstring s = v.wstr();
//        dbg->output_error(s.c_str());
//    }
    
    
    return v;
}


char_ptr script::error() {
    if(message != L"")
        return message.c_str();

    if(env.has_error)
        return env.error.c_str();

    return NULL;
}

script* script::fromfile(char_ptr filename) {
    return NULL;
#if 0
    script* ret;

	if (filename == NULL) {
		return NULL;
	}

//	unsigned int bytesRead = 0;
//	char* buffer = NULL;
//	jschar* widebuffer = NULL;
	
    FILE* fp= _wfopen(&fp, filename, L"r, ccs=UNICODE");
    if(!fp) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    int size = (int) ftell(fp);
    fseek(fp, 0, SEEK_SET);
    jschar* buff;
    if(size > 0) {
//        int count = size / sizeof(jschar);
        buff = new jschar[size+1];
        int read = fread(buff, sizeof(jschar), size, fp);
        buff[read] = '\0';
	    ret = new script(buff);
	    delete [] buff;
    }

    fclose(fp);
	return ret;
#endif
}

void script::test() {
    script s(L"x=12;");
    var v=s.run();
    assert(v.num() == 12);

	double mypi = script(L"x=Math.PI;").run().num();
    assert(mypi >= 3.4);
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// helper functions
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
vector<wstring> tokenize(const wstring& str, const wstring& delimiters) {
	vector<wstring> tokens;
    
    if(str.length() == 0)
        return tokens;

    size_t start = str.find_first_not_of(delimiters, 0);
    size_t end = str.find_first_of(delimiters, start);

    while(true) {
        tokens.push_back(str.substr(start, end-start));
        if(end >= str.size())
            break;
        start = end + 1;
        end = str.find_first_of(delimiters, start);
    }
	return tokens;
}


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// debugger functions
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
debugger::debugger(script* s) {
#ifdef DEBUG_NOTIFICATIONS
    last_line = no_line;
    last_script = -1;
#endif

    call_count_line = -1;
    _script = s;
    force_exit = false;
    last_print_start = 0;
#ifdef DEBUG_NOTIFICATIONS
    current_state = none;
#else
    current_state = running;
#endif
    
    ALLOC("DB",1);
    
    
#ifdef JS_OPT
    trace_level = 0;
#endif
}


void debugger::gcpush(obj_ptr o) {
    if(IS_FLAG(o->flags, OBJ_PINNED)) //  o->pinned)
        return;
    o->flags = SET_FLAG(o->flags, OBJ_PINNED);
//    o->pinned = true;
    _gclist.push_back(o);
}

debugger::~debugger() {
    FREE("DB",1);
    
    // 2013-02-22 shark@anui.com, we should probably clear the gclist
    // before deleting all included scripts so that if any objects
    // depend on script context state, we don't crash on shutdown
    _gclist.clear();
    
    // Free all included scripts at this point
    for(map<int,script*>::iterator i = scripts.begin(); i != scripts.end(); i++) {
        script* s = (*i).second;
        if(s != _script) {
            // For included scripts, we must also free their parse nodes here
            // since they are not executed using script::run()
            if(s && s->syntax)
            {
                s->syntax->free_nodes(s->root);
                delete s;
            }
        }
    }
}

#ifdef JS_OPT
void debugger::trace(int l, wstring &s) {
    if(l <= trace_level) {
        this->output_error(s.c_str());
    }
}

void debugger::profiler_update_node(context& ec, node_ptr n) {
    int line = n->token->line;
    if(line == call_count_line) {
        return;
    }
    
    call_count_line = line;
    script* s = this->get_script(n->token->script_index);
    if(s->line_calls.size() == 0) 
        s->init_lines();
    if(line < s->line_calls.size()) {
        s->line_calls[line]++;
        s->line_blocks[line] = n->block;
    }
}

struct line_info {
    int file;
    int line;
    int calls;
    
    line_info(int f,int l,int c) {
        file = f;
        line = l;
        calls = c;
    }
};


bool debug_same_block(script* root, int f1, int l1, int f2, int l2);
bool debug_same_block(script* root, int f1, int l1, int f2, int l2) {
    if(f1 != f2) return false;

    script* s = root->env.attached_debugger->get_script(f1); // root->includes[f1];
    if(l1 >= s->line_blocks.size() || l1 < 0)
        return false;
    if(l2 >= s->line_blocks.size() || l2 < 0)
        return false;
        
    if(s->line_blocks[l1] == s->line_blocks[l2])
        return true;
    return false;
}

bool debug_pib_same_block(debugger* dbg, int file, int line);
bool debug_pib_same_block(debugger* dbg, int file, int line) {
    vector<pib>& pibs = dbg->profiler_ignore_blocks;
    for(vector<pib>::iterator i = pibs.begin(); i != pibs.end(); i++) {
        pib& p = *i;
        if(debug_same_block(dbg->_script, p.file, p.line, file, line))
            return true;
    }
    return false;
}
void debugger::profiler_list(int count) {
    vector<line_info> top;

//    for(int si=0; si < _script->includes.size(); si++) {
        for(map<int, script*>::iterator i = scripts.begin(); i != scripts.end(); i++) {
            script* s = (*i).second;
            if(!s)
                continue;
                
            int si = (*i).first;
            for(int li=0; li < s->line_calls.size(); li++) {
            if(debug_pib_check(this, si, li))
                continue;
             
            if(debug_pib_same_block(this, si, li))
                continue;
                
            int sc = s->line_calls[li];
            
            if(sc <= 0)
                continue;
                
            if(top.size() < count) {
                top.push_back(line_info(si,li,s->line_calls[li]));            
            }
            else {
                for(vector<line_info>::iterator ti=top.begin(); ti != top.end(); ti++) {
                    int tl = (*ti).line;
                    int tc = (*ti).calls;
                    int tf = (*ti).file;
                    script* ts = get_script(tf);
                    int tb = ts->line_blocks[tl];
                    
                    int sb = s->line_blocks[li];
                    if(si == tf && tb == sb && sc >= tc) {
                        *ti = line_info(si, li, sc);
                        break;
                    }
                    else if(s->line_calls[li] > tc) {
                        top.insert(ti, line_info(si,li,sc));
                        top.pop_back();
                        break;
                    }
                }
            }
        }
    }
        
    for(int ti=0; ti < top.size(); ti++) {
        int file = top[ti].file;
        script* s = get_script(file);// _script->includes[file];
        int line = top[ti].line;
        wstring o;
        wstring& fn = s->file;
        wstlprintf(o, L"%ls:%d called %d times at block %d", fn.c_str(),line, s->line_calls[line], s->line_blocks[line]);
        
        output_profiler(o.c_str());
    }

    // List top functions
    if(_script->env.optimization & context::Opt_FuncTimeProfiler) {
        vector<func_info>& tf = _script->top_functions;
        for(vector<func_info>::iterator i= tf.begin(); i != tf.end(); i++) {
            func_info f = *i;
            script* s = get_script(f.file);
            wstring file = s->file;
            wstring o;
            wstlprintf(o, L"%ls:%d duration %dms", file.c_str(), f.line-1, f.duration);
            output_profiler(o.c_str());
        }
    }
}

#endif

bool breakpoint::operator==(js::breakpoint &other) {
    return (line == other.line && file_index == other.file_index);
}

bool breakpoint::operator!=(js::breakpoint &other) {
    return !(*this == other);
}

breakpoint::breakpoint(int f, int l) {
    file_index = f;
    line = l;
}

breakpoint::breakpoint() {
    file_index = 0;
    line = debugger::no_line;
}


stack_frame::stack_frame(int si, int ln, node* a, context* e) {
    script_index = si;
    line = ln;
    args = a;
    env = e;
    
}

void debug_pif_list(debugger* dbg);
void debug_pif_list(debugger* dbg) {
    vector<pif>& pifs = dbg->profiler_ignore_funcs;
    for(vector<pif>::iterator i = pifs.begin(); i != pifs.end(); i++) {
        pif p = *i;
        wstring o;
        script* s = dbg->get_script(p.file);
        wstring& fn = s->file;
        wstlprintf(o, L"%ls:%d", fn.c_str(), p.line);
        dbg->trace(2, o);
    }
}

void debug_pif_toggle(debugger* dbg, int file, int line);
void debug_pif_toggle(debugger* dbg, int file, int line) {
    vector<pif>& pifs = dbg->profiler_ignore_funcs;
    
    // find it
    for(vector<pif>::iterator i = pifs.begin(); i != pifs.end(); i++) {
        pif p = *i;
        if(p.file == file && p.line == line) {
            // remove it from pif list
            pifs.erase(i);
            return;
        }
    }
    
    // add it to list
    pifs.push_back(pif(file, line));    
    
    // remove function from script top_functions list
    vector<func_info>& fi = dbg->_script->top_functions;
    for(vector<func_info>::iterator i = fi.begin(); i != fi.end(); i++) {
        func_info& f = *i;
        if(f.file == file && f.line == line) {
            fi.erase(i);
            return;
        }
    }
}

void debug_pib_list(debugger* dbg);
void debug_pib_list(debugger* dbg) {
    vector<pib>& pibs = dbg->profiler_ignore_blocks;
    for(vector<pib>::iterator i = pibs.begin(); i != pibs.end(); i++) {
        pib p = *i;
        wstring o;
        script* s = dbg->get_script(p.file);
        wstring& fn = s->file;
        wstlprintf(o, L"%ls:%d", fn.c_str(), p.line);
        dbg->trace(2, o);
    }
}

void debug_pib_toggle(debugger* dbg, int file, int line);
void debug_pib_toggle(debugger* dbg, int file, int line) {
    vector<pib>& pibs = dbg->profiler_ignore_blocks;
    for(vector<pib>::iterator i = pibs.begin(); i != pibs.end(); i++) {
        pib p = *i;
        if(p.file == file && p.line == line) {
            pibs.erase(i);
            return;
        }
    }
    
    pibs.push_back(pib(file, line));
}

bool debug_pif_check(debugger* dbg, int file, int line) {
    // line always comes in up by 1
    line--;
    
    vector<pif>& pifs = dbg->profiler_ignore_funcs;
    for(vector<pif>::iterator i = pifs.begin(); i != pifs.end(); i++) {
        pif& p = *i;
        if(p.file == file && p.line == line)
            return true;
    }
    return false;
}

bool debug_pib_check(debugger* dbg, int file, int line) {
    vector<pib>& pibs = dbg->profiler_ignore_blocks;
    for(vector<pib>::iterator i = pibs.begin(); i != pibs.end(); i++) {
        pib& p = *i;
        
        if(p.file == file && p.line == line) {
            return true;
        }
    }
    return false;
}

bool debugger::handle_command(wstring &cmd, context* ctx, breakpoint curr) {
    debugger* dbg = this;
    
    int current_script = call_stack[0].script_index;
    int current_file = current_script;
    
    // Note: two letter commands that start with 't' are commands coming from the toolbar
    vector<wstring> commands = tokenize(cmd, L" ");

    if(commands.size() == 0)
        return true;

    wstring command = commands[0];

    if(command == L"help") {
        dbg->output(L"s\nstep into next statement\n ");
        dbg->output(L"n\nnext / step over next\nstatement\n ");
        dbg->output(L"o\nstep out of current function\n ");
        dbg->output(L"b <line>\nset breakpoint at given line\n ");
        dbg->output(L"b <file>:<line>\nset breakpoint at file:line\n ");
        dbg->output(L"clear <line>\nclear breakpoint at line\n ");
        dbg->output(L"clear <file>:<line>\nclear breakpoint @ file:line\n ");
        dbg->output(L"c\ncontinue execution\n ");
        dbg->output(L"c <line>\ncontinue until line\n ");
        dbg->output(L"stop - stop execution\n ");
        dbg->output(L"display <expression>\nadd expression to \ndisplay watch list\n ");
        dbg->output(L"display\nshow all display watch items\n ");
        dbg->output(L"undisplay <index>\nremove expression from\ndisplay list\n ");
        dbg->output(L"p <expression>[,<start>]\nprint and evaluate\nexpression or statement\n ");
        dbg->output(L"bt\nbacktrack / show call stack\n ");
        dbg->output(L"bt <count>\nbacktrack / show call stack limit depth\n ");
        dbg->output(L"info locals\nshow local variables in\nfunction\n ");
        dbg->output(L"info args\nshow function arguments\n ");
        dbg->output(L"frame <n>\nswitch to frame(n) for info and p commands\n ");
        dbg->output(L"l [<line>][<count>]\nlist source starting\n ");
        dbg->output(L"v [level]\ncontrol and display debug\ntrace level\n ");
        dbg->output(L"op [level]\ncontrol and display\noptimization level\n ");
        dbg->output(L"top [N]\nshow profiler top functions\n ");
        dbg->output(L"bw <id> | <obj.prop>\nbreak on write toggle\n ");
        dbg->output(L"pic [[<file>:]<line>]\nlist or toggle profiler\nignore call\ncounts\n ");
        dbg->output(L"pit [[<file>:]<line>]\nlist or toggle profiler\nignore time\n ");
        return true;
    }
    else if(command == L"n" || command == L"tn") {
        dbg->skip_line = curr;
        dbg->skip_context = ctx;
        dbg->set_state(debugger::next, NULL,0);
        return false;
    }
    else if(command == L"o" || command == L"to") {
        dbg->skip_line = curr;
        dbg->skip_context = ctx->parent;
        dbg->set_state(debugger::step_out,NULL,0);

#ifdef DEBUG_NOTIFICATIONS
        // if we are the root context, then remove execution pointer
        if(ctx->parent == NULL) {
            dbg->location_changed(0);
        }
#endif
        return false;
    }
    else if(command == L"s" || command == L"ts") {
        dbg->skip_line = curr;
        dbg->skip_context = NULL;
        dbg->set_state(debugger::step,NULL,0);
        return false;
    }
    else if(command == L"c" || command == L"tc") {
        dbg->set_state(debugger::running,NULL,0);
        if(commands.size() > 1) {
            dbg->temp_breakpoint.line = _wtoi(commands[1].c_str());
            dbg->temp_breakpoint.file_index = current_file;
        }
        // handle file notification driven by "x" command to reset file name
        // when the user opens another file during debugging
        #if TODO
        if(dbg->file_name == L"")
            dbg->file_changed(dbg->current_script);
        #endif

#ifdef DEBUG_NOTIFICATIONS
        // Let the editor know that it should remove the execution pointer
        dbg->location_changed(-1);
        
        // override last_line on debugger to make sure that the next
        // break sends a location changed messahe
        dbg->last_line = -1;
#endif
        return false;
    }
    else if(command == L"p" ) {
        int start = 0;
        
        if(commands.size() > 2) {
            start = _wtoi(commands[2].c_str());
        }
        wstring expr = L"";
        if(commands.size() > 1) {
            expr = commands[1];
        }
        
        wstring disp = debug_eval(dbg, dbg->call_stack[dbg->current_frame].env, expr, start);
        wstring out;
        if(expr != dbg->last_print_expr)
            wstlprintf(out, L"%ls = %ls", expr.c_str(), disp.c_str());
        else
            out = disp;
        dbg->output(out.c_str());
        if(commands.size() > 1) {
            last_print_expr = expr;
        }

        return true;
    }
    else if(command == L"ex" && commands.size() > 1) {
        int start = 0;
        
        wstring expr = cmd.substr(3); //  commands[1];
        wstring disp = debug_expr(dbg, dbg->call_stack[dbg->current_frame].env, expr, start);
        wstring out;
        wstlprintf(out, L"%ls", disp.c_str());
        dbg->output(out.c_str());
        return true;
    }
    else if(command == L"expr" && commands.size() > 1) {
        int start = 0;
        wstring expr = cmd.substr(5); //  commands[1];
        wstring disp = debug_expr(dbg, dbg->call_stack[dbg->current_frame].env, expr, start);
        return true;
    }
    else if(command == L"bt") {
        int count = 0;
        if(commands.size() > 1)
            count = _wtoi(commands[1].c_str());
        wstring out = debug_backtrack(dbg, count);
        dbg->output(out.c_str());
        return true;
    }
    else if(command == L"info" && commands.size() > 1) {
        wstring c2 = commands[1];
        stack_frame sf = dbg->call_stack[dbg->current_frame];
        wstring out;
        if(c2 == L"locals") {
            obj_ptr o = sf.env->vars;
            for(map<wstring,prop>::iterator p = o->properties.begin(); p != o->properties.end(); p++) {
                wstring name = p->first;
                if(debug_islocal(*ctx, name.c_str(), sf.args, p->second.value)) {
                    wstring val = debug_short_desc(dbg, p->second.value,0);
                    wstring line;
                    wstlprintf(line, L"%ls = %ls\n", name.c_str(), val.c_str());
                    out += line;                        
                }                    
            }
            dbg->output(out.c_str());
            return true;
        }
        else if(c2 == L"args") {
            if(sf.args != nullNode) {
                node* n = sf.args;
                while(n != nullNode) {
                    char_ptr s = n->token->sval->c_str();
                    wstring av = debug_eval(dbg, sf.env, s,0);
                    wstring line;
                    wstlprintf(line, L"%ls = %ls\n", s, av.c_str());
                    out += line;
                    n = n->left;
                }
            }
            dbg->output(out.c_str());
            return true;
        }
    }
    else if(command == L"stop") {
        dbg->stop();
        return false;
    }
    else if(command == L"frame" && commands.size() > 1) {
        int frame =  _wtoi(commands[1].c_str());
        if(frame >=0 && frame < dbg->call_stack.size()) {
            dbg->current_frame = frame;
        }
        return true;
    }
    else if(command == L"x") {
        // this command resets the current file forcing the debugger to 
        // issue a file changed command as soon as next, step, or continue
        // is encountered. this is used by the IDE to tell the debugger
        // that the user has opened another file 
        #if TODO
        dbg->file_name = L"";
        #endif
    }
    else if(command == L"l") {
        int start = curr.line;
        int count = 5;

        if(commands.size() > 1)
            start = _wtoi(commands[1].c_str());

        if(commands.size() > 2)
            count = _wtoi(commands[2].c_str());

        for(int l=start; l < start + count; l++) {
            list(dbg, l);
        }
        return true;
    }
#ifdef JS_OPT
    else if(command == L"op") {
        if(commands.size() > 1) {
            ctx->root->optimization = _wtoi(commands[1].c_str());
        }
        wstring s;
        wstlprintf(s, L"optimization level %x", ctx->root->optimization);
        dbg->output(s.c_str());
        return true;
    }
    
    else if(command == L"v") {
        if(commands.size() > 1) {
            dbg->trace_level = _wtoi(commands[1].c_str());
        }
        wstring s;
        wstlprintf(s, L"trace level %d", dbg->trace_level);
        dbg->output(s.c_str());
        return true;
    }
    else if(command == L"top") {
        if(commands.size() > 1) 
            dbg->profiler_list(_wtoi(commands[1].c_str()));
        else
            dbg->profiler_list(5);
        return true;
    }
    else if(command == L"pit") {
        if(commands.size() == 1) { 
            debug_pif_list(dbg);
            return true;
        }
        if(commands.size() < 2) 
            return true;
        wstring arg = commands[1];
        size_t pos = arg.find(L":", 0);
        if(pos == arg.npos) {
            // pif <line> case
            int line = (commands.size() > 1) ? _wtoi(arg.c_str()) : curr.line;
            debug_pif_toggle(dbg, current_file, line);
        }
        else {
            // b <file>:<line>
            wstring wfile = arg.substr(0, pos);
            wstring wline = arg.substr(pos+1);
            int line = _wtoi(wline.c_str());
            int fi = dbg->add_file(wfile, NULL);
            debug_pif_toggle(dbg, fi, line);
        }
        return true;
    }
    else if(command == L"pic") {
        if(commands.size() == 1) { 
            debug_pib_list(dbg);
            return true;
        }
        if(commands.size() < 2) 
            return true;
        wstring arg = commands[1];
        size_t pos = arg.find(L":", 0);
        if(pos == arg.npos) {
            // pif <line> case
            int line = (commands.size() > 1) ? _wtoi(arg.c_str()) : curr.line;
            debug_pib_toggle(dbg, current_file, line);
        }
        else {
            // b <file>:<line>
            wstring wfile = arg.substr(0, pos);
            wstring wline = arg.substr(pos+1);
            int line = _wtoi(wline.c_str());
            int fi = dbg->add_file(wfile, NULL);
            debug_pib_toggle(dbg, fi, line);
        }
        return true;
    }

#endif    
    else if(command == L"bw") {
    
        // print the list
        if(commands.size() < 2) {

            for(vector<bow>::iterator i=dbg->bow_list.begin(); i != dbg->bow_list.end(); i++) {
                dbg->output((*i).expression.c_str());
            }
            return true;
        }
        
        wstring arg = commands[1];
        
        // if we find the expression, remove it from the bow list
        for(vector<bow>::iterator i=dbg->bow_list.begin(); i != dbg->bow_list.end(); i++) {
            if((*i).expression == arg) {
                dbg->bow_list.erase(i);
                return true;
            }
        }
        
        // parse expression and add to bow list
        tokenizer tk;
        tk.tokenize(commands[1]);
        if(tk.hasError()) {
            dbg->output_error(L"invalid break on write expression");
            return true;
        }
        parser p(tk);
        node_ptr n = p.parse();
        if(p.error != L"") {
            dbg->output_error(L"invalid break on write expression");
            return true;
        }
        
        n = n->left;
        if(n->token == nullToken) {
            dbg->output_error(L"invalid break on write expression");
            return true;
        }
        int t = n->token->type;
        int k = n->token->kid;
        if(t != token::tkeyword && t != token::tidentifier) {
            dbg->output_error(L"invalid break on write expression");
            return true;
        }
        if(t == token::tkeyword) {
            if(k != token::kidLBracket && k != token::kidDot) {
                dbg->output_error(L"invalid break on write expression");
                return true;            
            }
        }
        
        prop* pr = ctx->ref(*ctx, n, false);
        if(pr == NULL) {
            dbg->output_error(L"invalid break on write expression");
            return true;        
        }
        bow bw;
        bw.expression = commands[1];
        bw.ref = pr;
        dbg->bow_list.push_back(bw);
        dbg->output(L"break on write added");
        return true;
    }
    else if(command == L"b") {
        if(commands.size() < 2) 
            return true;
        wstring arg = commands[1];
        size_t pos = arg.find(L":", 0);
        if(pos == arg.npos) {
            // b <line> case
            int bl = (commands.size() > 1) ? _wtoi(arg.c_str()) : curr.line;
            breakpoint bp(current_file, bl);
            if(!check_breakpoints(dbg, bp)) {
                dbg->breakpoints.push_back(bp);
                dbg->break_added(bp);
            }
        }
        else {
            // b <file>:<line>
            wstring wfile = arg.substr(0, pos);
            wstring wline = arg.substr(pos+1);
            int line = _wtoi(wline.c_str());
            int fi = dbg->add_file(wfile, NULL);
            breakpoint bp(fi, line);
            if(!check_breakpoints(dbg, bp)) {
                dbg->breakpoints.push_back(bp);
                dbg->break_added(bp);
            }
        }
        return true;
    }
    else if(command == L"display") {
        if(commands.size() > 1) {
            dbg->display_list.push_back(commands[1]);
        }
        else {
            debug_display(dbg, ctx);
        }
    }
    else if(command == L"undisplay" && commands.size() > 1) {
        int index = _wtoi(commands[1].c_str());
        if(index > 0 && index <= dbg->display_list.size()) {
            index--;
            dbg->display_list.erase(dbg->display_list.begin() + index);
        }
    }
    else if(command == L"be" && commands.size() > 0) {
        int e = _wtoi(commands[1].c_str());
        dbg->enable_breakpoints(!(e == 0));
        if(e)
            dbg->output(L"breakpoints enabled");
        else
            dbg->output(L"breakpoints disabled");
    }
    else if(command == L"clear") {
        if(commands.size() < 2) 
            return true;
    
        wstring arg = commands[1];
        size_t pos = arg.find(L":", 0);
        if(pos == arg.npos) {
            int bl = (commands.size() > 1) ? _wtoi(commands[1].c_str()) : curr.line;
            breakpoint bp(current_file, bl);
            if(check_breakpoints(dbg, bp)) {
                for(vector<breakpoint>::iterator i= dbg->breakpoints.begin(); i != dbg->breakpoints.end() ; i++) {
                    if(*(i) == bp) {
                        dbg->break_removed(bp);
                        dbg->breakpoints.erase(i);
                        break;
                    }
                }
            }
        }
        else {
            wstring wfile = arg.substr(0, pos);
            int fi = dbg->add_file(wfile, NULL);
            wstring wline = arg.substr(pos+1);
            int line = _wtoi(wline.c_str());
            for(vector<breakpoint>::iterator i=dbg->breakpoints.begin(); i != dbg->breakpoints.end(); i++) {
                breakpoint bp = *i;
                if(bp.file_index == fi && bp.line == line) {
                    dbg->breakpoints.erase(i);
                    dbg->break_removed(bp);
                    break;
                }
            }
        }
        return true;
    }
    return true;
}

const wchar_t* wcharFromVar(var& v) {
    return v.wchar();
}

//const wchar_t* wcharFromArg(oarray& o, int index) {
//    return o.getat(index).wchar();
//}

const wstring strFromArg(oarray& o, int index) {
    return o.getat(index).wstr();
}

const wchar_t* wcharFromStr(wstring& s) {
    return s.c_str();
}

std::wstring strFromVar(var& v) {
    return v.wstr();
}



}
