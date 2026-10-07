// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <cstdint> // for int32_t
#include <fstream>
//#include <unistd.h>
//#include <sys/socket.h>
#include <cstdio>
/*included by me*/
#include<stdexcept>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64; /*used for call stack*/
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node* next;
        Node(const T& d, Node* ptr) :data(d), next(ptr) {}
    };
    Node* top;
    int32_t count;

public:
    // Implement these functions:
    Stack() :top(nullptr), count(0) {} // initialize the stack

    ~Stack()
    {
        while (top != nullptr)
        {
            Node* temp = top;
            top = top->next;
            delete temp;
        }
    }

    /*my assumption is that call stack is never copied*/
    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;

    void push(const T& val) // pushes the value on the stack if max limit is not reached yet.
    {
        if (count >= MAX_STACK_DEPTH)
        {
            throw overflow_error("Stack has reached its Max Limit!");
        }
        Node* temp = new Node(val, top);
        top = temp;
        count++;
    }
    T pop() // pop the top value on the stack
    {
        if (count <= 0) {
            throw underflow_error("Stack is Empty!");
        }
        Node* temp = top;
        top = top->next;
        T tVal = temp->data;
        delete temp;
        count--;
        return tVal;
    }
    T& peek() // returns the top value on the stack
    {
        if (count <= 0) {
            throw underflow_error("Stack is Empty!");
        }
        return top->data;
    }
    bool isEmpty()
    {
        return count == 0;
    }
    int32_t depth()
    {
        return count;
    }
    /* copies every frame, top to bottom in the array given as a parameter
           this is what buildSnapshot() call, returns count written */
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        int32_t it = 0;
        Node* temp = top;
        while (temp != nullptr && it < maxLen) {
            out[it++] = temp->data;
            temp = temp->next;
        }
        return it;
    }
};

struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot* data;
    TimelineNode* next;
    TimelineNode* prev;
};
// Timeline : doubly linked list of Snapshots
class Timeline
{
    TimelineNode* head, * tail;
    int32_t stepCount;

public:
    // Implement these functions
    Timeline() :head(nullptr), tail(nullptr), stepCount(0) {}

    ~Timeline() {
        TimelineNode* temp = nullptr;
        while (head != nullptr) {
            temp = head;
            head = head->next;
            delete temp;
        }
    }

    /*my assumption is that TimeLine is never copied as we have only one instance of debugger at a time*/
    Timeline(const Timeline& other) = delete;
    Timeline& operator=(const Timeline& other) = delete;

    void record(Snapshot* s)
    {
        if (head == nullptr) {
            head = new TimelineNode{ s, nullptr, nullptr };
            tail = head;
            stepCount++;
            return;
        }
        tail->next = new TimelineNode{ s, nullptr, tail };
        tail = tail->next;
        stepCount++;
    }
    TimelineNode* begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

// Core structs
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};

Snapshot* buildSnapshot(Stack<Frame>& callStack)
{
    Snapshot* snap = new Snapshot(); /*heap: a Snapshot is far too big for the stack and will also be  returned after this function ends*/
    snap->stackDepth = callStack.snapshot_into(snap->callStack, MAX_STACK_DEPTH);
    return snap;
}

struct TTDBHeader
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};

/* Header = magic(4) + version(4) + stepCount(4) + indexOffset(8) = 20 bytes. */
/* Each field is written on its own, never the whole struct, so no padding bytes reach the file. */
bool writeHeader(FILE* f, const TTDBHeader& h)
{
    if (f == nullptr)
    {
        return false;
    }
    if (fwrite(h.magic, sizeof(char), 4, f) != 4)
    {
        return false;
    }
    if (fwrite(&h.version, sizeof(int32_t), 1, f) != 1)
    {
        return false;
    }
    if (fwrite(&h.stepCount, sizeof(int32_t), 1, f) != 1)
    {
        return false;
    }
    if (fwrite(&h.indexOffset, sizeof(int64_t), 1, f) != 1)
    {
        return false;
    }
    return true;
}

// resolve.bin - bookkeeping
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};

