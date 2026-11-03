#Requires AutoHotkey v2.0
#Warn
; --close must work while another run still shows a dialog.
#SingleInstance Off

; Windows two-client harness: starts the server (unless one is already running),
; then two quick-join clients side by side as Player1 and Player2.
;
;     tools\run_two_clients.ahk              server + two clients
;     tools\run_two_clients.ahk --no-server  two clients against a running server
;     tools\run_two_clients.ahk --close      close every client and the server
;     tools\run_two_clients.ahk --validate   parse and exit without launching
;     tools\run_two_clients.ahk --simulation[=full|wireframe|headless]
;                                             also start the simulation host
;     tools\run_two_clients.ahk --players=N  start N player clients (0-2, default 2)
;     tools\run_two_clients.ahk --pool-scale=N  pedestrian pool scale of the host (1-16, default 16)
;     tools\run_two_clients.ahk --release    run the Release builds (build-prod-32/64)
;     tools\run_two_clients.ahk --window=WxH  player window size (default 1024x768)
;
; Build first: builds\build.bat Mafia1OnlineServer 64, Mafia1OnlineClient 32 and
; Mafia1OnlineLauncher 32, each with Release as a third argument for --release.
; The game comes from Steam discovery, or from
; MAFIA1ONLINE_GAME_ROOT when that is set. Mafia needs an existing profile;
; both clients use profile 0 and the mod suppresses profile and save writes.
;
; Everything this script writes stays under builds\runtime. The server uses
; server.json from the project root when present, else a copy of
; config\server.example.json. With --simulation it uses a generated
; builds\runtime\config\server-simulation.json carrying a fresh
; simulationToken, which the simulation host's config repeats.

SetTitleMatchMode 2

Args := []
for argument in A_Args {
    Args.Push(argument)
}
if HasArg("--validate") {
    ExitApp
}

ProjectDir := NormalizePath(A_ScriptDir "\..")
BuildTree := HasArg("--release") ? "build-prod" : "build"
ClientBin := ProjectDir "\builds\" BuildTree "-32\bin"
ServerBin := ProjectDir "\builds\" BuildTree "-64\bin"
RuntimeDir := ProjectDir "\builds\runtime"
LauncherExe := ClientBin "\Mafia1OnlineLauncher.exe"
ServerExe := ServerBin "\Mafia1OnlineServer.exe"
ServerHost := "127.0.0.1"
ServerPort := 27015

if HasArg("--close") {
    CloseAll()
    ExitApp
}

for required in [LauncherExe, ClientBin "\Mafia1OnlineClient.dll"] {
    if !FileExist(required) {
        MsgBox "Missing " required "`n`nBuild it with:`nbuilds\build.bat Mafia1OnlineClient 32`nbuilds\build.bat Mafia1OnlineLauncher 32", "Mafia1Online", "Iconx"
        ExitApp 1
    }
}
if ProcessExist("Mafia1OnlineLauncher.exe") {
    MsgBox "A Mafia1Online client is already running. Close it first, or run with --close.", "Mafia1Online", "Iconx"
    ExitApp 1
}

DirCreate RuntimeDir "\config"
DirCreate RuntimeDir "\logs"

