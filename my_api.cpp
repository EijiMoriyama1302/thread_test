#include "my_api.h"

void MyApi::mp_api_init() {
    // th_demux スレッドを起動
    demux_thread = std::thread(&MyApi::th_demux, this);
}

void MyApi::th_demux() {
    is_running = true;
    // スレッドのメインループ処理...
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // 本来のデマルチプレクス処理
        break; // テスト用にすぐ抜ける構成にしています
    }
}
