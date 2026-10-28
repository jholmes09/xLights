#pragma once

/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include <cstdint>
#include <string>

// Sends Art-Net timecode (ArtTimeCode, OpCode 0x9700) so video software such as
// MadMapper can follow the sequencer's play position.
class ArtNetTimecode
{
public:
    enum class Rate : uint8_t { Film24 = 0, EBU25 = 1, DF2997 = 2, SMPTE30 = 3 };

    struct Fields {
        uint8_t hours = 0;
        uint8_t minutes = 0;
        uint8_t seconds = 0;
        uint8_t frames = 0;
    };

    // ip empty = broadcast to 255.255.255.255
    static void Configure(bool enabled, const std::string& ip, Rate rate, int offsetMs = 0);
    [[nodiscard]] static bool IsEnabled();

    // Sends the timecode for a sequence position. Repeats of the same
    // timecode frame are skipped unless force is set.
    static void Send(long positionMs, bool force = false);

    // Timecode fields for a position (no offset applied); exposed for tests.
    [[nodiscard]] static Fields ToFields(long positionMs, Rate rate);
    // Builds the 19-byte packet.
    static void BuildPacket(const Fields& f, Rate rate, uint8_t* packet);

    static constexpr int PacketLength = 19;
};
