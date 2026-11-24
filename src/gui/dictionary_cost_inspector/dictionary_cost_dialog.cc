// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#include "gui/dictionary_cost_inspector/dictionary_cost_dialog.h"

#include <QAbstractItemView>
#include <QByteArray>
#include <QCloseEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/time/time.h"
#include "gui/dictionary_cost_inspector/cost_model.h"
#include "gui/dictionary_cost_inspector/query_client.h"
#include "protocol/commands.pb.h"

namespace mozc::gui::cost_inspector {

// The worker never touches widgets. QThread::finished is delivered to the
// dialog's event loop; result_ is only read after the thread has finished.
class LookupThread final : public QThread {
 public:
  LookupThread(std::string reading, uint32_t max_entries)
      : reading_(std::move(reading)), max_entries_(max_entries) {}

  bool ok() const { return status_ == QueryStatus::kOk; }
  QueryStatus status() const { return status_; }
  const commands::DictionaryCostLookupResult& result() const { return result_; }

 protected:
  void run() override {
    // No server lifecycle operations: no launch, restart or shutdown.
    const QueryOutcome outcome =
        QueryRunningMozkey(reading_, max_entries_, absl::Seconds(5));
    status_ = outcome.status;
    if (outcome.ok()) result_ = outcome.result;
  }

 private:
  const std::string reading_;
  const uint32_t max_entries_;
  commands::DictionaryCostLookupResult result_;
  QueryStatus status_ = QueryStatus::kInvalidTarget;
};

namespace {

constexpr int kColumnCount = 7;

QString ErrorForStatus(QueryStatus status) {
  switch (status) {
    case QueryStatus::kInvalidInput:
      return QStringLiteral("読みまたは件数指定が無効です。");
    case QueryStatus::kInvalidTarget:
      return QStringLiteral("サーバーの実行ファイルパスを確認できません。");
    case QueryStatus::kServerUnavailable:
      return QStringLiteral("Mozkeyサーバーに接続できません。日本語入力が起動しているか確認してください（検索からサーバーは起動しません）。");
    case QueryStatus::kPeerMismatch:
      return QStringLiteral("接続先サーバーの実行ファイルまたはプロセスIDが一致しません。安全のため検索を中止しました。");
    case QueryStatus::kTransportError:
      return QStringLiteral("サーバーとの通信に失敗しました。検索内容は保存されていません。");
    case QueryStatus::kMalformedResponse:
      return QStringLiteral("サーバーからの応答を解釈できません。");
    case QueryStatus::kServerRejected:
      return QStringLiteral("サーバーが検索コマンドに対応していないか、検索を拒否しました。サーバーは再起動しません。");
    case QueryStatus::kUnsupportedPlatform:
      return QStringLiteral("この環境では読み取り専用検索をまだ利用できません。");
    case QueryStatus::kOk:
      return QString();
  }
  return QStringLiteral("不明な検索エラーです。");
}

QTableWidgetItem* Cell(const QString& text) {
  auto* item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  return item;
}

QTableWidgetItem* NumericCell(qint64 value) {
  auto* item = new QTableWidgetItem;
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  item->setData(Qt::DisplayRole, value);
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return item;
}

}  // namespace

DictionaryCostDialog::DictionaryCostDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(QStringLiteral("Mozkey — 辞書コスト検索（読み取り専用）"));
  resize(1050, 560);
  auto* layout = new QVBoxLayout(this);
  auto* controls = new QHBoxLayout;
  controls->addWidget(new QLabel(QStringLiteral("読み（完全一致）"), this));
  reading_ = new QLineEdit(this);
  reading_->setPlaceholderText(QStringLiteral("例：はし（表記の自動変換は行いません）"));
  controls->addWidget(reading_, 1);
  controls->addWidget(new QLabel(QStringLiteral("各辞書の最大件数"), this));
  limit_ = new QSpinBox(this);
  limit_->setRange(1, 256);
  limit_->setValue(128);
  controls->addWidget(limit_);
  search_ = new QPushButton(QStringLiteral("検索"), this);
  controls->addWidget(search_);
  layout->addLayout(controls);

  status_ = new QLabel(this);
  status_->setWordWrap(true);
  layout->addWidget(status_);
  table_ = new QTableWidget(this);
  table_->setColumnCount(kColumnCount);
  table_->setHorizontalHeaderLabels({QStringLiteral("辞書"), QStringLiteral("読み"),
                                     QStringLiteral("表記"), QStringLiteral("単語コスト"),
                                     QStringLiteral("左文脈ID"), QStringLiteral("右文脈ID"),
                                     QStringLiteral("属性ビット")});
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSortingEnabled(true);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  table_->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(table_, 1);

