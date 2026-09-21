#pragma once

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include "../config/AppConfig.h"
#include "../utils/Logger.h"

namespace db {

    // 查询结果行: 列名 -> 值
    using Row = std::map<std::string, std::string>;
    using ResultSet = std::vector<Row>;

    // 数据库异常
    class DatabaseException : public std::runtime_error {
    public:
        explicit DatabaseException(const std::string& msg) : std::runtime_error(msg), errno_(0) {}
        DatabaseException(int errCode, const std::string& msg)
            : std::runtime_error(msg), errno_(errCode) {}

        // MySQL 错误码（如 1062 唯一键冲突、3819/4025 CHECK 冲突），未知时为 0
        int errCode() const { return errno_; }

    private:
        int errno_;
    };

    // 独立数据库连接：每个实例持有自己的 MYSQL 会话，
    // 支持事务，供需要多会话并发的场景使用。
    class Connection {
    public:
        explicit Connection(const config::DatabaseConfig& cfg);
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        // 执行非查询 SQL (INSERT/UPDATE/DELETE)，返回受影响行数
        int execute(const std::string& sql);

        // 执行查询 SQL，返回结果集
        ResultSet query(const std::string& sql);

        // 执行 INSERT 并返回自增 ID
        long long insertAndGetId(const std::string& sql);

        // 转义字符串防 SQL 注入
        std::string escape(const std::string& str);

        // 事务控制
        void begin();
        void commit();
        void rollback();

        bool isConnected() const;
        void close();

    private:
        MYSQL* conn_ = nullptr;
        bool connected_ = false;

        void ensureConnected();
        [[noreturn]] void raiseError(const std::string& prefix);
    };

    class DatabaseManager {
    public:
        static DatabaseManager& instance() {
            static DatabaseManager inst;
            return inst;
        }

        // 初始化连接
        void init(const config::DatabaseConfig& cfg);

        // 关闭连接
        void close();

        // 执行非查询 SQL (INSERT/UPDATE/DELETE)，返回受影响行数
        int execute(const std::string& sql);

        // 执行查询 SQL，返回结果集
        ResultSet query(const std::string& sql);

        // 执行 INSERT 并返回自增 ID
        long long insertAndGetId(const std::string& sql);

        // 转义字符串防 SQL 注入
        std::string escape(const std::string& str);

        // 检查连接是否存活
        bool isConnected() const;

        // 单例内部连接，供默认 DAO 使用
        Connection& connection();

        // 打开一条独立连接（用于需要独立会话/事务的场景，如并发测试）
        static std::unique_ptr<Connection> openConnection(const config::DatabaseConfig& cfg);

        // 事务控制（作用于单例内部连接）
        void begin();
        void commit();
        void rollback();

    private:
        DatabaseManager() = default;
        ~DatabaseManager();

        DatabaseManager(const DatabaseManager&) = delete;
        DatabaseManager& operator=(const DatabaseManager&) = delete;

        std::unique_ptr<Connection> conn_;
    };

} // namespace db
