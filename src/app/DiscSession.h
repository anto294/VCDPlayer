#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

#include "../core/DiscLoader.h"
#include "../core/PbcEngine.h"
#include "../mpv/VcdStreamProtocol.h"

class MpvPlayer;

// Wires together DiscLoader (disc parsing), VcdStreamRegistry (vcdxa://
// streaming), and PbcEngine (PBC state machine), acting as PbcEngine's
// IPlaybackSink/ITimerHost via Qt (QTimer for Selection node timeouts, mpv
// for actual playback). A single instance lives for the whole app
// session; each disc open replaces the PbcEngine but reuses the same
// MpvPlayer and the same VcdStreamRegistry.
class DiscSession : public QObject, public vcd::IPlaybackSink, public vcd::ITimerHost {
  Q_OBJECT

 public:
  explicit DiscSession(MpvPlayer* player, QObject* parent = nullptr);

  // Opens a VCD disc from its .cue. On error returns false and fills
  // errorMessage (if not nullptr) with a human-readable message.
  bool openDiscPath(const QString& cuePath, QString* errorMessage);

  void sendRemoteButton(vcd::RemoteButton btn, int number = 0);
  bool hasDisc() const { return pbc_ != nullptr; }

 signals:
  void discLoaded(QString volumeId, bool pbcEnabled, int chapters);
  // kind: vcd::PbcEngine::State::Kind converted to int (Loading=0, Playing=1, Menu=2, End=3).
  void stateChanged(int kind, int choiceCount);

 private:
  // vcd::IPlaybackSink
  void loadAndPlay(int64_t lba, int64_t sectorCount) override;
  void play() override;
  void pause() override;

  // vcd::ITimerHost
  void scheduleTimeout(int64_t ms, std::function<void()> cb) override;
  void cancelTimeout() override;

  void onMpvEndFile(int reason);

  MpvPlayer* player_;
  vcd::VcdStreamRegistry registry_;
  std::unique_ptr<vcd::PbcEngine> pbc_;
  std::optional<vcd::LoadedDisc> disc_;
  QTimer* timeoutTimer_;
  std::function<void()> pendingTimeoutCb_;
};
