#include "ui/RecoveryStore.hpp"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace ui {
namespace {

constexpr quint32 kRecoveryMagic = 0x4E4C5243;  // NLRC
constexpr quint32 kRecoveryVersion = 1;
// 反序列化上限：防止损坏文件把计数读成超大值后一次分配巨量内存。
constexpr qint32 kRecoveryMaxItems = 1000000;

bool writeChapter(QDataStream& out, const novel::Chapter& ch) {
  out << qint32(ch.index) << qint32(ch.level);
  out << QString::fromStdString(ch.title);
  out << QString::fromStdString(ch.heading_line);
  out << quint64(ch.start_line) << quint64(ch.end_line);
  out << quint8(ch.synthetic ? 1 : 0);
  out << quint8(ch.renamed ? 1 : 0);
  out << quint8(ch.level_changed ? 1 : 0);
  out << quint8(ch.inserted ? 1 : 0);
  out << quint8(ch.suppressed ? 1 : 0);
  out << qint32(ch.skip_lines.size());
  for (const std::size_t line : ch.skip_lines) out << quint64(line);
  out << QString::fromStdString(ch.number);
  out << QString::fromStdString(ch.name);
  out << QString::fromStdString(ch.separator);
  out << quint8(ch.numbered ? 1 : 0);
  out << quint8(ch.excluded_from_numbering ? 1 : 0);
  out << qint32(ch.extra_body.size());
  for (const novel::LineRange& range : ch.extra_body) {
    out << quint64(range.begin) << quint64(range.end);
  }
  return out.status() == QDataStream::Ok;
}

bool readChapter(QDataStream& in, novel::Chapter& ch) {
  qint32 index = 0;
  qint32 level = 2;
  quint64 start = 0;
  quint64 end = 0;
  quint8 synthetic = 0;
  quint8 renamed = 0;
  quint8 levelChanged = 0;
  quint8 inserted = 0;
  quint8 suppressed = 0;
  qint32 skipCount = 0;
  qint32 extraCount = 0;
  quint8 numbered = 0;
  quint8 excluded = 0;
  QString title;
  QString heading;
  QString number;
  QString name;
  QString separator;
  in >> index >> level >> title >> heading >> start >> end >> synthetic >>
      renamed >> levelChanged >> inserted >> suppressed >> skipCount;
  if (skipCount < 0 || skipCount > kRecoveryMaxItems) return false;
  ch.skip_lines.resize(static_cast<std::size_t>(skipCount));
  for (std::size_t& line : ch.skip_lines) {
    quint64 value = 0;
    in >> value;
    line = static_cast<std::size_t>(value);
  }
  in >> number >> name >> separator >> numbered >> excluded >> extraCount;
  ch.title = title.toStdString();
  ch.heading_line = heading.toStdString();
  ch.number = number.toStdString();
  ch.name = name.toStdString();
  ch.separator = separator.toStdString();
  if (extraCount < 0 || extraCount > kRecoveryMaxItems) return false;
  ch.extra_body.resize(static_cast<std::size_t>(extraCount));
  for (novel::LineRange& range : ch.extra_body) {
    quint64 begin = 0;
    quint64 finish = 0;
    in >> begin >> finish;
    range.begin = static_cast<std::size_t>(begin);
    range.end = static_cast<std::size_t>(finish);
  }
  ch.index = index;
  ch.level = level;
  ch.start_line = static_cast<std::size_t>(start);
  ch.end_line = static_cast<std::size_t>(end);
  ch.synthetic = synthetic != 0;
  ch.renamed = renamed != 0;
  ch.level_changed = levelChanged != 0;
  ch.inserted = inserted != 0;
  ch.suppressed = suppressed != 0;
  ch.numbered = numbered != 0;
  ch.excluded_from_numbering = excluded != 0;
  return in.status() == QDataStream::Ok;
}

}  // namespace