/*Self Made Helpers for 0x0*/
static bool isSpaceChar(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static string nextWord(const string& line, size_t& i)
{
    /*skips all whitespace*/
    while (i < line.size() && isSpaceChar(line[i])) {
        i++;
    }
    size_t start = i;
    /*goes till the next whitespace char*/
    while (i < line.size() && !isSpaceChar(line[i])) {
        i++;
    }
    return line.substr(start, i - start);
}

// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream& in, string& out) // reads the next non blank line
{
    string line;
    while (getline(in, line))
    {
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end == string::npos)
            continue; // blank line
        line.erase(end + 1);
        line.erase(0, line.find_first_not_of(" \t"));
        out = line;
        return true;
    }
    return false;
}

string firstWord(const string& line) //returns first word from the input string
{
    size_t i = 0;
    return nextWord(line, i);
}
string secondWord(const string& line) // returns the second word
{
    size_t i = 0;
    nextWord(line, i); /*discarding the first word*/
    return nextWord(line, i);
}

bool validateProgram(const char* sourcePath) // for each func defined there should be exactly one func_end and no nested funcs allowed - 
{
    ifstream Rdr(sourcePath);
    if (!Rdr) {
        cerr << "Error: Cannot Open File " << sourcePath << endl;
        return false;
    }
    bool funcOpen = false;   /*true while a func is open(depth is only ever 0 or 1)*/
    int32_t funcLineNo = 0;  /*line number of the currently open func*/
    int32_t lineNo = 0;        /*counts non - blank lines only*/
    string currLine;

    while (readSourceLine(Rdr, currLine))
    {
        lineNo++;
        string word = firstWord(currLine);

        if (word == "func")
        {
            if (funcOpen)
            {
                cerr << "Error: nested function declaration at line " << lineNo << " (function opened at line " << funcLineNo << " is not closed)" << endl;
                return false;
            }
            funcOpen = true;
            funcLineNo = lineNo;
        }
        else if (word == "func_end")
        {
            if (!funcOpen)
            {
                cerr << "Error: func_end has no matching func at line " << lineNo << endl;
                return false;
            }
            funcOpen = false;
        }
    }
    if (funcOpen)
    {
        cerr << "Error: func at line " << funcLineNo << " has no matching func_end" << endl;
        return false;
    }
    return true;
}

// PASS 0x1: RESOLVE() -> resolve.bin
// writes one [offset(8B)][size(4B)][string] record at the current file position
// returns this record's own starting byte position
int64_t writeResolveRecord(FILE* f, int64_t offsetField, const string& text)
{
    /*Check pointer passed properly*/
    if (f == nullptr) {
        return -1;
    }
    /* Write byte offset of 8 bytes (int64_t)*/
    int64_t start = static_cast<int64_t>(ftell(f));
    if (fwrite(&offsetField, sizeof(int64_t), 1, f) != 1) {
        return -1;
    }
    /*Write text.size() of 4 byte (int32_t)*/
    int32_t len = static_cast<int32_t>(text.size());
    if (fwrite(&len, sizeof(int32_t), 1, f) != 1) {
        return -1;
    }
    /*Check to enforce if the string is not empty or is not written properly*/
    if (len > 0 && fwrite(text.data(), sizeof(char), text.size(), f) != text.size()) {
        return -1;
    }
    return start;
}
int64_t readResolveRecord(FILE* f, string& outText) // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
{
    if (f == nullptr) {
        return -1;
    }
    int64_t offsetRead = 0;

    /* return -1 from here means end of file i.e no more functions exist in resolve.bin */
    if (fread(&offsetRead, sizeof(int64_t), 1, f) != 1)
    {
        return -1;
    }

    /*return -1 from here means corrupted file*/
    int32_t strSize = 0;
    if (fread(&strSize, sizeof(int32_t), 1, f) != 1)
    {
        return -1;
    }

    /* return -1 from here means nonsense size */
    if (strSize < 0 || static_cast<uint64_t>(strSize) > MAX_SOURCE_BYTES)
    {
        return -1;
    }

    /* return -1 here means the file ended before all strSize text bytes were read */
    outText.resize(static_cast<size_t>(strSize));
    if (strSize > 0 && fread(&outText[0], sizeof(char), static_cast<size_t>(strSize), f) != static_cast<size_t>(strSize))
    {
        return -1;
    }

    return offsetRead;
}

