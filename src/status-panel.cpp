#include "status-panel.hpp"
#include "plugin-version.h"

#include <util/platform.h>
#include <util/bmem.h>

#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QStatusBar>

#include <numeric>

/* Marks our widget tree so the original lookup never finds the panel itself */
static constexpr const char *kOwnedProperty = "statusPanelMirror";

static constexpr int kBitrateUpdateSeconds = 2;
static constexpr int kCongestionUpdateSeconds = 4;
static constexpr float kExcellentThreshold = 0.0f;
static constexpr float kGoodThreshold = 0.3333f;
static constexpr float kMediocreThreshold = 0.6667f;
static constexpr float kBadThreshold = 1.0f;

static QString ObsText(const char *key)
{
	const char *text = obs_frontend_get_locale_string(key);
	return QString::fromUtf8(text && *text ? text : key);
}

static QString ModuleText(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static bool IsOurs(const QWidget *w)
{
	for (; w; w = w->parentWidget()) {
		if (w->property(kOwnedProperty).toBool())
			return true;
	}
	return false;
}

static QString FormatTime(int totalSeconds)
{
	int seconds = totalSeconds % 60;
	int totalMinutes = totalSeconds / 60;
	int minutes = totalMinutes % 60;
	int hours = totalMinutes / 60;
	return QString::asprintf("%02d:%02d:%02d", hours, minutes, seconds);
}

/* ======================================================================== */

StatusBarWidget::StatusBarWidget(QWidget *parent) : QWidget(parent) {}

/* ======================================================================== */

StatusPanel::StatusPanel()
{
	setProperty(kOwnedProperty, true);
	setObjectName(QStringLiteral("StatusPanel"));

	auto outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	/* Same structure, size policies and margins as OBS' StatusBarWidget.ui */
	bar = new StatusBarWidget(this);
	bar->setObjectName(QStringLiteral("StatusPanelBar"));
	bar->setMinimumHeight(34);
	barLayout = new QBoxLayout(QBoxLayout::LeftToRight, bar);
	barLayout->setSpacing(0);
	barLayout->setContentsMargins(0, 0, 6, 0);
	outer->addWidget(bar);
	outer->addStretch(1);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	messageFrame = AddFrame("messageFrame");
	messageFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	messageFrame->setStyleSheet(QStringLiteral("border: none;"));
	message = AddLabel(messageFrame, "message");
	message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);

	delayFrame = AddFrame("delayFrame");
	delayInfo = AddLabel(delayFrame, "delayInfo");

	issuesFrame = AddFrame("issuesFrame");
	droppedFrames = AddLabel(issuesFrame, "droppedFrames");

	QFrame *networkFrame = AddFrame("networkFrame");
	networkFrame->layout()->setSpacing(6);
	statusIcon = AddLabel(networkFrame, "statusIcon");
	kbps = AddLabel(networkFrame, "kbps");
	kbps->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);

	QFrame *streamFrame = AddFrame("streamFrame");
	streamIcon = AddLabel(streamFrame, "streamIcon");
	streamTime = AddLabel(streamFrame, "streamTime");

	QFrame *recordFrame = AddFrame("recordFrame");
	recordFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	recordIcon = AddLabel(recordFrame, "recordIcon");
	recordTime = AddLabel(recordFrame, "recordTime");

	QFrame *cpuFrame = AddFrame("cpuFrame");
	cpuUsage = AddLabel(cpuFrame, "cpuUsage");
	cpuUsage->setAlignment(Qt::AlignCenter);

	QFrame *fpsFrame = AddFrame("fpsFrame");
	fpsCurrent = AddLabel(fpsFrame, "fpsCurrent");
	fpsCurrent->setAlignment(Qt::AlignCenter);

	/* Initial state, as in the OBSBasicStatusBar constructor */
	delayInfo->setText(QString());
	droppedFrames->setText(ObsText("DroppedFrames").arg("0", "0.0"));
	kbps->setText(QStringLiteral("0 kbps"));
	streamTime->setText(QStringLiteral("00:00:00"));
	streamTime->setDisabled(true);
	recordTime->setText(QStringLiteral("00:00:00"));
	recordTime->setDisabled(true);
	cpuUsage->setText(QStringLiteral("CPU: 0.0%"));
	fpsCurrent->setText(QStringLiteral("0.00 / 0.00 FPS"));
	delayFrame->hide();
	issuesFrame->hide();
	kbps->hide();
	FallbackIcons();

	LoadSettings();

	cpuInfo = os_cpu_usage_info_start();

	refreshTimer = new QTimer(this);
	connect(refreshTimer, &QTimer::timeout, this, [this]() { FallbackRefresh(); });
	cpuTimer = new QTimer(this);
	connect(cpuTimer, &QTimer::timeout, this, [this]() { FallbackCpu(); });
	messageTimer = new QTimer(this);
	messageTimer->setSingleShot(true);
	connect(messageTimer, &QTimer::timeout, this, [this]() { message->setText(QString()); });

	/* OBS updates its labels at most once per second; 250 ms keeps the
	 * panel within a quarter second of the status bar. */
	mirrorTimer = new QTimer(this);
	mirrorTimer->setInterval(250);
	connect(mirrorTimer, &QTimer::timeout, this, [this]() { MirrorTick(); });
	mirrorTimer->start();

	obs_frontend_add_event_callback(OnFrontendEvent, this);

	MirrorTick();
}

