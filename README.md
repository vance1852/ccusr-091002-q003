# 工业检测系统 - C++ MySQL 数据访问层

## 运行

使用 Docker Compose 启动 MySQL 和应用：

```bash
docker compose up --build -d
docker compose run --rm test
```

查看应用输出：

```bash
docker compose logs -f backend
```

本地构建需要 CMake 3.16、C++17 编译器和 MySQL Connector/C。进入 `backend` 后执行 `cmake ..` 和 `cmake --build .`，连接参数通过 `DB_HOST`、`DB_PORT`、`DB_USER`、`DB_PASSWORD`、`DB_NAME` 环境变量提供。生产环境应从运行时安全存储注入密码，不要把凭据提交到仓库或打印到日志。

## 目录职责

`backend/src/entity` 定义检测数据结构，`backend/src/dao` 封装各业务表的数据访问，`backend/src/db` 管理 MySQL 连接和查询，`backend/src/utils` 提供日志，`backend/src/test` 保存回归测试。`backend/sql/schema.sql` 是数据库初始化脚本，`docker-compose.yml` 描述本地服务依赖。

## 数据范围

初始化脚本创建 SPEED、SPLICE、FLAW、STOP、COMPARE、HISTORY、REMOVE 七张原始检测表，以及建立在 FLAW 之上的复核队列两张表：REVIEW_QUEUE（每条损伤至多一条复核单，状态 PENDING/ASSIGNED/APPROVED/REJECTED）和 REVIEW_ACTION_LOG（指派/转派/通过/驳回的责任轨迹，只追加）。字段含义、默认值和索引以 SQL 脚本为准；应用通过 DAO 执行增删改查并在关键操作处记录日志。

### 复核队列

- 同一 `flaw_id` 在 REVIEW_QUEUE 上有唯一约束，缺陷不会重复进入不同人的待办。
- 指派/转派记录操作者（主管）与时间；结论只能由当前负责人提交；转派和驳回的理由由 DAO 与数据库 CHECK 约束双重强制非空。
- 状态推进在单条队列行的事务内以 `SELECT ... FOR UPDATE` 串行化：负责人失效返回 `NOT_OWNER`，客户端重送已形成的结论返回 `ALREADY_CONCLUDED`，驳回后未重新指派返回 `REJECTED_PENDING_REASSIGN`，调用方可按 `dao::ReviewError` 区分。
- 主管查询使用以 `q.id` 为游标的 keyset 翻页，配合 `(status,id)`、`(assignee,status,id)` 索引，按级别、摄像头、负责人过滤时不漏项、不重复。
