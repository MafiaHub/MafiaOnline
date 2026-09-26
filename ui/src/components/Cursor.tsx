import * as stylex from '@stylexjs/stylex';
import { cursor } from '../store';

// The game owns the OS cursor (DirectInput, exclusive), so the menu draws its
// own at the position the client forwards.
const styles = stylex.create({
    cursor: {
        position: 'fixed',
        left: 0,
        top: 0,
        width: '22px',
        height: '26px',
        pointerEvents: 'none',
        zIndex: 1000,
        filter: 'drop-shadow(0 2px 2px rgba(0, 0, 0, 0.8))',
    },
    pressed: {
        transform: 'scale(0.9)',
    },
});

export function Cursor() {
    const state = cursor.value;

    if (!state || state.shape === 'none') {
        return null;
    }

    const { x, y, pressed, shape } = state;
    const centered = shape !== 'arrow' && shape !== 'pointer';

    return (
        <svg {...stylex.props(styles.cursor)} style={{ translate: `${x - (centered ? 11 : 0)}px ${y - (centered ? 13 : 0)}px` }} viewBox="0 0 22 26" aria-hidden="true">
            <g {...stylex.props(pressed && styles.pressed)}>
                {shape === 'text' ? (
                    <path d="M6 3h10M11 3v20M6 23h10" fill="none" stroke="#f2e6cb" stroke-width="2" />
                ) : shape === 'pointer' ? (
                    <path d="M7 13V3a2 2 0 0 1 4 0v7l2-1 2 2 2-1 3 3v7l-4 5H9l-7-9a2 2 0 0 1 3-3l2 2Z" fill="#f2e6cb" stroke="#1a110b" stroke-width="1.4" stroke-linejoin="round" />
                ) : shape === 'move' ? (
                    <path d="M11 2v22M1 13h20M7 6l4-4 4 4M7 20l4 4 4-4M5 9l-4 4 4 4M17 9l4 4-4 4" fill="none" stroke="#f2e6cb" stroke-width="1.5" />
                ) : shape === 'blocked' ? (
                    <g fill="none" stroke="#f2e6cb" stroke-width="2"><circle cx="11" cy="13" r="8" /><path d="m5 7 12 12" /></g>
                ) : shape === 'wait' ? (
                    <g fill="none" stroke="#f2e6cb" stroke-width="2"><circle cx="11" cy="13" r="8" /><path d="M11 7v6l4 2" /></g>
                ) : (
                    <>
                        <path d="M1 1 L1 21 L6.5 16 L10.5 24.5 L14 23 L10 14.8 L17.5 14.8 Z" fill="#f2e6cb" stroke="#1a110b" stroke-width="1.4" stroke-linejoin="round" />
                        <path d="M3.4 5.6 L3.4 16.4 L6.9 13.2 L8.2 12.9 L13 12.9 Z" fill="#c9a45c" opacity="0.75" />
                    </>
                )}
            </g>
        </svg>
    );
}