StatusPanel::~StatusPanel()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	ConnectStreamSignals(false);
	obs_weak_output_release(streamOutput);
	obs_weak_output_release(recordOutput);
	if (cpuInfo)
		os_cpu_usage_info_destroy(cpuInfo);
}

void StatusPanel::Register()
{
	auto panel = new StatusPanel();
	if (!obs_frontend_add_dock_by_id("status-panel-dock", obs_module_text("DockTitle"), panel)) {
		blog(LOG_WARNING, "[%s] could not register the dock", PLUGIN_NAME);
		delete panel;
	}
}

/* ------------------------------------------------------------------------ */
/* UI                                                                       */

QFrame *StatusPanel::AddFrame(const char *name)
{
	auto frame = new QFrame(bar);
	frame->setObjectName(QStringLiteral("sp_") + QString::fromLatin1(name));
	frame->setFrameShape(QFrame::NoFrame);
	frame->setFrameShadow(QFrame::Plain);
	frame->setLineWidth(0);
	auto layout = new QHBoxLayout(frame);
	layout->setContentsMargins(0, 0, 0, 0);
	barLayout->addWidget(frame);
	frames.push_back(frame);
	items.push_back({name, frame, nullptr});
	return frame;
}

QLabel *StatusPanel::AddLabel(QFrame *frame, const char *name)
{
	auto label = new QLabel(frame);
	label->setObjectName(QStringLiteral("sp_") + QString::fromLatin1(name));
	label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	frame->layout()->addWidget(label);
	items.push_back({name, label, nullptr});
	return label;
}

int StatusPanel::HorizontalWidth() const
{
	QMargins m = barLayout->contentsMargins();
	int width = m.left() + m.right();
	for (QFrame *frame : frames) {
		if (!frame->isHidden() && frame != messageFrame)
			width += frame->sizeHint().width();
	}
	return width;
}

void StatusPanel::UpdateDirection()
{
	bool wantVertical = layoutMode == Layout::Vertical ||
			    (layoutMode == Layout::Auto && width() < HorizontalWidth());
	if (wantVertical != vertical) {
		vertical = wantVertical;
		barLayout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
	}

	/* In a vertical stack an empty message line would only waste space */
	bool showMessage = !vertical || !message->text().isEmpty();
	if (messageFrame->isHidden() == showMessage)
		messageFrame->setVisible(showMessage);
}

void StatusPanel::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	UpdateDirection();
}

void StatusPanel::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);
	QMenu *sub = menu.addMenu(ModuleText("Layout"));
	auto group = new QActionGroup(sub);
	auto add = [&](const char *key, Layout mode) {
		QAction *a = sub->addAction(ModuleText(key));
		a->setCheckable(true);
		a->setChecked(layoutMode == mode);
		group->addAction(a);
		connect(a, &QAction::triggered, this, [this, mode]() { SetLayoutMode(mode); });
	};
	add("Layout.Auto", Layout::Auto);
	add("Layout.Horizontal", Layout::Horizontal);
	add("Layout.Vertical", Layout::Vertical);
	menu.exec(event->globalPos());
}

