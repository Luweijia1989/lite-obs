#pragma once
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

struct SocketCallback {
    void (*connected)(void *);
    void (*disconnected)(void *);
    void (*log)(const char *, void *);
    void *userdata;
};

class TcpServer {
public:
    TcpServer(SocketCallback &cb);
    ~TcpServer();
    bool start(int port);
    void stop();
    int clientSocket();
    void closeClientSocket();
    
private:
    bool createSocket();
    bool prepareToListen(int port);
    
    static void socketThread(void *param);
    void socketThreadInternal();
    
private:
    int m_socket = -1;
    int m_clientSocket = -1;
    std::mutex m_mutex;
    SocketCallback m_callback;
    std::thread m_socketThread;
    bool m_endServer = false;
};

