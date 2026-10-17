# novel-lines-cpp

TXT 小说章节识别与重排工具。全项目纯 C++20：`novel_core` 零第三方依赖，
桌面版使用 Qt Widgets（C++），不使用 Python / JS / Electron。

## 构建

需要 Visual Studio 2022（MSVC）、CMake；桌面版另需 Qt 5 Widgets。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="<Qt5 安装目录>"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

将 `<Qt5 安装目录>` 替换为 Qt 5 的实际安装路径。

产物：
- `build\Release\novellines-gui.exe`（桌面版）
- `build\Release\novellines-cli.exe`（命令行 / 批处理）

仓库只包含合成测试文本；外部版权样本放入 `tests/data/real_local/`，不入库。

把 Qt 运行库复制到输出目录（只需一次）：

```powershell
cmake --build build --config Release --target deploy-gui
```

## 桌面版

```powershell
build\Release\novellines-gui.exe
build\Release\novellines-gui.exe --input book.txt
```

紧凑单窗布局：左侧 380px 章节目录从标题栏下方一直延伸到窗口底部，占满全高；
右侧为文件/导出工具条和正文预览。章节列表可直接按住行拖动排序（Alt+↑/↓ 可键盘移动），
右键可改名、并入上一章、整章删除；预览区可拆分章节。
打开/拖入新文件前若存在未导出的修改，会提示「导出后继续 / 放弃修改 / 取消」；
加载和导出期间显示等待光标与状态提示。
目录区直接可见「重新编排章节」「全部恢复原样」按钮与「识别无编号标题：开/关」文字按钮；
右侧「排序提示」会说明鼠标拖动与 Alt+↑/↓ 键盘排序。
序章 / 终章 / 后记等无编号章节可右键「参与排序」，自动占用下一个序号并顺延后续章节，操作可撤销。
支持撤销 / 重做（`Ctrl+Z` / `Ctrl+Y`），最多 60 步；快照总占用上限 24MB，
超大目录会自动少留几步。撤销/重做只作用于章节编辑（重命名、拆分、合并、删除、移动、重编号、
正文划转、无效内容标记等），不改变主题或搜索；新编辑会清空重做栈。
明 / 暗主题（顶栏「明暗主题」按钮切换，QSettings 持久化，默认暗色）。
导出工具条常驻可见「导出 TXT」「导出目录 JSON」；导出 TXT 默认「导出到程序位置：开」
（直接写 exe 同级 `<名>_reordered.txt`，已存在询问覆盖 / 另存，QSettings 记忆），
切到「关」后每次弹保存框；
JSON 导出保持原保存框行为。导出成功后导出工具条「打开导出位置」按钮启用，点击用资源管理器定位文件。
支持拖拽 TXT、`Ctrl+O`、`Alt+↑/↓`。

编辑后约 1.5 秒自动写入恢复备份（`%APPDATA%\novel-lines\novel-lines\recovery.bin`）。
程序异常退出后，下次启动会提示恢复；正常导出、恢复原样或正常关闭会清理备份。
备份依赖原 TXT 路径及文件大小/修改时间，原文件被移动或修改后无法安全恢复。

启动报 `Qt platform plugin "windows"`：确认发布目录存在
`platforms\qwindows.dll`，不要只复制 exe。普通启动会强制使用 `windows`
平台；环境变量里的 `offscreen` / `minimal` 不会生效。

## 命令行

解析并预览章节目录（`--dry-run` 只打印统计）：

```powershell
novellines-cli --input book.txt
```

把第 271 章移到第 1 位，正文同步重排后另存：

```powershell
novellines-cli --input book.txt --move 271:1 --out book_reordered.txt
```

同时导出目录 JSON：

```powershell
novellines-cli --input book.txt --move 271:1 --out book_reordered.txt --json toc.json
```

无编号短标题（如「启程」「相遇」单独成行）需显式开启：

```powershell
novellines-cli --input book.txt --title-lines
```

参数：

| 参数 | 说明 |
|---|---|
| `--input <file>` | 输入 TXT，必填 |
| `--out <file>` | 重排后另存 TXT（UTF-8、LF、章间一个空行） |
| `--json <file>` | 导出目录 JSON |
| `--move from:to` | 1 基位置；可重复，按顺序依次应用 |
| `--title-lines` | 识别无编号短标题行（默认关闭） |
| `--dry-run` | 只解析并打印统计，不写文件 |

无参数或参数错误时打印用法并返回 3。

## 识别规则

- 行首 `#` / `##`；`###第一章` 这类无空格井号也认。
- `第X章/回/话/節/节/幕/集`、`第X卷/部/篇`；阿拉伯、全角、中文数字（含百千万亿、大写与异体）。
- `序章/序言/楔子/引子/前言/尾声/终章/后记/番外/外传/前传/后传/特典/附录/跋` 一律算章节。
- 英文 `Chapter/Volume/Part/Book/Episode/Prologue/Epilogue`。
- `(1) 标题`、`1. 标题`、`I. 标题`。
- 开头目录区（连续 ≥3 条标题，带「目录」或与后文重复）自动去掉，不重复计数。
- 第一章之前若有正文，生成可见的「（前言）」章，绝不静默丢字；全书无标题则生成「（全文）」。
- 编码：UTF-8 / UTF-8 BOM / UTF-16LE / UTF-16BE / GB18030（Windows）。
- 编码容错：严格 UTF-8 / GB18030 失败时，仅当坏字节不超过 0.001% 才按 U+FFFD 替换打开；预览、状态栏和目录 JSON 均保留警告，比例过高仍拒绝。预览警告条和右下角「坏字节 N 处」可点击逐处定位。

## 限制

- 重排以标题块为单位；卷标题不会自动携带其下章节。
- GB18030 解码仅 Windows。
- `--title-lines` 是启发式，默认关闭以免把对白当标题。
- 仅 TXT，无 EPUB/PDF/DOCX。

## 开发文档

- [文档索引](docs/文档索引.md)：全部文档入口与维护约定。
- [更新日志](更新日志.md)：按发布日期记录功能、变更与修复。
- [代码与性能准则](docs/代码与性能准则.md)：写之前如何估算，写的时候如何落地，写完后如何审查和测量。
- [设计说明](docs/C++20重写设计方案.md)：分层、数据模型、解析、编辑、导出与恢复。
- [验收报告](docs/验收报告.md)：自动验证、样本与手工检查。

## 许可

GPL-3.0，见 `LICENSE`；第三方组件见 `THIRD_PARTY_NOTICES.txt`。
