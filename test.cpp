#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstring>
#include <vector>
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

class MyApiDecodeArgsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MyApiDecodeArgsTest, ThDecodeWorkerPassesCorrectArgsToDecSimple) {
    MyApi api;

    // 1. メモリプールとスレッドの初期化
    api.mp_api_init();

    // buffers[0] が正しく割り当てられていることを確認
    ASSERT_NE(api.buffers[0], nullptr);

    // 2. 非同期で th_decode_worker が動き、引数をセットして呼び出すのを少し待つ
    const int max_attempts = 100;
    bool is_called = false;

    for (int i = 0; i < max_attempts; ++i) {
        // プロダクション側で用意した呼び出し完了フラグをチェック
        if (api.is_dec_simple_called) {
            is_called = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 3. 検証：タイムアウトまでに確実に処理が走ったか
    ASSERT_TRUE(is_called) << "th_decode_worker did not call dec_simple logic within timeout.";

    // 4. 検証：dec_simple に渡された「アドレス」が buffers[0] と完全に一致するか
    EXPECT_EQ(api.passed_address, api.buffers[0]) 
        << "The address passed to dec_simple does not match buffers[0].";

    // 5. 検証：dec_simple に渡された「サイズ」が 2KB (2048) であるか
    EXPECT_EQ(api.passed_size, 2 * 1024) 
        << "The size passed to dec_simple is not 2048 bytes.";
}

class MyApiDecodeLoopTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MyApiDecodeLoopTest, ThDecodeWorkerAdvancesPointerEvery2KBUpTo2MB) {
    MyApi api;

    // 1. メモリプールとスレッドの初期化
    api.mp_api_init();

    // buffers[0] が正しく割り当てられていることを確認
    ASSERT_NE(api.buffers[0], nullptr);

    // 2. 非同期で th_decode_worker が2MB分のループを完了するのを待つ
    const int max_attempts = 100;
    bool is_completed = false;

    for (int i = 0; i < max_attempts; ++i) {
        if (api.is_clear_completed) {
            is_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 3. 検証：タイムアウトせずにループが最後まで走り切ったか
    ASSERT_TRUE(is_completed) << "th_decode_worker loop did not complete within the timeout period.";

    // 4. 検証：2KBずつの反復回数が【正確に1024回】であるか
    // 計算式: 2MB (2,097,152バイト) / 2KB (2,048バイト) = 1024回
    const size_t EXPECTED_LOOPS = (2 * 1024 * 1024) / (2 * 1024); 
    EXPECT_EQ(api.decode_loop_count, EXPECTED_LOOPS) 
        << "The loop did not execute exactly " << EXPECTED_LOOPS << " times.";

    // 5. 検証：最後に処理されたポインタのアドレスが正確か
    // ループの最後(1024回目)は、2MBの末尾からちょうど2KB手前のアドレスを指している必要があります。
    uint8_t* expected_last_addr = api.buffers[0] + (2 * 1024 * 1024) - (2 * 1024);
    EXPECT_EQ(api.last_processed_address, expected_last_addr)
        << "The last processed address is incorrect. "
        << "Expected: " << static_cast<void*>(expected_last_addr) << ", "
        << "Actual: " << static_cast<void*>(api.last_processed_address);
}

class MyApiBufferClearTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MyApiBufferClearTest, ThDecodeWorkerClearsBuffer0AfterProcessing) {
    MyApi api;

    // 1. メモリプールとスレッドの初期化
    api.mp_api_init();

    // buffers[0] が正しく割り当てられていることを確認
    ASSERT_NE(api.buffers[0], nullptr);

    // 2. 変化を確実に検知するため、事前に buffers[0] (2MB) を 0x55 で埋めておく
    // (これをしないと、元から0だったのかクリアされて0になったのか区別がつきません)
    std::memset(api.buffers[0], 0x55, MyApi::BUFFER_SIZE);

