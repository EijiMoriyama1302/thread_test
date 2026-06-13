#include "my_api.h"

void MyApi::mp_api_init() {
    // 1. 2MB × 8個 = 16MB のメモリを動的に一括確保
    shared_memory_pool.resize(BUFFER_SIZE * BUFFER_COUNT);

    // 2. 各バッファのアドレスを割り振り、初期状態としてすべて「空」キューに入れる
    for (size_t i = 0; i < BUFFER_COUNT; ++i) {
        buffers[i] = &shared_memory_pool[i * BUFFER_SIZE];
    }

    // th_demux スレッドを起動
    demux_thread = std::thread(&MyApi::th_demux, this);
}

void MyApi::th_demux() {
    is_running = true;

    // th_decode_worker スレッドを起動
    decode_thread = std::thread(&MyApi::th_decode_worker, this);

    // スレッドのメインループ処理...
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // 本来のデマルチプレクス処理
        break; // テスト用にすぐ抜ける構成にしています
    }
}

void MyApi::th_decode_worker() {
    is_decode_worker_running = true;

    // スレッドのメインループ処理...
    while (is_decode_worker_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // 本来のデマルチプレクス処理
        break; // テスト用にすぐ抜ける構成にしています
    }
}
