#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mafia1Online::Features::Debug {
    // Sync trace: one JSON object a line, for tools/sync_analyze.py. Off by
    // default; on with MAFIA1ONLINE_SYNCTRACE=1, the client config's
    // "syncTrace": {"enabled": true} or the chat command /synctrace.
    //
    // Every process on one machine stamps lines with the same clock (QPC, in
    // microseconds), so a host's, an owner's and an observer's logs join by
    // time. Record kinds ("k"):
    //  - "truth": what the entity's simulator sends, at its send rate. "clk"
    //    is the sample clock the receivers date it by.
    //  - "obs": what an observer shows, once a rendered frame. "oclk" is the
    //    owner clock the shown pose stands for.
    //  - "rx": a sample as an observer received it.
    //  - "event": a discrete action, "seq" keyed so owner and observers join.
    //  - "hdr": the first line of every file: role, pid, clock.
    //
    // Hook points: a feature calls Truth/Observed/Event where it sends or
    // places an entity and fills the record's fields. Records outside the
    // focus radius (Near) or past the frame budget are dropped.
    class SyncTrace final {
      public:
        class Record final {
          public:
            Record() = default;
            Record(const Record &)            = delete;
            Record &operator=(const Record &) = delete;
            Record(Record &&other) noexcept;
            ~Record();

            explicit operator bool() const {
                return _active;
            }
            Record &Num(std::string_view key, double value);
            Record &Int(std::string_view key, int64_t value);
            Record &Bool(std::string_view key, bool value);
            Record &Str(std::string_view key, std::string_view value);
            Record &Vec(std::string_view key, const glm::vec3 &value);
            Record &Quat(std::string_view key, const glm::quat &value);

          private:
            friend class SyncTrace;
            explicit Record(std::string *line);
            void Key(std::string_view key);
            std::string *_line = nullptr;
            bool _active       = false;
        };

        // Reads the environment and the client config; `role` names the
        // process ("host" or the player's nickname) in the file name.
        static void Initialize(const std::string &projectPath, const std::string &role);
        static void Shutdown();

        static bool Enabled() {
            return _enabled;
        }
        static bool Toggle();

        // QueryPerformanceCounter, in microseconds.
        static int64_t ClockUs();

        // Once a frame, before any record: resets the budget and sets the
        // points records are kept around (the camera; on the host, every
        // player).
        static void BeginFrame(std::vector<glm::vec3> focus);
        static uint64_t Frame() {
            return _frame;
        }
        static bool Near(const glm::vec3 &position);

        // `kind`: "car", "player", "npc", "walker", "train", "steamer".
        static Record Truth(std::string_view kind, uint64_t networkId);
        static Record Observed(std::string_view kind, uint64_t networkId);
        // "rx": a sample an observer received, with the clock it dates it by.
        static Record Received(std::string_view kind, uint64_t networkId);
        static Record Event(std::string_view kind, uint64_t networkId, std::string_view name);
        // Flushes once a second; also on toggling off and shutdown.
        static void EndFrame();

      private:
        static Record Begin(std::string_view type, std::string_view kind, uint64_t networkId, bool budgeted);
        static void Open();
        static void Close();

        static inline bool _enabled    = false;
        static inline uint64_t _frame  = 0;
    };
} // namespace Mafia1Online::Features::Debug