    // 3. 非同期で th_decode_worker がループ処理とゼロクリアを終えるのを待つ
    const int max_attempts = 100;
    bool is_completed = false;

    for (int i = 0; i < max_attempts; ++i) {
        if (api.is_clear_completed) {
            is_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 4. 検証：タイムアウトせずにクリア処理まで到達したか
    ASSERT_TRUE(is_completed) << "th_decode_worker processing and clear did not complete within timeout.";

    // 5. 検証：buffers[0] の 2MB の領域が「すべて 0」になっているか
    // 期待値として、すべて0で埋まった2MBの比較用ベクトルを用意
    std::vector<uint8_t> expected_zero_buffer(MyApi::BUFFER_SIZE, 0);

    // memcmp で実際のバッファと期待値（すべて0）を比較
    int result = std::memcmp(api.buffers[0], expected_zero_buffer.data(), MyApi::BUFFER_SIZE);
    
    EXPECT_EQ(result, 0) << "buffers[0] was not correctly cleared to 0 after processing.";
}

class MyApiAllBuffersWriteTest : public ::testing::Test {
protected:
    const std::string test_filename = "test.mpg";
    const size_t ONE_BUFFER_SIZE = 2 * 1024 * 1024; // 2MB
    const size_t TOTAL_SIZE = ONE_BUFFER_SIZE * MyApi::BUFFER_COUNT; // 16MB
    std::vector<uint8_t> dummy_file_data;

    // テストケース実行前に、16MBのテスト用ダミーファイルを生成する
    void SetUp() override {
        dummy_file_data.resize(TOTAL_SIZE);
        // 検証しやすいように、一意のパターン（インデックスに応じた値）で埋める
        for (size_t i = 0; i < TOTAL_SIZE; ++i) {
            dummy_file_data[i] = static_cast<uint8_t>(i % 256);
        }

        std::ofstream ofs(test_filename, std::ios::binary);
        ASSERT_TRUE(ofs.is_open()) << "Failed to create temporary 16MB test file.";
        ofs.write(reinterpret_cast<const char*>(dummy_file_data.data()), TOTAL_SIZE);
        ofs.close();
    }

    // テスト終了後に一時ファイルを削除する
    void TearDown() override {
        std::remove(test_filename.c_str());
    }
};

TEST_F(MyApiAllBuffersWriteTest, ThDemuxWritesFileDataToAllBuffersSequentially) {
    MyApi api;

    // 1. 初期化とスレッド起動（内部でファイルの読み込みが非同期に開始される）
    api.mp_api_init();

    // 2. 確実に全バッファへの書き込み変化を追うため、shared_memory_pool があれば 
    //    事前に 0xFF 等で初期化されているか、またはプロダクションコード側の処理完了フラグを待ちます。
    //    ここでは、最後のバッファ (buffers[7]) の末尾が期待される値に変わるかをタイムアウト付きで監視します。
    const int max_attempts = 100;
    bool all_buffers_ready = false;
    uint8_t expected_last_byte = dummy_file_data[TOTAL_SIZE - 1];

    for (int i = 0; i < max_attempts; ++i) {
        // 全バッファのポインタが有効かつ、最後のバッファの最後の1バイトが書き換わったかチェック
        if (api.buffers[MyApi::BUFFER_COUNT - 1] != nullptr && 
            api.buffers[MyApi::BUFFER_COUNT - 1][ONE_BUFFER_SIZE - 1] == expected_last_byte) {
            all_buffers_ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    // 3. アサーション：タイムアウトまでに全データが書き込まれたか確認
    ASSERT_TRUE(all_buffers_ready) << "th_demux failed to write all 8 buffers within the timeout period.";

    // 4. 検証：buffers[0] から buffers[7] までのデータ整合性をループで検証
    for (size_t b = 0; b < MyApi::BUFFER_COUNT; ++b) {
        // ポインタが nullptr でないことを保証
        ASSERT_NE(api.buffers[b], nullptr) << "buffers[" << b << "] is nullptr.";

        // 元の 16MB データから、各バッファに対応する 2MB 分のオフセット位置を計算
        const uint8_t* expected_buffer_start = dummy_file_data.data() + (b * ONE_BUFFER_SIZE);

        // メモリ内容を比較
        int result = std::memcmp(api.buffers[b], expected_buffer_start, ONE_BUFFER_SIZE);
        
        EXPECT_EQ(result, 0) << "Data mismatch detected in buffers[" << b << "]!";
    }
}

class MyApiRingBufferWriteTest : public ::testing::Test {
protected:
    const std::string test_filename = "test.mpg";
    const size_t BLOCK_SIZE = 2 * 1024 * 1024; // 2MB
    // 8個分(16MB) + 続きの1個分(2MB) = 計18MB
    const size_t TOTAL_SIZE = BLOCK_SIZE * (MyApi::BUFFER_COUNT + 1); 
    std::vector<uint8_t> dummy_file_data;

    void SetUp() override {
        dummy_file_data.resize(TOTAL_SIZE);
        // 検証しやすいように、ブロックごとに異なる値で埋める
        // ブロック0 (0~2MB): 0x01, ブロック1 (2~4MB): 0x02, ..., ブロック8 (16~18MB): 0x09
        for (size_t b = 0; b < MyApi::BUFFER_COUNT + 1; ++b) {
            std::memset(dummy_file_data.data() + (b * BLOCK_SIZE), static_cast<int>(b + 1), BLOCK_SIZE);
        }

        std::ofstream ofs(test_filename, std::ios::binary);
        ASSERT_TRUE(ofs.is_open());
        ofs.write(reinterpret_cast<const char*>(dummy_file_data.data()), TOTAL_SIZE);
        ofs.close();
    }

    void TearDown() override {
        std::remove(test_filename.c_str());
    }
};

TEST_F(MyApiRingBufferWriteTest, ThDemuxOverwritesBuffer0WithNextDataAfterClear) {
    MyApi api;

    // 1. スレッド起動＆初期化（まずはファイルから16MB分が全バッファに順次書き込まれる）
    api.mp_api_init();

    // 2. 1周目の書き込みが完了するのを待つ（buffers[7] の先頭が 0x08 になるのを監視）
    const int max_attempts = 100;
    bool first_round_completed = false;
    for (int i = 0; i < max_attempts; ++i) {
        if (api.buffers[MyApi::BUFFER_COUNT - 1] != nullptr && 
            api.buffers[MyApi::BUFFER_COUNT - 1][0] == 0x08) {
            first_round_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(first_round_completed) << "First round of writing to buffers[0~7] timed out.";

    // 3. 【チェック】この時点で、buffers[0] には「1周目のデータ (0x01)」が入っていることを確認
    EXPECT_EQ(api.buffers[0][0], 0x01);

    // 4. 【擬似シミュレート】テスト側から buffers[0] を「0クリア」して、デコーダーが消費したことにする
    std::memset(api.buffers[0], 0, BLOCK_SIZE);

    // 5. th_demux が「buffers[0] が空いた！」と検知して、続きのデータ（0x09）を書き込むのを待つ
    bool overwrite_completed = false;
    for (int i = 0; i < max_attempts; ++i) {
        // buffers[0] の先頭が、続きのデータの識別値（0x09）に変わったか監視
        if (api.buffers[0][0] == 0x09) {
            overwrite_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(overwrite_completed) << "th_demux did not overwrite buffers[0] with the next data.";

    // 6. 【最終検証】buffers[0] の2MB全域が、ファイルの後続データ（ブロック8）と完全一致するか検証
    const uint8_t* expected_next_data = dummy_file_data.data() + (MyApi::BUFFER_COUNT * BLOCK_SIZE);
    int result = std::memcmp(api.buffers[0], expected_next_data, BLOCK_SIZE);

    EXPECT_EQ(result, 0) << "The overwritten data in buffers[0] is corrupted or incorrect.";
}

class MyApiContinuousRingBufferTest : public ::testing::Test {
protected:
    const std::string test_filename = "test.mpg";
    const size_t BLOCK_SIZE = 2 * 1024 * 1024; // 2MB
    // 8個分(16MB) + 続きの2個分(4MB) = 計20MB
    const size_t TOTAL_SIZE = BLOCK_SIZE * (MyApi::BUFFER_COUNT + 2); 
    std::vector<uint8_t> dummy_file_data;

    void SetUp() override {
        dummy_file_data.resize(TOTAL_SIZE);
        // ブロックごとに異なる値で埋める (ブロック0: 0x01, ブロック1: 0x02, ..., ブロック9: 0x0A)
        for (size_t b = 0; b < MyApi::BUFFER_COUNT + 2; ++b) {
            std::memset(dummy_file_data.data() + (b * BLOCK_SIZE), static_cast<int>(b + 1), BLOCK_SIZE);
        }

        std::ofstream ofs(test_filename, std::ios::binary);
        ASSERT_TRUE(ofs.is_open());
        ofs.write(reinterpret_cast<const char*>(dummy_file_data.data()), TOTAL_SIZE);
        ofs.close();
    }

    void TearDown() override {
        std::remove(test_filename.c_str());
    }
};

TEST_F(MyApiContinuousRingBufferTest, ThDemuxOverwritesBuffersSequentiallyAfterClear) {
    MyApi api;

    // 1. 初期化とスレッド起動（まずはファイルから16MB分が全バッファに順次書き込まれる）
    api.mp_api_init();

    const int max_attempts = 100;

    // -------------------------------------------------------------------------
    // ステップ1: 1周目（buffers[0]〜[7]）の書き込み完了を待つ
    // -------------------------------------------------------------------------
    bool first_round_completed = false;
    for (int i = 0; i < max_attempts; ++i) {
        if (api.buffers[MyApi::BUFFER_COUNT - 1] != nullptr && 
            api.buffers[MyApi::BUFFER_COUNT - 1][0] == 0x08) {
            first_round_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(first_round_completed) << "First round of writing to buffers[0~7] timed out.";
    
    // この時点で buffers[0] は 0x01, buffers[1] は 0x02 であることを確認
    EXPECT_EQ(api.buffers[0][0], 0x01);
    EXPECT_EQ(api.buffers[1][0], 0x02);

    // -------------------------------------------------------------------------
    // ステップ2: buffers[0] を0クリアし、9個目のデータ（0x09）が書き込まれるのを待つ
    // -------------------------------------------------------------------------
    std::memset(api.buffers[0], 0, BLOCK_SIZE);

    bool buffer0_overwrite_completed = false;
    for (int i = 0; i < max_attempts; ++i) {
        if (api.buffers[0][0] == 0x09) {
            buffer0_overwrite_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(buffer0_overwrite_completed) << "th_demux did not overwrite buffers[0] with 9th block.";

    // buffers[0] の全域検証
    const uint8_t* expected_9th_data = dummy_file_data.data() + (MyApi::BUFFER_COUNT * BLOCK_SIZE);
    EXPECT_EQ(std::memcmp(api.buffers[0], expected_9th_data, BLOCK_SIZE), 0);

    // -------------------------------------------------------------------------
    // ステップ3: buffers[1] を0クリアし、10個目のデータ（0x0A）が書き込まれるのを待つ
    // -------------------------------------------------------------------------
    std::memset(api.buffers[1], 0, BLOCK_SIZE);

    bool buffer1_overwrite_completed = false;
    for (int i = 0; i < max_attempts; ++i) {
        if (api.buffers[1][0] == 0x0A) {
            buffer1_overwrite_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(buffer1_overwrite_completed) << "th_demux did not overwrite buffers[1] with 10th block.";

    // buffers[1] の全域検証
    const uint8_t* expected_10th_data = dummy_file_data.data() + ((MyApi::BUFFER_COUNT + 1) * BLOCK_SIZE);
    EXPECT_EQ(std::memcmp(api.buffers[1], expected_10th_data, BLOCK_SIZE), 0);
}

class MyApiSequentialDecodeTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MyApiSequentialDecodeTest, ThDecodeWorkerProcessesAndClearsBuffersSequentially) {
    MyApi api;

    // 1. メモリプールとスレッドの初期化
    api.mp_api_init();

    // 2. 変化を確実に検知するため、すべてのバッファ (buffers[0]~[7]) を事前に 0x55 で埋める
    for (size_t b = 0; b < MyApi::BUFFER_COUNT; ++b) {
        ASSERT_NE(api.buffers[b], nullptr) << "buffers[" << b << "] is nullptr at initialization.";
        std::memset(api.buffers[b], 0x55, MyApi::BUFFER_SIZE);
    }

    // 3. 非同期で th_decode_worker が全8個のバッファの処理とゼロクリアを終えるのを待つ
    const int max_attempts = 150;
    bool all_processing_completed = false;

    for (int i = 0; i < max_attempts; ++i) {
        // カウンターが 8 (BUFFER_COUNT) に達したら全バッファの処理が完了
        if (api.completed_buffer_count == MyApi::BUFFER_COUNT) {
            all_processing_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 4. 検証：タイムアウトせずに buffers[7] のクリア処理まで到達したか
    ASSERT_TRUE(all_processing_completed) 
        << "th_decode_worker sequential processing timed out. Completed count: " << api.completed_buffer_count;

    // 5. 検証：buffers[0] から buffers[7] までのすべての領域が「すべて 0」になっているか
    std::vector<uint8_t> expected_zero_buffer(MyApi::BUFFER_SIZE, 0);

    for (size_t b = 0; b < MyApi::BUFFER_COUNT; ++b) {
        int result = std::memcmp(api.buffers[b], expected_zero_buffer.data(), MyApi::BUFFER_SIZE);
        
        EXPECT_EQ(result, 0) << "buffers[" << b << "] was not correctly cleared to 0 after sequential processing.";
    }
}

class MyApiDataDetectionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MyApiDataDetectionTest, ThDecodeWorkerClearsAllThenDetectsAndProcessesNewDataInBuffer0) {
    MyApi api;

    // 準備: スレッドが動く前に、あらかじめ全バッファを 0x55 で満たしておく
    // (これらが自動的に 0 クリアされることを確認するため)
    api.shared_memory_pool.resize(MyApi::BUFFER_SIZE * MyApi::BUFFER_COUNT);
    for (size_t b = 0; b < MyApi::BUFFER_COUNT; ++b) {
        api.buffers[b] = &api.shared_memory_pool[b * MyApi::BUFFER_SIZE];
        std::memset(api.buffers[b], 0x55, MyApi::BUFFER_SIZE);
    }

    // 1. スレッドを起動（mp_api_init 内でフラグを立てて thread を走らせる）
    api.mp_api_init();

    const int max_attempts = 100;

    // -------------------------------------------------------------------------
    // 検証ステップ1: 起動直後に全バッファ（buffers[0]~[7]）が0クリアされるのを待つ
    // -------------------------------------------------------------------------
    bool init_clear_done = false;
    for (int i = 0; i < max_attempts; ++i) {
        if (api.is_init_clear_completed) {
            init_clear_done = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(init_clear_done) << "Initial all-buffer clear timed out.";

    // 実際に buffers[0] ～ buffers[7] がすべて 0 になっているか厳密にチェック
    std::vector<uint8_t> expected_zero_buffer(MyApi::BUFFER_SIZE, 0);
    for (size_t b = 0; b < MyApi::BUFFER_COUNT; ++b) {
        EXPECT_EQ(std::memcmp(api.buffers[b], expected_zero_buffer.data(), MyApi::BUFFER_SIZE), 0)
            << "buffers[" << b << "] was not cleared on startup.";
    }

    // -------------------------------------------------------------------------
    // 検証ステップ2: テスト側から buffers[0] に新データを書き込み、検出・処理させる
    // -------------------------------------------------------------------------
    // buffers[0] の全域を新データ 0xAA で埋める（データがセットされた状態を作る）
    std::memset(api.buffers[0], 0xAA, MyApi::BUFFER_SIZE);

    // th_decode_worker が「0以外になった！」と検出して、2KBずつ読み出し、再度0クリアするのを待つ
    bool second_decode_done = false;
    for (int i = 0; i < max_attempts; ++i) {
        if (api.is_second_decode_completed) {
            second_decode_done = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(second_decode_done) << "th_decode_worker failed to detect or process new data in buffers[0].";

    // 最終検証: 2回目の処理が終わり、buffers[0] が再び「すべて 0」に戻っているか確認
    EXPECT_EQ(std::memcmp(api.buffers[0], expected_zero_buffer.data(), MyApi::BUFFER_SIZE), 0)
        << "buffers[0] was not cleared to 0 after the second decode process.";
}

// -------------------------------------------------------------------------
// メモリカウンタの準備 (new / delete のオーバーライド)
// -------------------------------------------------------------------------
std::atomic<int64_t> global_allocated_bytes{0};

void* operator new(size_t size) {
    global_allocated_bytes += size;
    return std::malloc(size);
}

void operator delete(void* ptr, size_t size) noexcept {
    global_allocated_bytes -= size;
    std::free(ptr);
}

void operator delete(void* ptr) noexcept {
    // サイズ不明のdeleteはカウントを正確に引けないため、
    // vectorの解放（サイズ付きdeleteが呼ばれる）をメインに追跡します
    std::free(ptr);
}

// -------------------------------------------------------------------------
// テストクラス
// -------------------------------------------------------------------------
class MyApiMemoryReleaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        // テスト開始時のカウンタを記憶
        baseline_bytes = global_allocated_bytes.load();
    }
    void TearDown() override {}

    int64_t baseline_bytes = 0;
};

TEST_F(MyApiMemoryReleaseTest, MemoryIsCompletelyReleasedAfterThreadAndObjectDestruction) {
    // 計算上の期待される確保サイズ: 2MB * 8 = 16MB (16,777,216 バイト)
    const int64_t EXPECTED_POOL_SIZE = MyApi::BUFFER_SIZE * MyApi::BUFFER_COUNT;

    {
        // 1. スコープ内で MyApi オブジェクトを生み出す
        MyApi api;

        // 2. メモリ確保とスレッド起動
        api.mp_api_init();

        // 初期化によって、カウンタが 16MB 以上増えていることを確認
        int64_t current_allocated = global_allocated_bytes.load() - baseline_bytes;
        EXPECT_GE(current_allocated, EXPECTED_POOL_SIZE) 
            << "Memory pool was not allocated correctly in mp_api_init().";
        
        // (必要に応じて) スレッドが少し動くのを待つ
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        int64_t leaked_bytes = global_allocated_bytes.load() - baseline_bytes;
        EXPECT_NE(leaked_bytes, 0) 
            << "No memory leak detected! " << leaked_bytes << " bytes were not released.";

    } // ◄★★ ここで `api` のスコープが終了！
      // デストラクタが走り、th_demuxが終了(join)し、shared_memory_poolが解放されます。

    // 3. 【最終検証】オブジェクト消滅後のメモリ残量が、開始前（ベースライン）に戻っているかチェック
    int64_t leaked_bytes = global_allocated_bytes.load() - baseline_bytes;

    // 完全に 0 になるか、あるいはstd::threadの内部管理用メモリの極小の微増のみを許容
    EXPECT_LE(leaked_bytes, 0) 
        << "Memory leak detected! " << leaked_bytes << " bytes were not released.";
}
