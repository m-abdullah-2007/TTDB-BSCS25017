# Time-Travel Debugger - Phase 01 Progress

**Name:** Abdullah
**Roll Number:** BSCS25017

---

## Progress Log
## Phase 01

### Log 1
```
Date: 04 10 2026

Goal of this session:
Get a feel for the entire pipeline and then write the Stack<T> class that will contain the call stack.

What I did:
Reviewed the project document and server.cpp template, implemented Stack<T> as a singly linked list with required operations, added a destructor and disabled copying to prevent double-free errors, added overflow and underflow exception handling, implemented snapshot_into to copy frames top-to-bottom within maxLen, and included <stdexcept> for exception types.

Problems faced:
First implementation did not have a destructor; hence, memory leaks occurred; destructor was implemented.

Decisions / assumptions made:
Exceptions are used to report errors, and isEmpty() will be checked before popping, hence an exception implies a bug. Copying of the call stack does not occur, and therefore copying is off instead of deep copying.First implementation did not have a destructor; hence, memory leaks occurred; destructor was implemented.

```
---

### Log 2
```
Date: 04 10  2026 (MidNight)

Goal of this session:
Add the Timeline using Doubly Linked list with required operations and understood the need for forward declaration

What I did:
Added the timeline with all required operations and also added some extra things needed according to my assumptions

Problems faced:
First implementation did not have a destructor; hence, memory leaks occurred; destructor was implemented.Also I thought that the destructor should delete the snapshot but later on i understood that the relation of snapshot and timeline was aggregation so the timeline does not own it and hence should not delete it

Decisions / assumptions made:
Only one Instance of Debugger exists at a time, and therefore copying is off for timeline instead of deep copying.First implementation did not have a destructor; hence, memory leaks occurred; destructor was implemented

```
---

### Log 3
```
Date: 05 10  2026

Goal of this session:
Implement the helper functions used for reading source.bin, then validateProgram (Pass 0x0).

What I did:
Implemented readSourceLine to skip blank lines and trim whitespace and built firstWord and secondWord using common nextWord helper added validateProgram with funcOpen flag to detect nested unclosed or invalid function endings and report errors with line numbers

Problems faced:
Understood the difference between getline(in, line) and in.getline(buffer, size) and used the first one to read the line with any size. Also, handled Windows line endings by removing trailing \r in readSourceLine to make sure that func_end is located properly.

Decisions / assumptions made:
I have used one funcOpen flag as we cannot nest functions and line numbers are counted only on non-empty lines. This pass only checks that functions are properly opened and closed and reports the errors to cerr as there are no sockets yet. I have used size_t for storing positions in strings and fixed width integers for file values as later we have to write to .bin.

```
---

### Log 4
```
Date: 05 10 2026 (Evening)

Goal of this session:
Understand binary files properly and implement Pass 0x1 (Resolve): writeResolveRecord, readResolveRecord and resolveProgram.

What I did:
I studied files that had a fixed byte layout that the writing program understood. This made it possible to have fixed-size fields use fseek and do in-place updates. I understood how little-endian storage worked, how to read hexadecimal representation and how to handle ASCII characters that didn't have '\0' terminators. I wrote writeResolveRecord with handling for files tracking of offsets and checked fwrite operations. I also wrote readResolveRecord with checks, for EOF, negative or oversized lengths and truncated records. I created resolveProgram in two phases: first writing records while keeping track of functions and calls and then using fseek to update call offsets with the correct function offsets and returning the offset of main at last. I added funcFind to look up functions and resolveFail to handle errors close the file and return -1.

Problems faced:
Verified the file in the hex editor noting that an initially empty tab showed 00000000. When the correct file was opened again the hex editor displayed the 42 bytes. I learned that the hex view shows data, in lines of 16 bytes so a 22-byte record that spreads over two lines is still one piece of data. I changed the call-offset placeholder from -1 to 0 because the function readResolveRecord uses -1 to signal end‑of‑file or an error. I also discovered that the stream object ifstream closes automatically when ifstream goes out of scope but a FILE* needs a fclose. The function resolveFail takes care of closing the FILE* on every error path.

Decisions / assumptions made:
Errors return ‑1. Offset 0 is valid. Therefore main must check mainOffset < 0. Call text stays the same. Only the 8‑byte offset field of the call is patched. If the call text were changed its length would. All later records would move. Non‑call records keep their offset. Call records keep 0 until patched. Patching happens after the entire file is written. This lets call records point to function records that appear later. Duplicate function names are not allowed. Nameless func or call records are also rejected. The number of arguments in a call is not checked in this pass. Binary files open, with FILE* and flags "wb+" or "rb”. All stored fields use int64_t and int32_t types.
```
---