// Every source line becomes one record holding the raw line, as-is.
// resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
// (remember its position) and CALL (remember which function it needs
// and where its offset field sits).
// Once the whole file is written, every CALL's offset field is patched
// with its target's position. Patching happens after the full write
// Returns the byte offset of main's FUNC header record.
// if there is no main return the error 


/*Returns the index of function name in funcArray, or -1 if it is not found*/
static int32_t funcFind(const FuncEntry funcArr[], int32_t funcCt, const string& funcN)
{
    for (int32_t i = 0; i < funcCt; i++)
    {
        if (funcArr[i].funcName == funcN)
        {
            return i;
        }
    }
    return -1;
}

static int64_t throwResolveError(FILE* f, const string& msg)
{
    cerr << "Error: " << msg << endl;
    if (f != nullptr)
    {
        fclose(f);
    }
    return -1;
}


int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)
{
    /*the stack was getting too big of the function so heap was used*/
    FuncEntry* funcArray = new FuncEntry[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch* patches = new PendingPatch[MAX_PATCHES];
    int32_t patchCount = 0;

    ifstream Rdr(sourcePath);
    if (!Rdr) {
        cerr << "Cannot open Source file: " << sourcePath << endl;
        return -1;
    }
    FILE* f = fopen(resolveBinPath, "wb+"); /*need + so we can seek back and patch as it open files for reading too*/
    if (f == nullptr) {
        cerr << "Cannot open or create resolve file: " << resolveBinPath << endl;
        return -1;
    }

    /*write every line as a record form source.bin*/
    int64_t offset = 0;
    int32_t lineNo = 0; // counts non-blank lines
    string line;
    while (readSourceLine(Rdr, line))
    {
        lineNo++;
        string word = firstWord(line);
        int64_t offsetField = offset; // normal lines hold their own offset

        if (word == "func")
        {
            string name = secondWord(line);
            if (name.empty())
            {
                return throwResolveError(f, "function without a name at line " + to_string(lineNo));
            }
            if (funcCount >= MAX_FUNCS)
            {
                return throwResolveError(f, "too many functions (max " + to_string(MAX_FUNCS) + ")");
            }
            if (funcFind(funcArray, funcCount, name) != -1)
            {
                return throwResolveError(f, "duplicate function '" + name + "' at line " + to_string(lineNo));
            }
            funcArray[funcCount].funcName = name;
            funcArray[funcCount].byteOffsetInResolveBin = offset;
            funcCount++;
        }
        else if (word == "call")
        {
            string target = secondWord(line);
            if (target.empty())
            {
                return throwResolveError(f, "call without a function name at line " + to_string(lineNo));
            }
            if (patchCount >= MAX_PATCHES)
            {
                return throwResolveError(f, "too many calls (max " + to_string(MAX_PATCHES) + ")");
            }
            patches[patchCount].byteOffsetOfOffsetField = offset; /* the offset field is the first 8 bytes of the record */
            patches[patchCount].targetFuncName = target;
            patchCount++;
            offsetField = 0; // placeholder, patched below
        }

        int64_t start = writeResolveRecord(f, offsetField, line);
        if (start != offset)
        {
            return throwResolveError(f, "write failed or offset mismatch at line " + to_string(lineNo));
        }
        offset += 8 + 4 + static_cast<int64_t>(line.size()); /* next record offset */
    }

    /* patch every call with its func offset */
    for (int32_t i = 0; i < patchCount; i++)
    {
        int32_t idx = funcFind(funcArray, funcCount, patches[i].targetFuncName);
        if (idx == -1)
        {
            return throwResolveError(f, "call to undefined function '" + patches[i].targetFuncName + "'");
        }
        if (fseek(f, static_cast<long>(patches[i].byteOffsetOfOffsetField), SEEK_SET) != 0)
        {
            return throwResolveError(f, "seek failed while patching calls");
        }
        int64_t target = funcArray[idx].byteOffsetInResolveBin;
        if (fwrite(&target, sizeof(int64_t), 1, f) != 1)
        {
            return throwResolveError(f, "write failed while patching calls");
        }
    }
    int32_t mainIdx = funcFind(funcArray, funcCount, "main");
    if (mainIdx == -1)
    {
        return throwResolveError(f, "no main function");
    }
    int64_t mainOffset = funcArray[mainIdx].byteOffsetInResolveBin;
    fclose(f);
    delete[] funcArray;
    delete[] patches;
    return mainOffset;
}

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,  /*e.g: func,set*/
    IDENTIFIER, /*e.g: add,mul,sub*/
    PARAM  /*e.g a,b,c*/
};
struct Token
{
    TokenType type;
    string text;
};

