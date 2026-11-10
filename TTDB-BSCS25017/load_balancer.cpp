// ======================= TIME-TRAVEL DEBUGGER - LOAD BALANCER TEMPLATE =======================

// What load balancer does:
//   1. Many clients can connect at the same time. Each one sends one string (the source code).
//   2. Clients wait in a queue (first come, first served).
//   3. For each client, in order: make sure the server is running (start it with system() if not),
//      pass the string to the server, pass the server's reply back to the client.
//
// It does not read or understand the messages. It only moves bytes.
// The only messages it writes itself are errors ("server busy", "server failed to start").
//
// Two threads:
//   acceptor thread (acceptLoop) -> takes new clients and puts them in the queue. It never waits for the server.
//   main thread (dispatcherLoop) -> takes ONE client out of the queue and serves it completely, then the next.
//
// How the server is started:
//   system("./server 5001 &");
//   The "&" is required. Without it, system() waits until the server exits and the load balancer freezes.
//
//
// What the server program must do (the load balancer depends on this):
//   - it takes its port as the first argument, for example: ./server 5001
//   - it reads the string until the connection's write side is closed (recv returns 0)
//   - it sends its reply as [status: 1 byte][payload], then closes the connection

#include <iostream>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <signal.h>
/*included by me*/
#include "helper.h"
#include "stdexcept"
using namespace std;

// ---- Constants ----
const char *SERVER_PATH = "./server";     // the program we start when the server is down
const int LISTEN_BACKLOG = 16;            // connections the OS holds until accept() takes them
const int MAX_PENDING_CLIENTS = 8;        // clients allowed to wait in our queue. More than this get "server busy".
const int STARTUP_MAX_ATTEMPTS = 50;      // after starting the server, how many times we try to connect
const int STARTUP_POLL_INTERVAL_MS = 100; // wait between those tries (50 x 100 ms = 5 seconds in total)
const int CLIENT_RECV_TIMEOUT_SEC = 5;    // silent client can't hog the dispatcher
const int SERVER_RECV_TIMEOUT_SEC = 30;   // server may legitimately take longer
const int CLIENT_WAIT_TIMEOUT_SEC = 30;   // clients waiting longer are dropped silently
int lbPort = 5000;
string serverHost = "127.0.0.1";
int serverPort = 5001;

// ---- Queue: here it holds client sockets (int). ----
// Implement it yourself, do not use std::queue.
template <typename T>
class Queue
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *front, *rear;
    int count;

public:
    // Constructor
    Queue() : front(nullptr), rear(nullptr), count(0) {}
    // avoids memory leak in case of shallow copying
    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;
    // Destructor
    ~Queue()
    {
        while (front != nullptr)
        {
            Node *temp = front;
            front = front->next;
            delete temp;
        }
    }
    void enqueue(const T &val)
    {
        Node *temp = new Node{val, nullptr};
        if (rear == nullptr)
            front = rear = temp;
        else
        {
            rear->next = temp;
            rear = temp;
        }
        count++;
    }

    T dequeue()
    {
        if (front == nullptr)
            throw underflow_error("Queue is empty");
        Node *temp = front;
        T val = temp->data;
        front = front->next;
        if (front == nullptr)
            rear = nullptr;
        delete temp;
        count--;
        return val;
    }

    bool isEmpty() const { return front == nullptr; }
    int size() const { return count; }
};

struct ClientInfo
{
    int sock;         // the connected socket
    string address;   // the client's IP as text, e.g. "127.0.0.1"
    int port;         // the client's port
    time_t arrivedAt; // when accept() returned
};

// The queue is used by two threads, so every use of it must be inside queueMutex.
Queue<ClientInfo> pendingClients;
mutex queueMutex;
sig_atomic_t flag = 1; /*An integral type that can be modified atomically, without thepossibility of a signal arriving in the middle of the operation*/

// ---- Basic socket helpers ----
bool sendError(int sock, const string &message)
{
    // Reply format: [status: 1 byte][payload until close][checksum: 4 bytes].
    // For an error: send one byte with value 1, then the message text.

    return sendTextMessage(sock, STATUS_ERROR, message);
}

