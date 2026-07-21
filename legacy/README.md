# 无向网邻接多重表课设原型

本项目使用 C++17 实现无向连通网的邻接多重表，包括边的插入、覆盖、删除、DFS/BFS 遍历，以及 BFS 生成树的孩子链表表示。

## 输入文件

```text
5 6
A B C D E
A B 10
A C 3
B C 4
B D 8
C E 6
D E 2
```

第一行是顶点数和边数；第二行是全部顶点标志；之后每行是一条无向边的两个端点和整数权值。顶点标志不能包含空白。

## 构建与运行

```powershell
.\scripts\verify.ps1
.\build-mingw\graph_app.exe .\data\graph.txt
```

`verify.ps1` 会依次执行 CMake 配置、编译和 CTest。由于当前工程路径包含中文，而部分 MinGW Makefiles 版本不能正确处理这种路径，脚本会在执行期间临时将工程映射到未占用的 `R:` 盘，结束后自动移除映射；生成物仍保存在本目录的 `build-mingw` 中。

如果项目位于纯英文路径，也可以直接使用常规 CMake 命令：

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

程序不会自动覆盖输入文件。修改图后，可通过菜单中的“保存到文件”将当前图写入新文件。
