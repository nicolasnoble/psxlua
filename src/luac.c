/*
** $Id: luac.c,v 1.69 2011/11/29 17:46:33 lhf Exp $
** Lua compiler (saves bytecodes to files; also list bytecodes)
** See Copyright Notice in lua.h
*/

#define luac_c
#define LUA_CORE
#define LUA_TARGET_PSX

#include "common/hardware/pcsxhw.h"
#include "common/kernel/pcdrv.h"
#include "common/syscalls/syscalls.h"

#define EXIT_FAILURE -1
#define EXIT_SUCCESS 0

#include "lauxlib.h"
#include "lctype.h"
#include "llibc.h"
#include "lobject.h"
#include "lstate.h"
#include "lua.h"
#include "lundump.h"

static void PrintFunction(const Proto* f, int full);
#define luaU_print PrintFunction

#define PROGNAME "luac"        /* default program name */
#define OUTPUT PROGNAME ".out" /* default output file */

#define ARGS_BASE 0x40000000 /* unmapped; args.lua serves this through UnknownMemoryRead */
#define MAXARGS 64           /* size of the argv array main() builds from ARGS_BASE */

static int listing = 0;                 /* list bytecodes? */
static int dumping = 1;                 /* dump bytecodes? */
static int stripping = 0;               /* strip debug information? */
static char Output[] = {OUTPUT};        /* default output file name */
static const char* output = Output;     /* actual output file name */
static const char* progname = PROGNAME; /* actual program name */

static void fatal(const char* message) {
    ramsyscall_printf("%s: %s\n", progname, message);
    pcsx_exit(EXIT_FAILURE);
}

static void cannot(const char* what) {
    ramsyscall_printf("%s: cannot %s %s\n", progname, what, output);
    pcsx_exit(EXIT_FAILURE);
}

static void usage(const char* message) {
    if (*message == '-')
        ramsyscall_printf("%s: unrecognized option " LUA_QS "\n", progname, message);
    else
        ramsyscall_printf("%s: %s\n", progname, message);
    ramsyscall_printf(
  "usage: %s [options] [filenames]\n"
  "Available options are:\n"
  "  -l       list (use -l -l for full listing)\n"
  "  -o name  output to file " LUA_QL("name") " (default is \"%s\")\n"
  "  -p       parse only\n"
  "  -s       strip debug information\n"
  "  -v       show version information\n"
  "  --       stop handling options\n"
  ,progname,Output);
    pcsx_exit(EXIT_FAILURE);
}

#define IS(s) (luaA_strcmp(argv[i], s) == 0)

static int doargs(int argc, const char* argv[]) {
    int i;
    int version = 0;
    if (argv[0] != NULL && *argv[0] != 0) progname = argv[0];
    for (i = 1; i < argc; i++) {
        if (*argv[i] != '-') /* end of options; keep it */
            break;
        else if (IS("--")) /* end of options; skip it */
        {
            ++i;
            if (version) ++version;
            break;
        } else if (IS("-l")) /* list */
            ++listing;
        else if (IS("-o")) /* output file */
        {
            output = argv[++i];
            if (output == NULL || *output == 0 || (*output == '-' && output[1] != 0))
                usage(LUA_QL("-o") " needs argument");
        } else if (IS("-p")) /* parse only */
            dumping = 0;
        else if (IS("-s")) /* strip debug information */
            stripping = 1;
        else if (IS("-v")) /* show version */
            ++version;
        else /* unknown option */
            usage(argv[i]);
    }
    if (i == argc && (listing || !dumping)) {
        dumping = 0;
        argv[--i] = Output;
    }
    if (version) {
        ramsyscall_printf("%s\n", LUA_COPYRIGHT);
        if (version == argc - 1) pcsx_exit(EXIT_SUCCESS);
    }
    return i;
}

#define FUNCTION "(function()end)();"

static const char* reader(lua_State* L, void* ud, size_t* size) {
    UNUSED(L);
    if ((*(int*)ud)--) {
        *size = sizeof(FUNCTION) - 1;
        return FUNCTION;
    } else {
        *size = 0;
        return NULL;
    }
}

#define toproto(L, i) getproto(L->top + (i))

static const Proto* combine(lua_State* L, int n) {
    if (n == 1)
        return toproto(L, -1);
    else {
        Proto* f;
        int i = n;
        if (lua_load(L, reader, &i, "=(" PROGNAME ")", NULL) != LUA_OK) fatal(lua_tostring(L, -1));
        f = toproto(L, -1);
        for (i = 0; i < n; i++) {
            f->p[i] = toproto(L, i - n - 1);
            if (f->p[i]->sizeupvalues > 0) f->p[i]->upvalues[0].instack = 0;
        }
        f->sizelineinfo = 0;
        return f;
    }
}

