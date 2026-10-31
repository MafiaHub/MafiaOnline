// Timers the sandboxed client V8 provides; the generated client API leaves
// them out. The handle is opaque: keep it only to clear the timer.
declare global {
    interface TimerHandle {
        readonly __timer: unique symbol;
    }

    function setTimeout(handler: () => void, ms?: number): TimerHandle;
    function clearTimeout(handle: TimerHandle | null | undefined): void;
    function setInterval(handler: () => void, ms?: number): TimerHandle;
    function clearInterval(handle: TimerHandle | null | undefined): void;
}

export {};
