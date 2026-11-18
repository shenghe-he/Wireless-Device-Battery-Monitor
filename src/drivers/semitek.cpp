// SEMITEK single-chip 2.4 GHz receivers, vendor id 1EA7 (product string "2.4G Mouse"; rechargeable
// budget mice from many brands ship this firmware). Verified on 1EA7:0064.
// The dongle exposes a vendor collection on usage page FFB5 with numbered 8-byte reports
// (report id B5 + 7 payload bytes): send an output report, the answer arrives as an input report
// with the command echoed in byte[1].
//   cmd 01 = battery -> [B5][01][flags][pct][a][b][c][d]: byte[3] & 0x7F is the firmware-reported
//   percent, but some firmware freezes it (seen stuck at 56); bytes[5..6] little endian hold the
//   1S LiPo cell voltage in mV, which tracks the real level and is preferred when it reads
//   3000..4300. Charging is not signalled on the 2.4 GHz link.
// Protocol cross-checked against github.com/ksanjeev284/acer-popgo-companion (same firmware,
// independently reverse engineered) and verified against three repeated reads on 1EA7:0064.
// The dongle serves this status from a static cache: 565 polls across 2 h of active mouse use
// returned byte-identical data, and ~15 min of charging did not refresh it either, so a reading
// is the mouse's last measurement rather than a live value; it only changes when the firmware
// re-measures (power-cycle / re-pairing).
#include "drivers.h"

namespace {

constexpr BYTE kReportId = 0xB5;
constexpr BYTE kCmdBattery = 0x01;

// 1S LiPo linear model: 3300 mV empty, 4200 mV full.
int MvToPercent(int mv) {
    int pct = (mv - 3300) * 100 / 900;
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

}  // namespace

void PollSemitek(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(0x1EA7)) {
        // Only the vendor collection carries the command channel; the standard mouse collection
        // next to it never answers.
        auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
            return c.page() == 0xFFB5 && c.outLen() >= 8 && c.inLen() >= 8;
        });
        for (const HidColl* c : colls) {
            HidDev d;
            if (!d.Open(*c)) continue;
            std::vector<BYTE> r;
            if (d.Write({kReportId, kCmdBattery, 0, 0, 0, 0, 0, 0}) && d.Read(r, 500) >= 4 &&
                r[0] == kReportId && r[1] == kCmdBattery) {
                int fwPct = r[3] & 0x7F;
                int mv = r[5] | (r[6] << 8);
                DeviceStatus s;
                s.key = std::wstring(L"semitek:") + Hex4(vid) + L":" + Hex4(pid);
                s.name = HidProduct(*c);
                if (s.name.empty()) s.name = L"Wireless mouse";
                s.kind = KindFromCollections(ctx.colls, vid, pid);
                if (s.kind == DeviceKind::Other) s.kind = DeviceKind::Mouse;
                s.via = L"2.4 GHz receiver";
                s.millivolts = mv;
                if (mv >= 3000 && mv <= 4300) s.percent = MvToPercent(mv);
                else if (fwPct <= 100) s.percent = fwPct;
                s.graceSec = 1800;
                ctx.Add(s, vid, pid);
            }
            break;  // one vendor collection per dongle is enough
        }
    }
}