// first word is always a instruction keyword
// instruction set = [func, func_end, call, set, add, sub, mul and div]
// next word is identifier like name of a function, variable name
// after identifier all are the params/arg, space separated
int32_t tokenizeLine(const string& line, Token* tokens, int32_t maxTokens)
{
    size_t i = 0;
    int32_t count = 0; /*count of tokens in the line*/
    while (true)
    {
        string word = nextWord(line, i);
        if (word.empty())
        {
            break; /* line Ended*/
        }
        if (count >= maxTokens)
        {
            return -1; /*more words then size of token Array */
        }
        tokens[count].text = word;
        if (count == 0)
        {
            tokens[count].type = KEYWORD; /* instruction */
        }
        else if (count == 1)
        {
            tokens[count].type = IDENTIFIER; /* function or variable name */
        }
        else
        {
            tokens[count].type = PARAM; /*parameters and arguments*/
        }
        count++;
    }
    return count;
}

/*Self Made Pass 0x2 helpers*/

/*true if text is an integer like 10 or -5; the value goes into out*/
static bool parseNumber(const string& text, int32_t& out)
{
    if (text.empty())
    {
        return false;
    }
    size_t i = 0;
    bool negative = false;
    if (text[0] == '-')
    {
        negative = true;
        i = 1;
    }
    if (i >= text.size())
    {
        return false; /*only "-"*/
    }
    int64_t val = 0;
    for (; i < text.size(); i++)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return false; /*Not Number*/
        }
        val = val * 10 + (text[i] - '0'); /*char to int conversion along with insertion at end*/
        if (val > INT64_MAX)
        {
            return false; /*too big for int32_t*/
        }
    }
    if (negative)
    {
        val = -val;
    }
    if (val < INT32_MIN || val > INT32_MAX)
    {
        return false;
    }
    out = static_cast<int32_t>(val);
    return true;
}

/* finds a variable by name : parameters first, then locals then nullptr if missing */
static Variable* findVar(Frame& fr, const string& varName)
{
    for (int32_t i = 0; i < fr.argc; i++)
    {
        if (fr.argv[i].name == varName)
        {
            return &fr.argv[i];
        }
    }
    for (int32_t i = 0; i < fr.localCount; i++)
    {
        if (fr.locals[i].name == varName)
        {
            return &fr.locals[i];
        }
    }
    return nullptr;
}

/* text is either a number or a variable name; false if it is neither */
static bool getValue(Frame& fr, const string& text, int32_t& out)
{
    if (parseNumber(text, out))
    {
        return true;
    }
    Variable* v = findVar(fr, text);
    if (v == nullptr)
    {
        return false;
    }
    out = v->value;
    return true;
}

static bool throwRunTImeError(const string& msg, const string& instruction)
{
    cerr << "Runtime error: " << msg << " (Instruction: " << instruction << ")" << endl;
    return false;
}

