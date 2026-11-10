#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

/*Constants*/
const int32_t IO_BUFFER_SIZE = 64 * 1024; // fixed buffer for streaming to/from disk
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64; /*used for call stack*/
const int32_t STATUS_OK = 0;
const int32_t STATUS_ERROR = 1;
const int32_t Adelier_Size = 4; /*checksum size*/

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

struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH]; /*additive one instance of a function call i.e one line*/
    int32_t stackDepth;               /*current size of stack*/
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
    char tail[Adelier_Size]; /*the held-back bytes (oldest first)*/
    int tailLen;             /*how many of them are filled 0-4 */

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
            if (tailLen < Adelier_Size)
            {
                /* still filling the window, nothing can be released yet*/
                tail[tailLen++] = in[i];
            }
            else
            {
                /*window is full: the oldest byte is definitely body*/
                out[released++] = tail[0];
                for (int k = 0; k < Adelier_Size - 1; k++)
                    tail[k] = tail[k + 1];
                tail[Adelier_Size - 1] = in[i];
            }
        }
        a32.update(out, released); // only released bytes go into the checksum
        return released;
    }

    bool verify()
    {
        if (tailLen < Adelier_Size)
            return false; /*stream shorter than a checksum*/
        uint32_t got = (uint32_t)(uint8_t)tail[0] | ((uint32_t)(uint8_t)tail[1] << 8) | ((uint32_t)(uint8_t)tail[2] << 16) | ((uint32_t)(uint8_t)tail[3] << 24);

        return got == a32.value();
    }
};

/*Implemented Socket Helpers*/

inline bool sendAll(int sock, const char *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n)
    {
        ssize_t r = send(sock, buf + sent, n - sent, MSG_NOSIGNAL);
        if (r <= 0)
            return false;
        sent += (size_t)r;
    }
    return true;
}

inline bool recvAll(int sock, char *buf, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        ssize_t r = recv(sock, buf + got, n - got, 0);
        if (r <= 0)
            return false;
        got += (size_t)r;
    }
    return true;
}

/*Used by the server and the load balancer. It creates a socket that sits at a port number and waits for others to connect.*/
inline int createListenSocket(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
    {
        perror("socket");
        exit(1);
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); /*INADDR_ANY: Accept connections on any IP.*/
    addr.sin_port = htons((uint16_t)port);    /*htons(): Converts port to network byte order.*/

    /*bind claims the port for this socket. It fails if another program already uses that port.*/
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(1);
    }
    /*listen switches the socket to “waiting for visitors” mode.*/
    if (listen(sock, 16) < 0)
    {
        perror("listen");
        exit(1);
    }
    return sock;
}

/*The client uses it to reach the load balancer, and the load balancer uses it to reach the server.*/
inline int connectToHost(const char *host, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
        return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    /*Converts the text address into the binary form the network uses.
    This -1 telss that the ip address is not valid*/
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        close(sock);
        return -1;
    }
    /*Connects. If nobody is listening, it fails, so close the socket and returns -1.
     This -1 is how the load balancer knows the server is down and needs starting.*/
    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -1;
    }
    /*On success, return the connected socket. */
    return sock;
}

/* Message senders */
/* Message format on every hop: [status: 1 byte][body][checksum: 4 bytes, little-endian] */
inline bool sendChecksum(int sock, const Adelier32 &c)
{
    uint32_t v = c.value();
    char bytes[Adelier_Size];
    for (int i = 0; i < Adelier_Size; i++)
        bytes[i] = (char)((v >> (8 * i)) & 0xFF); /*Splits CheckSum into four bytes*/
    return sendAll(sock, bytes, Adelier_Size);
}
/*send a short written message with proof attached used for error messages*/
inline bool sendTextMessage(int sock, uint8_t status, const string &text)
{
    Adelier32 c;
    char st = (char)status;
    c.update(&st, 1);
    c.update(text.data(), text.size());
    /*Sends the status byte. Stop if it fails.*/
    if (!sendAll(sock, &st, 1))
    {
        return false;
    }
    /*Sends the text itself*/
    if (!text.empty() && !sendAll(sock, text.data(), text.size()))
    {
        return false;
    }
    return sendChecksum(sock, c);
}
/*send a whole file, piece by piece, with proof attached used to send the source.bin (client) and the .tdbg file (server)*/
inline bool sendFileMessage(int sock, uint8_t status, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    Adelier32 c;
    char st = (char)status;
    c.update(&st, 1);
    /*Sends the status byte. Stop if it fails.*/
    if (!sendAll(sock, &st, 1))
    {
        fclose(f);
        return false;
    }
    vector<char> buf(IO_BUFFER_SIZE);
    size_t got;
    /*Sends the file itself*/
    while ((got = fread(buf.data(), 1, buf.size(), f)) > 0)
    {
        c.update(buf.data(), got);
        if (!sendAll(sock, buf.data(), got))
        {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return sendChecksum(sock, c);
}