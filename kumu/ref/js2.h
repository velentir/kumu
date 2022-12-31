//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//	FILE:		JS2.H
//	PURPOSE:	Modern ECMAScript Language Implementation 
//				ECMA-262 3rd Addition - December 1999 Specification
//	CREATED:	mohsen@agsen.com
//	COPYRIGHT:	Mohsen Agsen
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#ifndef _JS2_H_
#define _JS2_H_

#define ES_11_9_3
#define ES_8_12_8
#define ES_11_9_6

#include <vector>
#include <map>
#include <set>
#include <string>
//#include <c++/4.2.1/tr1/memory>
#include <memory>
#include <sys/time.h>

#define ENABLE_DEBUGGER
#define JS_OPT                  // enable all JS optimizations

using namespace std;

namespace js {

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// forward declarations and type definitions
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
typedef wchar_t jschar;
int _wtoi(const jschar* s);
class object;
class node;
class oarray;
class context;
class token;
class script;
class obj_type;
typedef node* node_ptr;
typedef std::shared_ptr<object> obj_ptr;
typedef token* token_ptr;
typedef double number;
typedef std::shared_ptr<void> void_ptr;
typedef const jschar* char_ptr;
typedef const wstring* str_ptr;

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// exception class for handling errors
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class exception {
public:
    std::wstring error;
    int line;
    int column;
    exception(std::wstring& e, int l, int c);
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Variant (var) class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class var
{
public:
	enum { tundef, tnum, tstr, tbool, tobj, tnull };
	enum { ctlnone, ctlbreak, ctlcontinue, ctlreturn, ctlerror};	

    var(const var& other);                          // copy constructor
    var(number n);                                  // number var
    var(int n);                                     // number var
    var(unsigned int n);                            // number var
    var(char_ptr s);                                // string var
    var(wstring& s);                                // string var
    var();                                          // undefined var
    var(bool b);                                    // bool var
    var(obj_ptr o);                                 // object var
    ~var();                                         // destructor
    static var error();                             // error variant

    int ctl;                                        // control flow
    int vt;                                         // var type

    union
    {
        str_ptr sval;                               // string value
        number nval;                                // number value
        bool bval;                                  // bool value
    };

    // NOTE - for some reason, can't have a shared_ptr in a union
    // value. Must check in future
    obj_ptr oval;                                   // object value

    wstring type_name();                            // type name
    void clear();                                   // clear the variant
    void copy(const var& other);                    // copy from other
    void operator=(const var& other);               // copy constructor
    bool operator==(const var& other);              // compare
    var operator+(var& other);                      // addition
    var operator-(var& other);                      // subtraction
    var operator*(var& other);                      // multiplication
    var operator/(var& other);                      // division
    void operator+=(var& other);                    // += operator
    void operator-=(var& other);                    // -= operator
    void operator*=(var& other);                    // *= operator
    void operator/=(var& other);                    // /= operator
    bool operator<(var& other);                     // < operator
    bool operator>(var& other);                     // > operator
    bool operator<=(var& other);                    // <= operator
    bool operator>=(var& other);                    // >= operator
    bool operator!=(var& other);                    // != operator
    unsigned int operator&(var& other);             // & operator
    unsigned int operator|(var& other);             // | operator
    unsigned int operator^(var& other);             // ^ operator
    unsigned int operator~();                       // ~ operator
    bool isPrimitive();
    char_ptr wchar();                               // string buffer
    number num();                                   // to float
    bool c_bool();                                  // to boolean
    unsigned int c_unsigned();                      // to unsigned int
    float c_float();                                // to float
    obj_ptr obj();                                  // object conversion
    wstring wstr();                                 // convert to string
    int c_int();                                    // convert to int

    static void test();                             // self test function
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// property class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class prop
{
public:
    var value;                                      // property value
    bool read_only;                                 // disallow write
    bool dont_enum;                                 // skip in enum ops
    bool configurable;
    prop(const var& v, bool ro, bool de);                 // constructor
    prop();                                         // min constructor
    void operator=(const prop& other);              // copy constructor
    static void test();                             // self test
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// native method function type
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
typedef var(*method)(context& ec, object* o, oarray& args);

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// object type class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class obj_type {
public:
    map<wstring, method> methods;
    void add_method(const jschar* name, method m);
    obj_type(context& ec);
    ~obj_type();
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// object class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class object
{
public:
    object();                                       // constructor
    ~object();                                      // destructor
    obj_type* type;                                 // optional native type

    #define OBJ_PINNED      0x00000001
    #define OBJ_EXTENSIBLE  0x00000002

    // igloo reserved flags
    #define IGLOO_MESH      0x00010000
    #define IGLOO_TEX       0x00020000
    #define IGLOO_HUD       0x00040000
    #define IGLOO_SCENE     0x00080000
    #define IGLOO_LABEL     0x00100000
    #define IGLOO_SPRITE    0x00200000
    
    #define VIEW_FONT       0x00400000
    #define VIEW_IMAGE      0x00800000
    
    #define OBJ_RESERVED7   0x00800000
    
    #define IS_FLAG(i, f)       (i & f)
    #define SET_FLAG(i, f)      (i | f)
    #define CLEAR_FLAG(i,f)     (i & ~f)
    
    unsigned int flags;                             // object flags
//    bool pinned;                                    // is on gc list
//    bool is_extensible;
    map<wstring, prop> properties;                  // property list
    node_ptr node;                                  // parse node
    prop prototype;                                 // prototype property
    object* scope;                                  // for scope chain & closure
    virtual bool can_call_direct();                 // supports direct calls
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
    virtual bool is_array();                        // is it an array
	virtual bool isNative();                        // is it native
	virtual var call(context& ec, oarray& args);    // execute function object
    virtual var getat(int i);                       // get at
    virtual void putat(int i, const var& v);        // put at
    virtual var get(char_ptr p);                    // get value
    virtual void put(char_ptr p, const var& val);         // put value
    virtual prop* getprop(char_ptr p, bool proto);          // get prop reference
    virtual prop* addprop(char_ptr p);               // add property
    virtual void delprop(char_ptr p);                       // delete property
    
#ifdef ES_8_12_8
    enum dv { DVHINT_NONE, DVHINT_STR, DVHINT_NUM };
    virtual var defaultValue(context& ec, dv hint);              // [[DefaultValue]]
#endif
    
    static void test();                             // self test function
    virtual bool is_equal(object* other);           // equality test
    unsigned int __id;                              // unique id
    static unsigned int __next_id__;                // id counter
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// native_object class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class native_object : public object
{
public:
	virtual bool isNative();                        // is it native
    virtual bool can_call_direct();                 // supports direct calls
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// array class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class oarray : public native_object
{
public:
    static void reg_cons(obj_ptr vars);             // constructor
    virtual var call_direct(context& ec, char_ptr m, oarray& args);
    vector<prop> elements;                          // element array
    oarray(int count);                              // constructor
    virtual bool is_array();                        // is it an array
	virtual var getat(int i);                       // get at
	virtual var& getref(int i);                     // get reference
	virtual void putat(int a, const var& v);        // put value
    prop* refat(int i);                             // get element ref
	var pop();                                      // pop value
	void reverse();                                 // reverse version
	var shift();                                    // shift?
	void remove(int s, int c);                      // remove n elements
	void insert(int s, int c);                      // insert n elements
	var slice(int s, int e);                        // array from string
	wstring join(wstring& sep);                     // string from array
    virtual int length();                           // array length

#ifdef ES_8_12_8
    virtual var defaultValue(context& ec, dv hint);              // [[DefaultValue]]
#endif

    static void test();
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// token class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class token
{
public:
	enum { tnumlit, tstrlit, tkeyword, tpunc, tidentifier, tcomment, tword, tregex, tlineterm };

	enum Keywords 
    { 
     kidNone,       kidBreak,       kidCase,        kidCatch,       kidContinue,    kidDefault,
	 kidDelete,     kidDo,          kidElse,        kidFinally,     kidFor,         kidFunction,
	 kidIf,         kidIn,          kidInstanceOf,  kidNew,         kidReturn,      kidSwitch,
	 kidThis,       kidThrow,       kidTry,         kidTypeOf,      kidVar,         kidVoid, 
     kidWhile,      kidWith,        kidLBrace,      kidRBrace,      kidLPar,        kidRPar,    
     kidLBracket,   kidRBracket,    kidDot,         kidSemi,        kidComma,       kidLt,
     kidGt,         kidLe,          kidGe,          kidEqEq,        kidNe,          kidEqEqEq, 
     kidNEqEq,      kidPlus,        kidMinus,       kidMult,        kidMod,         kidPlusPlus, 
     kidMinusMinus, kidLShift,      kidRShift,      kidRShiftA,     kidAnd,         kidOr, 
     kidCaret,      kidNot,         kidTilde,       kidAndAnd,      kidOrOr,        kidQuestion, 
     kidColon,      kidEqual,       kidPlusEq,      kidMinusEq,     kidMultEq,      kidModEq, 
     kidLShiftEq,   kidRShiftEq,    kidRShiftAEq,   kidAndEq,       kidOrEq,        kidCaretEq, 
     kidDiv,        kidDivEq,       kidNull,        kidTrue,        kidFalse,       kidUndefined, 
     kidDebugger,   kidReserved};

    int column;                                     // column for token start
	int	type;						                // token type
    int line;                                       // line number
    int start;                                      // starting offset
    int end;                                        // ending offset
    int flagOffset : 16;                            // for regex tokens
    int script_index : 16;                          // associated script include index
    
#ifdef JS_OPT    
    unsigned int pnot_flags;                        // parser negative flags
#endif
        
    union
    {
        int kid;                                    // keyword id
        number nval;                                // number literal
        str_ptr sval;                               // string literal
    };

    token(number n, int l, int c, int so, int eo);          // number literal token
    token(int t, str_ptr s, int l, int c, int so, int eo);  // string and id token
    token(int t, str_ptr s, int l, int c, int so, int eo, int fo);  // string and flag offset
    token(int kid, int l, int c, int so, int eo);           // keyword token
    token(int t, int l, int c, int so, int eo, bool other); // other token
    token(int t, int l, int c, int so, int eo, bool other, int fo); // regex token
    bool is_numop();                                        //  num related operator
    
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Tokenizer class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class tokenizer
{
public:
    map<wstring, int> keywords;                     // keyword name to id map
    set<wstring> identifiers;                       // identifier list
    set<wstring> strings;                           // string literals
    vector<token> tokens;                           // token stream
    bool colorizer;                                 // lexer syntax color mode
	wstring word;                                   // current word
	wstring input;                                  // all input
	int line;                                       // current line
    int column;                                     // current column
	wstring error;                                  // tokenization error
    int current;
    int max;
    int wordColumn;                                 // current word column
    int offset;                                     // current word offset
    int maxWidth;                                   // maximum column width
    
    int script_index;                               // script index in root includes

    void advance(int n);                            // advance current
    void nextLine();                                // advance line
    void newWord();                                 // start new word
    
	bool hasError();                                // true if error
	bool setError(char_ptr e, int line, int col);   // set error message

    void initKeywords();                            // init keyword table
	void add(token& t, int line);                   // add token
	bool isWordStart(jschar c);
	bool isWordChar(jschar c);
	bool isPuncSolo(jschar c);
	bool isPuncTerm(jschar c);
	bool isSpace(jschar c);
	bool isLineTerm(jschar c);
	bool isHexDigit(jschar c);
	bool isAlpha(jschar c);
	bool isStringDelim(jschar c);
    bool isNum(jschar c);
	bool isAlNum(jschar c);
	bool tokenizeSpace();
	bool tokenizeLineTerm();
	bool tokenizeNumber();
	jschar hexVal(jschar c);
	bool isIdentifier(wstring& s);
	bool tokenizeString();
    bool tokenizeRegEx();
	bool tokenizeComment();
	bool tokenizeWord();
    void tokenizeAny();
    void tokenizeBegin(char_ptr inp, int index = -1, bool c = false);
    bool tokenizeNext();
    void tokenizeEnd();
	bool tokenize(char_ptr inp, int index = -1, bool c = false);
    bool tokenize(wstring& inp, int index = -1, bool c = false);
	token_ptr getToken(int i);
    bool isNumConstPrefix(jschar c);
    void add_token(token t);
    vector<wstring> match(int index, wstring substring);
    static void test();
    
    token_ptr* finalTokens;                         // speed up getToken()
    int *lineTokens;                                // line to start token map
    vector<int> lineOffsets;                        // dynamic list of line offset starts
    int* finalOffsets;                              // final list of line offset starts
    
    int lineFromOffset(int offset);                 // line number from string offset
    int tokenIndexFromLine(int line);               // starting token index for line
    int lineStartOffset(int line);                  // get starting offset for a line
    int lineEndOffset(int line);                    // get ending offset for a line
    int lineCount;
    int getLineCount();
    ~tokenizer();
    tokenizer();
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Parse node class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class node
{
public:
	token_ptr token;                                // associated token
	node_ptr left;                                  // left child
	node_ptr right;                                 // right child
    
    enum types { None, LocalPtr, GlobalPtr, MethodPtr, ObjProp };
    
    types type;
    
    union {
        int match_object;
        obj_type* match_type;
    };
    
    union {
        var* var_ptr;
        prop* prop_ptr;
        method func_ptr;
        int prop_index;
    };
    
    var eval(context& ec);
    prop* ref(context& ec, bool local);
    
#ifdef JS_OPT
    int block;                                      // block number
#endif

	node(token_ptr t);                              // constructor
    ~node();                                        // destrocutor
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Parser class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class parser
{
public:
	wstring error;                                  // error string if any
	tokenizer& lex;                                 // associated tokenizer
	int	current;                                    // current position in tokens
	int	next;                                       // nxt token position

    int node_count;                                 // number of allocated nodes
    node_ptr new_node(token_ptr t);                 // allocate new node
    void free_nodes(node_ptr n);                    // free node and all siblings
    
    parser(tokenizer& t);                           // constructor
	node_ptr parse();                               //  parse
    node_ptr setError(char_ptr sz, int pos);        // set error 
	node_ptr program(int start);
	node_ptr sourceElements(int start, bool checkEnd);
	node_ptr sourceElement(int start);
	node_ptr statement(int start);
	node_ptr funcDecl(int start,bool allowAnonymous);
	node_ptr block(int start);
	node_ptr varStatement(int start);
	node_ptr varDeclList(int start, bool notIn);
	node_ptr varDecl(int start, bool notIn);
	node_ptr initializer(int start, bool notIn);
	node_ptr emptyStatement(int start);
	node_ptr expressionStatement(int start);
	node_ptr ifStatement(int start);
    token_ptr getToken(int *i);
	token_ptr checkToken(int* i, int k);
    bool checkSemi(int* i);
	node_ptr shiftExpression(int start);
	node_ptr additiveExpression(int start);
	node_ptr multiplicativeExpression(int start);
	node_ptr unaryExpression(int start);
	node_ptr postfixExpression(int start);
	node_ptr conditionalExpression(int start, bool notIn);
	node_ptr logicalOrExpression(int start, bool notIn);
	node_ptr logicalAndExpression(int start, bool notIn);
	node_ptr bitwiseOrExpression(int start, bool notIn);
	node_ptr bitwiseXorExpression(int start, bool notIn);
	node_ptr bitwiseAndExpression(int start, bool notIn);
	node_ptr equalityExpression(int start, bool notIn);
	node_ptr relationalExpression(int start, bool notIn);
	node_ptr leftHandSideExpression(int start);
	node_ptr callExpression(int start);
	node_ptr callExpression2(node_ptr l, int start);
	node_ptr arguments(int start);
	node_ptr argumentList(int start);
	node_ptr primaryExpression(int start);
	node_ptr arrayLiteral(int start);
	node_ptr elision(int start);
	node_ptr objectLiteral(int start);
	node_ptr propNameValueList(int start);
	node_ptr propertyName(int start);
	node_ptr elementList(int start);
	node_ptr newExpression(int start);
	node_ptr memberExpression(int start);
	node_ptr memberExpression2(node_ptr l, int start);
	node_ptr functionExpression(int start);
	node_ptr expression(int start, bool notIn);
	node_ptr assignmentExpression(int start, bool notIn);
	node_ptr assignmentOperator(int start);
	node_ptr iterationStatement(int start);
	node_ptr doStatement(int start);
	node_ptr whileStatement(int start);
	node_ptr forStatement(int start);
	node_ptr forIn(int start);
	node_ptr forVar(int start);
	node_ptr forSimple(int start);
	node_ptr forVarIn(int start);
	node_ptr labelledStatement(int start);
	node_ptr continueStatement(int start);
	node_ptr contBreakStatement(int start,int k);
	node_ptr breakStatement(int start);
    node_ptr debuggerStatement(int start);
	node_ptr returnStatement(int start);
	node_ptr withStatement(int start);
	node_ptr throwStatement(int start);
	node_ptr tryStatement(int start);
	node_ptr catchBlock(int start);
	node_ptr finallyBlock(int start);
	node_ptr switchStatement(int start);
	node_ptr caseBlock(int start);
	node_ptr caseClauses(int start);
	node_ptr caseDefClause(int start);
	node_ptr caseClause(int start);
	node_ptr defaultClause(int start);
	node_ptr formalParamList(int start);
	node_ptr statementList(int start);

    node_ptr jsonObjectLiteral(int start);
    node_ptr jsonPropNameValueList(int start);
    node_ptr jsonValue(int start);
    node_ptr jsonArrayLiteral(int start);
    
#ifdef JS_OPT
    static const unsigned int NotBlock = (1 << 0);
    static const unsigned int NotStatement = (1 << 1);
    static const unsigned int NotExpressionStatement = (1 << 2);
    static const unsigned int NotExpression = (1 << 3);
    static const unsigned int NotFuncDecl = (1 << 4);
    static const unsigned int NotShiftExpression = (1 << 5);
    static const unsigned int NotAddExpression = (1 << 6);
    static const unsigned int NotMutExpression = (1 << 7);
    static const unsigned int NotUnaryExpression = (1 << 8);
    static const unsigned int NotPostfixExpression = (1 << 9);
    static const unsigned int NotAssignmentExpression = (1 << 10);
    static const unsigned int NotCallExpression = (1 << 11);
    static const unsigned int NotMemberExpression = (1 << 12);
    static const unsigned int NotArguments = (1 << 13);
    static const unsigned int NotPrimaryExpression = (1 << 14);
    static const unsigned int NotLeftHandSideExpression = (1 << 15);
    static const unsigned int NotConditionalExpression = (1 << 16);
    static const unsigned int NotSourceElements = (1 << 17);
    static const unsigned int NotSourceElement = (1 << 18);

    int current_block;
    int next_block;
    bool check(int start, unsigned int flag);
    int parse_hits;    
    int alloc_count;
#endif    
    static void test();
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// breakpoint class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
struct breakpoint {
    int file_index;
    int line;
    bool operator==(breakpoint& other);
    bool operator!=(breakpoint& other);
    breakpoint(int f, int l);
    breakpoint();
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// stack frame class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
struct stack_frame {
    int script_index;                               // script index
    int line;                                       // source line
    node* args;                                     // argument node tree
    context* env;                                   // environment    
    stack_frame(int si, int ln, node* a, context* e);
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// break on write expression class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
struct bow {
    wstring expression;
    prop* ref;
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// profiler ignore block
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
struct pib {
    int file;
    int line;
    int block;
    pib(int f, int l) : file(f), line(l), block(-1) {}
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// profiler ignore function
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
struct pif {
    int file;
    int line;
    pif(int f, int l) : file(f), line(l) {}
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// debugger class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class debugger
{
public:
    vector<obj_ptr> _gclist;
    void gcpush(obj_ptr o);
    
    debugger(script* s);                            // constructor
    script* _script;                                // associated root script
    int current_frame;                              // current stack frame index
    static const int no_line = -1;
    breakpoint skip_line;                           // used for next line
    context* skip_context;                          // used for step out
    enum state { running, step, next, step_out, stopped, pause };
    breakpoint temp_breakpoint;
    vector<breakpoint> breakpoints;
    vector<wstring> display_list;                   // for display command
    vector<stack_frame> call_stack;                 // for bt, frame, and info

#ifdef DEBUG_NOTIFICATIONS
    int last_script;
    int last_line;
#endif

    virtual ~debugger();

#ifdef DEBUG_NOTIFICATIONS
    virtual void location_changed(int line) = 0;
#endif

    virtual wstring get_command() = 0;
    bool handle_command(wstring& cmd, context* ctx, breakpoint curr);
    virtual void output(char_ptr text) = 0;
    virtual void output_error(char_ptr text) = 0;
    virtual void output_profiler(char_ptr text) = 0;
    virtual void break_added(breakpoint bp) = 0;
    virtual void break_removed(breakpoint bp) = 0;
    virtual bool should_exit() = 0;
    virtual bool is_basic() = 0;                  // basic debugger only should_exit() check
    virtual void call_enter(int file, int line, node*a, context* e) = 0;
    virtual void call_exit() = 0;
    virtual bool peek_exit() = 0;
    virtual void stop() = 0;
    bool suspend_exit_check;
    wstring last_command;
    vector<bow> bow_list;              // break on write list
    virtual void enable_breakpoints(bool e) = 0;
    virtual bool breakpoints_enabled() = 0;
    
    map<int, script*> scripts;                          // map of script index to script object
    virtual int add_file(wstring& file, script* s) = 0; // add or find file and get its index
    virtual script* get_script(int index) = 0;          // get script* from script index
    
    static const int next_print_count = 5;
    int last_print_start;                   // last index we printed on object
    wstring last_print_expr;                            // last print expression
    bool force_exit;
    
    virtual void set_state(state newState, wstring* file, int line) {
        current_state = newState;
    }
    
    state get_state() {
        return current_state;
    }
#ifdef JS_OPT
    // tracing support
    int trace_level;
    int call_count_line;
    void trace(int l, wstring& s);
    void profiler_update_node(context& ec, node_ptr n);
    void profiler_list(int count);

    vector<pif> profiler_ignore_funcs;
    vector<pib> profiler_ignore_blocks;
#endif

private:
    state current_state;                            // current dbg state
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// execution context clas
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class context
{
public:
	obj_ptr vars;			                         // Variables object
	prop othis;			                             // this object
	context* parent;				                 // Parent execution context
	context* root;				                     // Root execution context
	wstring error;			                         // Error string
    bool has_error;                                  // has an error
    debugger* attached_debugger;                     // attached debugger
    int call_depth;                                  // call depth
    obj_ptr arguments;                               // arguments object
    oarray* func_args;                               // function arguments
    object* func;                                    // current function
    token_ptr lastToken;                             // last seen token
    
    vector<obj_type*>* types;                        // root context type list

    void attach(debugger* dbg);                      // attach to root ctx
	context(context* par, obj_ptr vars);	         // non-root context
    context();                                       // empty constructor for Objective-C mixing
	obj_ptr toObject(var& v);
	bool isroot();
	bool isIdent(var& v1, var& v2);
	void setclass(obj_ptr o, char_ptr c);
	var execCatch(context& ec, node_ptr n, var& e);
	var callFunc(context& ec, node_ptr n, obj_ptr pthis);
	var setError(char_ptr s, token_ptr t);
	var run(context& ec, node_ptr n);
	var eval(context& ec, node_ptr n);
	prop* refProto(obj_ptr o);
	void propDel(context& ec, node_ptr n);
	prop* ref(context& ec, node_ptr n, bool local);
    bool refarray(context& ec, node_ptr n, oarray** array, int* index);
	var interpretKeyword(context& ec, node_ptr n, int k);

#ifdef JS_OPT
    static const int Opt_FastGlobalLookup   = 0x01;
    static const int Opt_DeferredProfiler = 0x02;
    static const int Opt_DynamicProfiler = 0x04;
    static const int Opt_FastPropertyLookup = 0x08;
    static const int Opt_FuncTimeProfiler = 0x10;
    int optimization;
    ~context();
#endif
    static void test();
};




//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// function class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class function : public object
{
public:
	function();
	virtual bool isNative();
	virtual var call(context& ec, oarray& args);
    
#ifdef JS_OPT
    struct timeval profile_start;               // function entry time
    unsigned int profile_duration;              // call duration

#endif    
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// math native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class math_object : public native_object
{
public:
	static void reg_cons(context& ec, obj_ptr vars);                 // register constructor
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
    static void test();
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// regex match state structure (corresponds to ES5 State(endIndex, captures)
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
struct match_state {
    static const int fail = -1;
    
    bool failed() { return (endIndex == fail); }
    
    match_state(int ei) : endIndex(ei), captures(NULL) {}
    match_state() : endIndex(fail), captures(NULL) {}
    match_state(int ei, oarray* oa) : endIndex(ei), captures(oa) {}
    int endIndex;
    oarray* captures;
    
    ~match_state() {
        delete captures;
    }
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// string native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class string_object : public native_object
{
public:
	static void reg_cons(obj_ptr vars);                 // reg. constructor
	string_object(const var& initial);
    string_object(const wstring& initial);
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
    var value;

    match_state splitMatch(wstring& S, int q, var R);
    wstring replace(context& ec, wstring& S, wstring& match, int start, int end, oarray* cap, var rep);

#ifdef ES_8_12_8
    virtual var defaultValue(context& ec, dv hint);              // [[DefaultValue]]
#endif
    
    static void test();
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// number native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class number_object : public native_object
{
public:
	static void reg_cons(obj_ptr vars);                 // reg. constructor
	number_object(const var& initial);
    number_object(const number initial);
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
#ifdef ES_8_12_8
    virtual var defaultValue(context& ec, dv hint);              // [[DefaultValue]]
#endif
    
    var value;
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// boolean native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class boolean_object : public native_object
{
public:
	static void reg_cons(obj_ptr vars);                 // reg. constructor
	boolean_object(const var& initial);
    boolean_object(const bool initial);
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
#ifdef ES_8_12_8
    virtual var defaultValue(context& ec, dv hint);              // [[DefaultValue]]
#endif
    
    var value;
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// object native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class js_object : public native_object
{
public:
	static void reg_cons(obj_ptr vars);                 // reg. constructor
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
};

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// date native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class date_object : public native_object {
public:
	static void reg_cons(obj_ptr vars);                 // reg. constructor
    date_object(number initial);
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
    number _value;
    number _localTZA;
#ifdef ES_8_12_8
    virtual var defaultValue(context& ec, dv hint);              // [[DefaultValue]]
#endif
    
    static void test();
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// JSON native object
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class json_object : public native_object {
public:
	static void reg_cons(obj_ptr vars);                 // reg. constructor
    virtual var call_direct(context& ec, char_ptr method, oarray& args);
    var eval(obj_ptr o, node_ptr root);
    var getvalue(node_ptr n);
#ifdef ES_8_12_8
    virtual var defaultValue(context& ec, dv hint);              // [[DefaultValue]]
#endif
    
};


#ifdef JS_OPT
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// function info struct used by time profiler
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
struct func_info {
    int file;
    int line;
    int duration;
    func_info(int f, int l, int d) : file(f), line(l), duration(d) {}
};
#endif

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// script class
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class script
{
public:
    script(char_ptr text, int index=0, char_ptr path = L"");
    var run(debugger* dbg=NULL);
    char_ptr error();                           
	static script* fromfile(char_ptr path);
    parser* syntax;
    tokenizer* lexer;
	node_ptr root;             
    context env;
    wstring message;
    wstring source;
    vector<wstring> source_lines;
    wstring get_line(int l);

    wstring path;                          // Full path to source file
    wstring file;                          // Short file name for script

    int index;                             // index in debugger script list
    void init_lines();                     // Initialize line buffers for list & profile
    ~script();
    
#ifdef JS_OPT
    int parse_time;
    vector<int> line_calls;                 // For use by the profiler
    vector<int> line_blocks;                // For use by the profiler
    vector<func_info> top_functions;        // For use by the profiler
#endif
    static void test();
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// weak_proxy is a forwarding object that doesn't hold a ref-count
// on the target object and forwards all calls to the target
// this is used to avoid circular references in the case of 'globals'
// where the require() context holds a global variable called 'global'
// which points back to the calling context vars
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
class weak_proxy : public object {
public:
    object* target;
    weak_proxy(object* t) : target(t) {
        type = t->type;
    }
    
    virtual bool is_equal(object* other) {
        if(this == other)
            return true;
        return(target == other);
    }
    
    virtual bool can_call_direct() {
        return target->can_call_direct();
    }
    
    virtual var call_direct(context& ec, char_ptr method, oarray& args) {
        return target->call(ec, args);
    }
    
    virtual bool is_array() {
        return target->is_array();
    }
    
	virtual bool isNative() {
        return target->isNative();
    }
    
    virtual var call(context& ec, oarray& args) {
        return target->call(ec, args);
    }
    virtual var getat(int i) {
        return target->getat(i);
    }
        
    virtual void putat(int i, const var& v) {
        target->putat(i, v);
    }
    
    virtual var get(char_ptr p) {
        return target->get(p);
    }
    
    virtual void put(char_ptr p, const var& val) {
        target->put(p,val);
    }
    
    virtual prop* getprop(char_ptr p, bool proto) {
        return target->getprop(p, proto);
    }
    
    virtual prop* addprop(char_ptr p) {
        return target->addprop(p);
    }
    
    virtual void delprop(char_ptr p) {
        return target->delprop(p);
    }
};


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// helper functions
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
vector<wstring> tokenize(const wstring& str, const wstring& delimiters);
int wstlprintf(wstring& dest, const jschar* format, ...);

const wchar_t* wcharFromVar(var& v);
// const wchar_t* wcharFromArg(oarray& o, int index);
const wchar_t* wcharFromStr(wstring& s);

const wstring strFromArg(oarray& o, int index);
std::wstring strFromVar(var& v);


}


#endif      // _JS2_H_