/* Runs the program.Returns false on a runtime error(the timeline recorded so far stays valid). */
static bool runProgram(FILE* f, int64_t mainOffset, Timeline& timeline) /*Time Line is passed by reference so snapshots recorded here stay visible and we also delete timeline's copy constructor and assignment operator*/
{   /*main offset returned by resolveProgram*/
    Stack<Frame> callStack;
    Token* tokens = new Token[MAX_TOKENS];
    string text; /*raw line entry*/

    /* main's header: must be exactly "func main" */
    if (fseek(f, static_cast<long>(mainOffset), SEEK_SET) != 0) /*fseek uses long*/
    {
        return throwRunTImeError("cannot seek to main", "None");
    }
    if (readResolveRecord(f, text) == -1)
    {
        return throwRunTImeError("cannot read main's header", "None");
    }
    int32_t n = tokenizeLine(text, tokens, MAX_TOKENS);
    if (n != 2 || tokens[0].text != "func" || tokens[1].text != "main")
    {
        return throwRunTImeError("main's header must be exactly 'func main'", text);
    }

    Frame mainFrame = Frame(); /* default initialized */
    mainFrame.func_name = "main";
    mainFrame.returnLine = -1;
    callStack.push(mainFrame);
    timeline.record(buildSnapshot(callStack)); /* step 0 */

    /* executed line by line */
    while (true)
    {
        /*Both used only if a function is called*/
        int64_t recStart = static_cast<int64_t>(ftell(f)); /*starting bit of offset where this record starts */
        int64_t field = readResolveRecord(f, text);        /* for a call : the target's offset is returned */
        /*Just in case if validation fails*/
        if (field == -1)
        {
            return throwRunTImeError("reached the end of resolve.bin before main ended", "None");
        }
        n = tokenizeLine(text, tokens, MAX_TOKENS);
        if (n == 0 || n == -1)
        {
            return throwRunTImeError("empty or too long instruction", text);
        }
        const string& identi = tokens[0].text;

        if (identi == "set")
        {
            if (n != 3)
            {
                return throwRunTImeError("set needs a variable and a value", text);
            }
            Frame& cur = callStack.peek();
            int32_t val = 0;
            if (!getValue(cur, tokens[2].text, val))
            {
                return throwRunTImeError("undefined variable '" + tokens[2].text + "'", text);
            }
            Variable* v = findVar(cur, tokens[1].text);
            if (v != nullptr)
            {
                v->value = val;
            }
            else
            {
                int32_t dummy = 0;
                if (parseNumber(tokens[1].text, dummy))
                {
                    return throwRunTImeError("a number cannot be used as a variable name", text);
                }
                if (cur.localCount >= MAX_VARS_PER_FRAME)
                {
                    return throwRunTImeError("too many local variables", text);
                }
                cur.locals[cur.localCount].name = tokens[1].text;
                cur.locals[cur.localCount].value = val;
                cur.localCount++;
            }
            timeline.record(buildSnapshot(callStack));
        }
        else if (identi == "add" || identi == "sub" || identi == "mul" || identi == "div")
        {
            if (n != 3)
            {
                return throwRunTImeError(identi + " needs a variable and a value", text);
            }
            Frame& cur = callStack.peek();
            Variable* dst = findVar(cur, tokens[1].text);
            if (dst == nullptr)
            {
                return throwRunTImeError("undefined variable '" + tokens[1].text + "'", text);
            }
            int32_t rhs = 0;
            if (!getValue(cur, tokens[2].text, rhs))
            {
                return throwRunTImeError("undefined variable '" + tokens[2].text + "'", text);
            }
            int64_t a = dst->value;
            int64_t b = rhs;
            int64_t r = 0;
            if (identi == "add")
            {
                r = a + b;
            }
            else if (identi == "sub")
            {
                r = a - b;
            }
            else if (identi == "mul")
            {
                r = a * b;
            }
            else
            {
                if (b == 0)
                {
                    return throwRunTImeError("division by zero", text);
                }
                r = a / b;
            }
            if (r < INT32_MIN || r > INT32_MAX)
            {
                return throwRunTImeError("arithmetic overflow", text);
            }
            dst->value = static_cast<int32_t>(r);
            timeline.record(buildSnapshot(callStack));
        }
        else if (identi == "call")
        {
            if (n < 2)
            {
                return throwRunTImeError("call needs a function name", text);
            }
            Frame& cur = callStack.peek();
            int32_t argc = n - 2;
            int32_t argVals[MAX_VARS_PER_FRAME];
            for (int32_t i = 0; i < argc; i++) /*check if all variables are defined*/
            {
                if (!getValue(cur, tokens[2 + i].text, argVals[i]))
                {
                    return throwRunTImeError("undefined variable '" + tokens[2 + i].text + "'", text);
                }
            }

            /* jump to the call's header */
            if (fseek(f, static_cast<long>(field), SEEK_SET) != 0)
            {
                return throwRunTImeError("cannot seek to the called function", text);
            }
            string header;
            if (readResolveRecord(f, header) == -1)
            {
                return throwRunTImeError("cannot read the called function's header", text);
            }
            Token ht[MAX_TOKENS];
            int32_t hn = tokenizeLine(header, ht, MAX_TOKENS);
            if (hn < 2 || ht[0].text != "func")
            {
                return throwRunTImeError("call target is not a function", text);
            }
            int32_t params = hn - 2;
            if (params != argc)
            {
                return throwRunTImeError("'" + ht[1].text + "' expects " + to_string(params) + " arguments but got " + to_string(argc), text);
            }
            Frame fr = Frame();
            fr.func_name = ht[1].text;
            fr.argc = params;
            for (int32_t i = 0; i < params; i++)
            {
                fr.argv[i].name = ht[2 + i].text;
                fr.argv[i].value = argVals[i]; // copy in
            }
            fr.returnLine = static_cast<int32_t>(recStart); // where the call record is
            callStack.push(fr);                             /* may throw stack overflow_error from our own custome built class */
            timeline.record(buildSnapshot(callStack));
            /* the file cursor is now right after the func's header i.e its first body line */
        }
        else if (identi == "func_end")
        {
            if (callStack.depth() == 1)
            {
                timeline.record(buildSnapshot(callStack)); // final state, taken BEFORE popping
                callStack.pop();
                return true;
            }
            Frame callee = callStack.pop();

            // re-read the call record: it gives the argument names and leaves the cursor on the next line
            if (fseek(f, static_cast<long>(callee.returnLine), SEEK_SET) != 0)
            {
                return throwRunTImeError("cannot seek back to the call", text);
            }
            string callText;
            if (readResolveRecord(f, callText) == -1)
            {
                return throwRunTImeError("cannot read the call record", text);
            }
            Token ct[MAX_TOKENS];
            int32_t cn = tokenizeLine(callText, ct, MAX_TOKENS);
            Frame& caller = callStack.peek();
            for (int32_t i = 0; i < callee.argc && 2 + i < cn; i++)
            {
                Variable* v = findVar(caller, ct[2 + i].text);
                if (v != nullptr) // literal arguments are skipped
                {
                    v->value = callee.argv[i].value; // copy out
                }
            }
            timeline.record(buildSnapshot(callStack));
        }
        else
        {
            return throwRunTImeError("unknown or unexpected keyword '" + identi + "'", text);
        }
    }
}

