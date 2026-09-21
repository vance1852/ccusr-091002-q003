#pragma once

#include "BaseDAO.h"
#include "../db/Connection.h"
#include "../entity/ReviewQueue.h"
#include "../entity/ReviewActionLog.h"
#include <vector>
#include <optional>

namespace dao {

    // 可区分的业务结果
    enum class ReviewError {
        Ok = 0,
        FlawNotFound,            // 入队时 FLAW 不存在
        ReviewNotFound,          // 复核单不存在
        AlreadyEnlisted,         // 该损伤已在队列（重复入队被唯一键拦截）
        InvalidAssignee,         // 指派目标负责人为空
        EmptyReason,             // 转派/驳回缺少理由
        NotInAssignableState,    // 已通过等不可指派状态
        NotAssigned,             // 尚未指派负责人，无人可提交结论
        RejectedPendingReassign, // 已驳回，等待主管重新指派后才能再出结论
        NotOwner,                // 负责人失效：操作者不是当前负责人
        AlreadyConcluded         // 客户端重送结论：该单已形成有效结论
    };

    inline const char* reviewErrorName(ReviewError e) {
        switch (e) {
            case ReviewError::Ok:                       return "OK";
            case ReviewError::FlawNotFound:             return "FLAW_NOT_FOUND";
            case ReviewError::ReviewNotFound:           return "REVIEW_NOT_FOUND";
            case ReviewError::AlreadyEnlisted:          return "ALREADY_ENLISTED";
            case ReviewError::InvalidAssignee:          return "INVALID_ASSIGNEE";
            case ReviewError::EmptyReason:              return "EMPTY_REASON";
            case ReviewError::NotInAssignableState:     return "NOT_IN_ASSIGNABLE_STATE";
            case ReviewError::NotAssigned:              return "NOT_ASSIGNED";
            case ReviewError::RejectedPendingReassign:  return "REJECTED_PENDING_REASSIGN";
            case ReviewError::NotOwner:                 return "NOT_OWNER";
            case ReviewError::AlreadyConcluded:         return "ALREADY_CONCLUDED";
        }
        return "UNKNOWN";
    }

    // 变更类操作的统一返回
    struct ReviewResult {
        ReviewError code = ReviewError::Ok;
        long long reviewId = 0;   // 命中的复核单 id（即便失败也尽量回填）
        int seq = 0;              // 新追加的轨迹序号（查询类为 0）

        bool ok() const { return code == ReviewError::Ok; }
    };

    // 负责人过滤范围
    enum class AssigneeScope {
        Any,         // 不限
        Unassigned,  // 仅无人负责（PENDING）
        Named        // 指定负责人
    };

    // 主管翻页查询条件
    struct ReviewFilter {
        int level = -1;                       // -1 不按级别过滤
        int camera = -1;                      // -1 不按摄像头过滤
        std::string status;                   // 空串不限状态
        AssigneeScope assigneeScope = AssigneeScope::Any;
        std::string assignee;                 // assigneeScope == Named 时生效
        long long afterId = 0;                // keyset 游标：上一页最后一条 q.id
        int limit = 20;
    };

    // 队列行 + 关联 FLAW 的展示字段
    struct ReviewQueueItem {
        entity::ReviewQueue queue;
        std::string flawCategory;
        int flawLevel = 0;
        int flawCamera = 0;
    };

    class ReviewQueueDAO : public BaseDAO {
    public:
        // ----------------------------------------------------------------
        // 变更类操作：调用方传入自己的 Connection（多线程并发安全），
        // 方法内部以事务包裹 SELECT ... FOR UPDATE + UPDATE + 轨迹追加
        // ----------------------------------------------------------------