  notice_ = new QLabel(QStringLiteral(
      "単語コストは候補順位や総合変換スコアを示しません。接続IDは品詞名ではありません。"
      "編集・保存はできません。"), this);
  notice_->setWordWrap(true);
  layout->addWidget(notice_);

  connect(search_, &QPushButton::clicked, this, [this] { Search(); });
  connect(reading_, &QLineEdit::returnPressed, this, [this] { Search(); });
  status_->setText(QStringLiteral(
      "実行中のMozkeyサーバーを検索します。接続できなくてもサーバーの起動・再起動は行いません。"));
}

DictionaryCostDialog::~DictionaryCostDialog() {
  // Never destroy a running QThread. The bounded IPC timeout normally makes
  // this wait short, and closeEvent prevents ordinary dismissal while busy.
  if (active_) {
    active_->wait();
    delete active_;
  }
}

void DictionaryCostDialog::closeEvent(QCloseEvent* event) {
  if (active_ && active_->isRunning()) {
    status_->setText(QStringLiteral("検索の完了後に閉じられます。"));
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

void DictionaryCostDialog::SetBusy(bool value) {
  search_->setEnabled(!value);
  reading_->setEnabled(!value);
  limit_->setEnabled(!value);
}

void DictionaryCostDialog::Search() {
  if (active_) return;
  const QString source = reading_->text();
  const QByteArray utf8 = source.toUtf8();
  const std::string bytes(utf8.constData(), static_cast<size_t>(utf8.size()));
  if (QString::fromUtf8(utf8) != source ||
      !ValidReading(bytes, static_cast<uint32_t>(limit_->value()))) {
    table_->setRowCount(0);
    status_->setText(QStringLiteral("読みを確認してください。UTF-8で1～300バイト、件数上限は1～256です。"));
    return;
  }
  table_->setRowCount(0);  // Never display stale data after a new request.
  status_->setText(QStringLiteral("検索しています…"));
  SetBusy(true);
  const uint64_t gen = ++generation_;
  auto* thread = new LookupThread(bytes, static_cast<uint32_t>(limit_->value()));
  active_ = thread;
  connect(thread, &QThread::finished, this,
          [this, thread, gen] { OnFinished(thread, gen); });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

void DictionaryCostDialog::OnFinished(LookupThread* thread, uint64_t generation) {
  if (!active_ || active_ != thread || generation != generation_) return;
  SetBusy(false);
  active_ = nullptr;
  if (!thread->ok()) {
    table_->setRowCount(0);
    status_->setText(ErrorForStatus(thread->status()));
    return;
  }
  FillTable(*thread);
}

void DictionaryCostDialog::FillTable(const LookupThread& thread) {
  const auto& result = thread.result();
  const auto rows = CopyRows(result);
  table_->setSortingEnabled(false);
  table_->setRowCount(static_cast<int>(rows.size()));
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i];
    const int r = static_cast<int>(i);
    table_->setItem(r, 0, Cell(row.source == commands::DictionaryCostEntry::SYSTEM
                                  ? QStringLiteral("システム")
                                  : row.source == commands::DictionaryCostEntry::USER
                                      ? QStringLiteral("ユーザー") : QStringLiteral("不明")));
    table_->setItem(r, 1, Cell(QString::fromUtf8(row.reading.data(), static_cast<int>(row.reading.size()))));
    table_->setItem(r, 2, Cell(QString::fromUtf8(row.value.data(), static_cast<int>(row.value.size()))));
    table_->setItem(r, 3, NumericCell(row.cost));
    table_->setItem(r, 4, NumericCell(row.lid));
    table_->setItem(r, 5, NumericCell(row.rid));
    table_->setItem(r, 6, NumericCell(row.attributes));
  }
  table_->setSortingEnabled(true);
  const bool truncated = result.system_truncated() || result.user_truncated();
  if (rows.empty()) {
    status_->setText(QStringLiteral("該当する辞書エントリはありません。"));
  } else {
    status_->setText(QStringLiteral("%1件の辞書エントリを取得しました（辞書格納順。見出しをクリックするとソート可能）。")
                         .arg(static_cast<int>(rows.size())));
  }
  if (truncated) {
    status_->setText(status_->text() + QStringLiteral(
        " 注意：上限または応答サイズ制限により一部省略されています（システム:%1、ユーザー:%2）。")
        .arg(result.system_truncated() ? QStringLiteral("省略あり") : QStringLiteral("省略なし"))
        .arg(result.user_truncated() ? QStringLiteral("省略あり") : QStringLiteral("省略なし")));
  }
}

}  // namespace mozc::gui::cost_inspector
