import * as stylex from '@stylexjs/stylex';
import { useEffect, useState } from 'preact/hooks';

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
    const [position, setPosition] = useState<{ x: number; y: number } | null>(null);
    const [pressed, setPressed] = useState(false);
    useEffect(() => {
        const move = (event: MouseEvent) => setPosition({ x: event.clientX, y: event.clientY });
        const down = () => setPressed(true);
        const up = () => setPressed(false);
        window.addEventListener('mousemove', move);
        window.addEventListener('mousedown', down);
        window.addEventListener('mouseup', up);
        return () => {
            window.removeEventListener('mousemove', move);
            window.removeEventListener('mousedown', down);
            window.removeEventListener('mouseup', up);
        };
    }, []);
    if (!position) {
        return null;
    }
    return (
        <svg {...stylex.props(styles.cursor)} style={{ translate: `${position.x}px ${position.y}px` }} viewBox="0 0 22 26" aria-hidden="true">
            <g {...stylex.props(pressed && styles.pressed)}>
                <path d="M1 1 L1 21 L6.5 16 L10.5 24.5 L14 23 L10 14.8 L17.5 14.8 Z" fill="#f2e6cb" stroke="#1a110b" stroke-width="1.4" stroke-linejoin="round" />
                <path d="M3.4 5.6 L3.4 16.4 L6.9 13.2 L8.2 12.9 L13 12.9 Z" fill="#c9a45c" opacity="0.75" />
            </g>
        </svg>
    );
}
