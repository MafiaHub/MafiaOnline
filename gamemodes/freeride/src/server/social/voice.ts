// The HUD lists who is talking nearby on proximity voice. Only players in
// earshot are told (a shout carries 25 m): sent to the whole server, every
// word anyone said anywhere repainted every HUD. The stop goes to the same
// players as the start, wherever they are by then.
import { distance, emit } from '../lib/util';
import { events } from '../lib/lifecycle';
import { PlayerMap } from '../lib/players';

const VOICE_LIST_RANGE = 40;

// The speaker's id -> the ids of the players told they started.
const voiceListeners = new PlayerMap<Set<number>>();

function voiceNote(
    player: Player,
    talking: boolean,
): { id: number; name: string; talking: boolean } {
    return { id: player.id, name: player.nickname, talking };
}

events.on('playerVoiceStart', (player) => {
    const listeners = World.getPlayers().filter(
        (other) =>
            other.id === player.id ||
            (other.virtualWorld === player.virtualWorld &&
                distance(other.position, player.position) <= VOICE_LIST_RANGE),
    );

    voiceListeners.set(player.id, new Set(listeners.map((other) => other.id)));

    for (const other of listeners) {
        emit(other, 'fr:voice', voiceNote(player, true));
    }
});

function endVoice(player: Player): void {
    const listeners = voiceListeners.get(player.id);

    voiceListeners.delete(player.id);

    if (!listeners) {
        return;
    }

    for (const other of World.getPlayers()) {
        if (other.id !== player.id && listeners.has(other.id)) {
            emit(other, 'fr:voice', voiceNote(player, false));
        }
    }
}

events.on('playerVoiceStop', (player) => {
    // The speaker's own HUD too.
    if (voiceListeners.has(player.id)) {
        emit(player, 'fr:voice', voiceNote(player, false));
    }

    endVoice(player);
});

// A disconnect ends speech without playerVoiceStop.
events.on('playerDisconnect', endVoice);
