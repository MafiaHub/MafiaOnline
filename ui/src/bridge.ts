import {
    type ChatLine,
    type Player,
    type Toast,
    type UiState,
    chat,
    chatOpen,
    pauseOpen,
    pushChat,
    pushToast,
    roster,
    scoreboardVisible,
    state,
    uid,
    cursor,
    type CursorState,
    gameplayMenu,
    type GameplayMenu,
} from './store';

// The client talks to this page in two directions:
//   C++ -> page: window.__m1o({ type, payload }) through CefFrame::ExecuteJavaScript.
//   page -> C++: callEvent(name, jsonString), injected by the framework's CEF
//                renderer and filtered to this page's origin.
declare global {
    interface Window {
        __m1o?: (message: InboundMessage) => void;
        callEvent?: (name: string, payload: string | null) => boolean;
    }
}

export type InboundMessage =
    | { type: 'cursor:state'; payload: CursorState }
    | { type: 'state'; payload: UiState }
    | { type: 'chat:message'; payload: Omit<ChatLine, 'id'> }
    | { type: 'chat:history'; payload: Omit<ChatLine, 'id'>[] }
    | { type: 'chat:open'; payload: { prefill: string } }
    | { type: 'pause:open'; payload: Record<string, never> }
    | { type: 'gameplay:open'; payload: GameplayMenu }
    | { type: 'gameplay:closed'; payload: Record<string, never> }
    | { type: 'scoreboard'; payload: { visible: boolean } }
    | { type: 'players'; payload: { players: Player[]; mission: string } }
    | { type: 'toast'; payload: { kind: Toast['kind']; text: string } }
    | { type: 'chat:closed'; payload: Record<string, never> }
    | { type: 'session:reset'; payload: Record<string, never> };

export type OutboundEvent =
    | 'ui:ready'
    | 'ui:alive'
    | 'ui:screen'
    | 'menu:connect'
    | 'menu:disconnect'
    | 'menu:play'
    | 'app:quit'
    | 'chat:send'
    | 'chat:close'
    | 'pause:close'
    | 'gameplay:select'
    | 'gameplay:close'
    | 'servers:favorite'
    | 'servers:forget'
    | 'settings:save';

let mockHandler: ((name: OutboundEvent, payload: unknown) => void) | null = null;

export function isInGame(): boolean {
    return typeof window.callEvent === 'function';
}

export function send(name: OutboundEvent, payload?: unknown) {
    if (typeof window.callEvent === 'function') {
        window.callEvent(name, payload === undefined ? '' : JSON.stringify(payload));
        return;
    }
    mockHandler?.(name, payload);
}

export function receive(message: InboundMessage) {
    switch (message.type) {
        case 'cursor:state':
            cursor.value = message.payload;
            break;
        case 'state':
            state.value = message.payload;
            break;
        case 'chat:message':
            pushChat(message.payload);
            break;
        case 'chat:history':
            chat.value = message.payload.map((line) => ({ ...line, id: uid() }));
            break;
        case 'chat:open':
            gameplayMenu.value = null;
            pauseOpen.value = false;
            chatOpen.value = { prefill: message.payload.prefill ?? '' };
            break;
        case 'pause:open':
            gameplayMenu.value = null;
            chatOpen.value = null;
            pauseOpen.value = true;
            break;
        case 'gameplay:open':
            chatOpen.value = null;
            pauseOpen.value = false;
            gameplayMenu.value = message.payload;
            break;
        case 'gameplay:closed':
            gameplayMenu.value = null;
            break;
        case 'scoreboard':
            scoreboardVisible.value = message.payload.visible;
            break;
        case 'players':
            roster.value = message.payload;
            break;
        case 'toast':
            pushToast(message.payload.kind, message.payload.text);
            break;
        case 'chat:closed':
            // The client took the keyboard back.
            chatOpen.value = null;
            pauseOpen.value = false;
            break;
        case 'session:reset':
            gameplayMenu.value = null;
            chat.value = [];
            chatOpen.value = null;
            pauseOpen.value = false;
            scoreboardVisible.value = false;
            roster.value = { players: [], mission: '' };
            break;
    }
}

export async function startBridge() {
    window.__m1o = receive;
    if (!isInGame()) {
        const mock = await import('./mock');
        mockHandler = mock.startMock(receive);
    }
    send('ui:ready');
    window.setInterval(() => send('ui:alive'), 1000);
}
