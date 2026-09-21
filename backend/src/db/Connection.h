#pragma once

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>

#include <string>
#include <memory>
#include "DatabaseManager.h"
#include "../config/AppConfig.h"

namespace db {

    // 独立数据库连接：每个工作线程持有自己的 Connection，
    // 互不共享 MYSQL*，可并发执行事务（复核提交的行级串行化依赖它）。
    class Connection {
    public:
        explicit Connection(const config::DatabaseConfig& cfg);
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        // 执行非查询 SQL，返回受影响行数
        int execute(const std::string& sql);

        // 执行查询 SQL，返回结果集
        ResultSet query(const std::string& sql);

        // 执行 INSERT 并返回自增 ID
        long long insertAndGetId(const std::string& sql);

        // 转义字符串
        std::string escape(const std::string& str);

        // 最近一次错误码（如 1062 唯一键冲突）
        unsigned int lastErrno() const;

        // 事务边界
        void begin();
        void commit();
        void rollback();

    private:
        MYSQL* conn_ = nullptr;
        config::DatabaseConfig config_;

        void ensureConnected();
    };

} // namespace db
