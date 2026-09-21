#pragma once
#include <string>

namespace entity {

    // 复核任务状态
    namespace reviewstatus {
        constexpr const char* PENDING   = "PENDING";    // 待复核（未指派）
        constexpr const char* ASSIGNED  = "ASSIGNED";   // 已指派
        constexpr const char* APPROVED  = "APPROVED";   // 通过
        constexpr const char* REJECTED  = "REJECTED";   // 驳回
    }

    // 复核轨迹事件类型
    namespace reviewevent {
        constexpr const char* CREATE   = "CREATE";    // 入队
        constexpr const char* ASSIGN   = "ASSIGN";    // 指派
        constexpr const char* TRANSFER = "TRANSFER";  // 转派
        constexpr const char* APPROVE  = "APPROVE";   // 通过
        constexpr const char* REJECT   = "REJECT";    // 驳回
    }

    // 复核队列任务，每条损伤至多一条
    struct ReviewTask {
        long long id = 0;
        long long flawId = 0;        // 关联 FLAW.id
        int level = 0;               // 冗余自 FLAW.level
        int camera = 0;              // 冗余自 FLAW.camera
        std::string status;          // PENDING / ASSIGNED / APPROVED / REJECTED
        std::string assignee;        // 当前负责人；空串表示未指派
        std::string assignedBy;      // 最近一次指派人
        std::string assignedAt;      // 最近一次指派时间
        std::string decisionBy;      // 结论提交人
        std::string decisionAt;      // 结论提交时间
        std::string createdAt;
        std::string updatedAt;
        int version = 0;
    };

    // 追加式责任轨迹事件
    struct ReviewEvent {
        long long id = 0;
        long long taskId = 0;
        long long flawId = 0;
        std::string eventType;       // CREATE / ASSIGN / TRANSFER / APPROVE / REJECT
        std::string actor;           // 操作者
        std::string fromAssignee;    // 变更前负责人（可空）
        std::string toAssignee;      // 变更后负责人（可空）
        std::string reason;          // 理由（转派/驳回必填）
        std::string createdAt;
    };

} // namespace entity
