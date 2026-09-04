#include <gtest/gtest.h>

#include <future>
#include <string>
#include <thread>
#include <vector>

#include <core/b_result.h>

/**
 * @file result_status_test.cpp
 * @brief Result<T> / Result<void> 全面测试
 *
 * 覆盖范围：
 *   1. 状态码 & Status
 *   2. 构造 / 拷贝 / 移动 / 赋值
 *   3. 状态查询（success / error / bool 转换）
 *   4. 值获取（value / valueOr / 异常）
 *   5. 受控访问（with_value / with_error）
 *   6. 函数式链式调用（transform / andThen / or_else）
 *   7. 工厂函数（Ok / Err）
 *   8. swap
 *   9. Result<void> 特化
 *  10. 多线程并发读写（验证互斥锁安全性）
 */

using namespace bakuon::gui;

// ============================================================================
// 1. 状态码与 Status
// ============================================================================

TEST(StatusCodeTest, CodeStringView_AllMappings)
{
    EXPECT_EQ(codeStringView(StatusCode::Ok), "OK");
    EXPECT_EQ(codeStringView(StatusCode::Cancelled), "CANCELLED");
    EXPECT_EQ(codeStringView(StatusCode::InvalidArgument), "INVALID_ARGUMENT");
    EXPECT_EQ(codeStringView(StatusCode::DeadlineExceeded), "DEADLINE_EXCEEDED");
    EXPECT_EQ(codeStringView(StatusCode::NotFound), "NOT_FOUND");
    EXPECT_EQ(codeStringView(StatusCode::Timeout), "TIMEOUT");
    EXPECT_EQ(codeStringView(StatusCode::Aborted), "ABORTED");
    EXPECT_EQ(codeStringView(StatusCode::AlreadyExists), "ALREADY_EXISTS");
    EXPECT_EQ(codeStringView(StatusCode::Unauthenticated), "UNAUTHENTICATED");
    EXPECT_EQ(codeStringView(StatusCode::PermissionDenied), "PERMISSION_DENIED");
    EXPECT_EQ(codeStringView(StatusCode::ResourceExhausted), "RESOURCE_EXHAUSTED");
    EXPECT_EQ(codeStringView(StatusCode::FailedPrecondition), "FAILED_PRECONDITION");
    EXPECT_EQ(codeStringView(StatusCode::Unimplemented), "UNIMPLEMENTED");
    EXPECT_EQ(codeStringView(StatusCode::Unavailable), "UNAVAILABLE");
    EXPECT_EQ(codeStringView(StatusCode::DataLoss), "DATA_LOSS");
    EXPECT_EQ(codeStringView(StatusCode::OutOfRange), "OUT_OF_RANGE");
    EXPECT_EQ(codeStringView(StatusCode::InternalError), "INTERNAL_ERROR");
    EXPECT_EQ(codeStringView(StatusCode::Unknown), "UNKNOWN");
}

TEST(StatusCodeTest, CodeString_ReturnsCopy)
{
    std::string s = codeString(StatusCode::NotFound);
    EXPECT_EQ(s, "NOT_FOUND");
    s[0] = 'X'; // 修改副本，不影响 constexpr 视图
    EXPECT_EQ(codeStringView(StatusCode::NotFound), "NOT_FOUND");
}

TEST(StatusCodeTest, StreamOperator_WritesNumberAndName)
{
    std::ostringstream oss;
    oss << StatusCode::NotFound;
    EXPECT_NE(oss.str().find('4'), std::string::npos);
    EXPECT_NE(oss.str().find("NOT_FOUND"), std::string::npos);
}

TEST(StatusTest, DefaultIsOk)
{
    Status s;
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.code, StatusCode::Ok);
    EXPECT_TRUE(s.message.empty());
    EXPECT_TRUE(s.stacktrace.empty());
}

TEST(StatusTest, FailureCapturesStacktrace)
{
    Status s(StatusCode::NotFound, "user not found");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code, StatusCode::NotFound);
    EXPECT_EQ(s.message, "user not found");
    // 失败状态应该包含堆栈信息（目前 mock 实现也返回非空字符串）
    EXPECT_FALSE(s.stacktrace.empty());
}

