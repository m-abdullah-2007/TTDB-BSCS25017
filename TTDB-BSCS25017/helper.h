#pragma once
#include <cstdint>
#include <string>

/*Constants*/
const int32_t IO_BUFFER_SIZE = 64 * 1024; // fixed buffer for streaming to/from disk
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64; /*used for call stack*/
const int32_t STATUS_OK = 0;
const int32_t STATUS_ERROR = 1;
const int32_t CRC_SIZE = 4; /*checksum size*/

// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files.

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
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;                        /*number of parameters*/
    Variable argv[MAX_VARS_PER_FRAME];   /*array of parameters*/
    int32_t returnLine;                  /*offset of line from which function is returned*/
    Variable locals[MAX_VARS_PER_FRAME]; /*array of variables created inside function*/
    int32_t localCount;                  /*count of local variables*/
};
struct TTDBHeader /*placeholder struct for header*/
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};

/*checksum computing and checking class*/
class CRC32
{
    uint32_t a, b;
    static const uint32_t MOD = 65521;

public:
    CRC32() : a(1), b(0) {}
    void update(const char *buf, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            a = (a + (uint8_t)buf[i]) % MOD;
            b = (b + a) % MOD;
        }
    }
    uint32_t value() const
    {
        return (b << 16) | a;
    }
    void reset()
    {
        a = 1;
        b = 0;
    }
};