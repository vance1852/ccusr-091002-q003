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
    
    E --> B
    F --> B
    G --> B
    H --> B
    I --> B
    J --> B
    K --> B
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
    REVIEW_TASK {
        BIGINT id PK "复核任务ID"
        BIGINT flaw_id FK "关联损伤"
        INT level "冗余级别(分页)"
        INT camera "冗余摄像头(分页)"
        VARCHAR status "PENDING/ASSIGNED/APPROVED/REJECTED"
        VARCHAR assignee "当前负责人"
        VARCHAR assigned_by "指派人"
        DATETIME assigned_at "指派时间"
        VARCHAR decision_by "结论提交人"
        DATETIME decision_at "结论时间"
        INT version "状态版本"
    }
    REVIEW_EVENT {
        BIGINT id PK "轨迹事件ID"
        BIGINT task_id FK "复核任务"
        BIGINT flaw_id FK "关联损伤"
        VARCHAR event_type "CREATE/ASSIGN/TRANSFER/APPROVE/REJECT"
        VARCHAR actor "操作者"
        VARCHAR from_assignee "变更前负责人"
        VARCHAR to_assignee "变更后负责人"
        VARCHAR reason "理由(转派/驳回必填)"
        DATETIME created_at "事件时间"
    }

    FLAW ||--|| REVIEW_TASK : "一条损伤至多一条复核任务"
    REVIEW_TASK ||--o{ REVIEW_EVENT : "责任轨迹"
    FLAW ||--o{ REVIEW_EVENT : "按损伤直查轨迹"

    FLAW ||--o{ STOP : "损伤触发停机"
    SPLICE ||--o{ STOP : "接缝触发停机"
    FLAW ||--o{ COMPARE : "损伤对比"
    SPLICE ||--o{ COMPARE : "接缝对比"
    FLAW ||--o{ HISTORY : "损伤归档"
    FLAW ||--o{ REMOVE : "损伤移除"
```

> REVIEW_TASK 对 flaw_id 建唯一键（一条损伤不会同时进入多个人的待办），
> 并带 (assignee,status,id)、(level,id)、(camera,id)、(status,id) 复合索引支撑键集翻页；
> REVIEW_EVENT 为只追加表，CHECK 约束强制 TRANSFER/REJECT 必须携带非空理由。
> 结论提交使用 `UPDATE ... WHERE status='ASSIGNED' AND assignee=?` 条件更新，
> 并发提交在 InnoDB 行锁下只会有一条生效，零行结果再区分为 NOT_OWNER / ALREADY_DECIDED / TASK_NOT_FOUND。

## 3. 模块清单

| 模块 | 文件 | 职责 |
|------|------|------|
| DatabaseManager | db/DatabaseManager.h/.cpp | 连接池管理、SQL执行 |
| Entity | entity/*.h | 各表实体类定义 |
| DAO | dao/*.h/*.cpp | 各表CRUD操作；ReviewDAO 提供入队/指派/转派/条件结论与键集翻页 |
| Connection | db/DatabaseManager.h/.cpp | 独立会话连接，支持事务与多连接并发 |
| Logger | utils/Logger.h/.cpp | 日志记录 |
| Main | main.cpp | 入口与演示 |

## 4. 技术选型

- C++17
- MySQL Connector/C++ 8.0 (X DevAPI / Legacy C API)
- CMake 3.16+
- spdlog (日志，可选，本项目使用自实现轻量Logger)