static int writer(lua_State* L, const void* p, size_t size, void* u) {
    UNUSED(L);
    int r = PCwrite(*(int*)u, p, size);
    return (r < 0 || (size_t)r != size) && (size != 0);
}

LUALIB_API int(luaL_loadfilex)(lua_State* L, const char* filename, const char* mode);

#define luaL_loadfile(L, f) luaL_loadfilex(L, f, NULL)

typedef struct LoadF {
    int n;                      /* number of pre-read characters */
    int f;                      /* file being read */
    char buff[LUAL_BUFFERSIZE]; /* area for reading file */
} LoadF;

static const char* getF(lua_State* L, void* ud, size_t* size) {
    LoadF* lf = (LoadF*)ud;
    (void)L;           /* not used */
    if (lf->n > 0) {   /* are there pre-read characters to be read? */
        *size = lf->n; /* return them (chars already in buffer) */
        lf->n = 0;     /* no more pre-read characters */
    } else {           /* read a block from file */
        int read = PCread(lf->f, lf->buff, sizeof(lf->buff)); /* read block */
        if (read <= 0) return NULL;
        *size = read;
    }
    return lf->buff;
}

static int errfile(lua_State* L, const char* what, int fnameindex) {
    const char* filename = lua_tostring(L, fnameindex) + 1;
    lua_pushfstring(L, "cannot %s %s", what, filename);
    lua_remove(L, fnameindex);
    return LUA_ERRFILE;
}

#define LUAC_EOF (-1)

static int luaA_getc(int f) {
    int c = 0;
    int r = PCread(f, &c, 1);
    return r == 1 ? c : LUAC_EOF;
}

static int skipBOM(LoadF* lf) {
    const char* p = "\xEF\xBB\xBF"; /* Utf8 BOM mark */
    int c;
    lf->n = 0;
    do {
        c = luaA_getc(lf->f);
        if (c == LUAC_EOF || c != *(const unsigned char*)p++) return c;
        lf->buff[lf->n++] = c; /* to be read by the parser */
    } while (*p != '\0');
    lf->n = 0;          /* prefix matched; discard it */
    return luaA_getc(lf->f); /* return next character */
}

/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
static int skipcomment(LoadF* lf, int* cp) {
    int c = *cp = skipBOM(lf);
    if (c == '#') { /* first line is a comment (Unix exec. file)? */
        do {        /* skip first line */
            c = luaA_getc(lf->f);
        } while (c != LUAC_EOF && c != '\n');
        *cp = luaA_getc(lf->f); /* skip end-of-line, if present */
        return 1;          /* there was a comment */
    } else
        return 0; /* no comment */
}

LUALIB_API int luaL_loadfilex(lua_State* L, const char* filename, const char* mode) {
    LoadF lf;
    int status;
    int c;
    int fnameindex = lua_gettop(L) + 1; /* index of filename on the stack */
    if (filename == NULL) {
        return LUA_ERRFILE;
    } else {
        const char * f = lua_pushfstring(L, "@%s", filename);
        lf.f = PCopen(f + 1, 0, 0);
        if (lf.f < 0) return errfile(L, "open", fnameindex);
    }
    if (skipcomment(&lf, &c))                 /* read initial portion */
        lf.buff[lf.n++] = '\n';               /* add line to correct line numbers */
    if (c != LUAC_EOF) lf.buff[lf.n++] = c; /* 'c' is the first character of the stream */
    status = lua_load(L, getF, &lf, lua_tostring(L, -1), mode);
    PCclose(lf.f);
    lua_remove(L, fnameindex);
    return status;
}

static int pmain(lua_State* L) {
    int argc = (int)lua_tointeger(L, 1);
    char** argv = (char**)lua_touserdata(L, 2);
    const Proto* f;
    int i;
    if (!lua_checkstack(L, argc)) fatal("too many input files");
    for (i = 0; i < argc; i++) {
        if (luaL_loadfile(L, argv[i]) != LUA_OK) fatal(lua_tostring(L, -1));
    }
    f = combine(L, argc);
    if (listing) luaU_print(f, listing > 1);
    if (dumping) {
        const char * fname = lua_pushfstring(L, "%s", output);
        int D = PCcreat(fname, 0);
        lua_pop(L, 1);
        if (D < 0) cannot("open");
        lua_lock(L);
        luaU_dump(L, f, writer, &D, stripping);
        lua_unlock(L);
        if (PCclose(D) != 0) cannot("close");
    }
    return 0;
}

