# 阶段六验收说明

## 1. 验收范围与通过条件

本说明对应《Novel Character Relationship Analysis System Documentation》第 31 章“第六阶段：测试与完善”的六项要求：单元测试、集成测试、文件损坏测试、图结构验证、性能测试和用户操作流程测试，并以第 29 章测试方案为用例基线。

阶段六只有在以下条件全部满足时才算通过：

1. `scripts/verify.ps1` 完成 Debug 配置、编译和全量 CTest，11 个测试目标全部通过。
2. 4 个阶段六新增目标可由 `phase-six` 标签独立运行并通过。
3. 性能目标当次输出的四项耗时均不超过 Debug 预算。
4. offscreen 界面自动化门禁通过，且 Windows 原生平台截图经逐张视觉检查无截断、重叠、乱码或状态不同步。
5. 验收记录保留当次命令、提交版本、编译器/Qt 版本、CTest 输出、性能输出和截图清单。

## 2. 测试类别到 CTest 目标的映射

`-TestLabel` 只接受表中的类别、`phase-six` 或默认值 `all`，并配合 CTest 的 `--no-tests=error`，避免标签拼写或配置错误导致零测试假绿。同一目标可同时属于多个类别。

| 阶段六类别 | CTest 标签 | CTest 目标 | 第 29 章对应的关键用例 |
| --- | --- | --- | --- |
| 单元测试 | `unit` | `domain_graph_tests`<br>`domain_phase_two_tests`<br>`domain_phase_three_tests`<br>`infrastructure_phase_four_tests`<br>`domain_phase_six_graph_validation_tests`<br>`infrastructure_phase_six_atomic_save_tests`<br>`presentation_graphics_phase_five_tests` | 边链头/中/尾插删，重复边、自环和删除人物；Jaccard 与章节统计；单点、链、环、星形、非连通图的 DFS/BFS 及孩子链表树；UTF-8/解析/摘要基础行为；图形布局、高亮和遍历树渲染。 |
| 集成测试 | `integration` | `domain_phase_two_tests`<br>`infrastructure_phase_four_tests`<br>`application_phase_five_tests`<br>`presentation_ui_phase_five_tests`<br>`presentation_phase_six_user_flow_tests` | 章节去重后重建统计与图；文本解析到二进制往返；导入/修改/删除失败时保持项目不变；新建、导入、查询、DFS/BFS、保存并重新打开的端到端 DTO 一致性。 |
| 文件损坏测试 | `corruption` | `infrastructure_phase_four_tests`<br>`application_phase_five_tests`<br>`infrastructure_phase_six_atomic_save_tests` | 错误魔数、不支持版本、截断文件和摘要错误；加载失败时当前项目不变；Windows `ReplaceFile` 后置失败时恢复旧文件，自动恢复也失败时保留可人工恢复的备份。 |
| 图结构验证 | `graph-validation` | `domain_graph_tests`<br>`domain_phase_two_tests`<br>`domain_phase_six_graph_validation_tests` | 每条无向边只有一个边结点且在两端边链各出现一次；项目派生统计可验证/修复；定向注入非法边链指针、非法 link、缺失端点、缺失边链出现、重复键、自环和越界共现数，并验证零共现不建边。 |
| 性能测试 | `performance` | `performance_phase_six_tests` | 5000 人物/5000 章节/5000 环形边的快照构建与完整验证、5000 次名称与关系查询、DFS+BFS 全覆盖、二进制保存/加载/逐字段往返。 |
| 用户操作流程测试 | `user-flow` | `application_phase_five_tests`<br>`presentation_graphics_phase_five_tests`<br>`presentation_ui_phase_five_tests`<br>`presentation_phase_six_user_flow_tests` | 合法导入与人工修正、重复章节原子拒绝、确认删除后表格/统计/图同步刷新、搜索人物、设置 Jaccard 阈值、真实点击人物结点和关系边、执行 DFS 并打开/关闭含虚拟根的遍历树、保存后重新打开。 |

其中前 7 个为既有回归目标；阶段六新增目标为 `domain_phase_six_graph_validation_tests`、`performance_phase_six_tests`、`infrastructure_phase_six_atomic_save_tests` 和 `presentation_phase_six_user_flow_tests`。

## 3. Jaccard 用例更正

PDF 第 43 页章节统计示例给出 `C(A)=3`、`C(B)=2`、`C(A,B)=2`，却将 `J(A,B)` 写为 `0.5`，这是笔误。按文档定义：

