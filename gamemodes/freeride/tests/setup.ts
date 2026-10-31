// Loaded before every test file (package.json "test"): the bits of the
// server's scripting runtime the gamemode's modules touch while they load.
// The modules register event handlers and timers as they are imported; a
// test needs neither the handlers to run nor the timers to keep Node alive.

type Handler = (...args: unknown[]) => void;

const handlers = new Map<string, Handler[]>();

const runtime = globalThis as unknown as Record<string, unknown>;

runtime.Events = {
    on(name: string, handler: Handler): void {
        handlers.set(name, [...(handlers.get(name) ?? []), handler]);
    },
    emit(name: string, ...args: unknown[]): void {
        for (const handler of handlers.get(name) ?? []) {
            handler(...args);
        }
    },
};

// every() and the retry delays must not hold the test process open.
const realSetInterval = globalThis.setInterval;
const realSetTimeout = globalThis.setTimeout;

runtime.setInterval = ((...args: Parameters<typeof setInterval>) =>
    realSetInterval(...args).unref()) as typeof setInterval;

runtime.setTimeout = ((...args: Parameters<typeof setTimeout>) =>
    realSetTimeout(...args).unref()) as typeof setTimeout;
