#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <condition_variable>
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
