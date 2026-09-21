-- ============================================
-- 工业检测系统 数据库初始化脚本
-- ============================================

CREATE DATABASE IF NOT EXISTS industrial_inspection
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE industrial_inspection;

-- 复核相关表存在外键依赖，必须先于 FLAW 删除
DROP TABLE IF EXISTS REVIEW_EVENT;
DROP TABLE IF EXISTS REVIEW_TASK;

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
    KEY idx_flaw_date (date)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '损伤表';

-- ============================================
-- 复核队列：每条损伤至多一条待复核任务
-- ============================================
DROP TABLE IF EXISTS REVIEW_TASK;
CREATE TABLE REVIEW_TASK (
    id               BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '复核任务ID',
    flaw_id          BIGINT NOT NULL COMMENT '关联损伤记录ID',
    level            INT NOT NULL COMMENT '冗余自FLAW.level，供按级别索引分页',
    camera           INT NOT NULL COMMENT '冗余自FLAW.camera，供按摄像头索引分页',
    status           VARCHAR(16) NOT NULL DEFAULT 'PENDING' COMMENT 'PENDING待复核/ASSIGNED已指派/APPROVED通过/REJECTED驳回',
    assignee         VARCHAR(64) NULL COMMENT '当前负责人（NULL=未指派）',
    assigned_by      VARCHAR(64) NULL COMMENT '最近一次指派人',
    assigned_at      DATETIME(3) NULL COMMENT '最近一次指派时间',
    decision_by      VARCHAR(64) NULL COMMENT '结论提交人',
    decision_at      DATETIME(3) NULL COMMENT '结论提交时间',
    created_at       DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '入队时间',
    updated_at       DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3) COMMENT '最近状态变更时间',
    version          INT NOT NULL DEFAULT 0 COMMENT '状态版本，每次状态变更自增',
    UNIQUE KEY uk_review_task_flaw (flaw_id),
    -- 负责人维度翻页：WHERE assignee=? [AND status=?] AND id<? ORDER BY id DESC
    KEY idx_rt_assignee_status_id (assignee, status, id),
    -- 状态维度翻页
    KEY idx_rt_status_id (status, id),
    -- 级别维度翻页：WHERE level=? AND id<? ORDER BY id DESC
    KEY idx_rt_level_id (level, id),
    -- 摄像头维度翻页
    KEY idx_rt_camera_id (camera, id),
    CONSTRAINT fk_rt_flaw FOREIGN KEY (flaw_id) REFERENCES FLAW (id),
    CONSTRAINT chk_rt_status CHECK (status IN ('PENDING', 'ASSIGNED', 'APPROVED', 'REJECTED')),
    CONSTRAINT chk_rt_assigned CHECK (
        (status = 'PENDING' AND assignee IS NULL) OR
        (status IN ('ASSIGNED', 'APPROVED', 'REJECTED') AND assignee IS NOT NULL)
    )
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '损伤复核队列表';

-- ============================================
-- 复核轨迹：仅追加的责任流水（谁、何时、把任务交给谁、理由）
-- ============================================
DROP TABLE IF EXISTS REVIEW_EVENT;
CREATE TABLE REVIEW_EVENT (
    id           BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '轨迹事件ID',
    task_id      BIGINT NOT NULL COMMENT '复核任务ID',
    flaw_id      BIGINT NOT NULL COMMENT '冗余损伤ID，便于按缺陷直查轨迹',
    event_type   VARCHAR(16) NOT NULL COMMENT 'CREATE入队/ASSIGN指派/TRANSFER转派/APPROVE通过/REJECT驳回',
    actor        VARCHAR(64) NOT NULL COMMENT '操作者',
    from_assignee VARCHAR(64) NULL COMMENT '变更前负责人',
    to_assignee   VARCHAR(64) NULL COMMENT '变更后负责人',
    reason       VARCHAR(512) NOT NULL DEFAULT '' COMMENT '理由（转派/驳回必填）',
    created_at   DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '事件时间',
    KEY idx_re_task (task_id, id),
    KEY idx_re_flaw (flaw_id, id),
    KEY idx_re_actor_time (actor, created_at),
    CONSTRAINT fk_re_task FOREIGN KEY (task_id) REFERENCES REVIEW_TASK (id),
    CONSTRAINT fk_re_flaw FOREIGN KEY (flaw_id) REFERENCES FLAW (id),
    CONSTRAINT chk_re_type CHECK (event_type IN ('CREATE', 'ASSIGN', 'TRANSFER', 'APPROVE', 'REJECT')),
    -- 转派和驳回不能省略理由
    CONSTRAINT chk_re_reason CHECK (
        (event_type IN ('TRANSFER', 'REJECT') AND CHAR_LENGTH(TRIM(reason)) > 0)
        OR event_type IN ('CREATE', 'ASSIGN', 'APPROVE')
    )
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '损伤复核责任轨迹表';

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