void StatusPanel::SetLayoutMode(Layout mode)
{
	layoutMode = mode;
	UpdateDirection();
	SaveSettings();
}

void StatusPanel::LoadSettings()
{
	char *path = obs_module_config_path("settings.json");
	if (!path)
		return;
	obs_data_t *data = obs_data_create_from_json_file(path);
	bfree(path);
	if (!data)
		return;
	long long mode = obs_data_get_int(data, "layout");
	if (mode >= 0 && mode <= 2)
		layoutMode = static_cast<Layout>(mode);
	obs_data_release(data);
}

void StatusPanel::SaveSettings()
{
	char *dir = obs_module_config_path("");
	char *path = obs_module_config_path("settings.json");
	if (dir && path) {
		os_mkdirs(dir);
		obs_data_t *data = obs_data_create();
		obs_data_set_int(data, "layout", static_cast<int>(layoutMode));
		obs_data_save_json_safe(data, path, "tmp", "bak");
		obs_data_release(data);
	}
	bfree(dir);
	bfree(path);
}

/* ------------------------------------------------------------------------ */
/* Mirror                                                                   */

QWidget *StatusPanel::FindOriginal()
{
	auto main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main)
		return nullptr;

	/* The usual place: main window -> QStatusBar -> StatusBarWidget */
	for (QWidget *w : main->findChildren<QWidget *>(QStringLiteral("StatusBarWidget"))) {
		if (!IsOurs(w) && w->inherits("StatusBarWidget"))
			return w;
	}
	/* Renamed object but same class */
	for (QStatusBar *sb : main->findChildren<QStatusBar *>()) {
		if (IsOurs(sb))
			continue;
		for (QWidget *w : sb->findChildren<QWidget *>()) {
			if (w->inherits("StatusBarWidget"))
				return w;
		}
	}
	return nullptr;
}

void StatusPanel::MirrorTick()
{
	if (exiting)
		return;

	if (!original) {
		if (searchCooldown > 0) {
			searchCooldown--;
		} else {
			searchCooldown = 8; /* ~2 s between searches */
			original = FindOriginal();
			for (Item &item : items)
				item.original = original ? original->findChild<QWidget *>(QString::fromLatin1(item.name))
							 : nullptr;
		}
	}

	bool canMirror = !original.isNull();
	if (canMirror != mirroring) {
		mirroring = canMirror;
		if (mirroring) {
			FallbackReset();
			blog(LOG_INFO, "[%s] mirroring the OBS status bar", PLUGIN_NAME);
		} else {
			blog(LOG_INFO, "[%s] OBS status bar not found, computing values", PLUGIN_NAME);
			FallbackInit();
		}
	}

	if (mirroring)
		Mirror();
	UpdateDirection();
}

void StatusPanel::Grow(QLabel *label)
{
	/* Like OBS: numeric labels never shrink, so the row does not jitter */
	int w = label->sizeHint().width();
	if (w > label->minimumWidth())
		label->setMinimumWidth(w);
}

void StatusPanel::Mirror()
{
	for (Item &item : items) {
		QWidget *o = item.original;
		if (!o)
			continue;
		QWidget *ours = item.ours;

		if (ours->isHidden() != o->isHidden() && ours != messageFrame)
			ours->setVisible(!o->isHidden());
		if (ours->isEnabled() != o->isEnabled())
			ours->setEnabled(o->isEnabled());
		if (ours->toolTip() != o->toolTip())
			ours->setToolTip(o->toolTip());

		auto ol = qobject_cast<QLabel *>(o);
		auto label = qobject_cast<QLabel *>(ours);
		if (!ol || !label)
			continue;

		if (label->text() != ol->text())
			label->setText(ol->text());
		QPixmap pm = ol->pixmap();
		if (pm.cacheKey() != label->pixmap().cacheKey())
			label->setPixmap(pm);
	}

	if (kbps->isHidden())
		kbps->setMinimumWidth(0);
	else
		Grow(kbps);
	Grow(cpuUsage);
	Grow(fpsCurrent);
}

/* ------------------------------------------------------------------------ */
/* Fallback: same algorithm as OBSBasicStatusBar, via the public API        */

static obs_output_t *Strong(obs_weak_output_t *weak)
{
	return weak ? obs_weak_output_get_output(weak) : nullptr;
}

