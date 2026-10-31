// The client's event bus with native payloads typed from EventMap (and
// emitServer, which the generic EventBus leaves out), the resource's start
// and stop hooks, and the console (declared by the scripting API as taking an
// array; the runtime takes the message itself).

export const events = Events as unknown as EventBus & Pick<typeof Events, 'emitServer'>;

export const RESOURCE = 'freeride';

type Sink = (message: string) => void;

const sink = console as unknown as { log: Sink; warn: Sink; error: Sink };

export const log = {
    info: (message: string): void => sink.log(`[freeride] ${message}`),
    warn: (message: string): void => sink.warn(`[freeride] ${message}`),
    error: (message: string): void => sink.error(`[freeride] ${message}`),
};

export function onStart(handler: () => void): void {
    events.on('resourceStart', (name) => {
        if (name === RESOURCE) {
            handler();
        }
    });
}

export function onStop(handler: () => void): void {
    events.on('resourceStop', (name) => {
        if (name === RESOURCE) {
            handler();
        }
    });
}

/**
 * Handles an event the server sends this player (`fr:*`). It arrives as one
 * parsed object of the shape the server module sends; anything else is
 * dropped.
 */
export function onServer<T extends object>(name: string, handler: (data: T) => void): void {
    events.on(name, (data) => {
        if (typeof data === 'object' && data !== null) {
            handler(data as T);
        }
    });
}
