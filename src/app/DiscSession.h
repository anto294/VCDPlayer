#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

#include "../core/DiscError.h"
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
  // Stopped or at the end of the disc: Play restarts it rather than resuming.
  bool isIdle() const;

 signals:
  void discLoaded(QString volumeId, bool pbcEnabled, int chapters);
  // kind: vcd::PbcEngine::State::Kind converted to int (Loading=0, Playing=1, Menu=2, End=3, Stopped=4).
  // firstChoice: number of the first menu choice (choices are numbered
  // firstChoice .. firstChoice + choiceCount - 1).
  void stateChanged(int kind, int choiceCount, int firstChoice);

 private:
  // vcd::IPlaybackSink
  void loadAndPlay(int64_t lba, int64_t sectorCount) override;
  void play() override;
  void pause() override;
  void stop() override;

  // vcd::ITimerHost
  void scheduleTimeout(vcd::TimerId id, int64_t ms, std::function<void()> cb) override;
  void cancelTimeout(vcd::TimerId id) override;
  void cancelAllTimeouts();

  void onMpvEndFile(int reason);

  // Translated, user-facing sentence for a disc-loading failure kind.
  static QString describe(vcd::DiscError::Code code);

  MpvPlayer* player_;
  vcd::VcdStreamRegistry registry_;
  std::unique_ptr<vcd::PbcEngine> pbc_;
  std::optional<vcd::LoadedDisc> disc_;
  struct PendingTimer {
    QTimer* timer = nullptr;
    std::function<void()> cb;
  };
  PendingTimer& timerFor(vcd::TimerId id);
  PendingTimer navigationTimer_;
  PendingTimer digitEntryTimer_;
};
