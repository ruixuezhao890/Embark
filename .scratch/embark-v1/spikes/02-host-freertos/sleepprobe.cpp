// 附带的计时探针：解释「为什么 1 tick 实测 2 ms」——
// 是 Windows Sleep() 粒度的问题，还是 FreeRTOS 端口的锅。
// 端口在启动时调用 timeBeginPeriod(wPeriodMin)，所以带 timeBeginPeriod(1) 的那组才是可比组。

#include <chrono>
#include <cstdio>
#include <windows.h>
#include <mmsystem.h>

namespace {

double measure_ms(int reps, void (*fn)()) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) {
        fn();
    }
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
               .count() /
           reps;
}

void sleep1() { Sleep(1); }
void sleep2() { Sleep(2); }
void sleep10() { Sleep(10); }

}  // namespace

int main() {
    TIMECAPS caps = {};
    if (timeGetDevCaps(&caps, sizeof(caps)) == MMSYSERR_NOERROR) {
        std::printf("[probe] timeGetDevCaps: wPeriodMin=%u ms wPeriodMax=%u ms\n",
                    static_cast<unsigned>(caps.wPeriodMin),
                    static_cast<unsigned>(caps.wPeriodMax));
    }

    constexpr int kReps = 1000;
    std::printf("[probe] Sleep(1)  default resolution : %.2f ms\n", measure_ms(kReps, sleep1));
    std::printf("[probe] Sleep(2)  default resolution : %.2f ms\n", measure_ms(kReps, sleep2));
    std::printf("[probe] Sleep(10) default resolution : %.2f ms\n", measure_ms(kReps, sleep10));

    timeBeginPeriod(1);
    std::printf("[probe] after timeBeginPeriod(1):\n");
    std::printf("[probe] Sleep(1)  : %.2f ms\n", measure_ms(kReps, sleep1));
    std::printf("[probe] Sleep(2)  : %.2f ms\n", measure_ms(kReps, sleep2));
    std::printf("[probe] Sleep(1)+Sleep(1) 组合 : %.2f ms (应约等于两次 Sleep(1))\n",
                measure_ms(kReps / 2, sleep1) * 2.0);
    timeEndPeriod(1);
    return 0;
}
