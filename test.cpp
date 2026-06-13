#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include "my_api.h"

// テスト用の派生クラス（内部状態やスレッド起動を同期するためのフックを作る場合）
class TestableMyApi : public MyApi {
    // 必要に応じて、内部関数をオーバーライドしたりフレンド指定したりします
};

TEST(MyApiTest, MpApiInitStartsThDemuxThread) {
    MyApi api;

    // 1. 初期状態ではスレッドは走っていないことを確認
    EXPECT_FALSE(api.is_demux_running());

    // 2. mp_api_init を呼び出してスレッドを起動
    api.mp_api_init();

    // 3. スレッドが非同期で起動するのを少し待つ（タイムアウト付きループ）
    // マルチスレッドテストでは、起動直後はまだフラグが立っていない可能性があるため
    const int max_attempts = 100;
    bool thread_started = false;
    
    for (int i = 0; i < max_attempts; ++i) {
        if (api.is_demux_running()) {
            thread_started = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 4. アサーション：スレッドが正常に起動したか
    ASSERT_TRUE(thread_started) << "th_demux thread failed to start within the timeout period.";
}

TEST(MyApiSubThreadTest, ThDemuxStartsThDecodeWorker) {
    MyApi api;

    // 1. 初期状態ではどちらのスレッドも動いていないことを確認
    ASSERT_FALSE(api.is_demux_running());
    ASSERT_FALSE(api.is_decode_running());

    // 2. 親スレッドの初期化（これにより内部で th_demux -> th_decode_worker と起動する）
    api.mp_api_init();

    // 3. 孫スレッド (th_decode_worker) が起動するのをタイムアウト付きで待つ
    // スレッドの起動にはわずかに時間がかかるため、ループでポーリングします
    const int max_attempts = 100; // 最大1秒（10ms × 100）待つ
    bool decode_worker_started = false;

    for (int i = 0; i < max_attempts; ++i) {
        if (api.is_decode_running()) {
            decode_worker_started = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 4. 検証：th_decode_worker が正常に起動したか
    EXPECT_TRUE(api.is_demux_running()) << "th_demux thread itself did not start.";
    
    // ここが今回の本命のテスト
    ASSERT_TRUE(decode_worker_started) << "th_decode_worker thread failed to start from th_demux.";
}

// =========================================================================
// テスト3: バッファ確保とアドレス格納の検証用フィクスチャとテスト
// =========================================================================
class MyApiMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 必要に応じて各テストケース実行前の共通処理を記述（今回は空でOK）
    }
};

// バッファ確保とアドレス格納の検証テスト
TEST_F(MyApiMemoryTest, InitBuffersCorrectly) {
    MyApi api;

    // 1. 実行前にポインタが初期化(nullptr)されていることを一応確認
    // (コンストラクタの挙動テスト)
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(api.buffers[i], nullptr);
    }

    // 2. 初期化メソッドを実行（ここでメモリ確保とアドレス割り当てが行われる）
    api.mp_api_init();

    // 定数の定義（検証用）
    const size_t EXPECTED_BUFFER_SIZE = 2 * 1024 * 1024; // 2MB
    const size_t EXPECTED_BUFFER_COUNT = 8;

    // 3. shared_memory_pool のサイズが 16MB になっているか確認
    EXPECT_EQ(api.shared_memory_pool.size(), EXPECTED_BUFFER_SIZE * EXPECTED_BUFFER_COUNT);

    // 先頭アドレスを取得
    uint8_t* pool_start = api.shared_memory_pool.data();
    uint8_t* pool_end = pool_start + api.shared_memory_pool.size();

    // 4. 各バッファのポインタが正しく計算され、格納されているかをループで検証
    for (size_t i = 0; i < EXPECTED_BUFFER_COUNT; ++i) {
        // ポインタが nullptr でないこと
        ASSERT_NE(api.buffers[i], nullptr) << "Buffer [" << i << "] is nullptr!";

        // ポインタが確保したメモリプールの範囲内にあること
        EXPECT_GE(api.buffers[i], pool_start);
        EXPECT_LT(api.buffers[i], pool_end);

        // アドレスが「先頭 + (2MB × i)」の正確な位置を指しているか
        uint8_t* expected_address = pool_start + (i * EXPECTED_BUFFER_SIZE);
        EXPECT_EQ(api.buffers[i], expected_address) 
            << "Buffer [" << i << "] address is incorrect. "
            << "Expected: " << static_cast<void*>(expected_address) << ", "
            << "Actual: " << static_cast<void*>(api.buffers[i]);
    }
}
