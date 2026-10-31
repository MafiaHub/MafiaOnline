// The city's traffic. The server clears jams on its own (Traffic jam watch,
// mod.trafficJam in server.json); admins can clear the road around them,
// restart the traffic or hand it to a standby simulation host. The console
// has the same: "traffic status | reset <x> <y> <z> [radius] | restart | handover".
import * as commands from './lib/commands';
import * as admin from './lib/admin';
import * as serverConsole from './lib/console';
import * as log from './lib/log';
import { toast } from './lib/notify';
import { events } from './lib/lifecycle';
import { plural } from './lib/util';

const DEFAULT_RADIUS = 60;
const MAX_RADIUS = 800;

function status(): string {
    const host = Traffic.getHost();
    const jam = Traffic.getJamSettings();
    const hostText = host.attached
        ? `host attached, ${host.standby} standby`
        : `no host, ${host.standby} standby`;
    const jamText = jam.enabled
        ? `jam watch on: ${jam.minCars} cars within ${jam.radius} m stopped ${jam.stuckSeconds} s`
        : 'jam watch off';

    return `Traffic: ${hostText}; ${jamText}.`;
}

function restart(handover: boolean): string {
    if (Traffic.restartHost(handover)) {
        return handover
            ? 'A standby host takes the traffic over.'
            : 'The traffic starts over; riders stepped off the trams.';
    }

    const host = Traffic.getHost();

    if (!host.attached) {
        return 'No simulation host runs the traffic.';
    }

    return 'No standby simulation host is connected to hand over to.';
}

commands.register(
    {
        name: 'traffic',
        usage: '/traffic [reset [radius] | restart | handover]',
        help: 'Admins: clear jammed traffic or restart it.',
        category: 'Dev',
        hidden: true,
    },
    (player, args) => {
        if (!admin.isAdmin(player)) {
            toast(player, 'Only admins manage the traffic.', { tone: 'bad' });

            return;
        }

        const [action = 'status', value] = args;

        if (action === 'reset') {
            const radius = value === undefined ? DEFAULT_RADIUS : Number(value);

            if (!Number.isFinite(radius) || radius < 0 || radius > MAX_RADIUS) {
                toast(player, `Use /traffic reset [radius 0-${MAX_RADIUS}].`, { tone: 'bad' });

                return;
            }

            const origin = player.getVehicle() ?? player;
            const count = Traffic.resetArea(origin.position, radius);

            toast(player, `${plural(count, 'traffic car')} within ${radius} m sent off the road.`, {
                tone: count ? 'good' : 'info',
            });

            log.info(`${player.nickname} cleared ${count} traffic cars within ${radius} m`);
        } else if (action === 'restart' || action === 'handover') {
            const message = restart(action === 'handover');

            toast(player, message);
            log.info(`${player.nickname}: traffic ${action}: ${message}`);
        } else {
            toast(player, status());
        }
    },
);

serverConsole.command('traffic', () =>
    log.info(
        `${status()} Console: traffic status | reset <x> <y> <z> [radius] | restart | handover`,
    ),
);

serverConsole.action('traffic', 'reset', (args) => {
    const [x, y, z, radius = DEFAULT_RADIUS] = args.map(Number);

    if (![x, y, z, radius].every(Number.isFinite) || radius < 0 || radius > MAX_RADIUS) {
        log.info(`Usage: traffic reset <x> <y> <z> [radius 0-${MAX_RADIUS}]`);

        return;
    }

    log.info(`${Traffic.resetArea({ x, y, z }, radius)} traffic cars sent off the road`);
});

serverConsole.action('traffic', 'restart', () => log.info(restart(false)));
serverConsole.action('traffic', 'handover', () => log.info(restart(true)));

events.on('trafficJamReset', ({ position, vehicles, stuckSeconds }) => {
    log.info(
        `Traffic jam cleared at ${position.x.toFixed(0)}, ${position.y.toFixed(0)}, ` +
            `${position.z.toFixed(0)}: ${vehicles} cars stood ${Math.round(stuckSeconds)} s`,
    );
});

events.on('trafficHostChange', ({ reason, ejected }) => {
    log.info(
        `Traffic ${reason}${ejected ? `: ${plural(ejected, 'rider')} taken off the trams` : ''}`,
    );
});

events.on('playerRailwayEject', (player) => {
    toast(player, 'The trams stopped for a moment: you stepped off where you boarded.', {
        title: 'Lost Heaven Transit',
    });
});