void executeProgram(const char* resolveBinPath, int64_t mainOffset, Timeline& timeline)
{
    FILE* f = fopen(resolveBinPath, "rb");
    if (f == nullptr)
    {
        cerr << "Error: cannot open " << resolveBinPath << endl;
        return;
    }
    try
    {
        runProgram(f, mainOffset, timeline);
    }
    catch (const exception& e) // e.g. the stack's overflow_error on runaway recursion
    {
        cerr << "Runtime error: " << e.what() << endl;
    }
    fclose(f);
}

// PASS 0x3: SERIALIZE TIMELINE
static bool writeInt32(FILE* f, int32_t v)
{
    return fwrite(&v, sizeof(int32_t), 1, f) == 1;
}

// length (4 bytes) followed by the characters, no '\0' (same idea as a resolve record)
static bool writeString(FILE* f, const string& s)
{
    int32_t len = static_cast<int32_t>(s.size());
    if (!writeInt32(f, len))
    {
        return false;
    }
    if (len > 0 && fwrite(s.data(), sizeof(char), s.size(), f) != s.size())
    {
        return false;
    }
    return true;
}

static bool writeVariable(FILE* f, const Variable& v)
{
    return writeString(f, v.name) && writeInt32(f, v.value);
}

/* Only the used slots are written : argc parameters and localCount locals */
static bool writeFrame(FILE* f, const Frame& fr)
{
    if (fr.argc < 0 || fr.argc > MAX_VARS_PER_FRAME || fr.localCount < 0 || fr.localCount > MAX_VARS_PER_FRAME)
    {
        return false; /* corrupted frame */
    }
    if (!writeString(f, fr.func_name) || !writeInt32(f, fr.argc))
    {
        return false;
    }
    for (int32_t i = 0; i < fr.argc; i++)
    {
        if (!writeVariable(f, fr.argv[i]))
        {
            return false;
        }
    }
    if (!writeInt32(f, fr.returnLine) || !writeInt32(f, fr.localCount))
    {
        return false;
    }
    for (int32_t i = 0; i < fr.localCount; i++)
    {
        if (!writeVariable(f, fr.locals[i]))
        {
            return false;
        }
    }
    return true;
}

