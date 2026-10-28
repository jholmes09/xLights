/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include "ArtNetTimecode.h"

#include <cstring>
#include <memory>
#include <mutex>

#include <spdlog/spdlog.h>

#include "SocketAbstraction.h"

namespace {
    constexpr uint16_t ARTNET_TC_PORT = 6454;

    std::mutex lock;
    bool enabled = false;
    std::string targetIp = "255.255.255.255";
    ArtNetTimecode::Rate rate = ArtNetTimecode::Rate::SMPTE30;
    int offsetMs = 0;
    std::unique_ptr<sockets::UDPSocket> tcSocket;
    bool socketFailed = false;
    long lastFrameCount = -1;

    // whole timecode frames since 00:00:00:00 (29.97 counts real frames)
    long FrameCount(long ms, ArtNetTimecode::Rate r) {
        if (ms < 0) ms = 0;
        switch (r) {
        case ArtNetTimecode::Rate::Film24: return (long)((long long)ms * 24 / 1000);
        case ArtNetTimecode::Rate::EBU25: return (long)((long long)ms * 25 / 1000);
        case ArtNetTimecode::Rate::DF2997: return (long)((long long)ms * 30000 / 1001000);
        case ArtNetTimecode::Rate::SMPTE30: default: return (long)((long long)ms * 30 / 1000);
        }
    }
}

void ArtNetTimecode::Configure(bool en, const std::string& ip, Rate r, int offset) {
    std::lock_guard<std::mutex> l(lock);
    enabled = en;
    targetIp = ip.empty() ? "255.255.255.255" : ip;
    rate = r;
    offsetMs = offset;
    lastFrameCount = -1;
    socketFailed = false;
    if (!enabled) {
        tcSocket.reset();
    }
    spdlog::info("Art-Net timecode {} to {} ({}).", enabled ? "on" : "off", targetIp, (int)rate);
}

bool ArtNetTimecode::IsEnabled() {
    std::lock_guard<std::mutex> l(lock);
    return enabled;
}

ArtNetTimecode::Fields ArtNetTimecode::ToFields(long ms, Rate r) {
    Fields f;
    long n = FrameCount(ms, r);
    int fps = 30;
    if (r == Rate::Film24) fps = 24;
    else if (r == Rate::EBU25) fps = 25;
    if (r == Rate::DF2997) {
        // drop-frame: frame numbers 0 and 1 are skipped each minute, except every 10th minute
        const long per10 = 17982; // real frames in 10 minutes
        const long perMin = 1798; // real frames in a dropped minute
        long d = n / per10;
        long m = n % per10;
        n += 18 * d + (m > 1 ? 2 * ((m - 2) / perMin) : 0);
    }
    f.frames = (uint8_t)(n % fps);
    long secs = n / fps;
    f.seconds = (uint8_t)(secs % 60);
    f.minutes = (uint8_t)((secs / 60) % 60);
    f.hours = (uint8_t)((secs / 3600) % 24);
    return f;
}

void ArtNetTimecode::BuildPacket(const Fields& f, Rate r, uint8_t* p) {
    std::memset(p, 0, PacketLength);
    std::memcpy(p, "Art-Net", 8); // includes the terminating 0
    p[8] = 0x00;  // OpCode 0x9700, low byte first
    p[9] = 0x97;
    p[10] = 0;    // protocol version 14
    p[11] = 14;
    p[12] = 0;    // filler
    p[13] = 0;    // stream id
    p[14] = f.frames;
    p[15] = f.seconds;
    p[16] = f.minutes;
    p[17] = f.hours;
    p[18] = (uint8_t)r;
}

void ArtNetTimecode::Send(long positionMs, bool force) {
    std::lock_guard<std::mutex> l(lock);
    if (!enabled || socketFailed) return;
    long ms = positionMs + offsetMs;
    long count = FrameCount(ms, rate);
    if (!force && count == lastFrameCount) return;
    lastFrameCount = count;

    if (!tcSocket) {
        tcSocket = std::make_unique<sockets::UDPSocket>();
        if (!tcSocket->Open(true)) {
            spdlog::error("Art-Net timecode: couldn't open a socket: {}", tcSocket->LastError());
            tcSocket.reset();
            socketFailed = true;
            return;
        }
    }
    uint8_t packet[PacketLength];
    BuildPacket(ToFields(ms, rate), rate, packet);
    if (!tcSocket->SendTo(targetIp, ARTNET_TC_PORT, packet, PacketLength)) {
        static int errors = 0;
        if (errors++ < 5) {
            spdlog::warn("Art-Net timecode: send to {} failed: {}", targetIp, tcSocket->LastError());
        }
    }
}
