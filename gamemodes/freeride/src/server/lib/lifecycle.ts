// The resource's life: its typed event bus, start and stop hooks, and timers
// that stop with it. A restarted resource loads a fresh copy of every module,
// so anything a module starts must end in onStop.

/** The global event bus, with native event payloads typed from EventMap. */
export const events = Events as unknown as EventBus;

export const RESOURCE = 'freeride';

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

/** Runs `tick` every `ms` from now until the resource stops. */
export function every(ms: number, tick: () => void): void {
    const timer = setInterval(tick, ms);

    onStop(() => clearInterval(timer));
}

/** Runs `handler` after the current event dispatch, when every module's handler has run. */
export function afterHandlers(handler: () => void): void {
    setTimeout(handler, 0);
}

/** Runs `handler` with the player after every disconnect handler of every module. */
export function afterDisconnect(handler: (player: Player) => void): void {
    events.on('playerDisconnect', (player) => afterHandlers(() => handler(player)));
}

/** Clears state that belongs to the loaded mission. */
export function onMissionChange(handler: (mission: MissionInfo) => void): void {
    events.on('missionChange', handler);
}
