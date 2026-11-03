// The car radio's on/off, kept with the player's record. The client owns the
// radio itself; F9 and /radio both flip it and the choice comes back on spawn.
import * as profiles from '../lib/profiles';
import * as commands from '../lib/commands';
import { emit, onClient } from '../lib/util';
import { events } from '../lib/lifecycle';
import { fields } from '../lib/payload';

function sendRadio(player: Player): void {
    const record = profiles.get(player);

    if (record) {
        emit(player, 'fr:radio:setting', { enabled: record.radio !== false });
    }
}

events.on('playerSpawn', sendRadio);

onClient('fr:radio:set', (player, request: unknown) => {
    const { enabled } = fields(request);

    if (typeof enabled === 'boolean' && profiles.get(player)) {
        profiles.setField(player, 'radio', enabled);
    }
});

commands.register(
    {
        name: 'radio',
        usage: '/radio [on|off]',
        help: 'Turn the car radio on or off; Free Ride remembers it (F9 does the same).',
        category: 'Cars',
    },
    (player, args) => {
        const record = profiles.get(player);

        if (!record) {
            return;
        }

        const wanted = args[0] === 'on' ? true : args[0] === 'off' ? false : record.radio === false;

        profiles.setField(player, 'radio', wanted);
        emit(player, 'fr:radio:setting', { enabled: wanted, announce: true });
    },
);
