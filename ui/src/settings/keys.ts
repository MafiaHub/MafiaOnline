// KeyboardEvent.code names the physical key (the client passes the scan code
// to CEF), which is what the client's key catalog binds: the names below are
// that catalog's (code/client/src/features/settings/key_catalog.cpp).
const codes: Record<string, string> = {
    Space: 'space',
    Enter: 'enter',
    Escape: 'escape',
    Tab: 'tab',
    Backspace: 'backspace',
    CapsLock: 'capslock',
    ShiftLeft: 'lshift',
    ShiftRight: 'rshift',
    ControlLeft: 'lctrl',
    ControlRight: 'rctrl',
    AltLeft: 'lalt',
    AltRight: 'ralt',
    ArrowUp: 'up',
    ArrowDown: 'down',
    ArrowLeft: 'left',
    ArrowRight: 'right',
    Insert: 'insert',
    Delete: 'delete',
    Home: 'home',
    End: 'end',
    PageUp: 'pageup',
    PageDown: 'pagedown',
    Minus: 'minus',
    Equal: 'equals',
    BracketLeft: 'lbracket',
    BracketRight: 'rbracket',
    Semicolon: 'semicolon',
    Quote: 'apostrophe',
    Backquote: 'grave',
    Backslash: 'backslash',
    Comma: 'comma',
    Period: 'period',
    Slash: 'slash',
    NumpadMultiply: 'numpadmultiply',
    NumpadAdd: 'numpadplus',
    NumpadSubtract: 'numpadminus',
    NumpadDecimal: 'numpaddecimal',
    NumpadDivide: 'numpaddivide',
    NumpadEnter: 'numpadenter',
};

// Fallback when a key event carries no code: Windows virtual keys. Letters
// and punctuation then follow the layout, which is the best left to go on.
const virtualKeys: Record<number, string> = {
    32: 'space',
    13: 'enter',
    27: 'escape',
    9: 'tab',
    8: 'backspace',
    20: 'capslock',
    37: 'left',
    38: 'up',
    39: 'right',
    40: 'down',
    45: 'insert',
    46: 'delete',
    36: 'home',
    35: 'end',
    33: 'pageup',
    34: 'pagedown',
    106: 'numpadmultiply',
    107: 'numpadplus',
    109: 'numpadminus',
    110: 'numpaddecimal',
    111: 'numpaddivide',
    189: 'minus',
    187: 'equals',
    219: 'lbracket',
    221: 'rbracket',
    186: 'semicolon',
    222: 'apostrophe',
    192: 'grave',
    220: 'backslash',
    188: 'comma',
    190: 'period',
    191: 'slash',
};

export function keyNameFromEvent(event: KeyboardEvent): string | null {
    const code = event.code;
    if (code) {
        if (/^Key[A-Z]$/.test(code)) return code.slice(3).toLowerCase();
        if (/^Digit[0-9]$/.test(code)) return code.slice(5);
        if (/^F([1-9]|1[0-2])$/.test(code)) return code.toLowerCase();
        if (/^Numpad[0-9]$/.test(code)) return `numpad${code.slice(6)}`;
        if (codes[code]) return codes[code];
    }
    const key = event.keyCode;
    if (key >= 65 && key <= 90) return String.fromCharCode(key + 32);
    if (key >= 48 && key <= 57) return String.fromCharCode(key);
    if (key >= 112 && key <= 123) return `f${key - 111}`;
    if (key >= 96 && key <= 105) return `numpad${key - 96}`;
    const right = event.location === 2;
    if (key === 16) return right ? 'rshift' : 'lshift';
    if (key === 17) return right ? 'rctrl' : 'lctrl';
    if (key === 18) return right ? 'ralt' : 'lalt';
    if (key === 13 && event.location === 3) return 'numpadenter';
    return virtualKeys[key] ?? null;
}

// MouseEvent.button: 0 left, 1 middle, 2 right, 3 back, 4 forward.
export function keyNameFromMouse(button: number): string | null {
    return ['mouse1', 'mouse3', 'mouse2', 'mouse4', 'mouse5'][button] ?? null;
}