### Log 5
```
Date: 07 10 2026 (Afternoon)

Goal of this session:
Implement Pass 0x2 (Execution), including tokenizeLine, buildSnapshot, execution helpers, and executeProgram.

What I did:
Implemented tokenizeLine, buildSnapshot, execution helpers, executeProgram, and runProgram, including tokenization, snapshot creation, number parsing, variable lookup, operand evaluation, error handling, arithmetic operations, function calls, returns, and per-line snapshots.

Problems faced:
Worked out copy-in/copy-out for function calls, understood getValue for handling literals and variables, clarified error reporting and naming, and reviewed the fseek and main-header validation logic.

Decisions / assumptions made:
The execution records one snapshot per executed line, with func main as step 0 and call snapshots showing the new frame. returnLine stores the call record’s byte position, arguments are copied in at call and back at func_end, and literals are not copied back. set creates new locals, operands may be literals or variables, and arithmetic destinations must already exist. Runtime errors stop execution while preserving and serializing the valid partial timeline. main must be exactly func main with no parameters.
```
---

### Log 6
```
Date: 08 10 2026

Goal of this session:
Implement Pass 0x3 (Serialization), wire up main(), and test the whole pipeline end to end on a sample C-- program with nested calls.

What I did:
Designed the .tdbg format, implemented snapshot serialization and helper functions, completed writeHeader and writeTdbg with indexing and header patching, updated main() error codes and memory cleanup, and created an end-to-end nested-call sample program.

Problems faced:
Understood indexOffset as the byte position where the index begins, which is patched after writing all variable-sized snapshots. The index enables direct access to any snapshot, structs must be serialized field-by-field, and hex viewers may display bytes differently even when the underlying data is identical.

Decisions / assumptions made:
The .tdbg format uses version 1, little-endian values, and top-first frame ordering. Only used slots are serialized, requiring a dense index. writeHeader and writeTdbg return bool for error handling, while runtime errors preserve and serialize the partial timeline and still return 0. Errors use cerr, main() frees snapshots after serialization, and source.bin is read from the program’s working directory.
```
---

### Log 7
```
Date: 12 10 2026

Goal of this session:
Test the TTDB pipeline end to end on Linux using WSL, verify the generated binary files, and improve code readability and maintainability.

What I did:
Compiled and tested the complete pipeline on Ubuntu using WSL, GCC 15.2.0, and C++17 with a sample C-- program involving assignments, arithmetic, and nested calls. Verified successful execution and inspected resolve.bin and session.tdbg.

Problems faced:
Understood indexOffset, snapshot indexing, and field-by-field struct serialization. Learned Linux compilation using GCC with C++17 and how hex viewers may display identical binary data differently.

Decisions / assumptions made:
Implemented .tdbg version 1 serialization using little-endian values, top-first frame ordering, and a dense index for used slots. Added error handling through writeHeader and writeTdbg, preserved partial timelines after runtime errors, and ensured snapshot cleanup after serialization. Confirmed that source.bin is read from the working directory and errors are reported using cerr.
```
---

