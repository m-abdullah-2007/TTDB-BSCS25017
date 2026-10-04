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