static bool AnyOutputActive()
{
	return obs_frontend_streaming_active() || obs_frontend_recording_active() ||
	       obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active();
}

void StatusPanel::SetIcon(QLabel *label, const char *file, bool themed)
{
	QString path = themed && obs_frontend_is_theme_dark() ? QStringLiteral("theme:Dark/")
							      : QStringLiteral(":/res/images/");
	label->setPixmap(QIcon(path + QString::fromLatin1(file)).pixmap(QSize(16, 16)));
}

void StatusPanel::FallbackIcons()
{
	if (!obs_frontend_streaming_active()) {
		SetIcon(streamIcon, "streaming-inactive.svg", true);
		SetIcon(statusIcon, "network-inactive.svg", true);
	} else if (disconnected) {
		SetIcon(statusIcon, "network-disconnected.svg", true);
	}
	if (!obs_frontend_recording_active())
		SetIcon(recordIcon, "recording-inactive.svg", true);
}

void StatusPanel::FallbackInit()
{
	FallbackReset();
	cpuTimer->start(3000);
	FallbackCpu();

	/* Pick up outputs that are already running */
	if (obs_frontend_streaming_active()) {
		obs_output_t *out = obs_frontend_get_streaming_output();
		if (out) {
			streamOutput = obs_output_get_weak_output(out);
			ConnectStreamSignals(true);
			lastBytesSent = obs_output_get_total_bytes(out);
			lastBytesSentTime = os_gettime_ns();
			obs_output_release(out);
		}
	}
	if (obs_frontend_recording_active()) {
		obs_output_t *out = obs_frontend_get_recording_output();
		if (out) {
			recordOutput = obs_output_get_weak_output(out);
			obs_output_release(out);
		}
	}
	if (streamOutput || recordOutput)
		Activate();

	/* Approximate the elapsed time from the frames the outputs produced */
	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.fps_num && ovi.fps_den) {
		double fps = double(ovi.fps_num) / double(ovi.fps_den);
		if (obs_output_t *out = Strong(streamOutput)) {
			totalStreamSeconds = int(obs_output_get_total_frames(out) / fps);
			streamTime->setText(FormatTime(totalStreamSeconds));
			obs_output_release(out);
		}
		if (obs_output_t *out = Strong(recordOutput)) {
			totalRecordSeconds = int(obs_output_get_total_frames(out) / fps);
			UpdateRecordTimeLabel();
			obs_output_release(out);
		}
	}
}

void StatusPanel::FallbackReset()
{
	refreshTimer->stop();
	cpuTimer->stop();
	ConnectStreamSignals(false);
	obs_weak_output_release(streamOutput);
	obs_weak_output_release(recordOutput);
	streamOutput = nullptr;
	recordOutput = nullptr;
	active = false;
	disconnected = false;
	firstCongestionUpdate = false;
	congestionArray.clear();
	lastCongestion = 0.0f;
	retries = 0;
	reconnectTimeout = 0;
	totalStreamSeconds = 0;
	totalRecordSeconds = 0;
	delaySecTotal = 0;
	bitrateSeconds = 0;
	lastBytesSent = 0;
	lastBytesSentTime = os_gettime_ns();
}

void StatusPanel::ConnectStreamSignals(bool connect)
{
	obs_output_t *out = Strong(streamOutput);
	if (!out)
		return;
	signal_handler_t *sh = obs_output_get_signal_handler(out);
	if (connect) {
		signal_handler_connect(sh, "reconnect", OnReconnect, this);
		signal_handler_connect(sh, "reconnect_success", OnReconnectSuccess, this);
	} else {
		signal_handler_disconnect(sh, "reconnect", OnReconnect, this);
		signal_handler_disconnect(sh, "reconnect_success", OnReconnectSuccess, this);
	}
	obs_output_release(out);
}

void StatusPanel::OnReconnect(void *data, calldata_t *params)
{
	auto self = static_cast<StatusPanel *>(data);
	int seconds = (int)calldata_int(params, "timeout_sec");
	QMetaObject::invokeMethod(
		self,
		[self, seconds]() {
			if (self->mirroring)
				return;
			self->reconnectTimeout = seconds;
			if (obs_output_t *out = Strong(self->streamOutput)) {
				self->delaySecTotal = (int)obs_output_get_active_delay(out);
				obs_output_release(out);
				self->UpdateDelayMsg();
				self->retries++;
			}
		},
		Qt::QueuedConnection);
}