        // 入队：FLAW 之上建立复核单；重复入队返回 AlreadyEnlisted 且回填既有 id
        ReviewResult enlist(db::Connection& conn, long long flawId) {
            ReviewResult r;
            r.reviewId = flawId;
            if (flawId <= 0) { r.code = ReviewError::FlawNotFound; return r; }

            try {
                conn.begin();
                auto flaw = conn.query("SELECT id FROM FLAW WHERE id = " + lltos(flawId) + " FOR UPDATE");
                if (flaw.empty()) { conn.rollback(); r.code = ReviewError::FlawNotFound; return r; }

                auto exist = conn.query("SELECT id FROM REVIEW_QUEUE WHERE flaw_id = " + lltos(flawId));
                if (!exist.empty()) {
                    conn.rollback();
                    r.code = ReviewError::AlreadyEnlisted;
                    r.reviewId = std::stoll(exist[0].at("id"));
                    return r;
                }

                std::string sql = "INSERT INTO REVIEW_QUEUE (flaw_id, status) VALUES ("
                    + lltos(flawId) + ", 'PENDING')";
                try {
                    r.reviewId = conn.insertAndGetId(sql);
                } catch (const db::DatabaseException&) {
                    // 并发入队撞 flaw_id 唯一键：缺陷只允许一条复核单
                    if (conn.lastErrno() == 1062) {
                        auto again = conn.query("SELECT id FROM REVIEW_QUEUE WHERE flaw_id = " + lltos(flawId));
                        conn.rollback();
                        r.code = ReviewError::AlreadyEnlisted;
                        if (!again.empty()) r.reviewId = std::stoll(again[0].at("id"));
                        return r;
                    }
                    throw;
                }
                conn.commit();
                LOG_INFO("ReviewQueueDAO", "Enlist flaw=" + lltos(flawId) + " review=" + lltos(r.reviewId));
                return r;
            } catch (const db::DatabaseException&) {
                conn.rollback();
                throw;
            }
        }

        // 指派/转派：supervisor 为主管，toAssignee 为新负责人，reason 仅转派时必填
        ReviewResult assign(db::Connection& conn, long long reviewId,
                            const std::string& supervisor,
                            const std::string& toAssignee,
                            const std::string& reason) {
            ReviewResult r;
            r.reviewId = reviewId;
            if (toAssignee.empty()) { r.code = ReviewError::InvalidAssignee; return r; }
            if (supervisor.empty()) { r.code = ReviewError::InvalidAssignee; return r; }

            try {
                conn.begin();
                auto rows = conn.query("SELECT * FROM REVIEW_QUEUE WHERE id = " + lltos(reviewId)
                                       + " FOR UPDATE");
                if (rows.empty()) { conn.rollback(); r.code = ReviewError::ReviewNotFound; return r; }

                std::string curStatus = getVal(rows[0], "status");
                std::string curAssignee = getVal(rows[0], "assignee");

                // APPROVED 终态不可再指派
                if (curStatus == entity::REVIEW_APPROVED) {
                    conn.rollback();
                    r.code = ReviewError::NotInAssignableState;
                    return r;
                }

                std::string action;
                if (curStatus == entity::REVIEW_PENDING) {
                    action = entity::REVIEW_ACT_ASSIGN;
                } else {
                    // ASSIGNED 或 REJECTED 上的再次指派均为转派，必须给理由
                    action = entity::REVIEW_ACT_REASSIGN;
                    if (reason.empty() || trimSpaces(reason).empty()) {
                        conn.rollback();
                        r.code = ReviewError::EmptyReason;
                        return r;
                    }
                }

                // 幂等：已在该负责人名下且为 ASSIGNED，主管重复点击不产生新轨迹
                if (curStatus == entity::REVIEW_ASSIGNED && curAssignee == toAssignee) {
                    conn.rollback();
                    return r; // Ok
                }

                std::string newStatus = entity::REVIEW_ASSIGNED;
                conn.execute(
                    "UPDATE REVIEW_QUEUE SET status = " + q(newStatus)
                    + ", assignee = " + q(conn.escape(toAssignee))
                    + ", assigned_by = " + q(conn.escape(supervisor))
                    + ", assigned_at = NOW(3)"
                    + ", version = version + 1"
                    + " WHERE id = " + lltos(reviewId));

                int seq = nextSeq(conn, reviewId);
                conn.execute(buildLogSql(conn, reviewId, flawIdOf(rows[0]), seq, action,
                                         curStatus, newStatus, curAssignee, toAssignee,
                                         supervisor, reason));
                conn.commit();
                r.seq = seq;
                LOG_INFO("ReviewQueueDAO", "Assign review=" + lltos(reviewId)
                         + " action=" + action + " to=" + toAssignee + " by=" + supervisor);
                return r;
            } catch (const db::DatabaseException&) {
                conn.rollback();
                throw;
            }
        }