SimulationMode := ArgValue("--simulation", "wireframe")
SimulationToken := ""
PoolArg := ArgValue("--pool-scale", "16")
PoolScale := PoolArg = "" ? 16 : Integer(PoolArg)
PlayerArg := ArgValue("--players", "2")
PlayerCount := PlayerArg = "" ? 2 : Integer(PlayerArg)
if (PlayerCount < 0 || PlayerCount > 2) {
    MsgBox "--players must be 0, 1 or 2.", "Mafia1Online", "Iconx"
    ExitApp 1
}
; The size the client asks the renderer for (its config's "window").
WindowWidth := 1024
WindowHeight := 768
WindowArg := ArgValue("--window", "")
if WindowArg != "" {
    if !RegExMatch(WindowArg, "^(\d+)x(\d+)$", &size) || size[1] < 640 || size[1] > 7680 || size[2] < 480 || size[2] > 4320 {
        MsgBox "--window must be WIDTHxHEIGHT, 640x480 to 7680x4320.", "Mafia1Online", "Iconx"
        ExitApp 1
    }
    WindowWidth := Integer(size[1])
    WindowHeight := Integer(size[2])
}
if SimulationMode != "" {
    if !(SimulationMode = "full" || SimulationMode = "wireframe" || SimulationMode = "headless") {
        MsgBox "--simulation must be full, wireframe or headless.", "Mafia1Online", "Iconx"
        ExitApp 1
    }
    if HasArg("--no-server") || ProcessExist("Mafia1OnlineServer.exe") {
        MsgBox "--simulation starts its own server with a fresh token. Close the running server first.", "Mafia1Online", "Iconx"
        ExitApp 1
    }
    SimulationToken := RandomToken()
}

if !HasArg("--no-server") {
    StartServer()
}

Gap := 16
LayoutWidth := WindowWidth * 2 + Gap
FirstX := Max(0, (A_ScreenWidth - LayoutWidth) // 2)
SecondX := LayoutWidth <= A_ScreenWidth ? FirstX + WindowWidth + Gap : Max(0, A_ScreenWidth - WindowWidth)
WindowY := Max(0, (A_ScreenHeight - WindowHeight) // 2)

started := []
if SimulationMode != "" {
    simulationPid := LaunchSimulation(SimulationMode)
    if !simulationPid {
        ExitApp 1
    }
    started.Push(simulationPid)
}
loop PlayerCount {
    processId := LaunchClient(A_Index, A_Index = 1 ? FirstX : SecondX)
    if !processId {
        ExitApp 1
    }
    started.Push(processId)
}

; All up: verify none died while booting.
Sleep 5000
for processId in started {
    if !ProcessExist(processId) {
        MsgBox "Client (pid " processId ") exited during startup. See " ClientBin "\logs.", "Mafia1Online", "Iconx"
        ExitApp 1
    }
}
ExitApp

StartServer() {
    global ServerExe, ServerBin, ProjectDir, RuntimeDir, ServerPort
    if ProcessExist("Mafia1OnlineServer.exe") {
        return
    }
    if !FileExist(ServerExe) {
        MsgBox "Missing " ServerExe "`n`nBuild it with: builds\build.bat Mafia1OnlineServer 64`nor pass --no-server.", "Mafia1Online", "Iconx"
        ExitApp 1
    }
    global SimulationToken
    config := ProjectDir "\server.json"
    if SimulationToken != "" {
        config := RuntimeDir "\config\server-simulation.json"
        handle := FileOpen(config, "w", "UTF-8-RAW")
        handle.Write('{"password":"","mod":{"mission":"freeride","simulationToken":"' SimulationToken '"}}')
        handle.Close()
    }
    else if !FileExist(config) {
        config := RuntimeDir "\config\server.json"
        FileCopy ProjectDir "\config\server.example.json", config, true
    }
    ; The project directory is the working directory so the server finds
    ; resources\ and mods\.
    Run Format('"{}" --config "{}"', ServerExe, config), ProjectDir, , &serverPid
    deadline := A_TickCount + 60000
    while !UdpPortBound(ServerPort, serverPid) {
        if !ProcessExist(serverPid) {
            MsgBox "Mafia1OnlineServer exited during startup.", "Mafia1Online", "Iconx"
            ExitApp 1
        }
        if A_TickCount > deadline {
            MsgBox "Mafia1OnlineServer did not bind UDP " ServerPort " within 60 seconds.", "Mafia1Online", "Iconx"
            ExitApp 1
        }
        Sleep 500
    }
}

; Launch one client with its own quick-join config, place its game window at x
; and return the launcher PID (0 on failure). The launcher maps Game.exe into
; its own process, so that PID owns the game window.
LaunchClient(index, x) {
    global LauncherExe, ClientBin, RuntimeDir, ServerHost, ServerPort, WindowY, WindowWidth, WindowHeight
    config := RuntimeDir "\config\client-" index ".json"
    handle := FileOpen(config, "w", "UTF-8-RAW")
    handle.Write('{"quickJoin":{"enabled":true,"nickname":"Player' index '","host":"' ServerHost '","port":' ServerPort ',"password":"","profileIndex":0},'
        . '"window":{"width":' WindowWidth ',"height":' WindowHeight '}}')
    handle.Close()

    EnvSet "MAFIA1ONLINE_CLIENT_CONFIG", config
    Run Format('"{}"', LauncherExe), ClientBin, , &clientPid
    EnvSet "MAFIA1ONLINE_CLIENT_CONFIG"

    game := WaitForGameWindow(clientPid, 120000)
    if !game {
        MsgBox "Timed out waiting for the window of client " index ".", "Mafia1Online", "Iconx"
        if ProcessExist(clientPid) {
            ProcessClose clientPid
        }
        return 0
    }
    WinSetTitle "Mafia1Online - Player" index, game
    PlaceWindow(game, x, WindowY)
    return clientPid
}

; Launch the simulation host: a client whose config enables the simulation
; role with the server's token. Headless hosts never show a window, so only
; the process is checked. Returns the launcher PID (0 on failure).
LaunchSimulation(mode) {
    global LauncherExe, ClientBin, RuntimeDir, ServerHost, ServerPort, SimulationToken, PoolScale
    config := RuntimeDir "\config\client-simulation.json"
    handle := FileOpen(config, "w", "UTF-8-RAW")
    handle.Write('{"quickJoin":{"enabled":true,"nickname":"Simulation","host":"' ServerHost '","port":' ServerPort ',"password":"","profileIndex":0},'
        . '"simulation":{"enabled":true,"token":"' SimulationToken '","render":"' mode '","tickHz":30,"pedestrianPoolScale":' PoolScale '}}')
    handle.Close()

    EnvSet "MAFIA1ONLINE_CLIENT_CONFIG", config
    Run Format('"{}"', LauncherExe), ClientBin, , &hostPid
    EnvSet "MAFIA1ONLINE_CLIENT_CONFIG"
    if mode = "headless" {
        Sleep 3000
        if !ProcessExist(hostPid) {
            MsgBox "The simulation host exited during startup. See " ClientBin "\logs.", "Mafia1Online", "Iconx"
            return 0
        }
        return hostPid
    }
    game := WaitForGameWindow(hostPid, 120000)
    if !game {
        MsgBox "Timed out waiting for the simulation host window.", "Mafia1Online", "Iconx"
        if ProcessExist(hostPid) {
            ProcessClose hostPid
        }
        return 0
    }
    return hostPid
}

RandomToken() {
    token := ""
    loop 32 {
        token .= Format("{:x}", Random(0, 15))
    }
    return token
}

; The largest visible top-level window of the process: CEF and the IME keep
; smaller or hidden windows of their own.
WaitForGameWindow(processId, timeout) {
    deadline := A_TickCount + timeout
    DetectHiddenWindows false
    loop {
        if !ProcessExist(processId) {
            return 0
        }
        best := 0, bestArea := 0
        for hwnd in WinGetList("ahk_pid " processId) {
            try {
                WinGetPos , , &w, &h, hwnd
                if (w >= 640 && h >= 480 && w * h > bestArea) {
                    best := hwnd, bestArea := w * h
                }
            }
        }
        if best {
            return best
        }
        if A_TickCount > deadline {
            return 0
        }
        Sleep 250
    }
}

; Move without resizing; the renderer owns the window size. Repeated because
; the renderer reapplies its own rect once the device settles.
PlaceWindow(hwnd, x, y) {
    deadline := A_TickCount + 8000
    loop {
        try {
            WinMove x, y, , , hwnd
            Sleep 250
            WinGetPos &cx, &cy, , , hwnd
            if (Abs(cx - x) <= 2 && Abs(cy - y) <= 2) {
                return true
            }
        }
    } until A_TickCount > deadline
    return false
}

UdpPortBound(port, processId) {
    global RuntimeDir
    out := RuntimeDir "\logs\netstat.txt"
    RunWait A_ComSpec ' /c netstat -ano -p UDP > "' out '"', , "Hide"
    try {
        text := FileRead(out)
    } catch {
        return false
    }
    return RegExMatch(text, "m)^\s*UDP\s+\S+:" port "\s+\S+\s+" processId "\s*$")
}

CloseAll() {
    for name in ["Mafia1OnlineLauncher.exe", "Mafia1OnlineServer.exe"] {
        while ProcessExist(name) {
            ProcessClose name
            Sleep 100
        }
    }
}

; "--name=value" gives value, "--name" alone gives defaultValue, absent gives "".
ArgValue(name, defaultValue) {
    global Args
    for arg in Args {
        if (arg = name) {
            return defaultValue
        }
        if (SubStr(arg, 1, StrLen(name) + 1) = name "=") {
            return SubStr(arg, StrLen(name) + 2)
        }
    }
    return ""
}

HasArg(name) {
    global Args
    for arg in Args {
        if (arg = name) {
            return true
        }
    }
    return false
}

NormalizePath(path) {
    pathBuffer := Buffer(32768 * 2)
    DllCall("GetFullPathNameW", "Str", path, "UInt", 32768, "Ptr", pathBuffer, "Ptr", 0)
    return StrGet(pathBuffer)
}
