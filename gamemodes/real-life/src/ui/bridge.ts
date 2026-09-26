import { EVENT, isRecord, type ViewState } from '../shared/protocol';

declare global {
    interface Window {
        callEvent?: (name: string, payload: string) => boolean;
    }
}

export const inGame = typeof window.callEvent === 'function';
export function send(name: string, payload: unknown = {}): void {
    window.callEvent?.(name, JSON.stringify(payload));
}

export function subscribe(receive: (state: ViewState) => void): () => void {
    const handler = (event: Event) => {
        const value: unknown = (event as CustomEvent).detail;

        if (
            isRecord(value) &&
            typeof value.generation === 'number' &&
            ['auth', 'spawning', 'playing'].includes(String(value.phase))
        ) {
            receive(value as unknown as ViewState);
        }
    };

    window.addEventListener(EVENT.state, handler);
    send(EVENT.ready);

    return () => window.removeEventListener(EVENT.state, handler);
}