        // 提交结论：只有当前负责人能提交；approve=true 通过，false 驳回（note 必填）
        ReviewResult conclude(db::Connection& conn, long long reviewId,
                              const std::string& operatorUser,
                              bool approve, const std::string& note) {
            ReviewResult r;
            r.reviewId = reviewId;
            if (operatorUser.empty()) { r.code = ReviewError::NotOwner; return r; }

            try {
                conn.begin();
                auto rows = conn.query("SELECT * FROM REVIEW_QUEUE WHERE id = " + lltos(reviewId)
                                       + " FOR UPDATE");
                if (rows.empty()) { conn.rollback(); r.code = ReviewError::ReviewNotFound; return r; }

                std::string curStatus = getVal(rows[0], "status");
                std::string curAssignee = getVal(rows[0], "assignee");
                long long flawId = std::stoll(getVal(rows[0], "flaw_id"));

                // 行锁拿到后复查：已形成的结论不可被客户端重送覆盖
                if (curStatus == entity::REVIEW_APPROVED) {
                    conn.rollback();
                    r.code = ReviewError::AlreadyConcluded;
                    return r;
                }
                // 驳回后必须经主管重新指派才能再出结论
                if (curStatus == entity::REVIEW_REJECTED) {
                    conn.rollback();
                    r.code = ReviewError::RejectedPendingReassign;
                    return r;
                }
                if (curStatus == entity::REVIEW_PENDING) {
                    conn.rollback();
                    r.code = ReviewError::NotAssigned;
                    return r;
                }
                // ASSIGNED：仅当前负责人可提交，其他人提交即“负责人失效”
                if (curAssignee != operatorUser) {
                    conn.rollback();
                    r.code = ReviewError::NotOwner;
                    return r;
                }
                if (!approve && (note.empty() || trimSpaces(note).empty())) {
                    conn.rollback();
                    r.code = ReviewError::EmptyReason;
                    return r;
                }

                std::string newStatus = approve ? entity::REVIEW_APPROVED : entity::REVIEW_REJECTED;
                std::string action = approve ? entity::REVIEW_ACT_APPROVE : entity::REVIEW_ACT_REJECT;
                conn.execute(
                    "UPDATE REVIEW_QUEUE SET status = " + q(newStatus)
                    + ", concluded_by = " + q(conn.escape(operatorUser))
                    + ", concluded_at = NOW(3)"
                    + ", conclusion_note = " + q(conn.escape(note))
                    + ", version = version + 1"
                    + " WHERE id = " + lltos(reviewId));

                int seq = nextSeq(conn, reviewId);
                conn.execute(buildLogSql(conn, reviewId, flawId, seq, action,
                                         curStatus, newStatus, curAssignee, "",
                                         operatorUser, note));
                conn.commit();
                r.seq = seq;
                LOG_INFO("ReviewQueueDAO", "Conclude review=" + lltos(reviewId)
                         + " action=" + action + " by=" + operatorUser);
                return r;
            } catch (const db::DatabaseException&) {
                conn.rollback();
                throw;
            }
        }

        // ----------------------------------------------------------------
        // 查询：沿用单例连接
        // ----------------------------------------------------------------

        std::optional<entity::ReviewQueue> findById(long long id) {
            auto rows = db().query("SELECT * FROM REVIEW_QUEUE WHERE id = " + lltos(id));
            if (rows.empty()) return std::nullopt;
            return mapQueue(rows[0]);
        }

        std::optional<entity::ReviewQueue> findByFlawId(long long flawId) {
            auto rows = db().query("SELECT * FROM REVIEW_QUEUE WHERE flaw_id = " + lltos(flawId));
            if (rows.empty()) return std::nullopt;
            return mapQueue(rows[0]);
        }

        // 责任轨迹：按单内序号严格升序
        std::vector<entity::ReviewActionLog> findActions(long long reviewId) {
            auto rows = db().query("SELECT * FROM REVIEW_ACTION_LOG WHERE review_id = "
                                   + lltos(reviewId) + " ORDER BY seq ASC");
            std::vector<entity::ReviewActionLog> out;
            out.reserve(rows.size());
            for (auto& r : rows) out.push_back(mapLog(r));
            return out;
        }

