#pragma once

#include "application.h"

#include <memory>
#include <string>

namespace Mafia1Online::Core {
    // Owns the Framework instance across native mission close/open cycles.
    class ApplicationModule final {
      public:
        explicit ApplicationModule(std::string projectPath): _projectPath(std::move(projectPath)) {}

        void OnTick();
        void OnShutdown();

      private:
        std::string _projectPath;
        std::unique_ptr<Application> _application;
        bool _initAttempted = false;
    };
} // namespace Mafia1Online::Core
