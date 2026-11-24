# Git 工作流基本流程

## 概述

本文档描述基于 Gitea 的 Git 团队协作工作流，分为**软件主管工作流程**和**程序员工作流程**两大部分。

---

## 一、软件主管工作流程

### 1.1 初始化仓库（仅首次）

```
Gitea 远程仓库                    本地 git 终端
─────────────                    ─────────────
创建远程仓库          →           git remote add origin <GITEA_仓库_URL>
                                 git add .
                                 git commit -m "初始化提交"
                                 git push -u origin main

                                 git checkout -b develop
                                 git add .
                                 git commit -m "初始化develop分支"
                                 git push -u origin develop
```

> **说明**：首次推送分支必须使用 `-u origin <分支名>` 建立本地与远端的追踪关系，后续才可以使用简写的 `git push`。

---

### 1.2 代码审核流程

当 Gitea 上出现合并请求（PR）时，主管执行以下步骤：

```bash
# 1. 获取远端最新分支信息
git fetch origin

# 2. 创建本地分支并追踪远端分支（正确命令需指定追踪来源）
git checkout --track origin/myfe

# 3. 在本地查看并审核代码
```

#### 审核结果处理

| 审核结果 | 操作 |
|---------|------|
| **通过** | 在 Gitea 上发表审核通过评论 → 合并 PR → 删除 `myfe` 分支 → 功能开发完成 |
| **不通过** | 在 Gitea 上发表审核失败评论 → 关闭 PR → **通知开发者修改代码并重新提交 PR** |

> **重要**：审核失败后必须通知开发者，否则开发者无从得知需要修改。

---

### 1.3 版本发布流程

当 develop 分支积累了足够的功能，需要发布新版本时：

```bash
# 1. 拉取所有分支最新状态
git pull --all

# 2. 切换到开发分支
git checkout develop

# 3. 将 main 分支的提交变基到 develop（保持线性历史）
git rebase main
```

#### 变基冲突处理

```
是否出现冲突？
├── 是 → 手动解决冲突文件
│        git add .
│        git rebase --continue        ← 关键步骤，不可省略！
│        git push --force-with-lease origin develop
│
└── 否 → git push --force-with-lease origin develop
```

> **说明**：
> - `git rebase --continue`：解决冲突后必须执行此命令才能继续变基进程，否则 rebase 处于中断状态无法推送。
> - `--force-with-lease`：比 `-f` 更安全，会检查远端是否有新提交，防止意外覆盖他人工作。

```bash
# 4. 确认 develop 分支经过测试且无误后
# 5. 在 Gitea 提交 PR（develop → main）
# 6. PR 被合并后，发布版本并打上标签
git tag v1.0.0
git push origin v1.0.0

# 7. 一个版本发布完成 🎉
```

---

## 二、程序员工作流程

### 2.1 初次克隆项目

```bash
# 克隆仓库到本地
git clone <GITEA_仓库_URL>

# 切换到 develop 分支（功能开发基于 develop 而非 main）
git checkout develop
```

---

### 2.2 创建功能分支开发

```bash
# 创建并切换到新的功能分支（命名规范：feature/<功能描述>）
git checkout -b myfe
# 推荐: git checkout -b feature/login-page

# 开始修改代码...
```

---

### 2.3 提交代码

```bash
# 本地测试通过后提交
git add .                          # 将修改提交到暂存区
git commit -m "feat: 实现xxx功能"   # 提交到本地仓库

# 同步远端状态（判断 develop 是否有新提交）
git fetch origin
```

---

### 2.4 同步远端变更（如有）

如果远端 `develop` 分支有新提交，需要先同步再推送：

```bash
# 切换回 develop
git checkout develop

# 拉取远端 develop 的最新提交
git pull origin develop

# 切换回功能分支
git checkout myfe

# 将 develop 的提交"移动"到我的分支顶部（变基）
git rebase develop
```

**变基的好处**：保留 develop 分支的所有提交记录，并将自己的提交叠加在最新代码之上，确保线性历史。

#### 变基冲突处理

```
是否出现冲突？
├── 是 → 手动解决冲突文件
│        git add .
│        git rebase --continue        ← 关键步骤，不可省略！
│        git push --force-with-lease origin myfe
│
└── 否 → git push -u origin myfe      ← 首次推送需指定远端分支
```

