#include "my_api.h"
#include <cstdio>
#include <cstring>

void MyApi::mp_api_init() {
    // 1. 2MB × 8個 = 16MB のメモリを動的に一括確保
    shared_memory_pool.resize(BUFFER_SIZE * BUFFER_COUNT);

    // 2. 各バッファのアドレスを割り振り、初期状態としてすべて「空」キューに入れる
    for (size_t i = 0; i < BUFFER_COUNT; ++i) {
        buffers[i] = &shared_memory_pool[i * BUFFER_SIZE];
    }

    // ★重要: スレッドを起動する「前」に、メインスレッド側でフラグを true にする
    is_running = true;

    // th_demux スレッドを起動
    demux_thread = std::thread(&MyApi::th_demux, this);
}

void MyApi::th_demux() {
    // ★【重要】もしスレッドが動き出した瞬間に、メイン側でデストラクタが始まっていたら（is_runningがfalseになっていたら）
    // 孫スレッドの起動処理をスキップして即終了するガードを入れる
    if (!is_running) {
        return;
    }

    // ★重要: decode_workerを起動する「前」に、フラグを true にする
    is_decode_worker_running = true;
    decode_thread = std::thread(&MyApi::th_decode_worker, this);

    // 【ファイル読み込み・書き込み処理のイメージ】
    // 実際のコードでは、ここで "test.mpg" を開き、2MB分を buffers[0] に読み込む
    FILE* fp = fopen("test.mpg", "rb");
    if (fp) {
        // 全8個のバッファに対して順番に2MBずつデータを読み込む
        for (size_t i = 0; i < BUFFER_COUNT; ++i) {
            if (buffers[i] != nullptr) {
                fread(buffers[i], 1, BUFFER_SIZE, fp); // BUFFER_SIZEは2MB
            }
        }
        fclose(fp);
    }

    // スレッドのメインループ処理...
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // 本来のデマルチプレクス処理
        
        // 注意: テストで break させる場合でも、
        // デストラクタが呼ばれるまではループを維持するか、
        // あるいは適切にフラグが連動するようにします。
        // ここでは即 break せず、フラグを見て回るようにしておくのがマルチスレッドテストの鉄則です。
    }
}

void MyApi::th_decode_worker() {
    // ここでのフラグセットは削除（親スレッド側でセット済み）

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    while (is_decode_worker_running) {
        // 本来は buffers[0] にデータが届くのを条件変数等で待つ
        
        // 2MBの領域を 2KB(2048バイト) ずつ進めるループ
        const size_t STEP_SIZE = 2 * 1024; // 2KB
        uint8_t* start_ptr = buffers[0];
        uint8_t* end_ptr = start_ptr + BUFFER_SIZE; // BUFFER_SIZEは2MB

        for (uint8_t* current_ptr = start_ptr; current_ptr < end_ptr; current_ptr += STEP_SIZE) {
            // ここで本来の dec_simple 処理などを実行
            this->dec_simple(current_ptr, STEP_SIZE);
        }

        // 2. ★ループ完了後にバッファ（2MB）を0クリアする
        std::memset(buffers[0], 0, BUFFER_SIZE);

        // 3. テスト側に完了を伝える
        is_clear_completed = true;

        break; // テスト用に1周したら抜ける
    }
}

void MyApi::dec_simple(uint8_t* data, size_t size) {
    if(this->is_dec_simple_called == false) {
        // 検証用に、実際に渡す値をメンバ変数に記録
        this->passed_address = data;
        this->passed_size = size;
        this->is_dec_simple_called = true; // 呼び出し完了フラグ
        decode_loop_count = 0;
    }

    // テスト検証用に記録
    decode_loop_count++;
    last_processed_address = data;
}