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

    void mp_api_init() {
        // th_demux スレッドを起動
        demux_thread = std::thread(&MyApi::th_demux, this);
    }

    // テストから状態を確認するためのヘルパー（または共通のフラグ）
    bool is_demux_running() const { return is_running; }

private:
    void th_demux() {
        is_running = true;
        // スレッドのメインループ処理...
        while (is_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // 本来のデマルチプレクス処理
            break; // テスト用にすぐ抜ける構成にしています
        }
    }

    std::thread demux_thread;
    std::atomic<bool> is_running;
};
