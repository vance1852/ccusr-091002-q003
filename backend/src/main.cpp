/**
 * 工业检测系统 - C++ MySQL 数据访问层
 * 
 * 演示所有 DAO 的 CRUD 操作
 * 编译环境: Visual Studio 2019+ / CMake 3.16+ / MySQL Connector C
 */

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <iostream>
#include <string>
#include <iomanip>

#include "config/AppConfig.h"
#include "utils/Logger.h"
#include "db/DatabaseManager.h"
#include "db/Connection.h"
#include "dao/SpeedDAO.h"
#include "dao/SpliceDAO.h"
#include "dao/FlawDAO.h"
#include "dao/StopDAO.h"
#include "dao/CompareDAO.h"
#include "dao/HistoryDAO.h"
#include "dao/RemoveDAO.h"
#include "dao/ReviewQueueDAO.h"

#include <thread>
#include <atomic>
#include <chrono>

using namespace std;

// ============================================
// 辅助打印函数
// ============================================
static void printSeparator(const string& title) {
    cout << "\n" << string(60, '=') << endl;
    cout << "  " << title << endl;
    cout << string(60, '=') << endl;
}

static void printResult(const string& operation, bool success) {
    cout << "  [" << (success ? "OK" : "FAIL") << "] " << operation << endl;
}

// ============================================
// 各表 CRUD 演示
// ============================================
static void demoSpeed(dao::SpeedDAO& speedDao) {
    printSeparator("SPEED 速度表 CRUD");

    // INSERT
    entity::Speed s;
    s.value = 120.5f;
    s.date = "2026-02-24";
    s.flag = 0;
    int id = speedDao.insert(s);
    printResult("INSERT speed (id=" + to_string(id) + ")", id > 0);

    // SELECT by ID
    auto found = speedDao.findById(id);
    printResult("SELECT by id=" + to_string(id), found.has_value());
    if (found) {
        cout << "    value=" << found->value << ", date=" << found->date << ", flag=" << found->flag << endl;
    }

    // UPDATE
    int affected = speedDao.updateValue(id, 135.8f);
    printResult("UPDATE value -> 135.8", affected > 0);

    // MARK USED
    affected = speedDao.markUsed(id);
    printResult("MARK USED", affected > 0);

    // COUNT
    int cnt = speedDao.count();
    cout << "  Total records: " << cnt << endl;

    // FIND ALL
    auto all = speedDao.findAll();
    cout << "  FindAll returned " << all.size() << " records" << endl;

    // DELETE
    affected = speedDao.deleteById(id);
    printResult("DELETE id=" + to_string(id), affected > 0);
}

