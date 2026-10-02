#include "EmberCore.h"

#include <chrono>
#include <cstdio>

using namespace com::amazon::kepler::turbomodule;

namespace EmberCoreTurboModule {
namespace {

double now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string quote(const char *s) {
  std::string out = "\"";
  for (; *s; s++) {
    switch (*s) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    default:
      if (static_cast<unsigned char>(*s) < 0x20) {
        char esc[8];
        std::snprintf(esc, sizeof esc, "\\u%04x", *s);
        out += esc;
      } else {
        out += *s;
      }
    }
  }
  return out + "\"";
}

const char *action_name(EmberActionType type) {
  switch (type) {
  case EA_START: return "start";
  case EA_STOP: return "stop";
  case EA_RESUME: return "resume";
  case EA_PAUSE: return "pause";
  case EA_REPLAY: return "replay";
  case EA_SEEK: return "seek";
  case EA_EXIT: return "exit";
  default: return "none";
  }
}

} // namespace

EmberCore::EmberCore() { ember_init(&core_); }
EmberCore::~EmberCore() noexcept {}

std::string EmberCore::getCatalog() {
  std::string out = "[";
  for (int i = 0; i < EMBER_MOVIES; i++) {
    const Movie &m = ember_movies[i];
    if (i)
      out += ",";
    out += "{\"title\":" + quote(m.title) + ",\"tag\":" + quote(m.tag) +
           ",\"description\":" + quote(m.description) +
           ",\"poster\":" + quote(m.poster) + ",\"url\":" + quote(m.url) + "}";
  }
  return out + "]";
}

std::string EmberCore::getState() {
  static const char *screens[] = {"home", "details", "player"};
  const Model &m = core_.model;
  auto flag = [](bool b) { return b ? "true" : "false"; };
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "{\"screen\":\"%s\",\"movie\":%d,\"control\":%d,\"controls\":%s,"
                "\"loading\":%s,\"started\":%s,\"paused\":%s,\"ended\":%s,"
                "\"position\":%d,\"duration\":%d,\"error\":",
                screens[m.screen], m.movie, m.control,
                flag(ember_controls_shown(&core_, now())), flag(core_.loading),
                flag(core_.started), flag(core_.paused), flag(core_.ended),
                core_.position, core_.duration);
  return buf + quote(core_.error) + "}";
}

std::string EmberCore::key(int32_t key, bool repeat) {
  if (key < EK_NONE || key > EK_REWIND)
    key = EK_NONE;
  EmberAction a = ember_key(&core_, static_cast<EmberKey>(key), repeat, now());
  std::string out = std::string("{\"type\":\"") + action_name(a.type) +
                    "\",\"position\":" + std::to_string(a.position);
  if (a.type == EA_START)
    out += ",\"url\":" + quote(ember_movies[core_.model.movie].url);
  return out + "}";
}

void EmberCore::stop() { ember_stop(&core_); }

void EmberCore::playerBusy() { ember_player_busy(&core_); }

void EmberCore::playerReady(int32_t durationMs) {
  ember_player_ready(&core_, durationMs, now());
}

bool EmberCore::playerProgress(int32_t positionMs, bool playing) {
  return ember_player_progress(&core_, positionMs, playing, now());
}

void EmberCore::playerEnded() { ember_player_ended(&core_, now()); }

void EmberCore::playerFailed(std::string message) {
  ember_player_failed(&core_, message.c_str());
}

} // namespace EmberCoreTurboModule
