#include "ui/Selftest.hpp"

#include <cstdio>
#include <fstream>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QModelIndex>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include "core/ChapterOrder.hpp"
#include "core/ChapterParser.hpp"
#include "core/DirectoryEdit.hpp"
#include "core/TextFile.hpp"
#include "core/TextUtf8.hpp"
#include "ui/ChapterDialogs.hpp"
#include "ui/ChapterModel.hpp"
#include "ui/ChapterTableView.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PreviewEdit.hpp"
#include "ui/Theme.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ui {
namespace {

// string_view 转 QString（UTF-8），零拷贝。
QString qs(std::string_view s) {
  return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

}  // namespace

int runSelftest(MainWindow& w, const std::filesystem::path& path,
                const std::string& outDir) {
  int failures = 0;
  w.recoveryEnabled_ = false;
  if (w.recoveryTimer_) w.recoveryTimer_->stop();
#ifdef _WIN32
  // PowerShell 下 stdout 为本地代码页，中文会乱码；统一切到 UTF-8。
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  std::error_code ec;
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    std::fprintf(stderr, "自检：创建输出目录失败 %s: %s\n", outDir.c_str(),
                 ec.message().c_str());
    return ++failures;
  }

  // 自检日志写文件（UTF-8），控制台只打印最终结论，避免管道乱码。
  const std::string logPath = outDir + "/selftest.log";
  std::ofstream logF(logPath, std::ios::binary);
  logF << "\xEF\xBB\xBF";  // UTF-8 BOM

  const auto check = [&](const char* step, bool ok, const std::string& detail) {
    std::string line = ok ? "[PASS] " : "[FAIL] ";
    line += step;
    if (!detail.empty()) {
      line += "  ";
      line += detail;
    }
    line += "\n";
    std::printf("%s", line.c_str());
    logF << line;
    if (!ok) ++failures;
  };

  const std::string kDelTitle = "第三章 离别";
  novel::TextLoadResult loaded = novel::load_text_file(path);
  check("加载文件", loaded.ok, loaded.error);
  if (!loaded.ok) return failures;

  // 先确定 w.loadedText_（之后不再修改），再切分视图，保证 w.lines_ 不悬垂。
  w.loadedText_ = std::move(loaded.text);
  w.loadedEncoding_ = loaded.encoding;
  w.encodingWarnings_ = loaded.warnings;
  w.warnings_ = w.encodingWarnings_;
  novel::ParseOptions opts;
  opts.accept_unnumbered_titles = false;
  novel::ParseResult parsed = novel::parse_chapters(w.loadedText_, opts);
  w.tocLines_ = parsed.toc_lines;
  w.initialChapters_ = parsed.chapters;
  w.lines_ = novel::split_lines_view(w.loadedText_);
  w.previewCache_.reset();
  w.isLoaded_ = true;
  w.currentFile_ = path;
  w.chapterModel_->resetChapters(parsed.chapters);
  std::vector<novel::Chapter>& chs = w.chapterModel_->chapters();

  auto sizeOf = [&]() { return chs.size(); };

  // 1. 改名第 2 章
  check("初始 >=3 章", sizeOf() >= 3, "章数=" + std::to_string(sizeOf()));
  w.pushUndo();
  check("撤销快照已计字节",
        !w.undoStack_.empty() && w.undoStack_.back().bytes > 0,
        "bytes=" + std::to_string(w.undoStack_.empty()
 ? 0
                                      : w.undoStack_.back().bytes));
  novel::rename_chapter(chs[1], "第二章 初遇之夜");
  check("改名第2章", chs[1].title == "第二章 初遇之夜",
        "title=" + chs[1].title);

  // 2. 设为卷（第 2 章）
  w.pushUndo();
  novel::set_chapter_level(chs[1], 1);
  check("设为卷", chs[1].level == 1, "level=" + std::to_string(chs[1].level));

  // 3. 合并第 3 章（并入上一章，保留标题）
  const std::size_t before3 = sizeOf();
  w.pushUndo();
  const bool merged = novel::merge_with_previous(chs, 2, false);
  check("合并第3章", merged && sizeOf() == before3 - 1,
        "size=" + std::to_string(sizeOf()));