TEST(StatusTest, SuccessDoesNotCaptureStacktrace)
{
    Status s(StatusCode::Ok, "");
    EXPECT_TRUE(s.ok());
    EXPECT_TRUE(s.stacktrace.empty());
}

TEST(StatusTest, StreamOperator)
{
    Status ok;
    std::ostringstream oss1;
    oss1 << ok;
    EXPECT_NE(oss1.str().find("Ok"), std::string::npos);

    Status err(StatusCode::PermissionDenied, "denied");
    std::ostringstream oss2;
    oss2 << err;
    EXPECT_NE(oss2.str().find("PERMISSION_DENIED"), std::string::npos);
    EXPECT_NE(oss2.str().find("denied"), std::string::npos);
}

// ============================================================================
// 2. 构造 / 拷贝 / 移动 / 赋值
// ============================================================================

TEST(ResultConstructTest, SuccessFromLValue)
{
    int value = 42;
    Result<int> r(value);
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultConstructTest, SuccessFromRValue)
{
    Result<std::string> r(std::string("hello"));
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.value(), "hello");
}

TEST(ResultConstructTest, InPlaceConstruction)
{
    Result<std::string> r(std::in_place, 5, 'x');
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.value(), "xxxxx");
}

TEST(ResultConstructTest, FailureFromCodeAndMessage)
{
    Result<int> r(StatusCode::NotFound, "missing");
    EXPECT_TRUE(r.error());
    EXPECT_EQ(r.status().code, StatusCode::NotFound);
    EXPECT_EQ(r.status().message, "missing");
}

TEST(ResultConstructTest, FailureFromStatus)
{
    Status s(StatusCode::InvalidArgument, "bad arg");
    Result<double> r(s);
    EXPECT_TRUE(r.error());
    EXPECT_EQ(r.status().code, StatusCode::InvalidArgument);
    EXPECT_EQ(r.status().message, "bad arg");
}

TEST(ResultConstructTest, ExplicitPreventsImplicitConversionFromStatus)
{
    // Result<int> r = Status(StatusCode::Timeout, "t");  // 不应编译
    // 运行时：explicit 构造依然可以手动调用
    Result<int> r(Status(StatusCode::Timeout, "t"));
    EXPECT_TRUE(r.error());
}

TEST(ResultCopyMoveTest, CopyConstructSuccess)
{
    Result<int> a(42);
    Result<int> b(a);
    EXPECT_TRUE(b.success());
    EXPECT_EQ(b.value(), 42);
    // 源对象仍有效
    EXPECT_TRUE(a.success());
    EXPECT_EQ(a.value(), 42);
}

TEST(ResultCopyMoveTest, CopyConstructFailure)
{
    Result<int> a(StatusCode::NotFound, "nope");
    Result<int> b(a);
    EXPECT_TRUE(b.error());
    EXPECT_EQ(b.status().code, StatusCode::NotFound);
}

TEST(ResultCopyMoveTest, MoveConstructSuccess)
{
    Result<std::string> a(std::string("moved-from"));
    Result<std::string> b(std::move(a));
    EXPECT_TRUE(b.success());
    EXPECT_EQ(b.value(), "moved-from");
}

TEST(ResultCopyMoveTest, MoveConstructFailure)
{
    Result<int> a(StatusCode::InternalError, "boom");
    Result<int> b(std::move(a));
    EXPECT_TRUE(b.error());
    EXPECT_EQ(b.status().code, StatusCode::InternalError);
}

TEST(ResultCopyMoveTest, CopyAssignment)
{
    Result<int> a(100);
    Result<int> b(StatusCode::NotFound, "e");
    b = a;
    EXPECT_TRUE(b.success());
    EXPECT_EQ(b.value(), 100);
}

TEST(ResultCopyMoveTest, MoveAssignment)
{
    Result<std::string> a(std::string("payload"));
    Result<std::string> b(StatusCode::Aborted, "ab");
    b = std::move(a);
    EXPECT_TRUE(b.success());
    EXPECT_EQ(b.value(), "payload");
}

TEST(ResultCopyMoveTest, SelfAssignmentSafe)
{
    Result<int> r(77);
    r = r; // 拷贝自赋值
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.value(), 77);
    r = std::move(r); // 移动自赋值
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.value(), 77);
}

// ============================================================================
// 3. 状态查询
// ============================================================================

