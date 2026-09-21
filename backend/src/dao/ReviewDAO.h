#pragma once

#include "BaseDAO.h"
#include "../entity/Review.h"
#include <vector>
#include <optional>
#include <stdexcept>
#include <string>
#include <ostream>

namespace dao {

    // 入队结果
    enum class EnqueueResult {
        OK,                 // 成功入队
        FLAW_NOT_FOUND,     // 损伤记录不存在
        ALREADY_QUEUED      // 该损伤已在复核队列中
    };

    struct EnqueueOutcome {
        EnqueueResult result = EnqueueResult::OK;
        long long taskId = 0;
    };

    // 指派/转派结果
    enum class AssignResult {
        OK,                     // 成功
        TASK_NOT_FOUND,         // 复核任务不存在
        INVALID_TRANSITION      // 当前状态不允许该操作（如已结论后再转派）
    };

    // 结论提交结果（负责人失效与客户端重送可区分）
    enum class DecisionResult {
        SUBMITTED,          // 结论被接受（唯一一次有效提交）
        TASK_NOT_FOUND,     // 复核任务不存在
        NOT_OWNER,          // 提交人不是当前负责人（离岗、被转派或尚未指派）
        ALREADY_DECIDED     // 任务已有结论（客户端重送）
    };

    struct DecisionOutcome {
        DecisionResult result = DecisionResult::SUBMITTED;
        long long taskId = 0;
    };

    inline std::ostream& operator<<(std::ostream& os, EnqueueResult r) {
        switch (r) {
            case EnqueueResult::OK: return os << "OK";
            case EnqueueResult::FLAW_NOT_FOUND: return os << "FLAW_NOT_FOUND";
            case EnqueueResult::ALREADY_QUEUED: return os << "ALREADY_QUEUED";
        }
        return os << "EnqueueResult(" << static_cast<int>(r) << ")";
    }

    inline std::ostream& operator<<(std::ostream& os, AssignResult r) {
        switch (r) {
            case AssignResult::OK: return os << "OK";
            case AssignResult::TASK_NOT_FOUND: return os << "TASK_NOT_FOUND";
            case AssignResult::INVALID_TRANSITION: return os << "INVALID_TRANSITION";
        }
        return os << "AssignResult(" << static_cast<int>(r) << ")";
    }

    inline std::ostream& operator<<(std::ostream& os, DecisionResult r) {
        switch (r) {
            case DecisionResult::SUBMITTED: return os << "SUBMITTED";
            case DecisionResult::TASK_NOT_FOUND: return os << "TASK_NOT_FOUND";
            case DecisionResult::NOT_OWNER: return os << "NOT_OWNER";
            case DecisionResult::ALREADY_DECIDED: return os << "ALREADY_DECIDED";
        }
        return os << "DecisionResult(" << static_cast<int>(r) << ")";
    }

    // 队列列表项：复核任务 + 关联损伤的展示字段
    struct ReviewQueueItem {
        entity::ReviewTask task;
        std::string category;    // 来自 FLAW.category
        std::string flawDate;    // 来自 FLAW.date
    };

    struct ReviewPage {
        std::vector<ReviewQueueItem> items;
        long long nextCursor = 0;   // 下一页游标（本页最后一条任务ID），0 表示无更多
        bool hasMore = false;
    };

    class ReviewDAO : public BaseDAO {
    public:
        ReviewDAO() = default;
        explicit ReviewDAO(db::Connection* conn) : BaseDAO(conn) {}