void StatusPanel::OnReconnectSuccess(void *data, calldata_t *)
{
	auto self = static_cast<StatusPanel *>(data);
	QMetaObject::invokeMethod(
		self,
		[self]() {
			if (self->mirroring)
				return;
			self->ShowMessage(ObsText("Basic.StatusBar.ReconnectSuccessful"), 4000);
			self->retries = 0;
			self->reconnectTimeout = 0;
			self->bitrateSeconds = -1;
			self->lastBytesSent = 0;
			self->lastBytesSentTime = os_gettime_ns();
			if (obs_output_t *out = Strong(self->streamOutput)) {
				self->delaySecTotal = (int)obs_output_get_active_delay(out);
				obs_output_release(out);
				self->UpdateDelayMsg();
				self->disconnected = false;
				self->firstCongestionUpdate = true;
			}
		},
		Qt::QueuedConnection);
}

void StatusPanel::Activate()
{
	if (!active) {
		startSkippedFrameCount = (int)video_output_get_skipped_frames(obs_get_video());
		startTotalFrameCount = (int)video_output_get_total_frames(obs_get_video());
		lastSkippedFrameCount = 0;
		refreshTimer->start(1000);
		active = true;
		if (streamOutput)
			SetIcon(statusIcon, "network-inactive.svg", true);
	}

	if (streamOutput) {
		SetIcon(streamIcon, "streaming-active.svg", false);
		streamTime->setDisabled(false);
		issuesFrame->show();
		kbps->show();
		firstCongestionUpdate = true;
	}

	if (recordOutput) {
		SetIcon(recordIcon, "recording-active.svg", false);
		recordTime->setDisabled(false);
	}
}

void StatusPanel::Deactivate()
{
	if (!streamOutput) {
		streamTime->setText(QStringLiteral("00:00:00"));
		streamTime->setDisabled(true);
		SetIcon(streamIcon, "streaming-inactive.svg", true);
		SetIcon(statusIcon, "network-inactive.svg", true);
		delayFrame->hide();
		issuesFrame->hide();
		kbps->hide();
		kbps->setMinimumWidth(0);
		totalStreamSeconds = 0;
		congestionArray.clear();
		disconnected = false;
		firstCongestionUpdate = false;
	}

	if (!recordOutput) {
		recordTime->setText(QStringLiteral("00:00:00"));
		recordTime->setDisabled(true);
		SetIcon(recordIcon, "recording-inactive.svg", true);
		totalRecordSeconds = 0;
	}

	if (!AnyOutputActive()) {
		refreshTimer->stop();
		delayInfo->setText(QString());
		droppedFrames->setText(ObsText("DroppedFrames").arg("0", "0.0"));
		kbps->setText(QStringLiteral("0 kbps"));
		delaySecTotal = 0;
		reconnectTimeout = 0;
		active = false;
		SetIcon(statusIcon, "network-inactive.svg", true);
	}
}

void StatusPanel::UpdateDelayMsg()
{
	if (!delaySecTotal)
		return;
	delayInfo->setText(ObsText("Basic.StatusBar.Delay").arg(QString::number(delaySecTotal)));
	delayFrame->show();
}

void StatusPanel::UpdateBandwidth()
{
	if (!streamOutput)
		return;
	if (++bitrateSeconds < kBitrateUpdateSeconds)
		return;

	obs_output_t *out = Strong(streamOutput);
	if (!out)
		return;

	uint64_t bytesSent = obs_output_get_total_bytes(out);
	uint64_t bytesSentTime = os_gettime_ns();
	obs_output_release(out);

	if (bytesSent < lastBytesSent)
		bytesSent = 0;
	if (bytesSent == 0)
		lastBytesSent = 0;

	uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
	double timePassed = double(bytesSentTime - lastBytesSentTime) / 1000000000.0;
	double kbitsPerSec = timePassed > 0.0 ? double(bitsBetween) / timePassed / 1000.0 : 0.0;

	kbps->setText(QString::number(kbitsPerSec, 'f', 0) + QStringLiteral(" kbps"));
	kbps->show();
	Grow(kbps);

	lastBytesSent = bytesSent;
	lastBytesSentTime = bytesSentTime;
	bitrateSeconds = 0;
}

