import * as stylex from '@stylexjs/stylex';
import { roster } from '../store';
import { colors, fonts } from '../tokens.stylex';

const styles = stylex.create({
    table: {
        width: '100%',
        borderCollapse: 'collapse',
        fontFamily: fonts.body,
        fontSize: '1rem',
    },
    head: {
        fontFamily: fonts.heading,
        fontSize: '0.62rem',
        fontWeight: 700,
        letterSpacing: '0.22em',
        textTransform: 'uppercase',
        color: colors.muted,
        textAlign: 'left',
        paddingBottom: '0.4rem',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.goldLine,
    },
    cell: {
        padding: '0.35rem 0.4rem 0.35rem 0',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.goldHair,
        color: colors.cream,
        whiteSpace: 'nowrap',
    },
    index: {
        width: '2rem',
        fontFamily: fonts.body,
        color: colors.muted,
    },
    local: {
        color: colors.goldBright,
    },
    health: {
        width: '7rem',
    },
    bar: {
        height: '0.35rem',
        backgroundColor: 'rgba(242, 230, 203, 0.12)',
    },
    fill: {
        height: '100%',
        backgroundImage: 'linear-gradient(90deg, #7a1515, #c23a2a)',
    },
    state: {
        width: '5rem',
        fontFamily: fonts.body,
        fontSize: '0.78rem',
        color: colors.muted,
        textAlign: 'right',
    },
    dead: {
        color: colors.bloodBright,
    },
    empty: {
        padding: '1rem 0',
        textAlign: 'center',
        fontStyle: 'italic',
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
                        <td {...stylex.props(styles.cell, styles.state, !player.alive && player.spawned && styles.dead)}>{!player.spawned ? 'arriving' : player.alive ? '' : 'wasted'}</td>
                    </tr>
                ))}
            </tbody>
        </table>
    );
}
