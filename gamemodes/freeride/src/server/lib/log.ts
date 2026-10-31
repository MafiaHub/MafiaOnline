// The server console; the framework tags each line with the resource name.
// The scripting API declares console's methods as taking an array; the
// runtime console (the framework's, which writes to the server log) takes
// the message itself.
type Sink = (message: string) => void;

const sink = console as unknown as { log: Sink; warn: Sink; error: Sink };

export function info(message: string): void {
    sink.log(message);
}

export function warn(message: string): void {
    sink.warn(message);
}

export function error(message: string): void {
    sink.error(message);
}

/** A caught value for a log line: an error's message, or its stack with `stack`. */
export function describe(caught: unknown, stack = false): string {
    if (caught instanceof Error) {
        return (stack ? caught.stack : undefined) ?? caught.message;
    }

    return String(caught);
}

/** Errors logged once each: a timer that fails on every tick would flood the log. */
export class Once {
    private readonly seen = new Set<string>();

    /** Logs "`what`: stack" unless the same error was logged before. */
    error(what: string, caught: unknown): void {
        const key = describe(caught);

        if (!this.seen.has(key)) {
            error(`${what}: ${describe(caught, true)}`);
        }

        this.seen.add(key);
    }
}
