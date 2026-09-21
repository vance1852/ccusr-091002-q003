# 工业检测系统 - C++ MySQL 数据访问层

## 1. 系统架构

```mermaid
flowchart TD
    A[C++ Application Main] --> B[DatabaseManager]
    B --> C[MySQL Connector/C++]
    C --> D[(MySQL 8.0)]
    
    A --> E[SpeedDAO]
    A --> F[SpliceDAO]
    A --> G[FlawDAO]
    A --> H[StopDAO]
    A --> I[CompareDAO]
    A --> J[HistoryDAO]
    A --> K[RemoveDAO]
    A --> L[ReviewQueueDAO]
    
    E --> B
    F --> B
    G --> B
    H --> B
    I --> B
    J --> B
    K --> B
    L --> B
```

## 2. ER 图

```mermaid
erDiagram
    SPEED {
        INT id PK "主键"
        FLOAT value "速度值"
        VARCHAR date "日期"
        TINYINT flag "使用标志"
    }
    SPLICE {
        INT id PK "主键"
        FLOAT location "当前位置"
        FLOAT distance "距离维修区距离"
        VARCHAR time "倒计时时间"
        TEXT url "保存路径"
        TINYINT last "当前检测接头标志"
        TINYINT flag "准备标志"
        TINYINT stop "停机标志"
    }
    FLAW {
        BIGINT id PK "主键"
        VARCHAR category "损伤类型"
        INT level "损伤级别"
        TEXT url "保存路径"
        INT camera "摄像头编号"
        FLOAT location "当前位置"
        FLOAT distance "距离维修区距离"
        VARCHAR size "损伤尺寸"
        VARCHAR coordinate "损伤坐标"
        VARCHAR date "记录日期"
        FLOAT time "倒计时时间"
        TINYINT flag "准备标志"
        TINYINT stop "停机标志"
        INT epoch "追踪圈数"
    }
    STOP {
        BIGINT id PK "主键"
        INT category "损伤类型"
        FLOAT distance "距离维修区距离"
        TINYINT flag "停机标志"
        TINYINT command "停机命令标志"
    }
    COMPARE {
        BIGINT id PK "主键"
        TEXT new_url "较新对比记录"
        TEXT old_url "较旧对比记录"
        FLOAT value "对比结果"
        INT category "类型"
        INT level "结果级别"
        VARCHAR old_size "较旧记录尺寸"
    }
    HISTORY {
        BIGINT id PK "主键"
        VARCHAR category "损伤类型"
        INT level "损伤级别"
        TEXT url "保存路径"
        INT camera "摄像头编号"
        VARCHAR size "损伤尺寸"
        VARCHAR date "记录日期"
    }
    REMOVE {
        BIGINT id PK "移除记录ID"
    }
    REVIEW_QUEUE {
        BIGINT id PK "复核单主键"
        BIGINT flaw_id UK "关联 FLAW.id（唯一）"
        VARCHAR status "PENDING/ASSIGNED/APPROVED/REJECTED"
        VARCHAR assignee "当前负责人"
        VARCHAR assigned_by "指派人/转派操作者"
        DATETIME assigned_at "指派/转派时间"
        VARCHAR concluded_by "结论提交人"
        DATETIME concluded_at "结论时间"
        VARCHAR conclusion_note "结论说明"
        INT version "状态版本号"
    }
    REVIEW_ACTION_LOG {
        BIGINT id PK "轨迹流水主键"
        BIGINT review_id FK "关联 REVIEW_QUEUE.id"
        BIGINT flaw_id "冗余 FLAW.id"
        INT seq "单内严格递增序号"
        VARCHAR action "ASSIGN/REASSIGN/APPROVE/REJECT"
        VARCHAR from_status "操作前状态"
        VARCHAR to_status "操作后状态"
        VARCHAR from_assignee "操作前负责人"
        VARCHAR to_assignee "操作后负责人"
        VARCHAR operator "操作者"
        VARCHAR reason "理由（转派/驳回强制非空）"
        DATETIME created_at "操作时间"
    }

    FLAW ||--o{ STOP : "损伤触发停机"
    SPLICE ||--o{ STOP : "接缝触发停机"
    FLAW ||--o{ COMPARE : "损伤对比"
    SPLICE ||--o{ COMPARE : "接缝对比"
    FLAW ||--o{ HISTORY : "损伤归档"
    FLAW ||--o{ REMOVE : "损伤移除"
    FLAW |o--|| REVIEW_QUEUE : "一损伤至多一复核单"
    REVIEW_QUEUE ||--o{ REVIEW_ACTION_LOG : "复核责任轨迹"
```

## 3. 模块清单

| 模块 | 文件 | 职责 |
|------|------|------|
| DatabaseManager | db/DatabaseManager.h/.cpp | 连接池管理、SQL执行 |
| Entity | entity/*.h | 各表实体类定义 |
| DAO | dao/*.h/*.cpp | 各表CRUD操作 |
| Logger | utils/Logger.h/.cpp | 日志记录 |
| Main | main.cpp | 入口与演示 |

## 4. 技术选型

- C++17
- MySQL Connector/C++ 8.0 (X DevAPI / Legacy C API)
- CMake 3.16+
- spdlog (日志，可选，本项目使用自实现轻量Logger)
