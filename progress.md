# Time-Travel Debugger - Phase 01 Progress

**Name:** Abdullah
**Roll Number:** BSCS25017

---

## Progress Log

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