TEST(ResultQueryTest, SuccessAndHasError)
{
    Result<int> ok(1);
    EXPECT_TRUE(ok.success());
    EXPECT_FALSE(ok.error());
    EXPECT_TRUE(static_cast<bool>(ok));

    Result<int> err(StatusCode::Timeout, "t");
    EXPECT_FALSE(err.success());
    EXPECT_TRUE(err.error());
    EXPECT_FALSE(static_cast<bool>(err));
}

TEST(ResultQueryTest, StatusReturnsOkForSuccess)
{
    Result<int> r(10);
    EXPECT_TRUE(r.status().ok());
}

// ============================================================================
// 4. 值获取 & 异常
// ============================================================================

TEST(ResultValueTest, ValueReturnsCopy)
{
    Result<int> r(42);
    int v = r.value();
    EXPECT_EQ(v, 42);
}

TEST(ResultValueTest, RValueValueMovesOut)
{
    Result<std::string> r(std::string("hello"));
    std::string v = std::move(r).value();
    EXPECT_EQ(v, "hello");
}

TEST(ResultValueTest, ValueThrowsOnFailure)
{
    Result<int> r(StatusCode::NotFound, "not here");
    try {
        (void) r.value();
        FAIL() << "Expected ResultError to be thrown";
    } catch (const ResultError& e) {
        EXPECT_STREQ(e.what(), "not here");
        EXPECT_EQ(e.status().code, StatusCode::NotFound);
        EXPECT_EQ(e.status().message, "not here");
    } catch (const std::exception&) {
        FAIL() << "Threw wrong exception type";
    }
}

TEST(ResultValueTest, RValueValueThrowsOnFailure)
{
    Result<std::string> r(StatusCode::DataLoss, "corrupted");
    EXPECT_THROW({ std::move(r).value(); }, ResultError);
}

TEST(ResultValueTest, ValueOrReturnsValueOnSuccess)
{
    Result<int> r(42);
    EXPECT_EQ(r.valueOr(0), 42);
}

TEST(ResultValueTest, ValueOrReturnsDefaultOnFailure)
{
    Result<int> r(StatusCode::NotFound, "n");
    EXPECT_EQ(r.valueOr(-1), -1);
}

TEST(ResultValueTest, RValueValueOr)
{
    Result<std::string> ok(std::string("hi"));
    EXPECT_EQ(std::move(ok).valueOr("default"), "hi");

    Result<std::string> err(StatusCode::Unknown, "?");
    EXPECT_EQ(std::move(err).valueOr("default"), "default");
}

// ============================================================================
// 5. 受控访问（with_value / with_error）
// ============================================================================

TEST(ResultAccessTest, WithValueCallsCallbackOnSuccess)
{
    Result<int> r(42);
    int seen = 0;
    int ret  = r.withValue([&](const int& v) {
        seen = v;
        return v * 2;
    });
    EXPECT_EQ(seen, 42);
    EXPECT_EQ(ret, 84);
}

TEST(ResultAccessTest, WithValueSkipsCallbackOnFailure)
{
    Result<int> r(StatusCode::NotFound, "n");
    bool called = false;
    int ret     = r.withValue([&](const int&) {
        called = true;
        return 99;
    });
    EXPECT_FALSE(called);
    EXPECT_EQ(ret, 0); // 默认构造 int
}

TEST(ResultAccessTest, WithValueVoidReturn)
{
    Result<int> r(7);
    int captured = 0;
    r.withValue([&](const int& v) { captured = v; });
    EXPECT_EQ(captured, 7);
}

TEST(ResultAccessTest, WithErrorCallsCallbackOnFailure)
{
    Result<int> r(StatusCode::PermissionDenied, "denied");
    StatusCode code = StatusCode::Ok;
    std::string msg;
    r.withError([&](const Status& s) {
        code = s.code;
        msg  = s.message;
    });
    EXPECT_EQ(code, StatusCode::PermissionDenied);
    EXPECT_EQ(msg, "denied");
}

TEST(ResultAccessTest, WithErrorSkipsOnSuccess)
{
    Result<int> r(1);
    bool called = false;
    r.withError([&](const Status&) { called = true; });
    EXPECT_FALSE(called);
}

// ============================================================================
// 6. 函数式链式调用
// ============================================================================