        // ============================================
        // 入队：一条损伤至多一条复核任务
        // ============================================
        EnqueueOutcome enqueue(long long flawId, const std::string& actor = "system") {
            EnqueueOutcome out;

            db().begin();
            try {
                // 事务内读取损伤属性，避免入队与损伤删除/修改交错
                auto flawRows = db().query("SELECT level, camera FROM FLAW WHERE id = "
                                           + lltos(flawId) + " LOCK IN SHARE MODE");
                if (flawRows.empty()) {
                    db().rollback();
                    out.result = EnqueueResult::FLAW_NOT_FOUND;
                    return out;
                }
                int level = getInt(flawRows[0], "level");
                int camera = getInt(flawRows[0], "camera");

                long long taskId = 0;
                try {
                    taskId = db().insertAndGetId(
                        "INSERT INTO REVIEW_TASK (flaw_id, level, camera, status) VALUES ("
                        + lltos(flawId) + ", " + itos(level) + ", " + itos(camera) + ", 'PENDING')");
                } catch (const db::DatabaseException& e) {
                    db().rollback();
                    if (e.errCode() == 1062) {            // uk_review_task_flaw
                        out.result = EnqueueResult::ALREADY_QUEUED;
                        return out;
                    }
                    if (e.errCode() == 1452) {            // fk_rt_flaw（并发删除损伤的兜底）
                        out.result = EnqueueResult::FLAW_NOT_FOUND;
                        return out;
                    }
                    throw;
                }
                insertEvent(taskId, flawId, entity::reviewevent::CREATE,
                            actor, "", "", "");
                db().commit();
                out.result = EnqueueResult::OK;
                out.taskId = taskId;
                return out;
            } catch (...) {
                db().rollback();
                throw;
            }
        }

        // ============================================
        // 指派：待复核 -> 某负责人；操作者与时间落库
        // ============================================
        AssignResult assign(long long taskId, const std::string& supervisor,
                            const std::string& assignee) {
            if (assignee.empty()) {
                throw std::invalid_argument("assignee must not be empty");
            }
            return changeAssignee(taskId, supervisor, assignee,
                                  entity::reviewevent::ASSIGN, "");
        }

        // ============================================
        // 转派：已指派 -> 新负责人，理由必填
        // ============================================
        AssignResult transfer(long long taskId, const std::string& actor,
                              const std::string& toAssignee, const std::string& reason) {
            if (toAssignee.empty()) {
                throw std::invalid_argument("toAssignee must not be empty");
            }
            if (reason.find_first_not_of(" \t\r\n") == std::string::npos) {
                throw std::invalid_argument("transfer reason must not be empty");
            }
            return changeAssignee(taskId, actor, toAssignee,
                                  entity::reviewevent::TRANSFER, reason);
        }

        // ============================================
        // 提交结论：只有当前负责人能提交；通过/驳回
        // ============================================
        DecisionOutcome decide(long long taskId, const std::string& assignee,
                               bool approved, const std::string& reason = "") {
            const char* newStatus = approved ? "APPROVED" : "REJECTED";
            const char* eventType = approved ? entity::reviewevent::APPROVE
                                             : entity::reviewevent::REJECT;
            if (!approved && reason.find_first_not_of(" \t\r\n") == std::string::npos) {
                throw std::invalid_argument("reject reason must not be empty");
            }

            DecisionOutcome out;
            out.taskId = taskId;

            db().begin();
            try {
                // 条件更新：行锁保证并发提交只会有一个生效
                std::string sql =
                    "UPDATE REVIEW_TASK SET status = " + esc(newStatus)
                    + ", decision_by = " + esc(assignee)
                    + ", decision_at = NOW(3), version = version + 1"
                    + " WHERE id = " + lltos(taskId)
                    + " AND status = 'ASSIGNED' AND assignee = " + esc(assignee);
                int affected = db().execute(sql);

                if (affected == 1) {
                    auto taskRows = db().query(
                        "SELECT flaw_id FROM REVIEW_TASK WHERE id = " + lltos(taskId));
                    long long flawId = getLong(taskRows[0], "flaw_id");
                    insertEvent(taskId, flawId, eventType, assignee,
                                assignee, assignee, reason);
                    db().commit();
                    out.result = DecisionResult::SUBMITTED;
                    return out;
                }

                db().rollback();

                // 零行更新：区分任务不存在 / 负责人失效 / 已有结论
                auto cur = db().query(
                    "SELECT status, assignee FROM REVIEW_TASK WHERE id = " + lltos(taskId));
                if (cur.empty()) {
                    out.result = DecisionResult::TASK_NOT_FOUND;
                } else {
                    std::string status = getVal(cur[0], "status");
                    if (status == entity::reviewstatus::APPROVED ||
                        status == entity::reviewstatus::REJECTED) {
                        out.result = DecisionResult::ALREADY_DECIDED;
                    } else {
                        // PENDING 未指派，或 ASSIGNED 但负责人已变更（离岗/被转派）
                        out.result = DecisionResult::NOT_OWNER;
                    }
                }
                return out;
            } catch (...) {
                db().rollback();
                throw;
            }
        }

