/*
 * Status Panel - a dock that shows exactly what the OBS status bar shows.
 *
 * Primary mode: MIRROR. The panel finds OBS' own status bar widget
 * (StatusBarWidget inside the main window's QStatusBar) and copies the text,
 * icon, enabled and visibility state of every label and frame. All the
 * numbers therefore come from OBS itself and are always identical to the
 * status bar - also when the status bar is hidden (View -> Status Bar).
 *
 * Fallback mode: COMPUTE. If a future OBS version changes its status bar so
 * that it cannot be found, the panel computes the same values through the
 * public libobs / obs-frontend-api (the same algorithm as OBSBasicStatusBar)
 * instead of going blank.
 */
#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <vector>

/*
 * The class is deliberately called StatusBarWidget (global namespace, with
 * Q_OBJECT): OBS themes style the status bar with the type selector
 * "StatusBarWidget > QFrame", so the panel gets the very same separators and
 * padding as the original in every theme.
 */
class StatusBarWidget : public QWidget {
	Q_OBJECT

public:
	explicit StatusBarWidget(QWidget *parent = nullptr);
};

struct os_cpu_usage_info;

class StatusPanel : public QWidget {
public:
	enum class Layout { Auto = 0, Horizontal = 1, Vertical = 2 };

	StatusPanel();
	~StatusPanel() override;

	static void Register();

protected:
	void resizeEvent(QResizeEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	/* --- UI ------------------------------------------------------------ */
	struct Item {
		const char *name;
		QWidget *ours;
		QPointer<QWidget> original;
	};

	QFrame *AddFrame(const char *name);
	QLabel *AddLabel(QFrame *frame, const char *name);
	QWidget *Ours(const char *name) const;
	void UpdateDirection();
	int HorizontalWidth() const;
	void SetLayoutMode(Layout mode);
	void LoadSettings();
	void SaveSettings();

	StatusBarWidget *bar = nullptr;
	QBoxLayout *barLayout = nullptr;
	std::vector<QFrame *> frames;
	std::vector<Item> items;
	Layout layoutMode = Layout::Auto;
	bool vertical = false;

	QLabel *message = nullptr, *delayInfo = nullptr, *droppedFrames = nullptr, *statusIcon = nullptr,
	       *kbps = nullptr, *streamIcon = nullptr, *streamTime = nullptr, *recordIcon = nullptr,
	       *recordTime = nullptr, *cpuUsage = nullptr, *fpsCurrent = nullptr;
	QFrame *delayFrame = nullptr, *issuesFrame = nullptr, *messageFrame = nullptr;

	/* --- mirror -------------------------------------------------------- */
	QPointer<QWidget> original;
	int searchCooldown = 0;
	bool mirroring = false;
	bool exiting = false;
	QTimer *mirrorTimer = nullptr;

	QWidget *FindOriginal();
	void MirrorTick();
	void Mirror();
	void Grow(QLabel *label);

	/* --- fallback (compute) -------------------------------------------- */
	QTimer *refreshTimer = nullptr;   /* 1 s, like OBS */
	QTimer *cpuTimer = nullptr;       /* 3 s, like OBS */
	QTimer *messageTimer = nullptr;
	os_cpu_usage_info *cpuInfo = nullptr;

	obs_weak_output_t *streamOutput = nullptr;
	obs_weak_output_t *recordOutput = nullptr;
	bool active = false;
	bool disconnected = false;
	bool firstCongestionUpdate = false;
	bool pauseIconToggle = false;
	std::vector<float> congestionArray;
	float lastCongestion = 0.0f;
	int retries = 0;
	int reconnectTimeout = 0;
	int totalStreamSeconds = 0;
	int totalRecordSeconds = 0;
	int delaySecTotal = 0;
	int startSkippedFrameCount = 0;
	int startTotalFrameCount = 0;
	int lastSkippedFrameCount = 0;
	int bitrateSeconds = 0;
	uint64_t lastBytesSent = 0;
	uint64_t lastBytesSentTime = 0;

	void FallbackInit();
	void FallbackReset();
	void FallbackEvent(enum obs_frontend_event event);
	void FallbackRefresh();
	void FallbackCpu();
	void FallbackIcons();
	void Activate();
	void Deactivate();
	void ConnectStreamSignals(bool connect);
	void UpdateDelayMsg();
	void UpdateBandwidth();
	void UpdateStreamTime();
	void UpdateRecordTime();
	void UpdateRecordTimeLabel();
	void UpdateDroppedFrames();
	void ShowMessage(const QString &text, int timeoutMs);
	void SetIcon(QLabel *label, const char *file, bool themed);

	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	static void OnReconnect(void *data, calldata_t *params);
	static void OnReconnectSuccess(void *data, calldata_t *params);
};