  // 4. 删除「第三章 离别」（按标题查找，合并后 index 3 已非原第 3 章）
  int delIdx = -1;
  for (int i = 0; i < static_cast<int>(chs.size()); ++i) {
    if (chs[static_cast<std::size_t>(i)].title == kDelTitle) {
      delIdx = i;
      break;
    }
  }
  check("找到第三章", delIdx >= 0, "title=" + kDelTitle);
  const std::size_t before4 = sizeOf();
  bool deleted = false;
  if (delIdx >= 0) {
    w.pushUndo();
    deleted = novel::delete_chapter(chs, delIdx, w.excludedRanges_);
    check("删除第4章", deleted && sizeOf() == before4 - 1,
          "size=" + std::to_string(sizeOf()));
  }

  // 5. 在第 1 章正文第 2 行拆分
  // 第 1 章正文：start_line 之后跳过标题行，取第 2 个正文行
  const std::size_t firstBody = chs[0].start_line + 1 + 1;  // 标题行后第 2 行
  w.pushUndo();
  const bool splitted = novel::split_chapter(chs, 0, firstBody,
                                             "第1.5章 前夜", 2);
  check("拆分第1章", splitted, "split at line " + std::to_string(firstBody));
  if (splitted) {
    check("拆分后章数 +1", sizeOf() == before4 - 1 + 1,
          "size=" + std::to_string(sizeOf()));
    check("新章标题正确", chs[1].title == "第1.5章 前夜",
          "title=" + chs[1].title);
  }

  // 6. 末尾新增
  const std::size_t beforeAdd = sizeOf();
  w.pushUndo();
  const bool appended =
      novel::append_chapter(chs, w.lines_.size(), "第N章 尾声之后", 2);
  check("末尾新增", appended && sizeOf() == beforeAdd + 1,
        "size=" + std::to_string(sizeOf()));

  // 7. 阿拉伯重编号
  w.pushUndo();
  const bool renum = novel::renumber_chapters(chs, 1, false);
  check("重编号", renum, "");
  // 验证第1章已重编号（标题含「第1章」或「第 1 章」）
  {
    bool found = false;
    for (const auto& c : chs) {
      if (c.title.find("第1章") != std::string::npos ||
          c.title.find("第 1 章") != std::string::npos) {
        found = true;
        break;
      }
    }
    check("重编号生效", found, "chapters[0].title=" + chs[0].title);
  }

  // 7b. 撤销两次回到末尾新增前（章数 -1），再重做两次回到重编号后
  const std::size_t afterRenum = sizeOf();
  w.undo();
  w.undo();
  check("撤销2次章数回退", sizeOf() == afterRenum - 1,
        "size=" + std::to_string(sizeOf()));
  // 此时应回到「末尾新增」之前，且重编号已撤销（第1章不再是阿拉伯编号）
  {
    bool found = false;
    for (const auto& c : chs) {
      if (c.title.find("第1章") != std::string::npos ||
          c.title.find("第 1 章") != std::string::npos) {
        found = true;
        break;
      }
    }
    check("撤销2次重编号已撤回", !found, "chapters[0].title=" + chs[0].title);
  }
  w.redo();
  w.redo();
  check("重做2次章数恢复", sizeOf() == afterRenum,
        "size=" + std::to_string(sizeOf()));
  {
    bool found = false;
    for (const auto& c : chs) {
      if (c.title.find("第1章") != std::string::npos ||
          c.title.find("第 1 章") != std::string::npos) {
        found = true;
        break;
      }
    }
    check("重做2次重编号生效", found, "chapters[0].title=" + chs[0].title);
  }

  // 8. 导出 TXT / JSON 到 %TEMP%
  const std::string txtOut = outDir + "/selftest_out.txt";
  const std::string jsonOut = outDir + "/selftest_out.json";
  const std::string txt = novel::render_txt(w.loadedText_, chs, w.tocLines_,
                                             w.excludedRanges_);
  {
    std::ofstream fo(txtOut, std::ios::binary);
    fo << txt;
    check("导出 TXT", fo.good(),
          "path=" + txtOut + " size=" + std::to_string(txt.size()));
  }
  const std::string js =
      novel::directory_json(novel::DirectoryJsonInput{novel::path_to_utf8(path),
                                                      w.loadedEncoding_, &chs,
                                                      &w.tocLines_, &w.warnings_});
  {
    std::ofstream fo(jsonOut, std::ios::binary);
    fo << js;
    check("导出 JSON", fo.good(), "path=" + jsonOut);
  }

