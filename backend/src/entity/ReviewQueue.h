#pragma once
#include <string>

namespace entity {

    // 复核状态常量
    constexpr const char* REVIEW_PENDING  = "PENDING";
    constexpr const char* REVIEW_ASSIGNED = "ASSIGNED";
    constexpr const char* REVIEW_APPROVED = "APPROVED";
    constexpr const char* REVIEW_REJECTED = "REJECTED";

    // 复核队列：一条 FLAW 至多一条在制复核单（flaw_id 唯一）
    struct ReviewQueue {
        long long id = 0;
        long long flawId = 0;
        std::string status = REVIEW_PENDING;
        std::string assignee;          // 当前负责人，PENDING 时为空
        std::string assignedBy;        // 最近一次指派/转派操作者
        std::string assignedAt;        // 最近一次指派/转派时间
        std::string concludedBy;       // 结论提交人
        std::string concludedAt;       // 结论时间
        std::string conclusionNote;    // 结论说明
        std::string createdAt;
        std::string updatedAt;
        int version = 0;
    };

} // namespace entity
