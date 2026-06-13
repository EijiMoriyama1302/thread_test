// my_api.h
#pragma once
#include <thread>
#include <atomic>

class MyApi {
public:
    MyApi() 
    : is_running(false)
    , is_decode_worker_running(false)
    {}
    ~MyApi() {
        if (decode_thread.joinable()) {
            decode_thread.join();
        }
        if (demux_thread.joinable()) {
            demux_thread.join();
        }
    }

    void mp_api_init();

    // テストから状態を確認するためのヘルパー（または共通のフラグ）
    bool is_demux_running() const { return is_running; }
    bool is_decode_running() {return is_decode_worker_running;}

private:
    void th_demux() ;
    void th_decode_worker() ;

    std::thread demux_thread;
    std::thread decode_thread;
    std::atomic<bool> is_running;
    std::atomic<bool> is_decode_worker_running;
};