  // 9. 字符串断言验证导出内容
  {
    bool hasNew = txt.find("第1.5章 前夜") != std::string::npos;
    check("导出含新章标题", hasNew, "");
    bool hasAppended = txt.find("第N章 尾声之后") != std::string::npos;
    check("导出含新增章标题", hasAppended, "");
    // 被删除的「第三章 离别」不应出现在导出文本中
    bool stillHas = txt.find(kDelTitle) != std::string::npos;
    check("删除章已移除", !stillHas, "title=" + kDelTitle);
  }

  // 10. 设置为无效内容：预览选中「序章」正文「很久以前」4 字，导出时被丢弃。
  {
    // 重新静默加载，保证 序章 正文完整（前序步骤对已改动章节做了拆分/删除）。
    w.loadFile(path, true);
    const QModelIndex idx0 =
        w.proxy_->mapFromSource(w.chapterModel_->index(0, 0));
    if (idx0.isValid()) {
      w.tableView_->setCurrentIndex(idx0);
      w.previewSourceRow(0);
    }
    // 序章正文第 1 行（index 0 章，start_line=0 为标题，正文第 1 行 line=2）
    const std::string target = "很久以前。";
    // 在预览文档中定位该行
    QTextDocument* doc = w.previewEdit_->document();
    int foundBlock = -1;
    QTextBlock blk = doc->firstBlock();
    for (; blk.isValid(); blk = blk.next()) {
      if (blk.text().contains(QString::fromStdString(target))) {
        foundBlock = blk.blockNumber();
        break;
      }
    }
    check("预览定位序章正文", foundBlock >= 0, "block=" + std::to_string(foundBlock));
    if (foundBlock >= 0) {
      const QTextBlock fb = doc->findBlockByNumber(foundBlock);
      // 选中前 4 个汉字「很久以前」
      QTextCursor sel(doc);
      sel.setPosition(fb.position());
      sel.setPosition(fb.position() + 4, QTextCursor::KeepAnchor);
      w.previewEdit_->setTextCursor(sel);

      w.pushUndo();
      w.onSetInvalid();
      check("标记无效内容非空", !w.textExclusions_.empty(),
            "size=" + std::to_string(w.textExclusions_.size()));

      const auto hasInvalidStrike = [&]() {
        QTextDocument* current = w.previewEdit_->document();
        for (QTextBlock b = current->firstBlock(); b.isValid(); b = b.next()) {
          if (!b.text().contains(QStringLiteral("很久以前"))) continue;
          QTextCursor c(current);
          c.setPosition(b.position());
          c.setPosition(b.position() + 1, QTextCursor::KeepAnchor);
          return c.charFormat().fontStrikeOut();
        }
        return false;
      };
      check("无效内容有删除线", hasInvalidStrike(), "");
      w.applyThemeToWindow(false);
      check("切亮色后删除线保留", hasInvalidStrike(), "");
      w.applyThemeToWindow(true);
      check("切回暗色后删除线保留", hasInvalidStrike(), "");

      // 导出到 %TEMP%，断言该段被丢弃、其余正文仍在
      const std::string invOut = outDir + "/selftest_invalid.txt";
      const std::string invTxt = novel::render_txt(
          w.loadedText_, w.chapterModel_->chapters(), w.tocLines_, w.excludedRanges_,
          w.textExclusions_);
      {
        std::ofstream fo(invOut, std::ios::binary);
        fo << invTxt;
        check("导出(含无效内容)", fo.good(), "path=" + invOut);
      }
      bool dropped = invTxt.find(target) == std::string::npos;
      check("导出不含被标无效内容", dropped, "target=" + target);
      // 其余正文仍在
      bool restKept = invTxt.find("少年背起行囊。") != std::string::npos;
      check("其余正文保留", restKept, "");

      // 撤销后 exclusions 清空
      w.undo();
      check("撤销后无效内容清空", w.textExclusions_.empty(),
            "size=" + std::to_string(w.textExclusions_.size()));
    }
  }

