#pragma once
#include "generated/EmberCoreSpec.h"

extern "C" {
#include "ember.h"
}

namespace EmberCoreTurboModule {

// Exposes the shared Ember C core to JavaScript. All methods run on the
// JSThread and return immediately, so no extra threading is needed.
class EmberCore : public EmberCoreSpec {
public:
  EmberCore();
  ~EmberCore() noexcept;

  std::string getCatalog() override;
  std::string getState() override;
  std::string key(int32_t key, bool repeat) override;
  void stop() override;
  void playerBusy() override;
  void playerReady(int32_t durationMs) override;
  bool playerProgress(int32_t positionMs, bool playing) override;
  void playerEnded() override;
  void playerFailed(std::string message) override;

private:
  Ember core_;
};

} // namespace EmberCoreTurboModule
