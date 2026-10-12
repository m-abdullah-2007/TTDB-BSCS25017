### Linux Compatibility

The TTDB server was compiled and tested on Ubuntu using WSL with GCC 15.2.0 and C++17.

Compilation command:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic server.cpp -o ttdb
```

The program successfully executed a test case involving variable assignment, arithmetic, function calls, and copy-in/copy-out behavior. The generated `resolve.bin` and `session.tdbg` files were inspected to verify the expected record structure and TTDB header fields.