static void demoSplice(dao::SpliceDAO& spliceDao) {
    printSeparator("SPLICE 接缝表 CRUD");

    entity::Splice s;
    s.location = 1500.0f;
    s.distance = 320.5f;
    s.time = "45";
    s.url = "/data/splice/img_001.jpg";
    s.last = 1;
    s.flag = 0;
    s.stop = 0;
    int id = spliceDao.insert(s);
    printResult("INSERT splice (id=" + to_string(id) + ")", id > 0);

    auto found = spliceDao.findById(id);
    printResult("SELECT by id", found.has_value());
    if (found) {
        cout << "    location=" << found->location << ", distance=" << found->distance
             << ", time=" << found->time << endl;
    }

    // 查询有效接头
    auto active = spliceDao.findActive();
    cout << "  Active splices: " << active.size() << endl;

    // 更新标志
    spliceDao.updateFlags(id, 1, 1);
    printResult("UPDATE flags (flag=1, stop=1)", true);

    // 清除 last
    spliceDao.clearAllLast();
    printResult("CLEAR all last flags", true);

    spliceDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoFlaw(dao::FlawDAO& flawDao) {
    printSeparator("FLAW 损伤表 CRUD");

    entity::Flaw f;
    f.category = "crack";
    f.level = 3;
    f.url = "/data/flaw/crack_001.jpg";
    f.camera = 2;
    f.location = 2500.0f;
    f.distance = 180.0f;
    f.size = "15x8mm";
    f.coordinate = "X:120,Y:340";
    f.date = "2026-02-24";
    f.time = 30.5f;
    f.flag = 0;
    f.stop = 0;
    f.epoch = 1;
    long long id = flawDao.insert(f);
    printResult("INSERT flaw (id=" + to_string(id) + ")", id > 0);

    auto found = flawDao.findById(id);
    printResult("SELECT by id", found.has_value());
    if (found) {
        cout << "    category=" << found->category << ", level=" << found->level
             << ", size=" << found->size << ", camera=" << found->camera << endl;
    }

    // 按类型查询
    auto cracks = flawDao.findByCategory("crack");
    cout << "  Cracks found: " << cracks.size() << endl;

    // 更新追踪圈数
    flawDao.updateEpoch(id, 5);
    printResult("UPDATE epoch -> 5", true);

    // 更新停机标志
    flawDao.updateFlags(id, 1, 1);
    printResult("UPDATE flags (flag=1, stop=1)", true);

    flawDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoStop(dao::StopDAO& stopDao) {
    printSeparator("STOP 停机表 CRUD");

    entity::Stop s;
    s.category = 1;
    s.distance = 200.0f;
    s.flag = 1;
    s.command = 0;
    long long id = stopDao.insert(s);
    printResult("INSERT stop (id=" + to_string(id) + ")", id > 0);

    auto found = stopDao.findById(id);
    printResult("SELECT by id", found.has_value());

    // 下发停机命令
    stopDao.issueCommand(id);
    printResult("ISSUE stop command", true);

    // 查询已下发命令的记录
    auto commanded = stopDao.findCommanded();
    cout << "  Commanded stops: " << commanded.size() << endl;

    stopDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoCompare(dao::CompareDAO& compareDao) {
    printSeparator("COMPARE 对比表 CRUD");

    entity::Compare c;
    c.newUrl = "/data/compare/new_001.jpg";
    c.oldUrl = "/data/compare/old_001.jpg";
    c.value = 0.85f;
    c.category = 1;
    c.level = 2;
    c.oldSize = "12x6mm";
    long long id = compareDao.insert(c);
    printResult("INSERT compare (id=" + to_string(id) + ")", id > 0);

    auto found = compareDao.findById(id);
    printResult("SELECT by id", found.has_value());
    if (found) {
        cout << "    value=" << found->value << ", level=" << found->level << endl;
    }

    // 更新
    c.id = id;
    c.value = 0.92f;
    c.level = 3;
    compareDao.update(c);
    printResult("UPDATE value -> 0.92, level -> 3", true);

    compareDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoHistory(dao::HistoryDAO& historyDao) {
    printSeparator("HISTORY 历史表 CRUD");

    entity::History h;
    h.category = "corrosion";
    h.level = 2;
    h.url = "/data/history/corrosion_001.jpg";
    h.camera = 1;
    h.size = "20x15mm";
    h.date = "2026-02-24";
    long long id = historyDao.insert(h);
    printResult("INSERT history (id=" + to_string(id) + ")", id > 0);

    auto found = historyDao.findById(id);
    printResult("SELECT by id", found.has_value());

    auto byCategory = historyDao.findByCategory("corrosion");
    cout << "  Corrosion records: " << byCategory.size() << endl;

    historyDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoRemove(dao::RemoveDAO& removeDao) {
    printSeparator("REMOVE 移除表 CRUD");

    long long id = removeDao.insert();
    printResult("INSERT remove (id=" + to_string(id) + ")", id > 0);

    bool exists = removeDao.exists(id);
    printResult("EXISTS check", exists);

    removeDao.insertWithId(99999);
    printResult("INSERT with specific id=99999", true);

    auto all = removeDao.findAll();
    cout << "  Total remove records: " << all.size() << endl;

    removeDao.deleteById(id);
    removeDao.deleteById(99999);
    printResult("DELETE all test records", true);
}

static void demoReview(dao::FlawDAO& flawDao) {
    printSeparator("REVIEW_QUEUE 复核队列（指派/转派/结论 + 责任轨迹）");

    config::DatabaseConfig cfg;
    cfg.loadFromEnv();
    db::Connection conn(cfg);
    dao::ReviewQueueDAO reviewDao;

    auto buildFlaw = []() {
        entity::Flaw f;
        f.category = "crack"; f.level = 3; f.url = "/data/flaw/night.jpg";
        f.camera = 2; f.location = 2500.0f; f.distance = 180.0f;
        f.size = "15x8mm"; f.coordinate = "X:120,Y:340";
        f.date = "2026-09-21"; f.time = 30.5f; f.flag = 0; f.stop = 0; f.epoch = 1;
        return f;
    };

    // 一条夜班遗留损伤入队
    long long flawId = flawDao.insert(buildFlaw());
    long long reviewId = 0;

    auto en = reviewDao.enlist(conn, flawId);
    reviewId = en.reviewId;
    printResult("ENLIST flaw=" + to_string(flawId) + " -> review=" + to_string(reviewId), en.ok());

    // 同一缺陷重复入队：返回可区分结果
    auto dup = reviewDao.enlist(conn, flawId);
    printResult("DUPLICATE enlist -> " + string(dao::reviewErrorName(dup.code)),
                dup.code == dao::ReviewError::AlreadyEnlisted);

    // 主管首次指派
    auto a1 = reviewDao.assign(conn, reviewId, "sup_zhang", "alice", "");
    printResult("ASSIGN -> alice by sup_zhang", a1.ok());

    // 工程师离岗，主管转派（必须带理由）
    auto noReason = reviewDao.assign(conn, reviewId, "sup_zhang", "bob", "");
    printResult("REASSIGN without reason -> " + string(dao::reviewErrorName(noReason.code)),
                noReason.code == dao::ReviewError::EmptyReason);
    auto a2 = reviewDao.assign(conn, reviewId, "sup_zhang", "bob", "夜班工程师离岗交接");
    printResult("REASSIGN -> bob（带理由）", a2.ok());

    // 旧负责人已失效，只有当前负责人能提交结论
    auto stale = reviewDao.conclude(conn, reviewId, "alice", true, "");
    printResult("STALE owner alice conclude -> " + string(dao::reviewErrorName(stale.code)),
                stale.code == dao::ReviewError::NotOwner);

    // 当前负责人驳回（必须带理由）
    auto rej = reviewDao.conclude(conn, reviewId, "bob", false, "证据不足，需补拍");
    printResult("REJECT by bob（带理由）", rej.ok());

    // 驳回后必须由主管重新指派才能再出结论
    auto pending = reviewDao.conclude(conn, reviewId, "bob", false, "重送驳回");
    printResult("RESEND on REJECTED -> " + string(dao::reviewErrorName(pending.code)),
                pending.code == dao::ReviewError::RejectedPendingReassign);
    auto a3 = reviewDao.assign(conn, reviewId, "sup_li", "carol", "驳回件升级给资深复核");
    printResult("REASSIGN after reject -> carol（带理由）", a3.ok());
    auto ap = reviewDao.conclude(conn, reviewId, "carol", true, "现场复核确认，通过");
    printResult("APPROVE by carol", ap.ok());

    // 客户端重送已形成的结论：可区分，不新增轨迹
    auto resend = reviewDao.conclude(conn, reviewId, "carol", true, "现场复核确认，通过");
    printResult("CLIENT RESEND -> " + string(dao::reviewErrorName(resend.code)),
                resend.code == dao::ReviewError::AlreadyConcluded);

    // 打印完整责任轨迹
    auto log = reviewDao.findActions(reviewId);
    cout << "  Responsibility trail (" << log.size() << " events):" << endl;
    for (auto& e : log) {
        cout << "    seq=" << e.seq << " " << e.action
             << " " << (e.fromAssignee.empty() ? "-" : e.fromAssignee)
             << " -> " << (e.toAssignee.empty() ? "-" : e.toAssignee)
             << " by " << e.oper
             << " [" << (e.fromStatus.empty() ? "-" : e.fromStatus)
             << "=>" << e.toStatus << "]"
             << (e.reason.empty() ? "" : (" reason=" + e.reason))
             << "  at " << e.createdAt << endl;
    }

    // 并发提交实证：三线程同时提交，只允许一个有效结论
    cout << "\n  Concurrent submit proof:" << endl;
    long long f2 = flawDao.insert(buildFlaw());
    auto e2 = reviewDao.enlist(conn, f2);
    reviewDao.assign(conn, e2.reviewId, "sup1", "alice", "");

    atomic<int> ready{0}, okN{0}, dupN{0};
    atomic<bool> go{false};
    vector<thread> ths;
    for (int i = 0; i < 3; ++i) {
        ths.emplace_back([&]() {
            db::Connection c(cfg);
            dao::ReviewQueueDAO d;
            ready.fetch_add(1);
            while (!go.load()) this_thread::sleep_for(chrono::milliseconds(2));
            auto r = d.conclude(c, e2.reviewId, "alice", true, "concurrent");
            if (r.code == dao::ReviewError::Ok) okN.fetch_add(1);
            else if (r.code == dao::ReviewError::AlreadyConcluded) dupN.fetch_add(1);
        });
    }
    while (ready.load() < 3) this_thread::sleep_for(chrono::milliseconds(2));
    go.store(true);
    for (auto& t : ths) t.join();
    cout << "    3 concurrent submits -> OK=" << okN.load()
         << ", ALREADY_CONCLUDED=" << dupN.load() << endl;
    auto final2 = reviewDao.findById(e2.reviewId);
    cout << "    final status=" << final2->status
         << ", concluded_by=" << final2->concludedBy
         << ", version=" << final2->version << endl;
    printResult("Exactly one valid conclusion", okN.load() == 1 && dupN.load() == 2);

    // 清理演示数据（先轨迹、再队列、后损伤，遵守外键）
    db::DatabaseManager::instance().execute("DELETE FROM REVIEW_ACTION_LOG WHERE review_id IN ("
        + to_string(reviewId) + "," + to_string(e2.reviewId) + ")");
    db::DatabaseManager::instance().execute("DELETE FROM REVIEW_QUEUE WHERE id IN ("
        + to_string(reviewId) + "," + to_string(e2.reviewId) + ")");
    flawDao.deleteById(flawId);
    flawDao.deleteById(f2);
}

// ============================================
// 主入口
// ============================================
int main() {
    cout << string(60, '*') << endl;
    cout << "  Industrial Inspection System - C++ MySQL Data Access Layer" << endl;
    cout << "  工业检测系统 - C++ 数据访问层" << endl;
    cout << string(60, '*') << endl;

    try {
        // 1. 初始化日志
        utils::Logger::instance().setLevel(utils::LogLevel::INFO);

        // 2. 加载数据库配置
        config::DatabaseConfig dbConfig;
        dbConfig.loadFromEnv();

        // 3. 连接数据库
        LOG_INFO("Main", "Connecting to database...");
        db::DatabaseManager::instance().init(dbConfig);

        // 4. 初始化 DAO
        dao::SpeedDAO speedDao;
        dao::SpliceDAO spliceDao;
        dao::FlawDAO flawDao;
        dao::StopDAO stopDao;
        dao::CompareDAO compareDao;
        dao::HistoryDAO historyDao;
        dao::RemoveDAO removeDao;

        // 5. 执行各表 CRUD 演示
        demoSpeed(speedDao);
        demoSplice(spliceDao);
        demoFlaw(flawDao);
        demoStop(stopDao);
        demoCompare(compareDao);
        demoHistory(historyDao);
        demoRemove(removeDao);
        demoReview(flawDao);

        // 6. 关闭连接
        db::DatabaseManager::instance().close();

        printSeparator("ALL TESTS COMPLETED SUCCESSFULLY");

    } catch (const db::DatabaseException& e) {
        LOG_ERROR("Main", string("Database error: ") + e.what());
        cerr << "\n[FATAL] Database error: " << e.what() << endl;
        return 1;
    } catch (const exception& e) {
        LOG_ERROR("Main", string("Unexpected error: ") + e.what());
        cerr << "\n[FATAL] Unexpected error: " << e.what() << endl;
        return 2;
    }

    return 0;
}