```text
J(A,B) = C(A,B) / (C(A) + C(B) - C(A,B))
       = 2 / (3 + 2 - 2)
       = 2/3
```

测试和验收统一以 `2/3` 为正确期望值。PDF 同页另一个 `5/4/3` 示例的结果 `0.5` 是正确的，不在更正范围内。

## 4. 自动化验收命令

在仓库根目录运行：

```powershell
# 最终验收：配置 + Debug 编译 + 全量 CTest
& .\scripts\verify.ps1

# 阶段六 4 个新增目标
& .\scripts\verify.ps1 -TestLabel 'phase-six'

# 按验收类别快速回归（示例）
& .\scripts\verify.ps1 -TestLabel 'graph-validation'
& .\scripts\verify.ps1 -TestLabel 'corruption'
& .\scripts\verify.ps1 -TestLabel 'performance'
& .\scripts\verify.ps1 -TestLabel 'user-flow'
```

如果脚本无法自动发现 Qt/MinGW，使用 `-Qt6Dir <Qt6Config.cmake 所在目录>` 和 `-QtMinGwBin <g++.exe 所在目录>` 显式指定。标签运行只用于快速定位，不代替最终的全量验收。

## 5. 性能验收

`performance_phase_six_tests` 构造一个 5000 人物、5000 章节、5000 条环形关系边的动态数据集。每人出现 2 章，每条边共现 1 次，期望 Jaccard 为 `1/3`。Debug 预算如下：

| 阶段 | Debug 上限 |
| --- | ---: |
| 快照构建、`fromSnapshot` 与完整验证 | 15 s |
| 5000 次名称与关系边查询 | 5 s |
| DFS 与 BFS 全图遍历 | 5 s |
| 二进制保存、加载与往返验证 | 15 s |

本文档不预填或推测实测耗时。验收结果以当次测试标准输出中的 `[性能]` 记录和 CTest 退出码为准，并应与机器、系统负载和工具链信息一起归档。

## 6. offscreen 门禁与 Windows 截图 QA

### 6.1 自动门禁

`scripts/verify.ps1` 在测试期间设置 `QT_QPA_PLATFORM=offscreen`；各 presentation CTest 目标也在 CMake 测试属性中设置同样的环境。因此 `presentation_graphics_phase_five_tests`、`presentation_ui_phase_five_tests` 和 `presentation_phase_six_user_flow_tests` 可在无可见桌面的环境中检查组件结构、信号、真实鼠标事件和状态同步。offscreen 通过只证明功能门禁通过，不能代替字体、DPI、层叠和视觉高亮的逐张检查。

### 6.2 Windows 原生截图流程

1. 先运行全量 `scripts/verify.ps1` 并确认自动门禁通过。
2. 在可见的 Windows 桌面会话中，将 Qt `bin` 和对应 MinGW `bin` 加入当前 PowerShell `PATH`；设置 `QT_QPA_PLATFORM=windows`。
3. 直接运行已编译的 `presentation_ui_phase_five_tests.exe populatedWindowRendersForVisualQa`，不要通过 CTest 运行，否则 CMake 的 offscreen 测试属性会生效。可使用以下环境变量保存图片：

   ```powershell
   $env:QT_QPA_PLATFORM = 'windows'
   $env:PHASE5_QA_OUTPUT = Join-Path (Get-Location) 'artifacts\phase-six-ui'
   $env:PHASE5_QA_SUFFIX = 'windows-debug'
   & '<build-qt-*>\presentation_ui_phase_five_tests.exe' populatedWindowRendersForVisualQa
   ```

4. 确认输出包含 `main-graph`、`edge-hover`、`person-table`、`relation-detail`、`traversal-highlight` 和 `traversal-tree` 六类截图；逐张检查中文、布局、表格/详情一致性、结点/边选中态和遍历树虚拟根及连线。
5. 运行 `presentation_phase_six_user_flow_tests`，以真实按钮和鼠标事件重放重复导入、确认删除、阈值调整、人物/关系点击和遍历树打开/关闭流程，核对截图所示 UI 与业务数据一致。
6. 记录 Windows 版本、缩放比例、Qt 版本、检查执行者和截图路径；如有不一致，本阶段不通过。

本轮实际命令、环境、耗时、CTest 结果和截图清单见 [阶段六验收记录](phase-six-acceptance-record.md)。
