# 阶段六验收记录

## 1. 结论

验收日期：2026-07-22。

《Novel Character Relationship Analysis System Documentation》第 31 章“第六阶段：测试与完善”要求的单元测试、集成测试、文件损坏测试、图结构验证、性能测试和用户操作流程测试均已实现并通过。本次验收未发现阻断问题。

## 2. 版本与环境

| 项目 | 本次记录 |
| --- | --- |
| Git 基线 | `d0b43c44bee1c80adee41b8751422f0170afc5a6`；阶段六修改位于当前工作树，尚未提交 |
| 操作系统 | Windows 11 企业版，10.0.26200（Build 26200） |
| CMake | 3.31.5 |
| 编译器 | MinGW-w64 GCC 13.1.0，x86_64-posix-seh |
| Qt | 6.8.3，MinGW 64-bit |
| 构建类型 | Debug |

## 3. 自动化结果

在仓库根目录执行：

```powershell
& .\scripts\verify.ps1
& .\scripts\verify.ps1 -TestLabel 'phase-six'
```

- 全量门禁：11/11 个 CTest 目标通过，0 个失败；CTest 总耗时 2.94 s。
- 阶段六门禁：4/4 个新增目标通过，0 个失败。
- 用户流程稳定性复跑：`presentation_phase_six_user_flow_tests --repeat until-fail:20`，20/20 通过。
- `git diff --check`：通过。

全量标签计数为：`unit` 7、`integration` 5、`corruption` 3、`graph-validation` 3、`performance` 1、`user-flow` 4、`phase-six` 4。同一测试目标可属于多个标签。

## 4. 性能结果

`performance_phase_six_tests` 使用 5000 人物、5000 章节和 5000 条环形关系边，Debug 实测如下：

| 阶段 | 实测 | 预算 | 结果 |
| --- | ---: | ---: | --- |
| 快照构建、恢复与完整验证 | 82 ms | 15 s | 通过 |
| 5000 次名称与关系查询 | 4 ms | 5 s | 通过 |
| DFS 与 BFS 全图遍历 | 38 ms | 5 s | 通过 |
| 二进制保存、加载与逐字段往返 | 256 ms | 15 s | 通过 |

这些数值只代表本次机器与负载下的结果；验收是否通过由预算上限和测试退出码共同决定。

## 5. Windows 原生界面 QA

检查执行者：Codex。测试以 `QT_QPA_PLATFORM=windows` 直接运行，在 100% 和 150% 缩放下各生成一组原生窗口截图；两个运行均以退出码 0 完成。逐张检查中文字体、控件截断与重叠、表格/详情同步、人物与关系高亮、遍历树虚拟根及连线，未发现问题。

| 场景 | 100% | 150% | 检查结果 |
| --- | --- | --- | --- |
| 主关系图 | [截图](phase-six-ui-qa/main-graph-100pct.png) | [截图](phase-six-ui-qa/main-graph-150pct.png) | 通过 |
| 关系边悬停 | [截图](phase-six-ui-qa/edge-hover-100pct.png) | [截图](phase-six-ui-qa/edge-hover-150pct.png) | 通过 |
| 人物表 | [截图](phase-six-ui-qa/person-table-100pct.png) | [截图](phase-six-ui-qa/person-table-150pct.png) | 通过 |
| 关系详情 | [截图](phase-six-ui-qa/relation-detail-100pct.png) | [截图](phase-six-ui-qa/relation-detail-150pct.png) | 通过 |
| 遍历高亮 | [截图](phase-six-ui-qa/traversal-highlight-100pct.png) | [截图](phase-six-ui-qa/traversal-highlight-150pct.png) | 通过 |
| 遍历树 | [截图](phase-six-ui-qa/traversal-tree-100pct.png) | [截图](phase-six-ui-qa/traversal-tree-150pct.png) | 通过 |

截图由 `presentation_ui_phase_five_tests.exe populatedWindowRendersForVisualQa` 生成并归档在 `docs/phase-six-ui-qa/`。功能交互由 offscreen 用户流程测试使用真实 Qt 按钮与鼠标事件独立验证，截图 QA 不替代该自动化门禁。
