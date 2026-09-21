-- ============================================
-- 工业检测系统 数据库初始化脚本
-- ============================================

CREATE DATABASE IF NOT EXISTS industrial_inspection
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE industrial_inspection;

-- 复核队列表引用 FLAW、轨迹表引用复核队列，重跑脚本时先于 FLAW 删除
DROP TABLE IF EXISTS REVIEW_ACTION_LOG;
DROP TABLE IF EXISTS REVIEW_QUEUE;

-- 速度表
DROP TABLE IF EXISTS SPEED;
CREATE TABLE SPEED (
    id        INT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    value     FLOAT NOT NULL COMMENT '速度值',
    date      VARCHAR(32) NOT NULL COMMENT '日期',
    flag      TINYINT DEFAULT 0 COMMENT '使用标志，1为已使用（废弃）'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '速度表';

-- 接缝表
DROP TABLE IF EXISTS SPLICE;
CREATE TABLE SPLICE (
    id       INT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    location FLOAT NOT NULL COMMENT '当前位置',
    distance FLOAT NOT NULL COMMENT '距离维修区距离',
    time     VARCHAR(32) NOT NULL COMMENT '倒计时时间（秒）',
    url      TEXT NOT NULL COMMENT '保存路径',
    last     TINYINT DEFAULT 0 COMMENT '当前检测接头标志，1有效',
    flag     TINYINT DEFAULT 0 COMMENT '准备标志，不为0则准备停机',
    stop     TINYINT DEFAULT 0 COMMENT '停机标志，不为0则可以停机'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '接缝表';

-- 损伤表
DROP TABLE IF EXISTS FLAW;
CREATE TABLE FLAW (
    id         BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    category   VARCHAR(64) NOT NULL COMMENT '损伤类型',
    level      INT NOT NULL COMMENT '损伤级别',
    url        TEXT NOT NULL COMMENT '损伤记录保存路径',
    camera     INT NOT NULL COMMENT '监控摄像头编号',
    location   FLOAT NOT NULL COMMENT '当前位置',
    distance   FLOAT NOT NULL COMMENT '距离维修区距离',
    size       VARCHAR(64) NOT NULL COMMENT '损伤尺寸',
    coordinate VARCHAR(64) NOT NULL COMMENT '损伤坐标',
    date       VARCHAR(32) NOT NULL COMMENT '记录日期',
    time       FLOAT NOT NULL COMMENT '倒计时时间（秒）',
    flag       TINYINT DEFAULT 0 COMMENT '准备标志，不为0则准备停机',
    stop       TINYINT DEFAULT 0 COMMENT '停机标志，不为0则可以停机',
    epoch      INT DEFAULT 0 COMMENT '追踪当前缺陷的圈数',
    KEY idx_flaw_level (level),
    KEY idx_flaw_camera (camera),
    KEY idx_flaw_category (category),
    KEY idx_flaw_date (date)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '损伤表';

-- 停机表
DROP TABLE IF EXISTS STOP;
CREATE TABLE STOP (
    id       BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '长ID为损伤，短ID为接缝',
    category INT NOT NULL COMMENT '损伤类型',
    distance FLOAT NOT NULL COMMENT '距离维修区距离',
    flag     TINYINT DEFAULT 0 COMMENT '停机标志，1为允许停机',
    command  TINYINT DEFAULT 0 COMMENT '停机命令标志，1为下发'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '停机表';

-- 对比表
DROP TABLE IF EXISTS COMPARE;
CREATE TABLE COMPARE (
    id       BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '长ID为损伤，短ID为接缝',
    new_url  TEXT NOT NULL COMMENT '较新对比记录',
    old_url  TEXT NOT NULL COMMENT '较旧对比记录',
    value    FLOAT NOT NULL COMMENT '对比结果',
    category INT NOT NULL COMMENT '类型',
    level    INT NOT NULL COMMENT '结果级别',
    old_size VARCHAR(64) NOT NULL COMMENT '较旧记录尺寸（只对损伤有效）'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '对比表';

-- 历史表
DROP TABLE IF EXISTS HISTORY;
CREATE TABLE HISTORY (
    id       BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '长ID为损伤，短ID为接缝',
    category VARCHAR(64) NOT NULL COMMENT '损伤类型',
    level    INT NOT NULL COMMENT '损伤级别',
    url      TEXT NOT NULL COMMENT '损伤记录保存路径',
    camera   INT NOT NULL COMMENT '监控摄像头编号',
    size     VARCHAR(64) NOT NULL COMMENT '损伤尺寸',
    date     VARCHAR(32) NOT NULL COMMENT '记录日期'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '历史表';

-- 移除表
DROP TABLE IF EXISTS REMOVE;
CREATE TABLE REMOVE (
    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '移除记录ID'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '移除表';

-- ============================================
-- 复核队列：每条 FLAW 损伤至多一张在制复核单
-- ============================================
-- 状态机:
--   PENDING(待复核) -- assign --> ASSIGNED(已指派)
--   ASSIGNED -- approve/reject --> APPROVED(通过) / REJECTED(驳回)
--   ASSIGNED -- reassign --> ASSIGNED(可多次转派, 必须填理由)
--   REJECTED(驳回) -- reassign --> ASSIGNED(驳回后重新指派使缺陷再进入某人待办, 必须填理由)
-- 一旦 APPROVED 不可逆; 驳回重派后可再次通过或驳回
CREATE TABLE REVIEW_QUEUE (
    id               BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '复核单主键, 稳定游标',
    flaw_id          BIGINT NOT NULL COMMENT '关联 FLAW.id',
    status           VARCHAR(16) NOT NULL DEFAULT 'PENDING'
                         COMMENT '状态: PENDING/ASSIGNED/APPROVED/REJECTED',
    assignee         VARCHAR(64) DEFAULT NULL COMMENT '当前负责人, PENDING 时为 NULL',
    assigned_by      VARCHAR(64) DEFAULT NULL COMMENT '最近一次指派/转派的操作者(主管)',
    assigned_at      DATETIME(3) DEFAULT NULL COMMENT '最近一次指派/转派时间',
    concluded_by     VARCHAR(64) DEFAULT NULL COMMENT '提交最终结论的负责人',
    concluded_at     DATETIME(3) DEFAULT NULL COMMENT '结论时间',
    conclusion_note  VARCHAR(1024) DEFAULT NULL COMMENT '通过/驳回说明, 驳回时强制非空',
    created_at       DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '入队时间',
    updated_at      DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                                       ON UPDATE CURRENT_TIMESTAMP(3) COMMENT '状态变更时间',
    version          INT NOT NULL DEFAULT 0 COMMENT '状态版本号, 每次变更+1, 乐观并发辅助',
    UNIQUE KEY uk_review_flaw (flaw_id),
    KEY idx_rq_status_id (status, id),
    KEY idx_rq_assignee_status_id (assignee, status, id),
    KEY idx_rq_assigned_at (assigned_at),
    CONSTRAINT fk_rq_flaw FOREIGN KEY (flaw_id) REFERENCES FLAW(id)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '损伤复核队列';

-- ============================================
-- 复核操作轨迹: 每次指派/转派/结论追加一条, 只追加不修改
-- ============================================
CREATE TABLE REVIEW_ACTION_LOG (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '轨迹流水主键',
    review_id   BIGINT NOT NULL COMMENT '关联 REVIEW_QUEUE.id',
    flaw_id     BIGINT NOT NULL COMMENT '冗余 FLAW.id, 免 join 查轨迹',
    seq         INT NOT NULL COMMENT '同一复核单内严格递增的操作序号',
    action      VARCHAR(16) NOT NULL COMMENT '操作: ASSIGN/REASSIGN/APPROVE/REJECT',
    from_status VARCHAR(16) DEFAULT NULL COMMENT '操作前队列状态',
    to_status   VARCHAR(16) NOT NULL COMMENT '操作后队列状态',
    from_assignee VARCHAR(64) DEFAULT NULL COMMENT '操作前负责人',
    to_assignee   VARCHAR(64) DEFAULT NULL COMMENT '操作后负责人(结论类操作为空)',
    operator    VARCHAR(64) NOT NULL COMMENT '执行该操作的人: 指派为主管, 结论为当前负责人',
    reason      VARCHAR(1024) DEFAULT NULL COMMENT '理由: 转派/驳回强制非空',
    created_at  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '操作时间',
    UNIQUE KEY uk_ral_review_seq (review_id, seq),
    KEY idx_ral_flaw (flaw_id, id),
    KEY idx_ral_review_id (review_id, id),
    CONSTRAINT fk_ral_review FOREIGN KEY (review_id) REFERENCES REVIEW_QUEUE(id),
    CONSTRAINT chk_ral_action CHECK (action IN ('ASSIGN','REASSIGN','APPROVE','REJECT')),
    CONSTRAINT chk_ral_reassign_reason CHECK (
        action <> 'REASSIGN' OR
        (reason IS NOT NULL AND CHAR_LENGTH(TRIM(reason)) > 0)
    ),
    CONSTRAINT chk_ral_reject_reason CHECK (
        action <> 'REJECT' OR
        (reason IS NOT NULL AND CHAR_LENGTH(TRIM(reason)) > 0)
    )
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '复核操作责任轨迹';