        // ============================================
        // 查询
        // ============================================
        std::optional<entity::ReviewTask> findById(long long id) {
            auto rows = db().query("SELECT * FROM REVIEW_TASK WHERE id = " + lltos(id));
            if (rows.empty()) return std::nullopt;
            return mapTask(rows[0]);
        }

        std::optional<entity::ReviewTask> findByFlawId(long long flawId) {
            auto rows = db().query("SELECT * FROM REVIEW_TASK WHERE flaw_id = " + lltos(flawId));
            if (rows.empty()) return std::nullopt;
            return mapTask(rows[0]);
        }

        // 某条任务的完整责任轨迹
        std::vector<entity::ReviewEvent> findEvents(long long taskId) {
            return mapEvents(db().query(
                "SELECT * FROM REVIEW_EVENT WHERE task_id = " + lltos(taskId) + " ORDER BY id ASC"));
        }

        // 按损伤直查轨迹
        std::vector<entity::ReviewEvent> findEventsByFlaw(long long flawId) {
            return mapEvents(db().query(
                "SELECT * FROM REVIEW_EVENT WHERE flaw_id = " + lltos(flawId) + " ORDER BY id ASC"));
        }

        // 主管翻页：按负责人（可叠加状态过滤），游标为上一页最后一条任务ID
        ReviewPage pageByAssignee(const std::string& assignee,
                                  const std::string& statusFilter = "",
                                  long long cursor = 0, int limit = 20) {
            std::string where = "WHERE q.assignee = " + esc(assignee);
            if (!statusFilter.empty()) where += " AND q.status = " + esc(statusFilter);
            return paginate(where, cursor, limit);
        }

        // 主管翻页：按损伤级别
        ReviewPage pageByLevel(int level, const std::string& statusFilter = "",
                               long long cursor = 0, int limit = 20) {
            std::string where = "WHERE q.level = " + itos(level);
            if (!statusFilter.empty()) where += " AND q.status = " + esc(statusFilter);
            return paginate(where, cursor, limit);
        }

        // 主管翻页：按摄像头
        ReviewPage pageByCamera(int camera, const std::string& statusFilter = "",
                                long long cursor = 0, int limit = 20) {
            std::string where = "WHERE q.camera = " + itos(camera);
            if (!statusFilter.empty()) where += " AND q.status = " + esc(statusFilter);
            return paginate(where, cursor, limit);
        }

        // 主管翻页：全量（可叠加状态过滤）
        ReviewPage pageAll(const std::string& statusFilter = "",
                           long long cursor = 0, int limit = 20) {
            std::string where;
            if (!statusFilter.empty()) where = "WHERE q.status = " + esc(statusFilter);
            return paginate(where, cursor, limit);
        }

        int countByStatus(const std::string& status) {
            auto rows = db().query(
                "SELECT COUNT(*) AS cnt FROM REVIEW_TASK WHERE status = " + esc(status));
            return rows.empty() ? 0 : getInt(rows[0], "cnt");
        }

    private:
        // 指派 / 转派的共同实现，区别只在事件类型与理由
        AssignResult changeAssignee(long long taskId, const std::string& actor,
                                    const std::string& toAssignee,
                                    const char* eventType, const std::string& reason) {
            db().begin();
            try {
                auto rows = db().query(
                    "SELECT flaw_id, status, assignee FROM REVIEW_TASK WHERE id = "
                    + lltos(taskId) + " FOR UPDATE");
                if (rows.empty()) {
                    db().rollback();
                    return AssignResult::TASK_NOT_FOUND;
                }
                long long flawId = getLong(rows[0], "flaw_id");
                std::string status = getVal(rows[0], "status");
                std::string from = getVal(rows[0], "assignee");

                bool valid = false;
                if (eventType == std::string(entity::reviewevent::ASSIGN)) {
                    // 首次指派只能发生在待复核任务上；已指派后换人必须走转派（强制理由）
                    valid = (status == entity::reviewstatus::PENDING);
                } else { // TRANSFER 只允许在已指派的活动任务上发生
                    valid = (status == entity::reviewstatus::ASSIGNED && !from.empty());
                }
                if (!valid) {
                    db().rollback();
                    return AssignResult::INVALID_TRANSITION;
                }

                int affected = db().execute(
                    "UPDATE REVIEW_TASK SET status = 'ASSIGNED', assignee = " + esc(toAssignee)
                    + ", assigned_by = " + esc(actor)
                    + ", assigned_at = NOW(3), version = version + 1"
                    + " WHERE id = " + lltos(taskId));
                if (affected != 1) {
                    db().rollback();
                    return AssignResult::INVALID_TRANSITION;
                }

                insertEvent(taskId, flawId, eventType, actor, from, toAssignee, reason);
                db().commit();
                return AssignResult::OK;
            } catch (...) {
                db().rollback();
                throw;
            }
        }

