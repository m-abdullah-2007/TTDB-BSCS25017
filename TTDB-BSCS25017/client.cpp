// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include "helper.h"
#include <sstream>
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
const char *TDBG_FILE = "ttdb_last.tdbg"; // the session file kept on disk
const char *TDBG_TEMP = "ttdb_new.tdbg";  // new download lands here first
const int MAX_JUMP = 1000000000;
const int REPLY_TIMEOUT_SEC = 40;       // longer than the LB's 30 s queue limit
const int32_t MAX_STRING_LEN = 1 << 20; // sanity limit when reading strings
const int64_t HEADER_SIZE = 20;         // magic 4 + version 4 + stepCount 4 + indexOffset 8
string LB_HOST = "127.0.0.1";
int LB_PORT = 5000;

// ---- Structs only the client needs ----
struct TdbgReader
{
    FILE *file; // stays open for the whole session
    TTDBHeader header;
    vector<int64_t> index; // index[n] = byte position of snapshot n
};
struct DebugSession
{
    int active_flag; // 0 until a "start" has worked
    TdbgReader reader;
    int64_t currentStep; // 0-based
};
// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================

bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.
    bool ok = sendFileMessage(sock, STATUS_OK, sourcePath); // [0][file][checksum]
    shutdown(sock, SHUT_WR);                                // "upload finished"
    return ok;
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][Adelier-32]. The Adelier covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.
    timeval tv{};
    tv.tv_sec = REPLY_TIMEOUT_SEC;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    TrailerChecker chk;
    vector<char> in(IO_BUFFER_SIZE), out(IO_BUFFER_SIZE);
    bool gotStatus = false;
    uint8_t status = 0;
    FILE *f = nullptr;
    bool writeFailed = false;
    string errText;
    ssize_t r;

    while ((r = recv(sock, in.data(), in.size(), 0)) > 0)
    {
        size_t m = chk.feed(in.data(), (size_t)r, out.data());
        size_t start = 0;
        if (!gotStatus && m > 0)
        {
            gotStatus = true;
            status = (uint8_t)out[0];
            start = 1;
            if (status == STATUS_OK)
            {
                f = fopen(outTdbgPath, "wb");
                if (!f)
                    writeFailed = true;
            }
        }
        if (m > start)
        {
            if (status == STATUS_OK)
            {
                if (f && fwrite(out.data() + start, 1, m - start, f) != m - start)
                    writeFailed = true;
            }
            else
                errText.append(out.data() + start, m - start);
        }
    }
    if (f)
        fclose(f);

    if (r < 0)
    {
        remove(outTdbgPath);
        outError = "connection error or timeout while waiting for the reply";
        return false;
    }
    if (!gotStatus)
    {
        outError = "no reply from the server (connection closed, it may have waited too long in the queue)";
        return false;
    }
    if (!chk.verify() || status > STATUS_ERROR)
    {
        remove(outTdbgPath);
        outError = "checksum mismatch, response corrupted";
        return false;
    }
    if (status == STATUS_ERROR)
    {
        outError = errText;
        return false;
    }
    if (writeFailed)
    {
        remove(outTdbgPath);
        outError = "could not write the .tdbg file to disk";
        return false;
    }
    return true;
}