void StatusPanel::FallbackCpu()
{
	double usage = cpuInfo ? os_cpu_usage_info_query(cpuInfo) : 0.0;
	cpuUsage->setText(QStringLiteral("CPU: ") + QString::number(usage, 'f', 1) + QStringLiteral("%"));
	Grow(cpuUsage);

	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	float targetFPS = ovi.fps_den ? (float)ovi.fps_num / (float)ovi.fps_den : 0.0f;
	fpsCurrent->setText(QString::asprintf("%.2f / %.2f FPS", obs_get_active_fps(), targetFPS));
	Grow(fpsCurrent);
}

void StatusPanel::UpdateStreamTime()
{
	totalStreamSeconds++;
	streamTime->setText(FormatTime(totalStreamSeconds));
	if (streamOutput && !streamTime->isEnabled())
		streamTime->setDisabled(false);

	if (reconnectTimeout > 0) {
		ShowMessage(ObsText("Basic.StatusBar.Reconnecting")
				    .arg(QString::number(retries), QString::number(reconnectTimeout)),
			    0);
		disconnected = true;
		SetIcon(statusIcon, "network-disconnected.svg", true);
		congestionArray.clear();
		reconnectTimeout--;
	} else if (retries > 0) {
		ShowMessage(ObsText("Basic.StatusBar.AttemptingReconnect").arg(QString::number(retries)), 0);
	}
}

void StatusPanel::UpdateRecordTime()
{
	bool paused = obs_frontend_recording_paused();
	if (!paused) {
		totalRecordSeconds++;
		if (recordOutput && !recordTime->isEnabled())
			recordTime->setDisabled(false);
	} else {
		if (pauseIconToggle)
			SetIcon(recordIcon, "recording-pause-inactive.svg", true);
		else
			SetIcon(recordIcon, "recording-pause.svg", false);
		pauseIconToggle = !pauseIconToggle;
	}
	UpdateRecordTimeLabel();
}

void StatusPanel::UpdateRecordTimeLabel()
{
	QString text = FormatTime(totalRecordSeconds);
	if (obs_frontend_recording_paused())
		text += QStringLiteral(" (PAUSED)");
	recordTime->setText(text);
}

void StatusPanel::UpdateDroppedFrames()
{
	obs_output_t *out = Strong(streamOutput);
	if (!out)
		return;

	int totalDropped = obs_output_get_frames_dropped(out);
	int totalFrames = obs_output_get_total_frames(out);
	float congestion = obs_output_get_congestion(out);
	obs_output_release(out);

	if (!totalFrames)
		return;

	double percent = (double)totalDropped / (double)totalFrames * 100.0;
	droppedFrames->setText(
		ObsText("DroppedFrames").arg(QString::number(totalDropped), QString::number(percent, 'f', 1)));
	issuesFrame->show();

	float avgCongestion = (congestion + lastCongestion) * 0.5f;
	if (avgCongestion < congestion)
		avgCongestion = congestion;
	if (avgCongestion > 1.0f)
		avgCongestion = 1.0f;
	lastCongestion = congestion;

	if (disconnected)
		return;

	bool update = firstCongestionUpdate;
	float congestionOverTime = avgCongestion;

	if (congestionArray.size() >= kCongestionUpdateSeconds) {
		congestionOverTime = std::accumulate(congestionArray.begin(), congestionArray.end(), 0.0f) /
				     (float)congestionArray.size();
		congestionArray.clear();
		update = true;
	} else {
		congestionArray.emplace_back(avgCongestion);
	}

	if (update) {
		if (congestionOverTime <= kExcellentThreshold + 1e-6f)
			SetIcon(statusIcon, "network-excellent.svg", false);
		else if (congestionOverTime <= kGoodThreshold)
			SetIcon(statusIcon, "network-good.svg", false);
		else if (congestionOverTime <= kMediocreThreshold)
			SetIcon(statusIcon, "network-mediocre.svg", false);
		else if (congestionOverTime <= kBadThreshold)
			SetIcon(statusIcon, "network-bad.svg", false);
		firstCongestionUpdate = false;
	}
}

