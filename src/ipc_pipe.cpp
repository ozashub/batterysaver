#include "ipc_pipe.h"
#include "console_log.h"

IpcServer::IpcServer(Handler handler) : handler_(std::move(handler)) {}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start() {
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) return false;

    running_.store(true);
    thread_ = std::thread(&IpcServer::listen_loop, this);
    Log::info("IPC server started");
    return true;
}

void IpcServer::stop() {
    running_.store(false);
    if (stop_event_) SetEvent(stop_event_);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        // unblock ConnectNamedPipe by connecting to it ourselves
        auto dummy = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (dummy != INVALID_HANDLE_VALUE) CloseHandle(dummy);
    }
    if (thread_.joinable()) thread_.join();
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
}

void IpcServer::listen_loop() {
    while (running_.load()) {
        pipe_ = CreateNamedPipeW(PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 512, 512, 0, nullptr);

        if (pipe_ == INVALID_HANDLE_VALUE) {
            Log::error("CreateNamedPipe failed", GetLastError());
            return;
        }

        BOOL connected = ConnectNamedPipe(pipe_, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            if (!running_.load()) break;
            continue;
        }

        if (!running_.load()) {
            DisconnectNamedPipe(pipe_);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            break;
        }

        IpcPacket req{};
        DWORD read = 0;
        if (ReadFile(pipe_, &req, sizeof(req), &read, nullptr) && read > 0) {
            IpcPacket resp{};
            handler_(req, resp);
            DWORD written = 0;
            WriteFile(pipe_, &resp, sizeof(resp), &written, nullptr);
        }

        FlushFileBuffers(pipe_);
        DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool IpcClient::send(const IpcPacket& req, IpcPacket& resp) {
    HANDLE pipe = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    DWORD written = 0;
    if (!WriteFile(pipe, &req, sizeof(req), &written, nullptr)) {
        CloseHandle(pipe);
        return false;
    }

    DWORD read = 0;
    bool ok = ReadFile(pipe, &resp, sizeof(resp), &read, nullptr) && read > 0;
    CloseHandle(pipe);
    return ok;
}

bool IpcClient::send_fire_forget(const IpcPacket& pkt) {
    HANDLE pipe = CreateFileW(PIPE_NAME, GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    WriteFile(pipe, &pkt, sizeof(pkt), &written, nullptr);
    CloseHandle(pipe);
    return true;
}
