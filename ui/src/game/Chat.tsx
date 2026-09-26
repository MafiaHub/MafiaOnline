import * as stylex from '@stylexjs/stylex';
import { useEffect, useRef, useState } from 'preact/hooks';
import { send } from '../bridge';
import { type ChatLine, chat, chatOpen, clampCodePoints, codePoints, now, preferences, state } from '../store';
import { colors, motion } from '../tokens.stylex';

const kVisibleRows = 10;
const kHistory = 30;
const chatFont = 'Arial, "Helvetica Neue", Helvetica, sans-serif';

const styles = stylex.create({
    root: {
        position: 'absolute',
        left: '1.8vw',
        top: '31vh',
        width: 'min(44vw, 34rem)',
        display: 'flex',
        flexDirection: 'column',
        gap: '0.35rem',
        fontFamily: chatFont,
        pointerEvents: 'none',
    },
    log: {
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        justifyContent: 'flex-end',
        padding: '0.5rem 0.7rem',
        overflow: 'hidden',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: 'transparent',
        transitionProperty: 'background-color, border-color',
        transitionDuration: motion.medium,
    },
    logOpen: {
        backgroundColor: 'rgba(10, 7, 5, 0.66)',
        borderColor: colors.goldHair,
        backgroundImage: 'linear-gradient(90deg, rgba(77, 13, 15, 0.18), rgba(0, 0, 0, 0))',
    },
    line: {
        overflowWrap: 'anywhere',
        lineHeight: 1.38,
        color: colors.cream,
        textShadow: '0 1px 1px rgba(0, 0, 0, 0.95), 0 0 0.6rem rgba(0, 0, 0, 0.7)',
        transitionProperty: 'opacity',
        transitionDuration: '1.2s',
    },
    faded: {
        opacity: 0,
    },
    time: {
        marginRight: '0.4rem',
        fontFamily: chatFont,
        fontSize: '0.78em',
        color: colors.muted,
    },
    author: {
        fontFamily: chatFont,
        fontWeight: 700,
        marginRight: '0.35rem',
    },
    more: {
        position: 'absolute',
        top: '0.2rem',
        right: '0.5rem',
        fontFamily: chatFont,
        fontSize: '0.72rem',
        color: colors.gold,
    },
    input: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        padding: '0 0.7rem',
        height: '2.3rem',
        backgroundColor: 'rgba(10, 7, 5, 0.88)',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.goldLine,
        boxShadow: '0 0.4rem 1.4rem rgba(0, 0, 0, 0.5)',
    },
    prompt: {
        fontFamily: chatFont,
        fontWeight: 700,
        fontSize: '0.7rem',
        letterSpacing: '0.2em',
        textTransform: 'uppercase',
        color: colors.gold,
    },
    command: {
        color: colors.bloodBright,
    },
    field: {
        flexGrow: 1,
        minWidth: 0,
        height: '100%',
        borderWidth: 0,
        outline: 'none',
        backgroundColor: 'transparent',
        color: colors.cream,
        fontFamily: chatFont,
        fontSize: '0.95em',
        caretColor: colors.goldBright,
    },
    counter: {
        fontFamily: chatFont,
        fontSize: '0.72rem',
        color: colors.muted,
    },
    counterFull: {
        color: colors.danger,
    },
});

// Wire colors are 0xRRGGBBAA; zero means the default for that line kind.
function wireColor(color: number, fallback: string): string {
    if (!color) {
        return fallback;
    }
    return `#${((color >>> 8) & 0xffffff).toString(16).padStart(6, '0')}`;
}

function clock(time: number): string {
    const date = new Date(time);
    return `${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`;
}

function Line(props: { line: ChatLine; faded: boolean; timestamps: boolean }) {
    const { line } = props;
    const isNotice = !line.author;
    return (
        <div {...stylex.props(styles.line, props.faded && styles.faded)} style={isNotice ? { color: wireColor(line.color, '#f4dfb0') } : undefined}>
            {props.timestamps && <span {...stylex.props(styles.time)}>{clock(line.time)}</span>}
            {!isNotice && (
                <span {...stylex.props(styles.author)} style={{ color: wireColor(line.color, '#e8c070') }}>
                    {line.author}:
                </span>
            )}
            {line.text}
        </div>
    );
}

