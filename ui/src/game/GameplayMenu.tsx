import * as stylex from '@stylexjs/stylex';
import { useLayoutEffect, useRef, useState } from 'preact/hooks';
import { send } from '../bridge';
import { Button, Heading, Panel } from '../components/ui';
import { gameplayMenu, type GameplayMenu as Menu } from '../store';
import { colors, fonts } from '../tokens.stylex';

const styles = stylex.create({
    overlay: {
        position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center',
        backgroundColor: 'rgba(6, 4, 3, 0.45)', pointerEvents: 'auto',
    },
    panel: { width: '30rem', maxWidth: '92vw', maxHeight: '90vh', display: 'flex', flexDirection: 'column' },
    rows: { overflowY: 'auto', minHeight: 0, display: 'flex', flexDirection: 'column', gap: '0.35rem', marginBottom: '1rem' },
    row: { display: 'flex', gap: '0.35rem', alignItems: 'stretch' },
    choice: { flexGrow: 1, minWidth: 0, justifyContent: 'flex-start', textAlign: 'left', whiteSpace: 'normal', padding: '0.6rem 0.8rem', letterSpacing: '0.05em' },
    selected: { borderColor: colors.goldBright, backgroundColor: colors.blood },
    number: { color: colors.gold, width: '1.4rem', flexShrink: 0 },
    description: { display: 'flex', flexDirection: 'column', gap: '0.2rem' },
    detail: { fontFamily: fonts.body, fontSize: '0.8rem', textTransform: 'none', color: colors.muted },
    hint: { fontFamily: fonts.body, color: colors.muted, fontSize: '0.8rem', textAlign: 'center', marginTop: '0.8rem' },
});

export function GameplayMenu({ menu }: { menu: Menu }) {
    const [selected, setSelected] = useState(0);
    const sent = useRef(false);
    const selectedRow = useRef<HTMLDivElement>(null);
    const close = () => {
        if (sent.current) return;
        sent.current = true;
        gameplayMenu.value = null;
        send('gameplay:close', { id: menu.id });
    };
    const choose = (index: number, drop = false) => {
        if (sent.current || !menu.choices[index]?.canSelect || (drop && !menu.choices[index].canDrop)) return;
        sent.current = true;
        gameplayMenu.value = null;
        send('gameplay:select', { id: menu.id, index, drop });
    };
    useLayoutEffect(() => {
        selectedRow.current?.scrollIntoView({ block: 'nearest' });
    }, [selected]);
    useLayoutEffect(() => {
        const onKey = (event: KeyboardEvent) => {
            if (event.repeat) return;
            if (event.key === 'Escape' || event.key === '0' || (menu.kind === 'inventory' && event.key.toLowerCase() === 'i')) {
                event.preventDefault(); close();
            } else if (event.key === 'ArrowUp' || event.key === 'ArrowDown' || event.key === 'Tab') {
                event.preventDefault();
                if (menu.choices.length) {
                    const delta = event.key === 'ArrowUp' || (event.key === 'Tab' && event.shiftKey) ? -1 : 1;
                    setSelected((value) => (value + delta + menu.choices.length) % menu.choices.length);
                }
            } else if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault(); choose(selected);
            } else if (/^[1-9]$/.test(event.key)) {
                event.preventDefault(); choose(Number(event.key) - 1);
            } else if (menu.kind === 'inventory' && (event.key === 'Delete' || event.key.toLowerCase() === 'x')) {
                event.preventDefault(); choose(selected, true);
            }
        };
        window.addEventListener('keydown', onKey);
        return () => window.removeEventListener('keydown', onKey);
    }, [menu, selected]);

    return (
        <div {...stylex.props(styles.overlay)} role="dialog" aria-modal="true" aria-label={menu.title}>
            <Panel style={styles.panel}>
                <Heading>{menu.title}</Heading>
                <div {...stylex.props(styles.rows)}>
                    {menu.choices.length === 0 && <p {...stylex.props(styles.hint)}>Your inventory is empty.</p>}
                    {menu.choices.map((choice, index) => (
                        <div key={index} ref={index === selected ? selectedRow : undefined} {...stylex.props(styles.row)}>
                            <Button
                                disabled={!choice.canSelect}
                                style={[styles.choice, index === selected && styles.selected]}
                                onMouseEnter={() => setSelected(index)}
                                onClick={() => choose(index)}
                            >
                                <span {...stylex.props(styles.number)}>{index < 9 ? index + 1 : '·'}</span>
                                <span {...stylex.props(styles.description)}>{choice.label}{choice.detail && <span {...stylex.props(styles.detail)}>{choice.detail}</span>}</span>
                            </Button>
                            {choice.canDrop && <Button onClick={() => choose(index, true)} aria-label={`Drop ${choice.label}`}>Drop</Button>}
                        </div>
                    ))}
                </div>
                <Button onClick={close}>Close</Button>
                <div {...stylex.props(styles.hint)}>1–9 choose · Enter select · Esc close{menu.kind === 'inventory' ? ' · X drop' : ''}</div>
            </Panel>
        </div>
    );
}
