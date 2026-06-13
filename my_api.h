// my_api.h
#pragma once
#include <thread>
#include <atomic>

class MyApi {
public:
    MyApi() : is_running(false) {}
    ~MyApi() {
        if (demux_thread.joinable()) {
            demux_thread.join();
        }
    }

    void mp_api_init();

    // テストから状態を確認するためのヘルパー（または共通のフラグ）
    bool is_demux_running() const { return is_running; }
    bool is_decode_running() {return false;}

private:
    void th_demux() ;

    std::thread demux_thread;
    std::atomic<bool> is_running;
};