## Phase 02
### Log 1
```
Date: 08 11 2026

Goal of this session:
Implement Queue and start the understanding and thinking part opf project phase 02

What I did:
Implemented the queue and decided to use the crc method for checksum and also tried to understand project structure

Problems faced:
Not much faced

Decisions / assumptions made:
we can use our choice of checksum method

```
---
### Log 2
```
Date: 09 11 2026

Goal of this session:
Updated helper.h with frequently used constants and structs 

What I did:
Added structs like variable,frame,ttdbHeader etc to helper.h

Problems faced:
decided to not add snapshot as it has been forward declared at a point in server.cpp

Decisions / assumptions made:
Not much made

```
---
### Log 3
```
Date: 09 11 2026

Goal of this session:
choose a checksum method.

What I did:
Chose Adler-32 as the checksum (incremental, 4-byte result)

Problems faced:
Not Much Faced

Decisions / assumptions made:
Checksum trailer is 4 bytes, little-endian, and covers the status byte too.

```
---
### Log 4
```
Date: 09 11 2026 -> midnight

Goal of this session:
Implement TrailerChecker

What I did:
The tail array maintains the last 4 bytes as a potential Adler-32 trailer. As new bytes arrive, older bytes are released as body data and fed into the checksum. At the end, the remaining 4 bytes are treated as the trailer and compared against the computed CRC-32 in little-endian order.

Problems faced:
Not Much Faced

Decisions / assumptions made:
Not Much Assumed
```
---
### Log 5
```
Date: 10 11 2026

Goal of this session:
Finish helper.h so the server, load balancer and client can share one set of network and checksum code.

What I did:
- Implemented sendAll and recvAll (loop until all bytes are sent/received).
- Implemented createListenSocket and connectToHost (shared by server, LB, client).
- Implemented sendChecksum (4 bytes, lowest byte first).
- Implemented sendTextMessage and sendFileMessage ([status][body][checksum]).

Problems faced:
- Unsure how TrailerChecker handles chunks smaller than 4 bytes.Solved: the 4-byte window just keeps filling across chunks.
- Default host in templates was 127.0.0.1 changed to 127.0.0.0.

Decisions / assumptions made:
- sendFileMessage never calls shutdown; the caller decides.
- All helper functions are marked inline to avoid multiple-definition errors.
```
---
### Log 6
```
Date: 10 11 2026  (Early Morning)

Goal of this session:
Build the server's network layer so it can receive a source file over a socket,run the Phase 1 passes, and reply with a .tdbg file or an error message.

What I did:
- Changed validateProgram and resolveProgram to report the reason for failure  through a string &err (instead of printing to cerr).
- Replaced new[] arrays in resolveProgram with vectors to fix memory leaks.
- Changed executeProgram and writeTdbg to return bool, with errors stored in err.
- Wrote processSource(): runs validate -> resolve -> execute -> writeTdbg and  frees every Snapshot afterwards (Timeline does not own them).
- Implemented receiveUpload(): reads [status][body][checksum] through  TrailerChecker, skips the status byte, writes only the body to source.bin, rejects files over MAX_SOURCE_BYTES, and verifies the checksum.
- Applied the SO_RCVTIMEO timeout (SOCKET_TIMEOUT_SEC) so a silent client cannot hang the server.
- Implemented serveClient() and a new main(argc, argv) that takes the port, listens, and serves one client at a time.
- Tested: valid program, program with no main, and a corrupted upload using a temprarily written test_client.cpp.

Problems faced:
- Server kept exiting immediately and tests said "cannot connect".
- Project folder is nested (TTDB-BSCS25017/TTDB-BSCS25017), so the testing  files were in a different folder from the executables.Solved: used relative paths .

Decisions / assumptions made:
- A runtime error sends an error message to the client instead of a partial .tdbg file, which keeps the client logic simple.
- Server handles one client at a time, since the load balancer only sends one.
- Snapshot layout on disk is field by field: strings as length + characters, and only the used argv/locals entries are written. The client readers must match this exactly.
- Stack overflow (max depth 64) is caught and reported as an error.

```
---
### Log 7
```
Date: 10 11 2026 (Evening)

Goal of this session:
Build the load balancer so clients can connect to it, wait in a queue, and be served one at a time, with the Load Balancer starting the server automatically if it is down.

What I did:
- load_balancer.cpp with a full implementation that uses the shared helpers from helper.h 
- Implemented startServer() using system("./server <port> &") and connectToServerWithWakeup() (try once, start the server, then retry up to  50 times at 100 ms intervals).
- Implemented relayVerified(): copies one message from one socket to another, checks the checksum on the way, and sends the trailer only if the check passed.
- Implemented relayOneClient(): connect to server (with wakeup), relay the upload, half-close the server socket, relay the reply back.
- Implemented acceptLoop() (detached thread): accepts clients, locks the queue mutex, enqueues up to 8, and replies "server busy, try again later" to the  rest.
- Implemented dispatcherLoop() (main thread): dequeues one client at a time, drops clients that waited more than 30 seconds, and closes the client socket in one place only.
- Added SIGINT handling with sigaction so Ctrl+C sets the flag and the loops exit cleanly for proper terminal handling.
- Added receive timeouts so a silent client or server cannot hang the dispatcher.

Problems faced:
- Template defaults used 127.0.0.0, which is not the loopback address.
  Solved: changed to 127.0.0.1.
- Template functions sendAll, createListenSocket and connectToServer clashed
  with the versions in helper.h (redefinition).
  Solved: removed the LB copies and call the shared ones.
- startServer runs ./server relative to the current folder.
  Solved: run the LB from the folder that contains the server executable better for only executable use.

Decisions / assumptions made:
- Reply messages on every hop use [status][body][checksum]; the LB verifies each direction and withholds the trailer on failure so the next receiver rejects the message.
- Only the dispatcher closes a queued client's socket; acceptLoop closes only the clients it refuses.
- Queue is not thread-safe by itself; every size/enqueue/dequeue check is done inside queueMutex.
- The dispatcher sleeps 20 ms when the queue is empty to avoid busy-spinning.
```
---
### Log 8
```
Date: 10 11 2026 (Afternoon)

Goal of this session:
Build the client so the full chain works: client -> load balancer -> server,then navigate the .tdbg file locally with next / prv / jmp.

What I did:
- Removed constants, structs and socket helpers from client.cpp that now live  in helper.h 
- Part A: uploadSource (sendFileMessage + shutdown), receiveResponse  (TrailerChecker, status byte, file or error text, checksum verify), requestDebugFile (checks path, connects, uploads, reads reply).
- Part B: readString/readVariable/readFrame/readSnapshotBody that mirror the server's field-by-field writers; openTdbg validates magic, stepCount, index position and offsets; readSnapshotAt seeks through the index; closeTdbg.
- Part C/D: initSession, closeSession, showCurrent, moveBy (clamped), cmdStart (downloads to a temp file and swaps only after it opens), cmdNext, cmdPrv, cmdJmpNext, cmdJmpPrv.
- Part E: printSnapshot, printHelp .

Problems faced:
- Saving the new download over the current .tdbg would destroy the open old session if the download failed.
  Solved: download to ttdb_new.tdbg, open it, then rename over the old file.
- Snapshot is very large, so it must be allocated on the heap.
- Template default host was 127.0.0.0.
  Solved: changed to 127.0.0.1.

Decisions / assumptions made:
- Steps are shown 1-based; internally 0-based.
- jmp accepts positive numbers up to 1,000,000,000 and clamps at the ends.
- Call stack prints current function first (the order the server writes it).
- A failed start never changes the old session.
- TdbgReader holds a FILE* instead of ssize_t file_index.
```
---
### Log 9
```
Date: 10 11 2026  (Midnight before submission)

Goal of this session:
Run the full test suite on client -> load balancer -> server, and fix the one weakness it exposed: a silent(idle) client could hold the single dispatcher thread for up to 30 s.

What I did:
- Diagnosed it: the dispatcher blocks in recv() on the silent client until RELAY_RECV_TIMEOUT_SEC (30 s) expires, but the test kills the real client after 20 s.
- Short-term: raised the test timeout to 45 s, which passed but did not fix the weakness.
- Real fix in load_balancer.cpp: replaced RELAY_RECV_TIMEOUT_SEC with CLIENT_RECV_TIMEOUT_SEC = 5 and SERVER_RECV_TIMEOUT_SEC = 30, and applied them in relayOneClient on the client socket and the server socket.
- Capture errno right after relayVerified() fails, so a timeout replies "upload timed out" instead of "upload checksum mismatch".
- Set test 12 back to timeout 20, so it now proves the fix. Final result: 48 passed, 0 failed.
- Reviewed client.cpp and server.cpp for timeout conflicts: client waits up to 40 s for the reply, server has a 5 s SO_RCVTIMEO on the upload.

Problems faced:
- Idle client blocked the dispatcher for 30 s, longer than the test's 20 s window. Solved: 5 s receive timeout on the client side of the LB.
- shutdown() and close() can overwrite errno before it is checked. Solved: save errno in a local variable immediately after relayVerified().

Decisions / assumptions made:
- The timeout applies per recv() call, not to the whole upload, so a client actively sending a large file is never cut off.
- The server-side timeout stays at 30 s because processing can legitimately take longer than an idle upload.
- Queue-wait limit (30 s, arrivedAt check) and the non-blocking drain for "server busy" were already correct and unchanged.
- Known limit: a client sending one byte every few seconds could still occupy the dispatcher. A total upload deadline would fix it but is beyond the spec.
---