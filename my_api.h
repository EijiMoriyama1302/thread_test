// my_api.h
#pragma once
#include <thread>
#include <atomic>

class MyApi {
public:
    MyApi() : is_running(false) {}
    ~MyApi() {}

    void mp_api_init() {}

    // テストから状態を確認するためのヘルパー（または共通のフラグ）
    bool is_demux_running() const { return is_running; }

private:
    void th_demux();

    std::atomic<bool> is_running;
};
