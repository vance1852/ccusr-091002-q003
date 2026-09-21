#include "Connection.h"
#include <cstring>

namespace db {

    static const char* CONN_TAG = "Connection";

    Connection::Connection(const config::DatabaseConfig& cfg) : config_(cfg) {
        conn_ = mysql_init(nullptr);
        if (!conn_) {
            throw DatabaseException("mysql_init() failed: out of memory");
        }

        unsigned int timeout = cfg.connectTimeout;
        mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

        bool reconnectOpt = cfg.autoReconnect;
        mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnectOpt);

        mysql_options(conn_, MYSQL_SET_CHARSET_NAME, cfg.charset.c_str());

        if (!mysql_real_connect(conn_,
                cfg.host.c_str(),
                cfg.user.c_str(),
                cfg.password.c_str(),
                cfg.database.c_str(),
                cfg.port, nullptr, 0)) {
            std::string err = "Connection failed: ";
            err += mysql_error(conn_);
            mysql_close(conn_);
            conn_ = nullptr;
            throw DatabaseException(err);
        }
        LOG_INFO(CONN_TAG, "Connection opened to " + cfg.host + ":" + std::to_string(cfg.port) + "/" + cfg.database);
    }

    Connection::~Connection() {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }

    void Connection::ensureConnected() {
        if (!conn_) {
            throw DatabaseException("Connection has been closed.");
        }
        if (mysql_ping(conn_) != 0) {
            throw DatabaseException(std::string("Connection lost: ") + mysql_error(conn_));
        }
    }

    int Connection::execute(const std::string& sql) {
        ensureConnected();
        LOG_DEBUG(CONN_TAG, "Execute: " + sql);
        if (mysql_query(conn_, sql.c_str()) != 0) {
            std::string err = std::string("Execute failed: ") + mysql_error(conn_);
            LOG_ERROR(CONN_TAG, err);
            throw DatabaseException(err);
        }
        return static_cast<int>(mysql_affected_rows(conn_));
    }

    ResultSet Connection::query(const std::string& sql) {
        ensureConnected();
        LOG_DEBUG(CONN_TAG, "Query: " + sql);
        if (mysql_query(conn_, sql.c_str()) != 0) {
            std::string err = std::string("Query failed: ") + mysql_error(conn_);
            LOG_ERROR(CONN_TAG, err);
            throw DatabaseException(err);
        }

        MYSQL_RES* result = mysql_store_result(conn_);
        if (!result) {
            if (mysql_field_count(conn_) == 0) return {};
            throw DatabaseException(std::string("Store result failed: ") + mysql_error(conn_));
        }

        ResultSet rows;
        int numFields = static_cast<int>(mysql_num_fields(result));
        MYSQL_FIELD* fields = mysql_fetch_fields(result);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            unsigned long* lengths = mysql_fetch_lengths(result);
            Row r;
            for (int i = 0; i < numFields; ++i) {
                r[fields[i].name] = row[i] ? std::string(row[i], lengths[i]) : std::string("");
            }
            rows.push_back(std::move(r));
        }
        mysql_free_result(result);
        return rows;
    }

    long long Connection::insertAndGetId(const std::string& sql) {
        ensureConnected();
        LOG_DEBUG(CONN_TAG, "InsertAndGetId: " + sql);
        if (mysql_query(conn_, sql.c_str()) != 0) {
            std::string err = std::string("Insert failed: ") + mysql_error(conn_);
            LOG_ERROR(CONN_TAG, err);
            throw DatabaseException(err);
        }
        return static_cast<long long>(mysql_insert_id(conn_));
    }

    std::string Connection::escape(const std::string& str) {
        ensureConnected();
        std::vector<char> buf(str.size() * 2 + 1);
        mysql_real_escape_string(conn_, buf.data(), str.c_str(), static_cast<unsigned long>(str.size()));
        return std::string(buf.data());
    }

    unsigned int Connection::lastErrno() const {
        return conn_ ? mysql_errno(conn_) : 0;
    }

    void Connection::begin() {
        execute("START TRANSACTION");
    }

    void Connection::commit() {
        execute("COMMIT");
    }

    void Connection::rollback() {
        execute("ROLLBACK");
    }

} // namespace db
