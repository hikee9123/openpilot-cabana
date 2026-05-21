#include "route.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>

#include "core/streams/replay_stream.h"
#include "modules/settings/settings.h"
#include "route_browser.h"

namespace {

bool parseSegmentDirName(const QString& name, QString* route_name = nullptr) {
  static const QRegularExpression segment_rx(R"(^(.{20})--\d+$)");
  auto match = segment_rx.match(name);
  if (!match.hasMatch()) return false;
  if (route_name) *route_name = match.captured(1);
  return true;
}

bool resolveLocalRoute(QString route_path, QString& route, QString& data_dir) {
  QFileInfo info(route_path);
  if (!info.exists()) return false;

  QString route_name;
  if (info.isDir() && parseSegmentDirName(info.fileName(), &route_name)) {
    route = route_name;
    data_dir = info.dir().absolutePath() + "/";
    return true;
  }

  if (info.isDir()) {
    QDir dir(info.absoluteFilePath());
    for (const QFileInfo& child : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
      if (parseSegmentDirName(child.fileName(), &route_name)) {
        route = route_name;
        data_dir = dir.absolutePath() + "/";
        return true;
      }
    }
  }

  return false;
}

}  // namespace

RouteWidget::RouteWidget(QWidget* parent) : AbstractStreamWidget(parent) {
  QGridLayout* grid_layout = new QGridLayout(this);
  grid_layout->addWidget(new QLabel(tr("Route")), 0, 0);
  grid_layout->addWidget(route_edit = new QLineEdit(this), 0, 1);
  route_edit->setPlaceholderText(tr("Enter route name or browse for local/remote route"));
  auto browse_remote_btn = new QPushButton(tr("Browse remote..."), this);
  grid_layout->addWidget(browse_remote_btn, 0, 2);
  auto browse_local_btn = new QPushButton(tr("Browse local..."), this);
  grid_layout->addWidget(browse_local_btn, 0, 3);

  QHBoxLayout* camera_layout = new QHBoxLayout();
  for (auto c : {tr("Road camera"), tr("Driver camera"), tr("Wide road camera")})
    camera_layout->addWidget(cameras.emplace_back(new QCheckBox(c, this)));
  cameras[0]->setChecked(true);
  camera_layout->addStretch(1);
  grid_layout->addItem(camera_layout, 1, 1);

  setMinimumWidth(550);
  setFocusProxy(route_edit);

  connect(browse_local_btn, &QPushButton::clicked, [=]() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Open Local Route"), settings.last_route_dir);
    if (!dir.isEmpty()) {
      route_edit->setText(dir);
      settings.last_route_dir = QFileInfo(dir).absolutePath();
    }
  });
  connect(browse_remote_btn, &QPushButton::clicked, [this]() {
    RouteBrowserDialog dlg(this);
    if (dlg.exec()) {
      route_edit->setText(dlg.route());
    }
  });
}

AbstractStream* RouteWidget::open() {
  QString route = route_edit->text();
  QString data_dir;
  bool local_route_resolved = resolveLocalRoute(route, route, data_dir);
  if (!local_route_resolved && route.lastIndexOf('/') != -1 && util::file_exists(route.toStdString())) {
    int idx = route.lastIndexOf('/');
    data_dir = route.mid(0, idx + 1);
    route = route.mid(idx + 1);
  }

  bool is_valid_format = Route::parseRoute(route.toStdString()).str.size() > 0;
  if (!is_valid_format) {
    QMessageBox::warning(nullptr, tr("Warning"), tr("Invalid route format: '%1'").arg(route));
  } else {
    auto replay_stream = std::make_unique<ReplayStream>(qApp);
    uint32_t flags = REPLAY_FLAG_NONE;
    if (cameras[1]->isChecked()) flags |= REPLAY_FLAG_DCAM;
    if (cameras[2]->isChecked()) flags |= REPLAY_FLAG_ECAM;
    if (flags == REPLAY_FLAG_NONE && !cameras[0]->isChecked()) flags = REPLAY_FLAG_NO_VIPC;

    if (replay_stream->loadRoute(route, data_dir, flags)) {
      return replay_stream.release();
    }
  }
  return nullptr;
}
