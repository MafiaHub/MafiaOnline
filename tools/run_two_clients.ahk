#Requires AutoHotkey v2.0
#Warn

; Windows two-client harness: starts the server (unless one is already running),
; then two quick-join clients side by side as Player1 and Player2.
;
;     tools\run_two_clients.ahk              server + two clients
;     tools\run_two_clients.ahk --no-server  two clients against a running server
;     tools\run_two_clients.ahk --close      close every client and the server
;     tools\run_two_clients.ahk --validate   parse and exit without launching
;
; Build first: builds\build.bat Mafia1OnlineServer 64, Mafia1OnlineClient 32 and
; Mafia1OnlineLauncher 32. The game comes from Steam discovery, or from
; MAFIA1ONLINE_GAME_ROOT when that is set. Mafia needs an existing profile;
; both clients use profile 0 and the mod suppresses profile and save writes.
;
; Everything this script writes stays under builds\runtime. The server uses
; server.json from the project root when present, else a copy of
; config\server.example.json.

SetTitleMatchMode 2

Args := []
for argument in A_Args {
    Args.Push(argument)
}
if HasArg("--validate") {
    ExitApp
}

ProjectDir := NormalizePath(A_ScriptDir "\..")
ClientBin := ProjectDir "\builds\build-32\bin"
ServerBin := ProjectDir "\builds\build-64\bin"
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

if !HasArg("--no-server") {
    StartServer()
}

; Matches the window the client asks the renderer for (1024 by 768).
WindowWidth := 1024
WindowHeight := 768
Gap := 16
LayoutWidth := WindowWidth * 2 + Gap
FirstX := Max(0, (A_ScreenWidth - LayoutWidth) // 2)
SecondX := LayoutWidth <= A_ScreenWidth ? FirstX + WindowWidth + Gap : Max(0, A_ScreenWidth - WindowWidth)
WindowY := Max(0, (A_ScreenHeight - WindowHeight) // 2)

pid1 := LaunchClient(1, FirstX)
if !pid1 {
    ExitApp 1
}
pid2 := LaunchClient(2, SecondX)
if !pid2 {
    ExitApp 1
}

; Both up: verify neither died while booting.
Sleep 5000
for processId in [pid1, pid2] {
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
    config := ProjectDir "\server.json"
    if !FileExist(config) {
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
    global LauncherExe, ClientBin, RuntimeDir, ServerHost, ServerPort, WindowY
    config := RuntimeDir "\config\client-" index ".json"
    handle := FileOpen(config, "w", "UTF-8-RAW")
    handle.Write('{"quickJoin":{"enabled":true,"nickname":"Player' index '","host":"' ServerHost '","port":' ServerPort ',"password":"","profileIndex":0}}')
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
