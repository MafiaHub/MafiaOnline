// Per-process client log names and the cleanup of old ones.
#include "shared/logging/process_log.h"

#include <cstdio>
#include <exception>

using namespace Mafia1Online::Logging;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                 \
    do {                                                                                                                                                 \
        if (!(condition)) {                                                                                                                              \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                  \
            ++failures;                                                                                                                                  \
        }                                                                                                                                                \
    } while (false)

    void Roles() {
        CHECK(ProcessLogRole(nlohmann::json::object()) == "client");
        CHECK(ProcessLogRole(nlohmann::json::parse(R"({"quickJoin":{"enabled":true,"nickname":"Player1"}})")) == "Player1");
        // A nickname only lends what a file name can hold.
        CHECK(ProcessLogRole(nlohmann::json::parse(R"({"quickJoin":{"enabled":true,"nickname":"Davo SK/..\\x:*"}})")) == "DavoSKx");
        CHECK(ProcessLogRole(nlohmann::json::parse(R"({"quickJoin":{"enabled":true,"nickname":" /"}})")) == "client");
        CHECK(ProcessLogRole(nlohmann::json::parse(R"({"quickJoin":{"enabled":false,"nickname":"Player1"}})")) == "client");
        // The simulation host joins with a nickname too.
        CHECK(ProcessLogRole(nlohmann::json::parse(R"({"quickJoin":{"enabled":true,"nickname":"Simulation"},"simulation":{"enabled":true}})")) == "host");
        CHECK(ProcessLogRole(nlohmann::json::parse(R"({"quickJoin":{"enabled":true,"nickname":"P2"},"simulation":{"enabled":false}})")) == "P2");
        CHECK(ProcessLogRole(nlohmann::json::parse(R"({"quickJoin":{"enabled":"yes","nickname":"P2"}})")) == "client");
        CHECK(ProcessLogName("host", 1234) == "Mafia1Online-host-1234");
        CHECK(ProcessLogNameFor(std::filesystem::path("does-not-exist.json"), 7) == "Mafia1Online-client-7");
    }

    void Cleanup() {
        std::vector<LogFile> files;
        for (int i = 0; i < 5; ++i) {
            files.push_back({"Mafia1Online-host-" + std::to_string(i) + ".log", i});
        }
        files.push_back({"Mafia1Online-host-0.1.log", -1});
        // Neither the old shared log, dumps nor the CEF host's log are ours.
        files.push_back({"Mafia1Online.log", -10});
        files.push_back({"Crash-1.dmp", -10});
        files.push_back({"cef_host.log", -10});
        const auto stale = StaleProcessLogs(files, 3);
        CHECK(stale.size() == 3);
        CHECK(stale.size() == 3 && stale[0] == "Mafia1Online-host-1.log" && stale[1] == "Mafia1Online-host-0.log" && stale[2] == "Mafia1Online-host-0.1.log");
        CHECK(StaleProcessLogs(files, 30).empty());
    }
} // namespace

int main() {
    try {
        Roles();
        Cleanup();
    }
    catch (const std::exception &error) {
        std::printf("exception: %s\n", error.what());
        return 1;
    }
    if (failures != 0) {
        std::printf("%d process log check(s) failed\n", failures);
        return 1;
    }
    std::printf("Process log tests passed\n");
    return 0;
}