int main() {
    const char* argv[MAXARGS] = {0};
    const char* argsPtr = (const char*)ARGS_BASE;
    int argc = 0;
    lua_State* L;
    while (argc < MAXARGS) {
        int len = luaA_strlen(argsPtr);
        if (len == 0) break;
        argv[argc++] = argsPtr;
        argsPtr += len + 1;
    }
    if (argc == MAXARGS && luaA_strlen(argsPtr) != 0) fatal("too many arguments");
    int i = doargs(argc, argv);
    argc -= i;
    if (argc <= 0) usage("no input files given");
    L = luaL_newstate();
    if (L == NULL) fatal("cannot create state: not enough memory");
    lua_pushcfunction(L, &pmain);
    lua_pushinteger(L, argc);
    lua_pushlightuserdata(L, argv + i);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) fatal(lua_tostring(L, -1));
    lua_close(L);
    pcsx_exit(EXIT_SUCCESS);
}

/*
** $Id: print.c,v 1.69 2013/07/04 01:03:46 lhf Exp $
** print bytecodes
** See Copyright Notice in lua.h
*/

#define luac_c
#define LUA_CORE

#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"

#define VOID(p) ((const void*)(p))

static void PrintString(const TString* ts) {
    const char* s = getstr(ts);
    size_t i, n = ts->tsv.len;
    ramsyscall_printf("%c", '"');
    for (i = 0; i < n; i++) {
        int c = (int)(unsigned char)s[i];
        switch (c) {
            case '"':
                ramsyscall_printf("\\\"");
                break;
            case '\\':
                ramsyscall_printf("\\\\");
                break;
            case '\a':
                ramsyscall_printf("\\a");
                break;
            case '\b':
                ramsyscall_printf("\\b");
                break;
            case '\f':
                ramsyscall_printf("\\f");
                break;
            case '\n':
                ramsyscall_printf("\\n");
                break;
            case '\r':
                ramsyscall_printf("\\r");
                break;
            case '\t':
                ramsyscall_printf("\\t");
                break;
            case '\v':
                ramsyscall_printf("\\v");
                break;
            default:
                if (lisprint(c))
                    ramsyscall_printf("%c", c);
                else
                    ramsyscall_printf("\\%03d", c);
        }
    }
    ramsyscall_printf("%c", '"');
}