int connectToServer(const char *host, int port) /*for Load balancer server acts as host*/
{
    return connectToHost(host, port);
}

void setRecvTimeout(int sock, int seconds)
{
    timeval tv{};
    tv.tv_sec = seconds;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// =============================================================================================
// PART 1: WAKING UP THE SERVER IF IT IS DOWN
// =============================================================================================

bool startServer(int port)
{
    // Build the command text: SERVER_PATH + " " + port as text + "&"
    //   Example: "./server 5001 &"
    // Call system() with it.
    // Return true if system() returned 0, otherwise false.
    string cmd = string(SERVER_PATH) + " " + to_string(port) + " &"; // "&" is required
    return system(cmd.c_str()) == 0;
}
int connectToServerWithWakeup()
{
    // 1. Try connectToServer(). If it works, return that socket. The server is alive.
    // 2. Otherwise call startServer(). If it fails, return -1.
    // 3. Try connectToServer() up to STARTUP_MAX_ATTEMPTS times
    // 4. Return the first working socket, or -1 if the server never came up.
    int s = connectToServer(serverHost.c_str(), serverPort);
    if (s >= 0)
        return s; // returned connected socket

    if (!startServer(serverPort))
        return -1;

    for (int i = 0; i < STARTUP_MAX_ATTEMPTS; i++) /*try starting the server*/
    {
        usleep(STARTUP_POLL_INTERVAL_MS * 1000);
        s = connectToServer(serverHost.c_str(), serverPort);
        if (s >= 0)
            return s;
    }
    return -1;
}

// =============================================================================================
// PART 2: PASSING THE STRING AND THE REPLY (one client, called by the main thread only)
// =============================================================================================

/*Helper: Copies one whole message from 'from' to 'to' while checking its checksum.
  The trailer is sent ONLY if the check passed, so the next receiver rejects bad data. */
bool relayVerified(int from, int to)
{
    TrailerChecker chk;
    Adelier32 sum; /* checksum of what we forward */
    vector<char> in(IO_BUFFER_SIZE), out(IO_BUFFER_SIZE);
    ssize_t r;

    while ((r = recv(from, in.data(), in.size(), 0)) > 0)
    {
        size_t m = chk.feed(in.data(), (size_t)r, out.data());
        if (m > 0)
        {
            sum.update(out.data(), m);
            if (!sendAll(to, out.data(), m))
                return false;
        }
    }
    if (r < 0)
        return false; /* error or timeout */
    if (!chk.verify())
        return false; /* damaged: do not send the trailer */
    return sendChecksum(to, sum);
}

void relayOneClient(ClientInfo &c)
{
    // 1. serverSock = connectToServerWithWakeup().
    //    If it is -1: sendError "backend server failed to start", close clientSock, return.
    // 3. Client -> server: repeat recv() from clientSock into a buffer of IO_BUFFER_SIZE and sendAll() it to serverSock.
    //    Stop when recv() returns 0 (the client finished sending) or fails.
    // 4. Call shutdown(serverSock, SHUT_WR), so the server knows the string is complete.
    // 5. Server -> client: repeat recv() from serverSock and sendAll() to clientSock.
    //    Stop when recv() returns 0 (the server sent its whole reply and closed) or fails.
    // 6. Close both sockets.
    setRecvTimeout(c.sock, CLIENT_RECV_TIMEOUT_SEC);

    int serverSock = connectToServerWithWakeup();
    if (serverSock < 0)
    {
        sendError(c.sock, "backend server failed to start");
        return;
    }
    setRecvTimeout(serverSock, SERVER_RECV_TIMEOUT_SEC);

    // client -> server
    errno = 0;
    bool ok = relayVerified(c.sock, serverSock);
    int relayErr = errno; // capture before shutdown/close can change it

    if (!ok)
    {
        shutdown(serverSock, SHUT_WR); // trailer was withheld; server will reject it too
        close(serverSock);
        sendError(c.sock, (relayErr == EAGAIN || relayErr == EWOULDBLOCK) ? "upload timed out" : "upload checksum mismatch");
        return;
    }
    shutdown(serverSock, SHUT_WR); // tells the server the upload is complete

    // server -> client
    relayVerified(serverSock, c.sock); // on failure the trailer is withheld; client reports it
    close(serverSock);
}

// =============================================================================================
// PART 3: THE TWO THREADS
// =============================================================================================

void acceptLoop(int listenSock)
{
    // Repeat: accept() a new client.
    //  If pendingClients.size() is already MAX_PENDING_CLIENTS: unlock, sendError "server busy, try again later",
    // else
    // Lock queueMutex.
    //   close the client, and go on.
    //   Otherwise enqueue the client socket.
    // Never wait for the server here, so new clients are always accepted quickly.
    // If accept() fails, print the error and keep going (do not end the thread).

    while (flag)
    {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int s = accept4(listenSock, (sockaddr *)&addr, &len, SOCK_CLOEXEC);
        if (s < 0)
        {
            if (errno != EINTR)
                perror("accept");
            continue;
        }

        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        ClientInfo ci{s, ip, (int)ntohs(addr.sin_port), time(nullptr)};

        bool full = false;
        {
            lock_guard<mutex> lock(queueMutex);
            if (pendingClients.size() >= MAX_PENDING_CLIENTS)
                full = true;
            else
                pendingClients.enqueue(ci);
        }

        if (full) // handled outside the lock
        {
            sendError(s, "server busy, try again later");
            shutdown(s, SHUT_WR);
            char tmp[4096];
            while (recv(s, tmp, sizeof(tmp), MSG_DONTWAIT) > 0)
            {
            } // drain so close() doesn't reset the reply
            close(s);
        }
    }
}
void dispatcherLoop()
{
    // This runs in the main thread and never ends.
    // Repeat:
    //   Lock queueMutex, take ONE client out of the queue if there is one, unlock.
    //   If we got a client: call relayOneClient(). The next client is taken out of the queue only after this one is finished.

    while (flag)
    {
        ClientInfo ci{};
        bool got = false;
        {
            lock_guard<mutex> lock(queueMutex);
            if (!pendingClients.isEmpty())
            {
                ci = pendingClients.dequeue();
                got = true;
            }
        }
        if (!got)
        {
            usleep(20 * 1000); // queue empty: don't spin
            continue;
        }

        if (time(nullptr) - ci.arrivedAt > CLIENT_WAIT_TIMEOUT_SEC)
        {
            close(ci.sock); // waited too long: closed, no reply
            continue;
        }

        relayOneClient(ci);
        close(ci.sock); // the only place a queued client's socket is closed
    }
}

void handler(int sg)
{
    (void)sg;
    flag = 0;
}
int main(int argc, char *argv[])
{

    signal(SIGINT, handler); // whenever ctrl+c will be pressed to exit the program the flag wll be set so insead of using infinite loop you can loop on flag to safely close things and exit
    // 2. listenSock = createListenSocket(LB_PORT).
    // 3. Start a thread that runs acceptLoop(listenSock) and detach it.
    // 4. Call dispatcherLoop().
    if (argc >= 3)
    {
        serverHost = argv[1];
        serverPort = atoi(argv[2]);
    }
    if (argc >= 4)
        lbPort = atoi(argv[3]);

    // sigaction without SA_RESTART so Ctrl+C interrupts accept()
    struct sigaction sa{};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    int listenSock = createListenSocket(lbPort);
    cout << "load balancer on port " << lbPort << " -> server " << serverHost << ":" << serverPort << endl;

    thread(acceptLoop, listenSock).detach();
    dispatcherLoop();

    /* shutdown: close anyone still waiting */
    {
        lock_guard<mutex> lock(queueMutex);
        while (!pendingClients.isEmpty())
            close(pendingClients.dequeue().sock);
    }
    close(listenSock);
    return 0;
}