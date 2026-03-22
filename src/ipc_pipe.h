#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <string>
#include <functional>
#include <thread>
#include <atomic>

static constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\BatterySaverIPC";

enum class IpcMsg : unsigned char {
    Ping         = 0x01,
    Pong         = 0x02,
    SetMode      = 0x10,
    GetStatus    = 0x20,
    StatusReply  = 0x21,
    Shutdown     = 0xFF
};

struct IpcPacket {
    IpcMsg msg;
    char   data[255];
    int    len;
};

class IpcServer {
public:
    using Handler = std::function<void(const IpcPacket&, IpcPacket&)>;

    explicit IpcServer(Handler handler);
    ~IpcServer();

    bool start();
    void stop();

private:
    Handler handler_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE stop_event_ = nullptr;

    void listen_loop();
};

class IpcClient {
public:
    static bool send(const IpcPacket& req, IpcPacket& resp);
    static bool send_fire_forget(const IpcPacket& pkt);
};