/* Only the stackDepth real frames are written, top frame first(same order as in memory) */
static bool writeSnapshot(FILE* f, const Snapshot* s)
{
    if (s == nullptr || s->stackDepth < 0 || s->stackDepth > MAX_STACK_DEPTH)
    {
        return false;
    }
    if (!writeInt32(f, s->stackDepth))
    {
        return false;
    }
    for (int32_t i = 0; i < s->stackDepth; i++)
    {
        if (!writeFrame(f, s->callStack[i]))
        {
            return false;
        }
    }
    return true;
}

// Prints the error, closes the file, frees the index and returns false (used on every error path)
static bool tdbgFail(FILE* f, int64_t* index, const string& msg)
{
    cerr << "Error: " << msg << endl;
    if (f != nullptr)
    {
        fclose(f);
    }
    delete[] index; // deleting nullptr is safe
    return false;
}

// placeholder for header
// index array of the size of stepcount from the timeline
// placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
// after timeline add the index array i the file
// update the header
bool writeTdbg(Timeline& timeline, const char* tdbgPath)
{
    FILE* f = fopen(tdbgPath, "wb");
    if (f == nullptr)
    {
        cerr << "Error: cannot create " << tdbgPath << endl;
        return false;
    }

    int32_t stepCount = timeline.getStepCount();
    int64_t* index = nullptr;
    if (stepCount > 0)
    {
        index = new int64_t[stepCount]; /* index[n] = where snapshot n starts */
    }

    /* placeholder header : reserves the first 20 bytes, filled in properly at the end */
    TTDBHeader header;
    header.magic[0] = 'T';
    header.magic[1] = 'T';
    header.magic[2] = 'D';
    header.magic[3] = 'B';
    header.version = 1;
    header.stepCount = 0;
    header.indexOffset = 0;
    if (!writeHeader(f, header))
    {
        return tdbgFail(f, index, "cannot write the header");
    }

    /* snapshot stream : remember where each snapshot starts, then write it */
    int32_t i = 0;
    for (TimelineNode* n = timeline.begin(); n != nullptr; n = n->next)
    {
        if (i >= stepCount)
        {
            return tdbgFail(f, index, "timeline has more snapshots than its step count");
        }
        long pos = ftell(f);
        if (pos < 0)
        {
            return tdbgFail(f, index, "cannot read the file position");
        }
        index[i] = static_cast<int64_t>(pos);
        if (!writeSnapshot(f, n->data))
        {
            return tdbgFail(f, index, "cannot write snapshot " + to_string(i));
        }
        i++;
    }
    if (i != stepCount)
    {
        return tdbgFail(f, index, "timeline has fewer snapshots than its step count");
    }

    /* dense index : the cursor is right after the last snapshot, so this is where it starts */
    long indexPos = ftell(f);
    if (indexPos < 0)
    {
        return tdbgFail(f, index, "cannot read the file position");
    }
    if (stepCount > 0 && fwrite(index, sizeof(int64_t), static_cast<size_t>(stepCount), f) != static_cast<size_t>(stepCount))
    {
        return tdbgFail(f, index, "cannot write the index");
    }

    /* go back and overwrite the placeholder with the real values */
    header.stepCount = stepCount;
    header.indexOffset = static_cast<int64_t>(indexPos);
    if (fseek(f, 0, SEEK_SET) != 0 || !writeHeader(f, header))
    {
        return tdbgFail(f, index, "cannot update the header");
    }

    delete[] index;
    fclose(f);
    return true;
}
// main section
int32_t main()
{
    if (!validateProgram("source.bin"))
    {
        cerr << "The Source file contains some error it did not pass the validation check\n";
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
    if (mainOffset < 0)
    {
        cerr << "An error occured while writing resolve.bin\n";
        return 2;
    }

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    bool written = writeTdbg(timeline, "session.tdbg");

    /* the Timeline does not own its snapshots, so we free them here, after serialization */
    for (TimelineNode* n = timeline.begin(); n != nullptr; n = n->next)
    {
        delete n->data;
    }

    if (!written)
    {
        return 3;
    }
    return 0;
}