export function Chat() {
    const lines = chat.value;
    const open = chatOpen.value;
    const prefs = preferences.value;
    const limit = state.value.limits.message;
    const [text, setText] = useState('');
    const [scroll, setScroll] = useState(0);
    const sent = useRef<string[]>([]);
    const cursor = useRef(-1);
    const draft = useRef('');
    const input = useRef<HTMLInputElement>(null);

    useEffect(() => {
        if (!open) {
            setScroll(0);
            return;
        }
        setText(open.prefill);
        cursor.current = -1;
        draft.current = '';
        // The host focuses the view in the same frame; retry until the caret lands.
        let tries = 0;
        const focus = () => {
            input.current?.focus();
            if (document.activeElement !== input.current && tries++ < 10) {
                window.setTimeout(focus, 30);
            }
        };
        focus();
    }, [open]);

    // The wheel scrolls the log wherever the (hidden) cursor is while typing.
    useEffect(() => {
        if (!open) {
            return;
        }
        const onWheel = (event: WheelEvent) => {
            const rows = event.deltaY < 0 ? 3 : -3;
            setScroll((value) => Math.min(Math.max(0, chat.value.length - kVisibleRows), Math.max(0, value + rows)));
        };
        window.addEventListener('wheel', onWheel);
        return () => window.removeEventListener('wheel', onWheel);
    }, [open]);

    const close = () => {
        chatOpen.value = null;
        setText('');
        send('chat:close');
    };

    const onKeyDown = (event: KeyboardEvent) => {
        switch (event.key) {
            case 'Enter': {
                event.preventDefault();
                const line = text.trim();
                if (line) {
                    send('chat:send', { text: line });
                    if (sent.current[sent.current.length - 1] !== line) {
                        sent.current = [...sent.current, line].slice(-kHistory);
                    }
                }
                close();
                break;
            }
            case 'ArrowUp':
            case 'ArrowDown': {
                event.preventDefault();
                const history = sent.current;
                if (history.length === 0) {
                    break;
                }
                if (event.key === 'ArrowUp') {
                    if (cursor.current < 0) {
                        draft.current = text;
                        cursor.current = history.length - 1;
                    } else if (cursor.current > 0) {
                        cursor.current -= 1;
                    }
                } else if (cursor.current >= 0) {
                    cursor.current = cursor.current < history.length - 1 ? cursor.current + 1 : -1;
                }
                setText(cursor.current < 0 ? draft.current : history[cursor.current]);
                break;
            }
            case 'PageUp':
                event.preventDefault();
                setScroll((value) => Math.min(Math.max(0, lines.length - kVisibleRows), value + kVisibleRows - 1));
                break;
            case 'PageDown':
                event.preventDefault();
                setScroll((value) => Math.max(0, value - (kVisibleRows - 1)));
                break;
        }
    };

    const fadeMs = prefs.chatFadeSeconds * 1000;
    const end = lines.length - scroll;
    const visible = lines.slice(Math.max(0, end - kVisibleRows), end);
    const count = codePoints(text);

    return (
        <div {...stylex.props(styles.root)} style={{ fontSize: `${prefs.chatScale * 1.25}rem` }}>
            <div {...stylex.props(styles.log, open && styles.logOpen)}>
                {open && scroll > 0 && <span {...stylex.props(styles.more)}>▲ {scroll} older</span>}
                {visible.map((line) => (
                    <Line key={line.id} line={line} timestamps={prefs.chatTimestamps} faded={!open && fadeMs > 0 && now.value - line.time > fadeMs} />
                ))}
            </div>
            {open && (
                <div {...stylex.props(styles.input)}>
                    <span {...stylex.props(styles.prompt, text.startsWith('/') && styles.command)}>{text.startsWith('/') ? 'Command' : 'Say'}</span>
                    <input
                        ref={input}
                        {...stylex.props(styles.field)}
                        value={text}
                        spellcheck={false}
                        autocomplete="off"
                        onInput={(event) => setText(clampCodePoints((event.currentTarget as HTMLInputElement).value, limit))}
                        onKeyDown={onKeyDown}
                        onBlur={() => window.setTimeout(() => chatOpen.value && input.current?.focus(), 0)}
                    />
                    <span {...stylex.props(styles.counter, count >= limit && styles.counterFull)}>
                        {count}/{limit}
                    </span>
                </div>
            )}
        </div>
    );
}