TEST(ResultMonadTest, TransformOnSuccess)
{
    Result<int> r(21);
    auto doubled = r.transform([](const int& v) { return v * 2; });
    static_assert(std::is_same_v<decltype(doubled), Result<int>>,
                  "transform should produce Result<int>");
    EXPECT_TRUE(doubled.success());
    EXPECT_EQ(doubled.value(), 42);
}

TEST(ResultMonadTest, TransformChangesType)
{
    Result<int> r(5);
    auto str_r = r.transform([](const int& v) { return std::to_string(v); });
    static_assert(std::is_same_v<decltype(str_r), Result<std::string>>, "");
    EXPECT_TRUE(str_r.success());
    EXPECT_EQ(str_r.value(), "5");
}

TEST(ResultMonadTest, TransformPropagatesError)
{
    Result<int> r(StatusCode::Timeout, "timeout");
    auto doubled = r.transform([](const int& v) { return v * 2; });
    EXPECT_TRUE(doubled.error());
    EXPECT_EQ(doubled.status().code, StatusCode::Timeout);
}

TEST(ResultMonadTest, AndThenChainsSuccess)
{
    Result<int> r(10);
    auto chained = r.andThen([](const int& v) -> Result<std::string> {
        if (v > 0)
            return Ok(std::to_string(v));
        return Fail<std::string>(StatusCode::InvalidArgument, "non-positive");
    });
    EXPECT_TRUE(chained.success());
    EXPECT_EQ(chained.value(), "10");
}

TEST(ResultMonadTest, AndThenPropagatesFirstError)
{
    Result<int> r(StatusCode::NotFound, "missing");
    auto chained = r.andThen(
        [](const int& v) -> Result<std::string> { return Ok(std::to_string(v)); });
    EXPECT_TRUE(chained.error());
    EXPECT_EQ(chained.status().code, StatusCode::NotFound);
}

TEST(ResultMonadTest, AndThenCatchesInnerError)
{
    Result<int> r(10);
    auto chained = r.andThen([](const int&) -> Result<std::string> {
        return Fail<std::string>(StatusCode::InternalError, "inner error");
    });
    EXPECT_TRUE(chained.error());
    EXPECT_EQ(chained.status().code, StatusCode::InternalError);
}

TEST(ResultMonadTest, OrElseRecoversOnFailure)
{
    Result<int> r(StatusCode::NotFound, "missing");
    auto recovered = r.orElse([](const Status&) -> Result<int> { return Ok(-1); });
    EXPECT_TRUE(recovered.success());
    EXPECT_EQ(recovered.value(), -1);
}

TEST(ResultMonadTest, OrElsePassThroughOnSuccess)
{
    Result<int> r(42);
    auto untouched = r.orElse([](const Status&) -> Result<int> { return Ok(0); });
    EXPECT_TRUE(untouched.success());
    EXPECT_EQ(untouched.value(), 42);
}

TEST(ResultMonadTest, LongChain)
{
    // 模拟多步流程：字符串 -> int -> 翻倍 -> 转字符串
    auto final_r = Ok(std::string("3"))
                       .andThen([](const std::string& s) -> Result<int> {
                           try {
                               return Ok(std::stoi(s));
                           } catch (...) {
                               return Fail<int>(StatusCode::InvalidArgument, "bad number");
                           }
                       })
                       .transform([](const int& v) { return v * 10; })
                       .transform([](const int& v) { return std::to_string(v) + "!"; });

    EXPECT_TRUE(final_r.success());
    EXPECT_EQ(final_r.value(), "30!");
}

// ============================================================================
// 7. 工厂函数
// ============================================================================

TEST(FactoryTest, OkLValue)
{
    int v   = 5;
    auto r1 = Result<int>::Ok(v);
    static_assert(std::is_same_v<decltype(r1), Result<int>>);
    EXPECT_TRUE(r1.success());
    EXPECT_EQ(r1.value(), 5);

    auto r2 = Ok(v);
    static_assert(std::is_same_v<decltype(r2), Result<int>>);
    EXPECT_TRUE(r2.success());
    EXPECT_EQ(r2.value(), 5);
}