void StatusPanel::FallbackRefresh()
{
	UpdateBandwidth();
	if (streamOutput)
		UpdateStreamTime();
	if (recordOutput)
		UpdateRecordTime();
	UpdateDroppedFrames();

	int skipped = (int)video_output_get_skipped_frames(obs_get_video()) - startSkippedFrameCount;
	int total = (int)video_output_get_total_frames(obs_get_video()) - startTotalFrameCount;
	int diff = skipped - lastSkippedFrameCount;
	double percentage = total > 0 ? double(skipped) / double(total) * 100.0 : 0.0;
	if (diff > 10 && percentage >= 0.1)
		ShowMessage(ObsText("HighResourceUsage"), 4000);
	lastSkippedFrameCount = skipped;
}

void StatusPanel::ShowMessage(const QString &text, int timeoutMs)
{
	messageTimer->stop();
	message->setText(text);
	if (timeoutMs)
		messageTimer->start(timeoutMs);
}

static QString TakeString(char *str)
{
	QString s = QString::fromUtf8(str ? str : "");
	bfree(str);
	return s;
}

void StatusPanel::FallbackEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED: {
		ConnectStreamSignals(false);
		obs_weak_output_release(streamOutput);
		streamOutput = nullptr;
		if (obs_output_t *out = obs_frontend_get_streaming_output()) {
			streamOutput = obs_output_get_weak_output(out);
			obs_output_release(out);
		}
		ConnectStreamSignals(true);
		retries = 0;
		lastBytesSent = 0;
		lastBytesSentTime = os_gettime_ns();
		if (obs_output_t *out = Strong(streamOutput)) {
			delaySecTotal = (int)obs_output_get_active_delay(out);
			obs_output_release(out);
			UpdateDelayMsg();
		}
		Activate();
		break;
	}
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (streamOutput) {
			ConnectStreamSignals(false);
			retries = 0;
			reconnectTimeout = 0;
			bitrateSeconds = -1;
			lastBytesSent = 0;
			lastBytesSentTime = os_gettime_ns();
			delaySecTotal = 0;
			obs_weak_output_release(streamOutput);
			streamOutput = nullptr;
			message->setText(QString());
			Deactivate();
		}
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		obs_weak_output_release(recordOutput);
		recordOutput = nullptr;
		if (obs_output_t *out = obs_frontend_get_recording_output()) {
			recordOutput = obs_output_get_weak_output(out);
			obs_output_release(out);
		}
		Activate();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED: {
		obs_weak_output_release(recordOutput);
		recordOutput = nullptr;
		Deactivate();
		QString path = TakeString(obs_frontend_get_last_recording());
		if (!path.isEmpty())
			ShowMessage(ObsText("Basic.StatusBar.RecordingSavedTo").arg(path), 10000);
		break;
	}
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		if (recordOutput) {
			SetIcon(recordIcon, "recording-pause.svg", false);
			pauseIconToggle = true;
		}
		UpdateRecordTimeLabel();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		if (recordOutput)
			SetIcon(recordIcon, "recording-active.svg", false);
		UpdateRecordTimeLabel();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED: {
		QString path = TakeString(obs_frontend_get_last_replay());
		if (!path.isEmpty())
			ShowMessage(ObsText("Basic.StatusBar.ReplayBufferSavedTo").arg(path), 10000);
		break;
	}
	case OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN: {
		QString path = TakeString(obs_frontend_get_last_screenshot());
		if (!path.isEmpty())
			ShowMessage(ObsText("Basic.StatusBar.ScreenshotSavedTo").arg(path), 10000);
		break;
	}
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
		Deactivate();
		break;
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		FallbackIcons();
		break;
	default:
		break;
	}
}

void StatusPanel::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto self = static_cast<StatusPanel *>(data);
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		self->exiting = true;
		self->mirrorTimer->stop();
		self->refreshTimer->stop();
		self->cpuTimer->stop();
		self->ConnectStreamSignals(false);
		return;
	}

	if (!self->mirroring) {
		self->FallbackEvent(event);
	} else {
		/* Mirror right away instead of waiting for the next tick */
		QTimer::singleShot(0, self, [self]() { self->MirrorTick(); });
	}
}
