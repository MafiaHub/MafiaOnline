/** CEF may provide a logical key without a physical code. */
export function recorderKey(event: Pick<KeyboardEvent, 'code' | 'key' | 'location'>): string {
    if (event.code) {
        return event.code;
    }

    if (/^[wasdqe]$/i.test(event.key)) {
        return `Key${event.key.toUpperCase()}`;
    }

    if (event.key === 'Shift' || event.key === 'Alt') {
        return `${event.key}${event.location === 2 ? 'Right' : 'Left'}`;
    }

    if (event.key === ' ' || event.key === 'Spacebar') {
        return 'Space';
    }

    return event.key;
}
