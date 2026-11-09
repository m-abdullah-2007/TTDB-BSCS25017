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
class Adelier32
{
    uint32_t a, b;
    static const uint32_t MOD = 65521;

public:
    Adelier32() : a(1), b(0) {}
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

class TrailerChecker
{
    Adelier32 a32;
    char tail[CRC_SIZE]; /*the held-back bytes (oldest first)*/
    int tailLen;         /*how many of them are filled 0-4 */

public:
    TrailerChecker() : tailLen(0) {}

    /* Takes n received bytes. Writes the bytes now confirmed as body into 'out'
        and returns how many. 'out' must have room for at least n bytes.
        The first byte ever released is the status byte.*/
    size_t feed(const char *in, size_t n, char *out)
    {
        size_t released = 0;
        for (size_t i = 0; i < n; i++)
        {
            if (tailLen < CRC_SIZE)
            {
                /* still filling the window, nothing can be released yet*/
                tail[tailLen++] = in[i];
            }
            else
            {
                /*window is full: the oldest byte is definitely body*/
                out[released++] = tail[0];
                for (int k = 0; k < CRC_SIZE - 1; k++)
                    tail[k] = tail[k + 1];
                tail[CRC_SIZE - 1] = in[i];
            }
        }
        a32.update(out, released); // only released bytes go into the checksum
        return released;
    }

    bool verify()
    {
        if (tailLen < CRC_SIZE)
            return false; /*stream shorter than a checksum*/
        uint32_t got = (uint32_t)(uint8_t)tail[0] | ((uint32_t)(uint8_t)tail[1] << 8) | ((uint32_t)(uint8_t)tail[2] << 16) | ((uint32_t)(uint8_t)tail[3] << 24);

        return got == a32.value();
    }
};