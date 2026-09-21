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

初始化脚本创建 SPEED、SPLICE、FLAW、STOP、COMPARE、HISTORY 和 REMOVE 七张基础表，以及建立在 FLAW 之上的复核队列两张表：REVIEW_TASK（每条损伤至多一条复核任务，记录 PENDING/ASSIGNED/APPROVED/REJECTED 状态、当前负责人、指派与结论的操作者和时间）与 REVIEW_EVENT（仅追加的责任轨迹，记录 CREATE/ASSIGN/TRANSFER/APPROVE/REJECT，转派与驳回的理由由数据库 CHECK 约束强制）。结论只能由当前负责人通过条件更新提交，并发/重送提交会得到 SUBMITTED、NOT_OWNER、ALREADY_DECIDED、TASK_NOT_FOUND 等可区分的业务结果；主管按级别、摄像头、负责人的翻页查询走 REVIEW_TASK 上的复合索引并以任务ID为键集游标，不漏不重。字段含义、默认值和索引以 SQL 脚本为准；应用通过 DAO 执行增删改查并在关键操作处记录日志。
