# novelsort

TXT 小说章节识别与重排工具。核心 `novel_core` 为纯 C++20，零第三方依赖；
桌面版为 Qt 5 Widgets（C++）。不含 Python / JS / Electron。

- 自动识别章节标题，生成可编辑目录。
- 拖拽调整章节顺序，正文同步重排。
- 导出 TXT 与目录 JSON，绝不静默丢字。
- 异常退出可恢复未导出的编辑。

## 构建

| 依赖 | 版本 |
|---|---|
| Visual Studio | 2022（MSVC） |
| CMake | ≥ 3.20 |
| Qt | 5 Widgets（仅桌面版） |

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="<Qt5 安装目录>"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`<Qt5 安装目录>` 替换为 Qt 5 实际安装路径。

| 产物 | 说明 |
|---|---|
| `build\Release\novellines-gui.exe` | 桌面版 |
| `build\Release\novellines-cli.exe` | 命令行 / 批处理 |

发布前复制 Qt 运行库（只需一次）：

```powershell
cmake --build build --config Release --target deploy-gui
```

仓库只含合成测试文本。外部版权样本放 `tests/data/real_local/`，不入库。

## 桌面版

```powershell
build\Release\novellines-gui.exe
build\Release\novellines-gui.exe --input book.txt
build\Release\novellines-gui.exe --input book.txt --page dialogue
```

### 子页面

- 章节整理：原章节识别、重排与目录导出。
- 台词拆分：打开 TXT，按章列出章节；点章查看该章片段与详情，导出脚本 JSON。

启动时加 `--page dialogue` 可直接打开台词拆分页。

### 布局（章节整理页）

| 区域 | 说明 |
|---|---|
| 左侧目录 | 固定 380px，占满窗口高度 |
| 右侧工具条 | 文件、导出、主题 |
| 右侧预览 | 当前章节正文 |

### 操作

| 操作 | 方式 |
|---|---|
| 排序 | 拖动章节行；`Alt+↑` / `Alt+↓` |
| 改名 | 双击或 `F2`，序号与标题两列 |
| 编辑 | 右键：合并、删除、拆分、参与排序、正文划转 |
| 无效内容 | 预览选中文本后右键标记，导出时丢弃 |
| 撤销 | `Ctrl+Z` / `Ctrl+Y`，最多 60 步 |
| 打开 | `Ctrl+O`，或直接拖入 TXT |

撤销快照总占用上限 24MB，超大目录自动少留几步。
撤销只作用于章节编辑，不影响主题与搜索。

### 导出

工具条常驻「导出 TXT」「导出目录 JSON」。

「导出到程序位置：开」时直接写 exe 同级 `<名>_reordered.txt`，
已存在则询问覆盖 / 另存；位置由 QSettings 记忆。
导出成功后「打开导出位置」可用。

### 恢复

编辑后约 1.5 秒写入恢复备份：
`%APPDATA%\novel-lines\novel-lines\recovery.bin`。

- 异常退出后下次启动提示恢复。
- 正常导出、恢复原样、正常关闭会清理备份。
- 备份依赖原 TXT 路径、大小与修改时间；原文件被移动或修改后无法恢复。
- 备份只存章节状态与区间，不存原文全文。

启动报 `Qt platform plugin "windows"`：确认发布目录存在
`platforms\qwindows.dll`，不要只复制 exe。

## 命令行

```powershell
novellines-cli --input book.txt
novellines-cli --input book.txt --move 271:1 --out book_reordered.txt
novellines-cli --input book.txt --move 271:1 --out book_reordered.txt --json toc.json
novellines-cli --input book.txt --script script.json
novellines-cli --input book.txt --title-lines
```

| 参数 | 说明 |
|---|---|
| `--input <file>` | 输入 TXT，必填 |
| `--out <file>` | 重排后另存 TXT |
| `--json <file>` | 导出目录 JSON |
| `--script <file>` | 导出有声书脚本 JSON |
| `--move from:to` | 1 基位置，可重复，按序应用 |
| `--title-lines` | 识别无编号短标题（默认关闭） |
| `--dry-run` | 只解析并打印统计 |

无参数或参数错误时打印用法并返回 3。

### 脚本 JSON

`--script` 输出 `novelsort/script/1`。
片段类型为 `heading`、`narration`、`dialogue`、`thought`。
保留解码、统一换行后的 UTF-8 字节偏移。
`speaker_hint` 只是归属候选，不是最终说话人。
格式见 [有声书脚本数据层](docs/有声书脚本数据层.md)。

## 识别规则

| 类别 | 示例 |
|---|---|
| Markdown | `# 标题`、`###第一章` |
| 中文编号 | `第X章/回/话/节/幕/集`、`第X卷/部/篇` |
| 特殊章名 | 序章、楔子、前言、终章、后记、番外、附录等 |
| 英文 | `Chapter`、`Volume`、`Prologue`、`Epilogue` |
| 括号与数字 | `(1) 标题`、`1. 标题`、`I. 标题` |

数字形式支持阿拉伯、全角、中文数字（含百千万亿、大写与异体）。

- 开头目录区（连续 ≥3 条标题，带「目录」或与后文重复）自动去掉，不重复计数。
- 首章之前若有正文，生成「（前言）」章；全书无标题生成「（全文）」。
- 编码：UTF-8 / UTF-8 BOM / UTF-16LE / UTF-16BE / GB18030。
- 容错：严格解码失败且坏字节 ≤ 0.001% 时按 `U+FFFD` 替换打开，
  预览、状态栏与 JSON 保留警告；比例过高仍拒绝。

## 导出格式

| 格式 | 说明 |
|---|---|
| TXT | UTF-8、LF、章间一个空行 |
| JSON | 版本、源文件、编码、章节、目录行、警告 |

## 限制

- 重排以标题块为单位；卷标题不会自动携带其下章节。
- GB18030 解码仅 Windows。
- `--title-lines` 为启发式，默认关闭以免把对白当标题。
- 仅 TXT，无 EPUB / PDF / DOCX。
- 预览单章最多 5 万字；自动备份只保留一份。

## 开发文档

| 文档 | 内容 |
|---|---|
| [文档索引](docs/文档索引.md) | 全部文档入口与维护约定 |
| [更新日志](更新日志.md) | 按发布日期记录变更 |
| [设计说明](docs/C++20重写设计方案.md) | 分层、数据模型、解析、编辑、导出、恢复 |
| [代码与性能准则](docs/代码与性能准则.md) | 估算、约束、审查与测量 |
| [验收报告](docs/验收报告.md) | 自动验证、样本与手工检查 |

## 许可

GPL-3.0，见 `LICENSE`。第三方组件见 `THIRD_PARTY_NOTICES.txt`。

- 源码：https://github.com/yezijinn/novelsort
- 镜像：https://gitee.com/yezijinn/novelsort