TEST(FactoryTest, OkRValue)
{
    auto r1 = Result<std::string>::Ok(std::string("hi"));
    static_assert(std::is_same_v<decltype(r1), Result<std::string>>);
    EXPECT_TRUE(r1.success());
    EXPECT_EQ(r1.value(), "hi");

    auto r2 = Ok(std::string("hi"));
    static_assert(std::is_same_v<decltype(r2), Result<std::string>>);
    EXPECT_TRUE(r2.success());
    EXPECT_EQ(r2.value(), "hi");
}

TEST(FactoryTest, ErrFromCode)
{
    auto r1 = Result<int>::Fail(StatusCode::NotFound, "x");
    static_assert(std::is_same_v<decltype(r1), Result<int>>);
    EXPECT_TRUE(r1.error());
    EXPECT_EQ(r1.status().code, StatusCode::NotFound);

    auto r2 = Fail<int>(StatusCode::NotFound, "x");
    static_assert(std::is_same_v<decltype(r2), Result<int>>);
    EXPECT_TRUE(r2.error());
    EXPECT_EQ(r2.status().code, StatusCode::NotFound);
}

TEST(FactoryTest, ErrFromStatus)
{
    Status s(StatusCode::Aborted, "ab");
    auto r1 = Result<std::string>::Fail(s);
    EXPECT_TRUE(r1.error());
    EXPECT_EQ(r1.status().message, "ab");

    auto r2 = Fail<std::string>(s);
    EXPECT_TRUE(r2.error());
    EXPECT_EQ(r2.status().message, "ab");
}

// ============================================================================
// 8. swap
// ============================================================================

TEST(SwapTest, SwapBothSuccess)
{
    Result<int> a(1), b(2);
    a.swap(b);
    EXPECT_EQ(a.value(), 2);
    EXPECT_EQ(b.value(), 1);
}

TEST(SwapTest, SwapSuccessAndError)
{
    Result<int> a(1);
    Result<int> b(StatusCode::NotFound, "n");
    swap(a, b);
    EXPECT_TRUE(a.error());
    EXPECT_TRUE(b.success());
    EXPECT_EQ(b.value(), 1);
}

TEST(SwapTest, SwapSelfNoOp)
{
    Result<int> r(42);
    r.swap(r);
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.value(), 42);
}

// ============================================================================
// 9. Result<void> 特化
// ============================================================================

TEST(ResultVoidTest, DefaultIsSuccess)
{
    Result<void> r;
    EXPECT_TRUE(r.success());
    EXPECT_FALSE(r.error());
    EXPECT_TRUE(r.status().ok());
}

TEST(ResultVoidTest, FailureConstruction)
{
    Result<void> r(StatusCode::Unauthenticated, "login required");
    EXPECT_TRUE(r.error());
    EXPECT_EQ(r.status().code, StatusCode::Unauthenticated);
}

TEST(ResultVoidTest, ExplicitBool)
{
    Result<void> ok;
    Result<void> err(StatusCode::Unknown, "?");
    EXPECT_TRUE(static_cast<bool>(ok));
    EXPECT_FALSE(static_cast<bool>(err));
}

TEST(ResultVoidTest, ValueNoThrowOnSuccess)
{
    Result<void> r;
    EXPECT_NO_THROW(r.value());
}

TEST(ResultVoidTest, ValueThrowsOnFailure)
{
    Result<void> r(StatusCode::DataLoss, "corrupt");
    EXPECT_THROW(r.value(), ResultError);
}

TEST(ResultVoidTest, OkFactory)
{
    auto r1 = Ok();
    static_assert(std::is_same_v<decltype(r1), Result<void>>);
    EXPECT_TRUE(r1.success());

    auto r2 = Fail<void>(StatusCode::Cancelled, "cancelled");
    EXPECT_TRUE(r2.error());
}

TEST(ResultVoidTest, OKStaticFactory)
{
    auto r1 = Result<void>::OK();
    EXPECT_TRUE(r1.success());

    auto r2 = Result<void>::Fail(StatusCode::Cancelled, "cancelled");
    EXPECT_TRUE(r2.error());
}

TEST(ResultVoidTest, AndThenContinuesOnSuccess)
{
    Result<void> r;
    int counter  = 0;
    auto chained = r.andThen([&]() -> Result<void> {
        ++counter;
        return Ok();
    });
    EXPECT_TRUE(chained.success());
    EXPECT_EQ(counter, 1);
}