QString recoveryFilePath() {
  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (dir.isEmpty()) dir = QDir::tempPath();
  QDir().mkpath(dir);
  return dir + QDir::separator() + QStringLiteral("recovery.bin");
}

bool writeRecovery(const QString& path, const RecoveryData& data) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  QDataStream out(&file);
  out.setVersion(QDataStream::Qt_5_9);
  out << kRecoveryMagic << kRecoveryVersion;
  out << data.sourcePath;
  out << qint64(data.sourceSize);
  out << qint64(data.sourceMTime);
  out << qint64(data.savedAtMs);
  out << data.encoding;
  out << quint8(data.acceptUnnumbered ? 1 : 0);
  out << qint32(data.selectedSourceRow);

  out << qint32(data.chapters.size());
  for (const novel::Chapter& ch : data.chapters) {
    if (!writeChapter(out, ch)) {
      file.cancelWriting();
      return false;
    }
  }

  out << qint32(data.excluded.size());
  for (const novel::LineRange& range : data.excluded) {
    out << quint64(range.begin) << quint64(range.end);
  }
  out << qint32(data.exclusions.size());
  for (const novel::TextExclusion& exclusion : data.exclusions) {
    out << quint64(exclusion.line) << quint64(exclusion.begin_byte)
        << quint64(exclusion.end_byte);
  }

  if (out.status() != QDataStream::Ok) {
    file.cancelWriting();
    return false;
  }
  return file.commit();
}

bool readRecovery(const QString& path, RecoveryData& data) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  QDataStream in(&file);
  in.setVersion(QDataStream::Qt_5_9);

  quint32 magic = 0;
  quint32 version = 0;
  in >> magic >> version;
  if (magic != kRecoveryMagic || version != kRecoveryVersion) return false;

  qint64 sourceSize = 0;
  qint64 sourceMTime = 0;
  qint64 savedAt = 0;
  quint8 acceptUnnumbered = 0;
  qint32 selected = -1;
  in >> data.sourcePath >> sourceSize >> sourceMTime >> savedAt >>
      data.encoding >> acceptUnnumbered >> selected;
  if (data.sourcePath.isEmpty()) return false;
  data.sourceSize = sourceSize;
  data.sourceMTime = sourceMTime;
  data.savedAtMs = savedAt;
  data.acceptUnnumbered = acceptUnnumbered != 0;
  data.selectedSourceRow = selected;

  qint32 chapterCount = 0;
  in >> chapterCount;
  if (chapterCount < 0 || chapterCount > kRecoveryMaxItems) return false;
  data.chapters.resize(static_cast<std::size_t>(chapterCount));
  for (novel::Chapter& ch : data.chapters) {
    if (!readChapter(in, ch)) return false;
  }

  qint32 excludedCount = 0;
  in >> excludedCount;
  if (excludedCount < 0 || excludedCount > kRecoveryMaxItems) return false;
  data.excluded.resize(static_cast<std::size_t>(excludedCount));
  for (novel::LineRange& range : data.excluded) {
    quint64 begin = 0;
    quint64 finish = 0;
    in >> begin >> finish;
    range.begin = static_cast<std::size_t>(begin);
    range.end = static_cast<std::size_t>(finish);
  }

  qint32 exclusionCount = 0;
  in >> exclusionCount;
  if (exclusionCount < 0 || exclusionCount > kRecoveryMaxItems) return false;
  data.exclusions.resize(static_cast<std::size_t>(exclusionCount));
  for (novel::TextExclusion& exclusion : data.exclusions) {
    quint64 line = 0;
    quint64 begin = 0;
    quint64 finish = 0;
    in >> line >> begin >> finish;
    exclusion.line = static_cast<std::size_t>(line);
    exclusion.begin_byte = static_cast<std::size_t>(begin);
    exclusion.end_byte = static_cast<std::size_t>(finish);
  }

  return in.status() == QDataStream::Ok;
}

}  // namespace ui
