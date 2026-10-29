#pragma once

#include <mafianet/bit_stream.h>
#include <mafianet/get_time.h>
#include <networking/replication/network_entity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>

namespace Mafia1Online::Shared::Net {
    // A sender's sample clock: its own milliseconds, 16 bits on the wire,
    // taken when it sampled the pose. Relayed poses are otherwise dated by
    // the server pass that forwarded them, which puts the sender's frame
    // cadence (50 or 67 ms at 20 Hz) into every interval.
    inline uint16_t SampleClock(uint64_t milliseconds) {
        return static_cast<uint16_t>(milliseconds);
    }

    // The transform channel's bytes for `entity` as it stands.
    template <typename TEntity>
    void WriteTransform(TEntity &entity, MafiaNet::BitStream &out) {
        Framework::Networking::Replication::FieldSerializer fields(&out, true);
        entity.SerializeTransform(fields);
    }

    // Whether `entity` still writes `before`, WriteTransform's bytes.
    template <typename TEntity>
    bool TransformUnchanged(TEntity &entity, const MafiaNet::BitStream &before) {
        MafiaNet::BitStream after;
        WriteTransform(entity, after);
        return before.GetNumberOfBitsUsed() == after.GetNumberOfBitsUsed() &&
               (after.GetNumberOfBytesUsed() == 0 || std::memcmp(before.GetData(), after.GetData(), after.GetNumberOfBytesUsed()) == 0);
    }

    // RelayPose's comparison, for a caller that changes the pose across
    // several branches: `before` is WriteTransform's bytes from before.
    template <typename TEntity>
    void AdvanceSampleClock(TEntity &entity, const MafiaNet::BitStream &before, uint16_t clock) {
        if (!TransformUnchanged(entity, before)) {
            entity.sampleClock = clock;
        }
    }

    // Server: applies a relayed pose to `entity` (`apply` sets its fields)
    // and moves its sample clock to `clock` only when the pose's wire form
    // changed. An unchanged pose then serializes to the same bytes, which
    // ReplicaManager3 does not send again, so an idle body or a corpse costs
    // nothing; the first pose that differs (a body that stops still sends
    // its resting pose, a body that starts again its first step) carries the
    // new clock, and receivers read the long gap behind it as a restart
    // from rest (Framework::Utils::SnapshotBuffer).
    template <typename TEntity, typename TApply>
    void RelayPose(TEntity &entity, uint16_t clock, TApply &&apply) {
        MafiaNet::BitStream before;
        WriteTransform(entity, before);
        apply();
        AdvanceSampleClock(entity, before, clock);
    }

    // Receiver: maps one sender's 16-bit clock onto the local clock. The
    // clock is unwrapped against the previous value, and placed by the
    // least-delayed arrival of the recent window (the way car poses and
    // railway snapshots are), so one late packet never moves a sample's time
    // and the offset only slews; a jump beyond a second starts over. A
    // repeated clock is the same sample sent again (an idle refresh, or a
    // pose the server holds while it is unchanged): it maps to the time it
    // had and never moves the offset, however late it arrives. A clock held
    // longer than the 16-bit half range unwraps wrongly, which the reset
    // absorbs: that sample is placed at its arrival.
    class SampleClockMapper final {
      public:
        // `arrival` is the local clock (ms) the sample was received at.
        double Map(uint16_t clock, double arrival) {
            if (!_started) {
                _started  = true;
                _extended = clock;
            }
            else if (clock == _last) {
                return _mapped;
            }
            else {
                _extended += static_cast<int16_t>(static_cast<uint16_t>(clock - _last));
            }
            _last = clock;
            const double offset = arrival - static_cast<double>(_extended);
            if (_window.empty() || std::abs(offset - _offset) > kResetMs) {
                _window.clear();
                _offset = offset;
            }
            _window.push_back(offset);
            while (_window.size() > kWindow) {
                _window.pop_front();
            }
            const double floor = *std::min_element(_window.begin(), _window.end());
            _offset += std::clamp(floor - _offset, -kSlewMs, kSlewMs);
            _mapped = static_cast<double>(_extended) + _offset;
            return _mapped;
        }

        void Reset() {
            _started = false;
            _window.clear();
        }

      private:
        static constexpr size_t kWindow   = 60;
        static constexpr double kResetMs  = 1000.0;
        static constexpr double kSlewMs   = 0.5;

        bool _started     = false;
        uint16_t _last    = 0;
        int64_t _extended = 0;
        double _offset    = 0.0;
        double _mapped    = 0.0;
        std::deque<double> _window;
    };
} // namespace Mafia1Online::Shared::Net