  // 11. 两列字段 / 排除编号 / 按当前序号拆分 / 正文划转
  w.loadFile(path, true);
  {
    const std::vector<novel::Chapter>& all = w.chapterModel_->chapters();
    const int n0 = static_cast<int>(all.size());
    int firstNum = -1;
    int secondNum = -1;
    int firstPlain = -1;
    for (int i = 0; i < n0; ++i) {
      if (all[static_cast<std::size_t>(i)].numbered) {
        if (firstNum < 0) {
          firstNum = i;
        } else if (secondNum < 0) {
          secondNum = i;
        }
      } else if (firstPlain < 0) {
        firstPlain = i;
      }
    }
    check("自检文件含编号章节", firstNum >= 0 && secondNum > firstNum,
          "first=" + std::to_string(firstNum) +
              " second=" + std::to_string(secondNum));

    if (firstNum >= 0) {
      const novel::Chapter& c1 = all[static_cast<std::size_t>(firstNum)];
      const QString numCell =
          w.chapterModel_->data(w.chapterModel_->index(firstNum, 0), RoleNumberText)
              .toString();
      const QString nameCell =
          w.chapterModel_->data(w.chapterModel_->index(firstNum, 0), RoleTitleText)
              .toString();
      const QString display = w.chapterModel_->data(w.chapterModel_->index(firstNum, 0),
                                                 Qt::DisplayRole)
                                  .toString();
      const std::string composed = c1.number + c1.separator + c1.name;
      check("两列字段", numCell == qs(c1.number) && nameCell == qs(c1.name) &&
                            display == qs(c1.title) && c1.title == composed,
            "number=" + c1.number + " name=" + c1.name);
      if (firstPlain >= 0) {
        check("无编号章序号列为空",
              w.chapterModel_
                  ->data(w.chapterModel_->index(firstPlain, 0), RoleNumberText)
                  .toString()
                  .isEmpty(),
              "row=" + std::to_string(firstPlain));
      }
    }

    // 排除编号：不参与计数，后续章节仍从起始号开始。
    std::vector<novel::Chapter>& editChs = w.chapterModel_->chapters();
    if (firstNum >= 0 && secondNum > firstNum) {
      const std::string keptNumber =
          editChs[static_cast<std::size_t>(firstNum)].number;
      w.pushUndo();
      novel::set_excluded_from_numbering(
          editChs[static_cast<std::size_t>(firstNum)], true);
      const bool renumbered = novel::renumber_chapters(editChs, 1, false);
      check("排除编号后重编号", renumbered, "");
      check("排除章序号不变",
            editChs[static_cast<std::size_t>(firstNum)].number == keptNumber,
            "number=" + editChs[static_cast<std::size_t>(firstNum)].number);
      check("排除章不计数",
            editChs[static_cast<std::size_t>(secondNum)].number == "第1章",
            "number=" + editChs[static_cast<std::size_t>(secondNum)].number);
      w.undo();
      check("撤销恢复排除标记",
            !editChs[static_cast<std::size_t>(firstNum)].excluded_from_numbering,
            "");
    }

    // 供拆分 / 划转使用的正文行（跳过空行 / 跳行 / 目录行）。
    const auto bodyLineOf = [&](const novel::Chapter& c) -> long long {
      const std::size_t b = c.start_line + 1;
      for (std::size_t ln = b; ln < c.end_line && ln < w.lines_.size(); ++ln) {
        if (qs(w.lines_[ln]).trimmed().isEmpty()) continue;
        if (std::binary_search(c.skip_lines.begin(), c.skip_lines.end(), ln)) {
          continue;
        }
        if (std::find(w.tocLines_.begin(), w.tocLines_.end(), ln) != w.tocLines_.end()) {
          continue;
        }
        return static_cast<long long>(ln);
      }
      return -1;
    };

    // 按当前序号拆分：新章 = 当前 +1，后续章节各 +1。
    if (firstNum >= 0 && secondNum > firstNum) {
      std::vector<novel::Chapter>& chs2 = w.chapterModel_->chapters();
      const novel::Chapter& origin = chs2[static_cast<std::size_t>(firstNum)];
      const long long line = bodyLineOf(origin);
      check("拆分位置合法", line > 0 &&
                                static_cast<std::size_t>(line) < origin.end_line,
            "line=" + std::to_string(line));
      if (line > 0) {
        const std::size_t sizeBefore = chs2.size();
        const std::string nextOldNumber =
            chs2[static_cast<std::size_t>(secondNum)].number;
        w.pushUndo();
        const bool splitted2 = novel::split_chapter_numbered(
            chs2, firstNum, static_cast<std::size_t>(line), "",
            "拆分测试", 2);
        check("按当前序号拆分", splitted2 && chs2.size() == sizeBefore + 1,
              "size=" + std::to_string(chs2.size()));
        if (splitted2) {
          check("新章序号=当前+1",
                chs2[static_cast<std::size_t>(firstNum) + 1].number ==
                    nextOldNumber,
                "number=" +
                    chs2[static_cast<std::size_t>(firstNum) + 1].number);
          check("新章标题",
                chs2[static_cast<std::size_t>(firstNum) + 1].name == "拆分测试",
                "name=" + chs2[static_cast<std::size_t>(firstNum) + 1].name);
          check("后续章节+1",
                chs2[static_cast<std::size_t>(secondNum) + 1].number !=
                    nextOldNumber,
                "number=" +
                    chs2[static_cast<std::size_t>(secondNum) + 1].number);
        }
        w.undo();
        check("撤销拆分", chs2.size() == sizeBefore &&
                              chs2[static_cast<std::size_t>(secondNum)].number ==
                                  nextOldNumber,
              "size=" + std::to_string(chs2.size()));
      }
    }

    // 划为上一章内容：当前章只剩标题。
    if (firstNum > 0) {
      std::vector<novel::Chapter>& chs3 = w.chapterModel_->chapters();
      const std::size_t idx = static_cast<std::size_t>(firstNum);
      const std::size_t bodyLen = chs3[idx].end_line - chs3[idx].start_line;
      const std::size_t prevEnd = chs3[idx - 1].end_line;
      w.pushUndo();
      const bool toPrev = novel::move_body_to_previous(chs3, firstNum);
      check("划为上一章内容", toPrev, "");
      check("当前章只剩标题",
            chs3[idx].end_line <= chs3[idx].start_line + 1,
            "end=" + std::to_string(chs3[idx].end_line));
      check("上一章收下正文", chs3[idx - 1].end_line >= prevEnd, "");
      w.undo();
      check("撤销划转(上)",
            chs3[idx].end_line - chs3[idx].start_line == bodyLen, "");
    }

    // 划为下一章内容：正文进入邻章 extra_body，预览包含被划走的正文。
    if (firstNum >= 0 && firstNum + 1 < n0) {
      std::vector<novel::Chapter>& chs4 = w.chapterModel_->chapters();
      const std::size_t idx = static_cast<std::size_t>(firstNum);
      const long long movedLine = bodyLineOf(chs4[idx]);
      const std::size_t savedLen = chs4[idx].end_line - chs4[idx].start_line;
      w.pushUndo();
      const bool toNext = novel::move_body_to_next(chs4, firstNum);
      check("划为下一章内容", toNext, "");
      check("当前章只剩标题",
            chs4[idx].end_line <= chs4[idx].start_line + 1, "");
      check("下一章收到前置正文", !chs4[idx + 1].extra_body.empty(), "");
      if (toNext && movedLine > 0) {
        w.previewCache_.reset();
        w.previewSourceRow(firstNum + 1);
        const QString body = w.previewEdit_->document()->toPlainText();
        check("邻章预览含划入正文",
              body.contains(qs(w.lines_[static_cast<std::size_t>(movedLine)])),
              "");
      }
      w.undo();
      check("撤销划转(下)",
            chs4[idx].end_line - chs4[idx].start_line == savedLen, "");
    }
  }

  // 12. 撤销快照内存上限：小限额下应只保留最新一步；单份超限也至少保留一步。
  {
    std::vector<MainWindow::Snapshot> stack;
    for (int i = 0; i < 3; ++i) {
      MainWindow::Snapshot s;
      s.chapters.resize(1);
      s.chapters[0].title.assign(512, 'x');
      s.selectedSourceRow = 0;
      stack.push_back(std::move(s));
    }
    MainWindow::trimUndoStack(stack, 60, 1500);
    check("撤销快照按内存裁剪", stack.size() == 1,
          "size=" + std::to_string(stack.size()));
    MainWindow::trimUndoStack(stack, 60, 1);
    check("撤销至少保留一步", stack.size() == 1,
          "size=" + std::to_string(stack.size()));
  }

  logF.flush();
  if (failures == 0) {
    std::printf("自检全部通过。日志：%s\n", logPath.c_str());
  } else {
    std::printf("自检失败 %d 处。日志：%s\n", failures, logPath.c_str());
  }
  return failures;
}

}  // namespace ui
