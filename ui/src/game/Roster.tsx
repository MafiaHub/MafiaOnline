import * as stylex from '@stylexjs/stylex';
import { roster } from '../store';
import { colors } from '../tokens.stylex';

const styles = stylex.create({
    table: {
        width: '100%',
        borderCollapse: 'collapse',
        fontSize: '0.92rem',
    },
    head: {
        fontSize: '0.62rem',
        fontWeight: 600,
        letterSpacing: '0.16em',
        textTransform: 'uppercase',
        color: colors.muted,
        textAlign: 'left',
        paddingBottom: '0.5rem',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.line,
    },
    cell: {
        padding: '0.375rem 0.5rem 0.375rem 0',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.border,
        color: colors.text,
        whiteSpace: 'nowrap',
    },
    index: {
        width: '2rem',
        color: colors.faint,
        fontVariantNumeric: 'tabular-nums',
    },
    local: {
        fontWeight: 600,
    },
    health: {
        width: '7rem',
    },
    bar: {
        height: '0.3rem',
        backgroundColor: colors.raised,
        overflow: 'hidden',
    },
    fill: {
        height: '100%',
        backgroundColor: colors.accentHover,
    },
    ping: {
        width: '3.5rem',
        fontSize: '0.78rem',
        color: colors.muted,
        textAlign: 'right',
        fontVariantNumeric: 'tabular-nums',
    },
    // The ping column's cell size must not leak into its header.
    headPing: {
        fontSize: '0.62rem',
    },
    state: {
        width: '5rem',
        fontSize: '0.65rem',
        fontWeight: 600,
        letterSpacing: '0.14em',
        textTransform: 'uppercase',
        color: colors.muted,
        textAlign: 'right',
    },
    dead: {
        color: colors.danger,
    },
    empty: {
        padding: '1rem 0',
        textAlign: 'center',
        fontSize: '0.875rem',
        color: colors.muted,
    },
});

export function Roster() {
    const players = roster.value.players;
    if (players.length === 0) {
        return <div {...stylex.props(styles.empty)}>Nobody in town yet.</div>;
    }
    return (
        <table {...stylex.props(styles.table)}>
            <thead>
                <tr>
                    <th {...stylex.props(styles.head)}>#</th>
                    <th {...stylex.props(styles.head)}>Name</th>
                    <th {...stylex.props(styles.head)}>Health</th>
                    <th {...stylex.props(styles.head, styles.ping, styles.headPing)}>Ping</th>
                    <th {...stylex.props(styles.head)} />
                </tr>
            </thead>
            <tbody>
                {players.map((player, index) => (
                    <tr key={player.id}>
                        <td {...stylex.props(styles.cell, styles.index)}>{index + 1}</td>
                        <td {...stylex.props(styles.cell, player.local && styles.local)}>{player.name}</td>
                        <td {...stylex.props(styles.cell, styles.health)}>
                            <div {...stylex.props(styles.bar)}>
                                <div {...stylex.props(styles.fill)} style={{ width: `${Math.max(0, Math.min(100, player.health))}%` }} />
                            </div>
                        </td>
                        <td {...stylex.props(styles.cell, styles.ping)}>{player.ping > 0 ? `${player.ping} ms` : ''}</td>
                        <td {...stylex.props(styles.cell, styles.state, !player.alive && player.spawned && styles.dead)}>{!player.spawned ? 'arriving' : player.alive ? '' : 'wasted'}</td>
                    </tr>
                ))}
            </tbody>
        </table>
    );
}