bool requestDebugFile(const char *host, int port, const string &sourcePath, const char *outTdbgPath, string &outError)
{
    FILE *test = fopen(sourcePath.c_str(), "rb"); // fail early on a wrong path
    if (!test)
    {
        outError = "cannot open source file '" + sourcePath + "'";
        return false;
    }
    fclose(test);

    int sock = connectToHost(host, port);
    if (sock < 0)
    {
        outError = "cannot connect to the load balancer";
        return false;
    }
    uploadSource(sock, sourcePath.c_str());
    // even if the upload broke, the LB may have sent a reason ("server busy"), so still read
    bool ok = receiveResponse(sock, outTdbgPath, outError);
    close(sock);
    return ok;
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================

static bool readInt32(FILE *f, int32_t &v)
{
    return fread(&v, sizeof(int32_t), 1, f) == 1;
}
bool readString(FILE *f, string &out)
{
    int32_t len;
    if (!readInt32(f, len) || len < 0 || len > MAX_STRING_LEN)
        return false;
    out.resize((size_t)len);
    if (len > 0 && fread(&out[0], 1, (size_t)len, f) != (size_t)len)
        return false;
    return true;
}
bool readVariable(FILE *f, Variable &v)
{
    return readString(f, v.name) && readInt32(f, v.value);
}
bool readFrame(FILE *f, Frame &fr)
{
    if (!readString(f, fr.func_name))
        return false;
    if (!readInt32(f, fr.argc) || fr.argc < 0 || fr.argc > MAX_VARS_PER_FRAME)
        return false;
    for (int32_t i = 0; i < fr.argc; i++)
        if (!readVariable(f, fr.argv[i]))
            return false;
    if (!readInt32(f, fr.returnLine))
        return false;
    if (!readInt32(f, fr.localCount) || fr.localCount < 0 || fr.localCount > MAX_VARS_PER_FRAME)
        return false;
    for (int32_t i = 0; i < fr.localCount; i++)
        if (!readVariable(f, fr.locals[i]))
            return false;
    return true;
}
bool readSnapshotBody(FILE *f, Snapshot &s)
{
    if (!readInt32(f, s.stackDepth) || s.stackDepth < 0 || s.stackDepth > MAX_STACK_DEPTH)
        return false;
    for (int32_t i = 0; i < s.stackDepth; i++)
        if (!readFrame(f, s.callStack[i]))
            return false;
    return true;
}

bool openTdbg(const char *path, TdbgReader &r)
{
    r.file = nullptr;
    r.index.clear();
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    TTDBHeader h{};
    bool good = false;
    do
    {
        if (fread(h.magic, 1, 4, f) != 4 || memcmp(h.magic, "TTDB", 4) != 0)
            break;
        if (!readInt32(f, h.version) || !readInt32(f, h.stepCount))
            break;
        if (fread(&h.indexOffset, sizeof(int64_t), 1, f) != 1)
            break;
        if (h.stepCount <= 0)
            break;
        if (fseek(f, 0, SEEK_END) != 0)
            break;
        int64_t size = (int64_t)ftell(f);
        // the index must sit exactly at the end of the file
        if (h.indexOffset < HEADER_SIZE || h.indexOffset + (int64_t)h.stepCount * 8 != size)
            break;
        if (fseek(f, (long)h.indexOffset, SEEK_SET) != 0)
            break;
        r.index.resize((size_t)h.stepCount);
        if (fread(r.index.data(), sizeof(int64_t), (size_t)h.stepCount, f) != (size_t)h.stepCount)
            break;
        bool offsetsOk = true;
        for (int64_t off : r.index)
            if (off < HEADER_SIZE || off >= h.indexOffset)
                offsetsOk = false;
        if (!offsetsOk)
            break;
        good = true;
    } while (false);

    if (!good)
    {
        fclose(f);
        r.index.clear();
        return false;
    }
    r.file = f;
    r.header = h;
    return true;
}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    if (r.file)
    {
        fclose(r.file);
    }
    r.file = nullptr;
    vector<int64_t>().swap(r.index); // free the memory
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
    if (!r.file || step < 0 || step >= (int32_t)r.index.size())
        return nullptr;
    if (fseek(r.file, (long)r.index[step], SEEK_SET) != 0)
        return nullptr;
    Snapshot *s = new Snapshot(); // far too big for the stack
    if (!readSnapshotBody(r.file, *s))
    {
        delete s;
        return nullptr;
    }
    return s;
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
    s.active_flag = 0;
    s.reader.file = nullptr;
    s.currentStep = 0;
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
    if (s.active_flag)
    {
        closeTdbg(s.reader);
    }
    s.active_flag = 0;
}