        // keyset 翻页：以 q.id 为稳定游标，并发插入也不漏不重
        std::vector<ReviewQueueItem> queryPage(const ReviewFilter& f) {
            std::string sql =
                "SELECT q.*, f.category AS flaw_category, f.level AS flaw_level, f.camera AS flaw_camera"
                " FROM REVIEW_QUEUE q INNER JOIN FLAW f ON f.id = q.flaw_id"
                " WHERE q.id > " + lltos(f.afterId);
            if (f.level >= 0)  sql += " AND f.level = " + itos(f.level);
            if (f.camera >= 0) sql += " AND f.camera = " + itos(f.camera);
            if (!f.status.empty()) sql += " AND q.status = " + esc(f.status);
            if (f.assigneeScope == AssigneeScope::Unassigned) {
                sql += " AND q.assignee IS NULL";
            } else if (f.assigneeScope == AssigneeScope::Named) {
                sql += " AND q.assignee = " + esc(f.assignee);
            }
            int lim = f.limit <= 0 ? 20 : (f.limit > 500 ? 500 : f.limit);
            sql += " ORDER BY q.id ASC LIMIT " + itos(lim);

            auto rows = db().query(sql);
            std::vector<ReviewQueueItem> out;
            out.reserve(rows.size());
            for (auto& r : rows) {
                ReviewQueueItem item;
                item.queue = mapQueue(r);
                item.flawCategory = getVal(r, "flaw_category");
                item.flawLevel = getInt(r, "flaw_level");
                item.flawCamera = getInt(r, "flaw_camera");
                out.push_back(std::move(item));
            }
            return out;
        }

        long long countByStatus(const std::string& status) {
            auto rows = db().query("SELECT COUNT(*) AS cnt FROM REVIEW_QUEUE WHERE status = " + esc(status));
            return rows.empty() ? 0 : std::stoll(rows[0].at("cnt"));
        }

    private:
        // 生成下一轨迹序号；调用前队列行已 FOR UPDATE 锁定，同单变更串行
        static int nextSeq(db::Connection& conn, long long reviewId) {
            auto rows = conn.query("SELECT COALESCE(MAX(seq), 0) + 1 AS next_seq"
                                   " FROM REVIEW_ACTION_LOG WHERE review_id = " + lltos(reviewId));
            return std::stoi(rows[0].at("next_seq"));
        }

        static long long flawIdOf(const db::Row& row) {
            return std::stoll(row.at("flaw_id"));
        }

        // 拼一条轨迹 INSERT（所有字符串经连接转义）
        std::string buildLogSql(db::Connection& conn,
                                long long reviewId, long long flawId, int seq,
                                const std::string& action,
                                const std::string& fromStatus, const std::string& toStatus,
                                const std::string& fromAssignee, const std::string& toAssignee,
                                const std::string& oper, const std::string& reason) {
            auto lit = [&conn](const std::string& v) -> std::string {
                return v.empty() ? "NULL" : ("'" + conn.escape(v) + "'");
            };
            return
                "INSERT INTO REVIEW_ACTION_LOG"
                " (review_id, flaw_id, seq, action, from_status, to_status,"
                "  from_assignee, to_assignee, operator, reason)"
                " VALUES (" + lltos(reviewId) + ", " + lltos(flawId) + ", " + itos(seq)
                + ", '" + conn.escape(action) + "'"
                + ", " + lit(fromStatus)
                + ", '" + conn.escape(toStatus) + "'"
                + ", " + lit(fromAssignee)
                + ", " + lit(toAssignee)
                + ", '" + conn.escape(oper) + "'"
                + ", " + lit(reason)
                + ")";
        }

        static std::string trimSpaces(const std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        // 字符串字面量（值已转义）
        static std::string q(const std::string& escaped) { return "'" + escaped + "'"; }

        entity::ReviewQueue mapQueue(const db::Row& r) {
            entity::ReviewQueue qq;
            qq.id = getLong(r, "id");
            qq.flawId = getLong(r, "flaw_id");
            qq.status = getVal(r, "status");
            qq.assignee = getVal(r, "assignee");
            qq.assignedBy = getVal(r, "assigned_by");
            qq.assignedAt = getVal(r, "assigned_at");
            qq.concludedBy = getVal(r, "concluded_by");
            qq.concludedAt = getVal(r, "concluded_at");
            qq.conclusionNote = getVal(r, "conclusion_note");
            qq.createdAt = getVal(r, "created_at");
            qq.updatedAt = getVal(r, "updated_at");
            qq.version = getInt(r, "version");
            return qq;
        }

        entity::ReviewActionLog mapLog(const db::Row& r) {
            entity::ReviewActionLog l;
            l.id = getLong(r, "id");
            l.reviewId = getLong(r, "review_id");
            l.flawId = getLong(r, "flaw_id");
            l.seq = getInt(r, "seq");
            l.action = getVal(r, "action");
            l.fromStatus = getVal(r, "from_status");
            l.toStatus = getVal(r, "to_status");
            l.fromAssignee = getVal(r, "from_assignee");
            l.toAssignee = getVal(r, "to_assignee");
            l.oper = getVal(r, "operator");
            l.reason = getVal(r, "reason");
            l.createdAt = getVal(r, "created_at");
            return l;
        }
    };

} // namespace dao
