# 小说人物关联分析系统

当前仓库已完成开发文档中的第一至第六阶段，包括领域数据结构、章节与统计、图算法、文件系统、Qt 6 图形界面，以及测试与完善。正式实现位于根目录的 `src/domain`、`src/infrastructure`、`src/application` 与 `src/presentation`，`legacy/` 仅保留为早期邻接多重表原型，不参与正式构建。

## 启动程序

完成下方“构建与测试”后，可直接双击生成的 `novel_relation_app.exe`，或在项目根目录执行：

```powershell
.\build-qt-r\novel_relation_app.exe
```

构建脚本会自动部署 Qt、MinGW 运行库和 Windows 平台插件，无需手动修改 `PATH`。如果 `R:` 已被占用，请使用脚本输出的 `build-qt-s`、`build-qt-t` 或 `build-qt-u` 目录。

## 使用说明

1. 在“文件 → 新建项目”中可选人物词典和别名词典，也可两者留空创建空项目。人物词典每行一个 UTF-8 标准人物名，空行与以 `#` 开头的注释会被忽略。
2. 别名词典每行使用一个 TAB 分隔“别名”和“已存在的标准人物名”，例如 `宝二爷<TAB>贾宝玉`；不支持别名指向另一个别名。
3. 章节文件为 UTF-8 文本，文件开头可用下列格式提供章节编号和标题；元数据后的非空内容属于正文。

   ```text
   @chapter=001
   @title=黛玉进府

   林黛玉初进贾府……
   ```

4. 点击“导入章节”后，先在预览对话框检查标准名/别名匹配、搜索补充人物、删除误识别项，或暂存新标准人物与别名；只有“确认导入”才会一次性更新项目。通过“人物管理”新增标准人物时，程序会自动扫描全部既有章节，将新识别的人物增量补入匹配章节并更新关系统计，不会覆盖原有人工选择。
5. 关系图可设置最低 Jaccard 阈值、搜索定位、拖动结点、滚轮缩放和中键/空白处平移。点击结点或边会联动右侧详情；人物表、关系表和章节表可搜索与排序。
6. 选择遍历起点后运行 DFS 或 BFS，可播放/停止高亮动画并打开遍历树。项目使用 `.nprg` 二进制文件保存，“保存”写回当前路径，“另存为”选择新路径。

## 构建与测试

项目要求 CMake 3.21 或更高版本、Qt 6 Widgets/Test，以及与该 Qt 安装匹配的 C++17 MinGW 编译器。在 Windows PowerShell 中执行：

```powershell
.\scripts\verify.ps1
```

脚本会自动寻找 `qmake` 或常见 Qt 安装目录，也可显式传入工具链：

```powershell
.\scripts\verify.ps1 `
  -Qt6Dir 'C:\Qt\6.8.3\mingw_64\lib\cmake\Qt6' `
  -QtMinGwBin 'C:\Qt\Tools\mingw1310_64\bin'
```

脚本会强制启用测试，配置、构建并运行全部 CTest。由于当前工程路径包含中文，脚本会临时选择未占用的 `R:`、`S:`、`T:` 或 `U:` 盘符映射工程目录，并在结束时自动解除；构建产物保存在根目录对应的 `build-qt-r`、`build-qt-s` 等目录，且已被 Git 忽略。找不到 Qt 或匹配的 MinGW 时脚本会明确失败，不会跳过阶段六测试。

需要快速重跑某一类阶段六门禁时，可使用 CTest 标签过滤；脚本仍会先完成统一配置和构建：

```powershell
.\scripts\verify.ps1 -TestLabel phase-six
.\scripts\verify.ps1 -TestLabel graph-validation
.\scripts\verify.ps1 -TestLabel performance
.\scripts\verify.ps1 -TestLabel user-flow
```

提交前仍应执行不带 `-TestLabel` 的完整验证。

验证后可直接启动实际生成目录中的程序，例如：

```powershell
.\build-qt-r\novel_relation_app.exe
```

在纯英文路径中也可以直接执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON -DQt6_DIR='C:\Qt\6.8.3\mingw_64\lib\cmake\Qt6'
cmake --build build
ctest --test-dir build --output-on-failure
```

## 领域层约束

人物和边由 `std::unique_ptr` 所在容器统一拥有，边链指针均为非拥有指针。项目聚合只公开子对象的只读视图，正式章节统计只能从章节记录重建。公开修改操作保持强异常安全；失败的业务请求不会改变已有章节、人物关系图或编号状态。文件读取只生成文本 DTO，持久化只处理字节与 `ProjectSnapshot`，二者都不直接修改邻接多重表内部结构。表示层只提交应用命令并消费只读 DTO，不持有可修改领域图引用。正式代码不包含可修改的业务全局变量，也不使用固定长度数组保存人物、关系或章节。
