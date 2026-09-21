#pragma once
#include <string>

namespace entity {

    // 复核操作类型
    constexpr const char* REVIEW_ACT_ASSIGN   = "ASSIGN";    // 首次指派
    constexpr const char* REVIEW_ACT_REASSIGN = "REASSIGN";  // 转派（必须填理由）
    constexpr const char* REVIEW_ACT_APPROVE  = "APPROVE";   // 通过
    constexpr const char* REVIEW_ACT_REJECT   = "REJECT";    // 驳回（必须填理由）

    // 复核操作轨迹：只追加
    struct ReviewActionLog {
        long long id = 0;
        long long reviewId = 0;
        long long flawId = 0;
        int seq = 0;
        std::string action;
        std::string fromStatus;
        std::string toStatus;
        std::string fromAssignee;
        std::string toAssignee;
        std::string oper;            // 执行操作的人
        std::string reason;
        std::string createdAt;
    };

} // namespace entity