TEST(ResultVoidTest, AndThenShortCircuitsOnError)
{
    Result<void> r(StatusCode::Cancelled, "c");
    bool called  = false;
    auto chained = r.andThen([&]() -> Result<void> {
        called = true;
        return Ok();
    });
    EXPECT_FALSE(called);
    EXPECT_TRUE(chained.error());
    EXPECT_EQ(chained.status().code, StatusCode::Cancelled);
}

TEST(ResultVoidTest, OrElseRecovers)
{
    Result<void> r(StatusCode::Unavailable, "down");
    bool recovered = false;
    auto fixed     = r.orElse([&](const Status&) -> Result<void> {
        recovered = true;
        return Ok();
    });
    EXPECT_TRUE(recovered);
    EXPECT_TRUE(fixed.success());
}

TEST(ResultVoidTest, OrElsePassThroughOnSuccess)
{
    Result<void> r;
    bool called = false;
    auto same   = r.orElse([&](const Status&) -> Result<void> {
        called = true;
        return Ok();
    });
    EXPECT_FALSE(called);
    EXPECT_TRUE(same.success());
}

// ============================================================================
// 10. 多线程并发安全
// ============================================================================

TEST(ConcurrencyTest, ConcurrentReads)
{
    const Result<int> shared(42);
    constexpr int kThreads    = 16;
    constexpr int kIterations = 1000;

    std::vector<std::future<bool>> futures;
    futures.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        futures.push_back(std::async(std::launch::async, [&]() {
            for (int j = 0; j < kIterations; ++j) {
                if (!shared.success())
                    return false;
                if (shared.status().code != StatusCode::Ok)
                    return false;
                if (shared.value() != 42)
                    return false;
                if (shared.valueOr(0) != 42)
                    return false;
                int v = 0;
                shared.withValue([&](const int& x) { v = x; });
                if (v != 42)
                    return false;
            }
            return true;
        }));
    }

    for (auto& f : futures) {
        EXPECT_TRUE(f.get());
    }
}

TEST(ConcurrencyTest, ConcurrentReadsOnErrorResult)
{
    const Result<int> shared(StatusCode::InternalError, "error");
    constexpr int kThreads    = 8;
    constexpr int kIterations = 1000;

    std::vector<std::future<bool>> futures;
    futures.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        futures.push_back(std::async(std::launch::async, [&]() {
            for (int j = 0; j < kIterations; ++j) {
                if (shared.success())
                    return false;
                if (!shared.error())
                    return false;
                if (shared.status().code != StatusCode::InternalError)
                    return false;
                StatusCode seen = StatusCode::Ok;
                shared.withError([&](const Status& s) { seen = s.code; });
                if (seen != StatusCode::InternalError)
                    return false;
            }
            return true;
        }));
    }

    for (auto& f : futures) {
        EXPECT_TRUE(f.get());
    }
}

TEST(ConcurrencyTest, CopyFromSharedWhileReading)
{
    const Result<int> shared(99);
    std::atomic<bool> stop{false};

    auto reader = std::async(std::launch::async, [&]() {
        while (!stop.load()) {
            EXPECT_TRUE(shared.success());
            EXPECT_EQ(shared.value(), 99);
        }
    });

    std::vector<Result<int>> copies;
    copies.reserve(200);
    for (int i = 0; i < 200; ++i) {
        copies.emplace_back(shared); // 并发拷贝
    }
    for (const auto& c : copies) {
        EXPECT_TRUE(c.success());
        EXPECT_EQ(c.value(), 99);
    }

    stop.store(true);
    reader.get();
}

TEST(ConcurrencyTest, MoveAssignFromTemporary)
{
    Result<std::string> target(StatusCode::Unknown, "init");
    std::atomic<bool> stop{false};

    // 一个线程不断读 target
    auto reader = std::async(std::launch::async, [&]() {
        while (!stop.load()) {
            bool s = target.success();
            if (s) {
                // 成功时取值，不应崩溃
                std::string v;
                target.withValue([&](const std::string& x) { v = x; });
            }
        }
    });

    // 主线程不断移动赋值新值
    for (int i = 0; i < 200; ++i) {
        target = Result<std::string>(std::string("hello_") + std::to_string(i));
        target = Result<std::string>(StatusCode::Aborted, "aborted");
    }

    stop.store(true);
    reader.get();
    SUCCEED();
}
