#include "DatabaseManager.h"
#include <cstring>

namespace db {

    static const char* TAG = "DatabaseManager";

    // ============================================
    // Connection
    // ============================================

    Connection::Connection(const config::DatabaseConfig& cfg) {
        conn_ = mysql_init(nullptr);
        if (!conn_) {
            throw DatabaseException("mysql_init() failed: out of memory");
        }

        unsigned int timeout = cfg.connectTimeout;
        mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

        bool reconnectOpt = cfg.autoReconnect;
        mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnectOpt);

        mysql_options(conn_, MYSQL_SET_CHARSET_NAME, cfg.charset.c_str());

        if (mysql_real_connect(conn_,
                cfg.host.c_str(),
                cfg.user.c_str(),
                cfg.password.c_str(),
                cfg.database.c_str(),
                cfg.port, nullptr, 0)) {
            connected_ = true;
            LOG_INFO(TAG, "Connection opened to " + cfg.host + ":" + std::to_string(cfg.port) + "/" + cfg.database);
            return;
        }

        int code = mysql_errno(conn_);
        std::string err = "Connection failed: ";
        err += mysql_error(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        throw DatabaseException(code, err);
    }

    Connection::~Connection() {
        close();
    }

    void Connection::close() {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
            connected_ = false;
        }
    }

    void Connection::ensureConnected() {
        if (!conn_ || !connected_) {
            throw DatabaseException("Database not connected.");
        }
        if (mysql_ping(conn_) != 0) {
            throw DatabaseException(mysql_errno(conn_),
                std::string("Connection lost: ") + mysql_error(conn_));
        }
    }

    void Connection::raiseError(const std::string& prefix) {
        int code = mysql_errno(conn_);
        std::string err = prefix + ": " + mysql_error(conn_);
        LOG_ERROR(TAG, err);
        throw DatabaseException(code, err);
    }

    int Connection::execute(const std::string& sql) {
        ensureConnected();
        LOG_DEBUG(TAG, "Execute: " + sql);

        if (mysql_query(conn_, sql.c_str()) != 0) {
            raiseError("Execute failed");
        }

        int affected = static_cast<int>(mysql_affected_rows(conn_));
        LOG_DEBUG(TAG, "Affected rows: " + std::to_string(affected));
        return affected;
    }

    ResultSet Connection::query(const std::string& sql) {
        ensureConnected();
        LOG_DEBUG(TAG, "Query: " + sql);

        if (mysql_query(conn_, sql.c_str()) != 0) {
            raiseError("Query failed");
        }

        MYSQL_RES* result = mysql_store_result(conn_);
        if (!result) {
            if (mysql_field_count(conn_) == 0) {
                return {}; // 非 SELECT 语句
            }
            raiseError("Store result failed");
        }

        ResultSet rows;
        int numFields = mysql_num_fields(result);
        MYSQL_FIELD* fields = mysql_fetch_fields(result);

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            unsigned long* lengths = mysql_fetch_lengths(result);
            Row r;
            for (int i = 0; i < numFields; ++i) {
                std::string colName = fields[i].name;
                std::string colVal = row[i] ? std::string(row[i], lengths[i]) : "";
                r[colName] = colVal;
            }
            rows.push_back(std::move(r));
        }

        mysql_free_result(result);
        LOG_DEBUG(TAG, "Query returned " + std::to_string(rows.size()) + " rows");
        return rows;
    }

    long long Connection::insertAndGetId(const std::string& sql) {
        ensureConnected();
        LOG_DEBUG(TAG, "InsertAndGetId: " + sql);

        if (mysql_query(conn_, sql.c_str()) != 0) {
            raiseError("Insert failed");
        }

        long long id = static_cast<long long>(mysql_insert_id(conn_));
        LOG_DEBUG(TAG, "Inserted ID: " + std::to_string(id));
        return id;
    }

    std::string Connection::escape(const std::string& str) {
        ensureConnected();
        std::vector<char> buf(str.size() * 2 + 1);
        mysql_real_escape_string(conn_, buf.data(), str.c_str(), static_cast<unsigned long>(str.size()));
        return std::string(buf.data());
    }

    void Connection::begin()    { execute("START TRANSACTION"); }
    void Connection::commit()   { execute("COMMIT"); }
    void Connection::rollback() { execute("ROLLBACK"); }

    bool Connection::isConnected() const {
        return connected_ && conn_ != nullptr;
    }

    // ============================================
    // DatabaseManager
    // ============================================

    void DatabaseManager::init(const config::DatabaseConfig& cfg) {
        conn_ = std::make_unique<Connection>(cfg);
    }

    void DatabaseManager::close() {
        if (conn_) {
            conn_->close();
            conn_.reset();
            LOG_INFO(TAG, "Database connection closed");
        }
    }

    DatabaseManager::~DatabaseManager() {
        close();
    }

    Connection& DatabaseManager::connection() {
        if (!conn_) {
            throw DatabaseException("Database not connected. Call init() first.");
        }
        return *conn_;
    }

    std::unique_ptr<Connection> DatabaseManager::openConnection(const config::DatabaseConfig& cfg) {
        return std::make_unique<Connection>(cfg);
    }

    int DatabaseManager::execute(const std::string& sql) { return connection().execute(sql); }
    ResultSet DatabaseManager::query(const std::string& sql) { return connection().query(sql); }
    long long DatabaseManager::insertAndGetId(const std::string& sql) { return connection().insertAndGetId(sql); }
    std::string DatabaseManager::escape(const std::string& str) { return connection().escape(str); }

    bool DatabaseManager::isConnected() const {
        return conn_ && conn_->isConnected();
    }

    void DatabaseManager::begin()    { connection().begin(); }
    void DatabaseManager::commit()   { connection().commit(); }
    void DatabaseManager::rollback() { connection().rollback(); }

} // namespace db
