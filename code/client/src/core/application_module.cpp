#include <utils/safe_win32.h>

#include "application_module.h"

#include "shared/version.h"

#include <logging/logger.h>
#include <mafia1/sdk/graphics/native_graph.h>

namespace Mafia1Online::Core {
    void ApplicationModule::OnTick() {
        if (!_initAttempted) {
            _initAttempted = true;

            Framework::Integrations::Client::InstanceOptions opts;
            opts.useImGUI       = false;
            opts.usePresence    = false;
            opts.gameName       = "Mafia 1";
            opts.gameVersion    = "1.2";
            opts.modSlug        = "mafia1online";
            opts.modVersion     = Mafia1Online::Version::rel;
            opts.assetCacheRoot = _projectPath + "\\cache";

            // Borrow the engine's Direct3D 8 device for web views; ImGui has no D3D8 path.
            opts.useRenderer                  = true;
            opts.rendererOptions.backend      = Framework::Graphics::RendererBackend::BACKEND_D3D_8;
            opts.rendererOptions.platform     = Framework::Graphics::PlatformBackend::PLATFORM_WIN32;
            opts.rendererOptions.d3d8.device  = static_cast<Framework::Graphics::D3D8::Device *>(SDK::Graphics::GetD3DDevice());
            opts.rendererOptions.windowHandle = static_cast<HWND>(SDK::Graphics::GetGraph()->MainWindow());

            auto application = std::make_unique<Application>();
            application->SetProjectPath(_projectPath);
            if (const auto result = application->Init(opts); !result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Mafia1Online client initialization failed: {}", result.GetError().message);
                ExitProcess(1);
            }
            _application = std::move(application);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Mafia1Online is running on the native mission tick");
        }

        if (_application) {
            _application->Update();
        }
    }

    void ApplicationModule::OnShutdown() {
        if (_application) {
            _application->Shutdown();
            _application.reset();
        }
    }
} // namespace Mafia1Online::Core
