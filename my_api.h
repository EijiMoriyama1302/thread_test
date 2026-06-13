// my_api.h
#pragma once
#include <thread>
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

class MyApi {

public:
    MyApi() 
    : is_running(false)
    , is_decode_worker_running(false)
    {}

    ~MyApi() {
        // 1. スレッドのループフラグを折る
        is_running = false;
        is_decode_worker_running = false;

        if (demux_thread.joinable()) {
            demux_thread.join();
        }
        if (decode_thread.joinable()) {
            decode_thread.join();
        }
    }

    // --- メモリ・バッファ管理用の追加コード ---
    static constexpr size_t BUFFER_SIZE = 2 * 1024 * 1024; // 2MB
    static constexpr size_t BUFFER_COUNT = 8;

    // 16MBの一括確保用バッファ
    std::vector<uint8_t> shared_memory_pool;

    // バッファの実際の先頭ポインタを保持する配列
    uint8_t* buffers[BUFFER_COUNT] = {nullptr};

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