        void insertEvent(long long taskId, long long flawId, const char* eventType,
                         const std::string& actor, const std::string& from,
                         const std::string& to, const std::string& reason) {
            std::string sql =
                "INSERT INTO REVIEW_EVENT (task_id, flaw_id, event_type, actor, from_assignee, "
                "to_assignee, reason) VALUES ("
                + lltos(taskId) + ", " + lltos(flawId) + ", " + esc(eventType) + ", "
                + esc(actor) + ", " + nullOrQuote(from) + ", " + nullOrQuote(to) + ", "
                + esc(reason) + ")";
            db().execute(sql);
        }

        static std::string nullOrQuote(const std::string& v) {
            return v.empty() ? "NULL" : ("'" + v + "'");
        }

        // 键集分页：严格以任务ID为游标，DESC 顺序天然不漏不重
        ReviewPage paginate(const std::string& filter, long long cursor, int limit) {
            std::string where = filter;
            if (cursor > 0) {
                where += where.empty() ? "WHERE q.id < " : " AND q.id < ";
                where += lltos(cursor);
            }
            std::string sql =
                "SELECT q.*, f.category AS flaw_category, f.date AS flaw_date"
                " FROM REVIEW_TASK q JOIN FLAW f ON f.id = q.flaw_id "
                + where
                + " ORDER BY q.id DESC LIMIT " + std::to_string(limit + 1);
            auto rows = db().query(sql);

            ReviewPage page;
            page.hasMore = static_cast<int>(rows.size()) > limit;
            int take = page.hasMore ? limit : static_cast<int>(rows.size());
            for (int i = 0; i < take; ++i) {
                ReviewQueueItem item;
                item.task = mapTask(rows[i]);
                item.category = getVal(rows[i], "flaw_category");
                item.flawDate = getVal(rows[i], "flaw_date");
                page.items.push_back(std::move(item));
            }
            if (!page.items.empty()) {
                page.nextCursor = page.items.back().task.id;
            }
            return page;
        }

        static entity::ReviewTask mapTask(const db::Row& row) {
            entity::ReviewTask t;
            t.id = getLong(row, "id");
            t.flawId = getLong(row, "flaw_id");
            t.level = getInt(row, "level");
            t.camera = getInt(row, "camera");
            t.status = getVal(row, "status");
            t.assignee = getVal(row, "assignee");
            t.assignedBy = getVal(row, "assigned_by");
            t.assignedAt = getVal(row, "assigned_at");
            t.decisionBy = getVal(row, "decision_by");
            t.decisionAt = getVal(row, "decision_at");
            t.createdAt = getVal(row, "created_at");
            t.updatedAt = getVal(row, "updated_at");
            t.version = getInt(row, "version");
            return t;
        }

        static std::vector<entity::ReviewEvent> mapEvents(const db::ResultSet& rows) {
            std::vector<entity::ReviewEvent> result;
            result.reserve(rows.size());
            for (const auto& r : rows) {
                entity::ReviewEvent e;
                e.id = getLong(r, "id");
                e.taskId = getLong(r, "task_id");
                e.flawId = getLong(r, "flaw_id");
                e.eventType = getVal(r, "event_type");
                e.actor = getVal(r, "actor");
                e.fromAssignee = getVal(r, "from_assignee");
                e.toAssignee = getVal(r, "to_assignee");
                e.reason = getVal(r, "reason");
                e.createdAt = getVal(r, "created_at");
                result.push_back(std::move(e));
            }
            return result;
        }
    };

} // namespace dao