// Part E declared Early for printing
static void printVars(const char *label, const Variable *v, int32_t n)
{
    cout << "      " << label << ": ";
    if (n == 0)
        cout << "(none)";
    for (int32_t i = 0; i < n; i++)
        cout << (i ? ", " : "") << v[i].name << "=" << v[i].value;
    cout << "\n";
}
void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
    cout << "\n===== Step " << step << " / " << totalSteps << " =====\n";
    if (s.stackDepth == 0)
    {
        cout << "(empty call stack)\n";
        return;
    }
    cout << "Call stack (current function first):\n";
    for (int32_t i = 0; i < s.stackDepth; i++)
    {
        const Frame &fr = s.callStack[i];
        cout << "  #" << i << " " << fr.func_name << "\n";
        printVars("args  ", fr.argv, fr.argc);
        printVars("locals", fr.locals, fr.localCount);
    }
}
void printHelp()
{
    cout << "Commands:\n"
         << "  start <file.trace>     send the program to the server and show step 1\n"
         << "  next                   go one step forward\n"
         << "  prv                    go one step back\n"
         << "  jmp next <n>           go n steps forward\n"
         << "  jmp prv <n>            go n steps back\n"
         << "  help                   show this text\n"
         << "  quit | exit            leave\n";
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
    Snapshot *snap = readSnapshotAt(s.reader, (int32_t)s.currentStep);
    if (!snap)
    {
        cout << "damaged .tdbg file\n";
        return false;
    }
    printSnapshot((int32_t)s.currentStep + 1, (int32_t)s.reader.index.size(), *snap);
    delete snap;
    return true;
}
bool moveBy(DebugSession &s, int32_t delta)
{
    if (!s.active_flag)
    {
        cout << "no session yet, use: start <file.trace>\n";
        return false;
    }
    int64_t last = (int64_t)s.reader.index.size() - 1;
    int64_t target = s.currentStep + delta;
    if (target < 0)
        target = 0;
    if (target > last)
        target = last;
    if (target == s.currentStep && delta != 0)
        cout << (delta > 0 ? "already at the last step\n" : "already at the first step\n");
    s.currentStep = target;
    return showCurrent(s);
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
    string err;
    // download to a temp file so the old session stays intact if anything fails
    if (!requestDebugFile(LB_HOST.c_str(), LB_PORT, sourcePath, TDBG_TEMP, err))
    {
        cout << "Error: " << err << "\n";
        remove(TDBG_TEMP);
        return false;
    }
    TdbgReader fresh;
    fresh.file = nullptr;
    if (!openTdbg(TDBG_TEMP, fresh))
    {
        cout << "server sent a damaged .tdbg file\n";
        remove(TDBG_TEMP);
        return false;
    }
    if (s.active_flag)
        closeTdbg(s.reader);
    rename(TDBG_TEMP, TDBG_FILE); // replaces the old file
    s.reader = fresh;
    s.currentStep = 0;
    s.active_flag = 1;
    return showCurrent(s);
}
bool cmdNext(DebugSession &s) { return moveBy(s, 1); }
bool cmdPrv(DebugSession &s) { return moveBy(s, -1); }
bool cmdJmpNext(DebugSession &s, int32_t n) { return moveBy(s, n); }
bool cmdJmpPrv(DebugSession &s, int32_t n) { return moveBy(s, -(int64_t)n); }

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

static bool parsePositive(const string &t, int32_t &out)
{
    if (t.empty() || t.size() > 10)
        return false;
    long long v = 0;
    for (char c : t)
    {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (c - '0');
    }
    if (v < 1 || v > MAX_JUMP)
        return false;
    out = (int32_t)v;
    return true;
}

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.

    vector<string> w;
    istringstream iss(line);
    string tok;
    while (iss >> tok)
        w.push_back(tok);
    if (!w.empty() && w[0] == PROGRAM_NAME)
        w.erase(w.begin());
    if (w.empty())
        return true; // blank line: ignore

    if (w[0] == "start" && w.size() == 2)
        return cmdStart(s, w[1]);
    if (w[0] == "next" && w.size() == 1)
        return cmdNext(s);
    if (w[0] == "prv" && w.size() == 1)
        return cmdPrv(s);
    if (w[0] == "jmp" && w.size() == 3 && (w[1] == "next" || w[1] == "prv"))
    {
        int32_t n;
        if (parsePositive(w[2], n))
            return w[1] == "next" ? cmdJmpNext(s, n) : cmdJmpPrv(s, n);
    }
    if (w[0] == "help" && w.size() == 1)
    {
        printHelp();
        return true;
    }
    if ((w[0] == "quit" || w[0] == "exit") && w.size() == 1)
    {
        quitRequested = true;
        return true;
    }
    cout << "unknown command\n";
    printHelp();
    return false;
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function.

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.

    if (argc >= 2)
        LB_HOST = argv[1];
    if (argc >= 3)
        LB_PORT = atoi(argv[2]);
    signal(SIGPIPE, SIG_IGN);

    DebugSession session;
    initSession(session);
    cout << "Time-Travel Debugger (" << PROGRAM_NAME << ") talking to "
         << LB_HOST << ":" << LB_PORT << "\n";
    printHelp();

    bool quitRequested = false;
    string line;
    while (!quitRequested)
    {
        cout << PROMPT << flush;
        if (!getline(cin, line))
            break; // end of input
        runCommand(session, line, quitRequested);
    }
    closeSession(session);
    return 0;
}