static void PrintConstant(const Proto* f, int i) {
    const TValue* o = &f->k[i];
    switch (ttypenv(o)) {
        case LUA_TNIL:
            ramsyscall_printf("nil");
            break;
        case LUA_TBOOLEAN:
            ramsyscall_printf(bvalue(o) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            ramsyscall_printf(LUA_NUMBER_FMT, nvalue(o));
            break;
        case LUA_TSTRING:
            PrintString(rawtsvalue(o));
            break;
        default: /* cannot happen */
            ramsyscall_printf("? type=%d", ttype(o));
            break;
    }
}

#define UPVALNAME(x) ((f->upvalues[x].name) ? getstr(f->upvalues[x].name) : "-")
#define MYK(x) (-1 - (x))

static void PrintCode(const Proto* f) {
    const Instruction* code = f->code;
    int pc, n = f->sizecode;
    for (pc = 0; pc < n; pc++) {
        Instruction i = code[pc];
        OpCode o = GET_OPCODE(i);
        int a = GETARG_A(i);
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        int ax = GETARG_Ax(i);
        int bx = GETARG_Bx(i);
        int sbx = GETARG_sBx(i);
        int line = getfuncline(f, pc);
        ramsyscall_printf("\t%d\t", pc + 1);
        if (line > 0)
            ramsyscall_printf("[%d]\t", line);
        else
            ramsyscall_printf("[-]\t");
        ramsyscall_printf("%-9s\t", luaP_opnames[o]);
        switch (getOpMode(o)) {
            case iABC:
                ramsyscall_printf("%d", a);
                if (getBMode(o) != OpArgN) ramsyscall_printf(" %d", ISK(b) ? (MYK(INDEXK(b))) : b);
                if (getCMode(o) != OpArgN) ramsyscall_printf(" %d", ISK(c) ? (MYK(INDEXK(c))) : c);
                break;
            case iABx:
                ramsyscall_printf("%d", a);
                if (getBMode(o) == OpArgK) ramsyscall_printf(" %d", MYK(bx));
                if (getBMode(o) == OpArgU) ramsyscall_printf(" %d", bx);
                break;
            case iAsBx:
                ramsyscall_printf("%d %d", a, sbx);
                break;
            case iAx:
                ramsyscall_printf("%d", MYK(ax));
                break;
        }
        switch (o) {
            case OP_LOADK:
                ramsyscall_printf("\t; ");
                PrintConstant(f, bx);
                break;
            case OP_GETUPVAL:
            case OP_SETUPVAL:
                ramsyscall_printf("\t; %s", UPVALNAME(b));
                break;
            case OP_GETTABUP:
                ramsyscall_printf("\t; %s", UPVALNAME(b));
                if (ISK(c)) {
                    ramsyscall_printf(" ");
                    PrintConstant(f, INDEXK(c));
                }
                break;
            case OP_SETTABUP:
                ramsyscall_printf("\t; %s", UPVALNAME(a));
                if (ISK(b)) {
                    ramsyscall_printf(" ");
                    PrintConstant(f, INDEXK(b));
                }
                if (ISK(c)) {
                    ramsyscall_printf(" ");
                    PrintConstant(f, INDEXK(c));
                }
                break;
            case OP_GETTABLE:
            case OP_SELF:
                if (ISK(c)) {
                    ramsyscall_printf("\t; ");
                    PrintConstant(f, INDEXK(c));
                }
                break;
            case OP_SETTABLE:
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_POW:
            case OP_EQ:
            case OP_LT:
            case OP_LE:
                if (ISK(b) || ISK(c)) {
                    ramsyscall_printf("\t; ");
                    if (ISK(b))
                        PrintConstant(f, INDEXK(b));
                    else
                        ramsyscall_printf("-");
                    ramsyscall_printf(" ");
                    if (ISK(c))
                        PrintConstant(f, INDEXK(c));
                    else
                        ramsyscall_printf("-");
                }
                break;
            case OP_JMP:
            case OP_FORLOOP:
            case OP_FORPREP:
            case OP_TFORLOOP:
                ramsyscall_printf("\t; to %d", sbx + pc + 2);
                break;
            case OP_CLOSURE:
                ramsyscall_printf("\t; %p", VOID(f->p[bx]));
                break;
            case OP_SETLIST:
                if (c == 0)
                    ramsyscall_printf("\t; %d", (int)code[++pc]);
                else
                    ramsyscall_printf("\t; %d", c);
                break;
            case OP_EXTRAARG:
                ramsyscall_printf("\t; ");
                PrintConstant(f, ax);
                break;
            default:
                break;
        }
        ramsyscall_printf("\n");
    }
}

#define SS(x) ((x == 1) ? "" : "s")
#define S(x) (int)(x), SS(x)

static void PrintHeader(const Proto* f) {
    const char* s = f->source ? getstr(f->source) : "=?";
    if (*s == '@' || *s == '=')
        s++;
    else if (*s == LUA_SIGNATURE[0])
        s = "(bstring)";
    else
        s = "(string)";
    ramsyscall_printf("\n%s <%s:%d,%d> (%d instruction%s at %p)\n", (f->linedefined == 0) ? "main" : "function", s, f->linedefined,
           f->lastlinedefined, S(f->sizecode), VOID(f));
    ramsyscall_printf("%d%s param%s, %d slot%s, %d upvalue%s, ", (int)(f->numparams), f->is_vararg ? "+" : "", SS(f->numparams),
           S(f->maxstacksize), S(f->sizeupvalues));
    ramsyscall_printf("%d local%s, %d constant%s, %d function%s\n", S(f->sizelocvars), S(f->sizek), S(f->sizep));
}

static void PrintDebug(const Proto* f) {
    int i, n;
    n = f->sizek;
    ramsyscall_printf("constants (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        ramsyscall_printf("\t%d\t", i + 1);
        PrintConstant(f, i);
        ramsyscall_printf("\n");
    }
    n = f->sizelocvars;
    ramsyscall_printf("locals (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        ramsyscall_printf("\t%d\t%s\t%d\t%d\n", i, getstr(f->locvars[i].varname), f->locvars[i].startpc + 1,
               f->locvars[i].endpc + 1);
    }
    n = f->sizeupvalues;
    ramsyscall_printf("upvalues (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        ramsyscall_printf("\t%d\t%s\t%d\t%d\n", i, UPVALNAME(i), f->upvalues[i].instack, f->upvalues[i].idx);
    }
}

static void PrintFunction(const Proto* f, int full) {
    int i, n = f->sizep;
    PrintHeader(f);
    PrintCode(f);
    if (full) PrintDebug(f);
    for (i = 0; i < n; i++) PrintFunction(f->p[i], full);
}

int luaI_sprintf(char *str, const char *format, ...) {
    *str = 0;
    return 0;
}
