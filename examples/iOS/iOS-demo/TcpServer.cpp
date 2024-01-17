#include "TcpServer.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

void handleSIGPIPE(int signum) {
    (void)signum;
}

TcpServer::TcpServer(SocketCallback &cb)
: m_callback(cb)
{
    static bool signalRegistered = false;
    if (!signalRegistered)
    {
        signal(SIGPIPE, handleSIGPIPE);
        signalRegistered = true;
    }
}

TcpServer::~TcpServer()
{
    
}

bool TcpServer::createSocket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket() failed");
        return false;
    }
    
    /*************************************************************/
    /* Allow socket descriptor to be reuseable                   */
    /*************************************************************/
    int on = 1;
    auto rc = setsockopt(fd, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on));
    if (rc < 0)
    {
        perror("setsockopt() failed");
        close(fd);
        return false;
    }
    
    m_socket = fd;
    return true;
}

bool TcpServer::prepareToListen(int port)
{
    int on = 1;
    auto rc = setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (rc < 0) {
        m_callback.log("set port reuse error", m_callback.userdata);
        close(m_socket);
        return false;
    }
    
    /*************************************************************/
    /* Bind the socket                                           */
    /*************************************************************/
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr("0.0.0.0");
    addr.sin_port        = htons(port);
    rc = bind(m_socket, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0)
    {
        perror("bind() failed");
        close(m_socket);
        return false;
    }
    
    /*************************************************************/
    /* Set the listen back log                                   */
    /*************************************************************/
    rc = listen(m_socket, 32);
    if (rc < 0)
    {
        perror("listen() failed");
        close(m_socket);
        return false;
    }
    return true;
}

void TcpServer::socketThread(void *param)
{
    auto server = (TcpServer *)param;
    server->socketThreadInternal();
}

static int64_t currentTime()
{
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
    return std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
}

void TcpServer::socketThreadInternal()
{
    int cfd = -1;
    do 
    {
        struct sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        m_callback.log("start accept client connect request\n", m_callback.userdata);
        cfd = accept(m_socket, (struct sockaddr *)&clientAddr, &len);
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (cfd <= 0)
            {
                m_callback.log("server accpet error!\n", m_callback.userdata);
                break;
            }
            
            m_callback.log("new connection received\n", m_callback.userdata);
            
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 5000;
            
            setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            m_clientSocket = cfd;
        }
        m_callback.connected(m_callback.userdata);
        
        //check client disconnect
        while (!m_endServer) 
        {
            char buffer[80];
            auto ret = recv(m_clientSocket, buffer, sizeof(buffer), 0);
            if (ret == 0 || (ret == -1 && errno != EAGAIN)) 
            {
                //disconnect
                closeClientSocket();
                break;
            }
            
        }
    } while (!m_endServer);
    
    closeClientSocket();
    
    m_callback.log("server socket thread end.\n", m_callback.userdata);
}

bool TcpServer::start(int port)
{
    if (!createSocket())
        return false;
    
    if (!prepareToListen(port))
        return false;
    
    m_socketThread = std::thread(TcpServer::socketThread, this);
    m_socketThread.detach();
    
    m_callback.log("server started\n", m_callback.userdata);
    
    return true;
}

void TcpServer::stop()
{
    m_callback.log("request server stop\n", m_callback.userdata);
    
    m_endServer = true;
    
    if (m_socket > 0)
    {
        close(m_socket);
        m_socket = -1;
    }
    
    if (m_socketThread.joinable())
        m_socketThread.join();
}

int TcpServer::clientSocket()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_clientSocket;
}

void TcpServer::closeClientSocket()
{
    m_callback.log("closeClientSocket\n", m_callback.userdata);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_clientSocket != -1) {
        close(m_clientSocket);
        m_clientSocket = -1;
    }
}