如果没有远端变更（`develop` 无新提交），直接推送：

```bash
git push -u origin myfe   # 首次推送需要 -u 参数
```

---

### 2.5 提交 PR（合并请求）

在 Gitea/GitHub 上创建 PR，请求将 `myfe` → `develop`。

> **注意：** 创建 PR 时，平台默认的目标分支（`base`）是仓库的默认分支（通常是 `main`），**必须手动改为 `develop`**，否则会误合并到 `main`。
>
> **一劳永逸的方法：** 在仓库设置中将默认分支改为 `develop`（GitHub：`Settings → General → Default branch`），之后创建 PR 时会自动选 `develop`，无需每次手动修改。

```
PR 结果
├── 被合并    → 一个功能开发完成 🎉
└── 被关闭    → 查看审核意见 → 修改代码 → 重新提交 PR（回到 2.3）
```

---

## 三、完整流程图说明

```
程序员                    Gitea 远程仓库              软件主管
──────                   ──────────────              ──────────
git clone
  ↓
git checkout develop
  ↓
git checkout -b myfe
  ↓
修改代码
  ↓
测试无问题
  ↓
git add & git commit
  ↓
git fetch origin ──────→ 检查 develop 是否有新提交
  ↓ (有新提交)
git checkout develop
  ↓
git pull origin develop
  ↓
git checkout myfe
  ↓
git rebase develop
  ↓ (有冲突)
解决冲突
  ↓
git add . && git rebase --continue
  ↓
git push --force-with-lease origin myfe
  ↓ (无新提交)
git push -u origin myfe ──────────────→ 创建 PR
                                              ↓
                                       是否有合并请求？
                                              ↓ (是)
                                       git fetch origin ←── 软件主管
                                              ↓
                                  git checkout --track origin/myfe
                                              ↓
                                          审核代码
                                              ↓
                              ┌─────── 审核是否通过？ ───────┐
                              ↓ (是)                       ↓ (否)
                        发表通过评论                  发表失败评论
                              ↓                           ↓
                           合并 PR                      关闭 PR
                              ↓                           ↓
                        删除 myfe 分支            通知开发者修改重提
                              ↓                           ↓
                        功能开发完成              开发者修改后重提 PR
```

---

## 四、常用命令速查

| 操作 | 命令 |
|------|------|
| 克隆仓库 | `git clone <URL>` |
| 切换分支 | `git checkout <分支名>` 或 `git switch <分支名>` |
| 创建并切换分支 | `git checkout -b <分支名>` 或 `git switch -c <分支名>` |
| 暂存所有修改 | `git add .` |
| 提交到本地仓库 | `git commit -m "<信息>"` |
| 首次推送分支 | `git push -u origin <分支名>` |
| 安全强制推送 | `git push --force-with-lease origin <分支名>` |
| 拉取并变基 | `git pull --rebase` |
| 变基到目标分支 | `git rebase <目标分支>` |
| 继续变基（冲突后） | `git add . && git rebase --continue` |
| 中止变基 | `git rebase --abort` |
| 追踪远端分支 | `git checkout --track origin/<分支名>` |
| 打标签 | `git tag <标签名> && git push origin <标签名>` |

---

## 五、改进说明（相比原始流程图）

| 问题 | 原始 | 改进后 |
|------|------|--------|
| 悬空箭头 | "无冲突"路径箭头没有正确连接到节点 | 正确连接到 `git push -u origin myfe` |
| 缺失关键步骤 | 解决冲突后直接 push，跳过 `git rebase --continue` | 新增 `git add . && git rebase --continue` 步骤 |
| 命令不完整 | `git push`（未指定分支和追踪关系） | `git push -u origin myfe`（首次推送必须指定） |
| 危险命令 | `git push -f`（无条件强制覆盖） | `git push --force-with-lease`（安全强制推送） |
| 流程死路 | 审核失败关闭 PR 后无任何后续 | 新增"通知开发者修改代码并重新提交 PR"节点 |
| 孤立节点 | "是否有合并请求"节点无入边 | 从"创建推送 develop"连接到该节点 |
| 命令语义错误 | `git checkout -b myfe`（未指定追踪来源） | `git checkout --track origin/myfe` |
