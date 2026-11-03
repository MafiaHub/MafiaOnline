// Development mode: /dev commands and the interior recorder. Start the server
// with FREERIDE_DEV=1, or type "fr dev on" in the server console.
let enabled = process.env.FREERIDE_DEV === '1';

export function isOn(): boolean {
    return enabled;
}

export function set(value: boolean): void {
    enabled = value;
}
