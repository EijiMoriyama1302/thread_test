#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstring>
#include <fstream>
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

class MyApiFileWriteTest : public ::testing::Test {
protected:
    const std::string test_filename = "test.mpg";
    const size_t FILE_SIZE = 2 * 1024 * 1024; // 2MB
    std::vector<uint8_t> dummy_data;

    // テストケース実行前に、2MBのダミーファイルを作成する
    void SetUp() override {
        // 判別しやすいように 0xAA で埋めた2MBのデータを用意
        dummy_data.resize(FILE_SIZE, 0xAA);

        std::ofstream ofs(test_filename, std::ios::binary);
        ASSERT_TRUE(ofs.is_open()) << "Failed to create temporary test file.";
        ofs.write(reinterpret_cast<const char*>(dummy_data.data()), FILE_SIZE);
        ofs.close();
    }

    // テスト終了後に、ゴミが残らないようファイルを削除する
    void TearDown() override {
        std::remove(test_filename.c_str());
    }
};

TEST_F(MyApiFileWriteTest, ThDemuxWritesFileDataToBuffer0) {
    MyApi api;

    // 1. 初期化とスレッド起動（内部で test.mpg の読み込みが走る）
    api.mp_api_init();

    // 2. 非同期でファイル読み込み・バッファ書き込みが行われるのを少し待つ
    //（実務では書き込み完了フラグ等を用意するのがベストですが、ここではタイムアウト付きで少し猶予を上げます）
    const int max_attempts = 50;
    for (int i = 0; i < max_attempts; ++i) {
        // buffers[0] の先頭が初期値(nullptrや0)から、ファイルデータの 0xAA に変わったか簡易チェック
        if (api.buffers[0] != nullptr && api.buffers[0][0] == 0xAA) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 3. 検証：buffers[0] が nullptr でないこと
    ASSERT_NE(api.buffers[0], nullptr);

    // 4. 検証：buffers[0] に書き込まれた2MBのデータが、元ファイル（dummy_data）と完全一致するか
    int result = std::memcmp(api.buffers[0], dummy_data.data(), FILE_SIZE);
    
    EXPECT_EQ(result, 0) << "Data in buffers[0] does not match the source file 'test.mpg'.";
}
