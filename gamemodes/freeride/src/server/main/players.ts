// A player's time in town: spawning on stock missions, the welcome and the
// tips, the join and leave lines in the feed, and the record written once
// every module has had its say on the way out.
import * as profiles from '../lib/profiles';
import * as life from '../lib/life';
import * as nametag from '../lib/nametag';
import * as log from '../lib/log';
import { toast, feed, name, text, COLORS } from '../lib/notify';
import { inFreeRide, formatMoney } from '../lib/util';
import { afterDisconnect, events, every, onStop } from '../lib/lifecycle';
import { PlayerMap, PlayerSet } from '../lib/players';
import { onLogin } from '../lib/login';

const SPAWN_CHECK_MS = 200;
const TIP_EVERY_MS = 7 * 60_000;

const TIPS = [
    'F2 opens the city menu: jobs, races, crews, events and everyone in town.',
    'Buy a car at the car lot with /dealer. /garage brings it to you, /park puts it away, /lock keeps it yours.',
    'Gold arrows at front doors mark homes for sale: a room in a tenement for a few thousand, a flat, or a villa on the hills. Press G at the door. F2 → Homes.',
    'Your home is yours to furnish (F2 → Homes), lock or share with your crew, and a flat or villa keeps a car at its kerb: drive onto the spot and press G.',
    "Jobs pay: taxi fares, courier envelopes, Lucas's export list, police duty and bootleg runs.",
    'Fancy a uniform? Sign on under the arrow at the fire station or the hospital (both on the city map), then take a truck or an ambulance.',
    'Out in the countryside the tobacco farm takes on farmhands: plant, water, pick and cure the crop, then truck it to a buyer. F2 → Jobs.',
    'Tobacco exported at the docks fetches far more than in the village, but the whole city sees the loaded farm truck coming, and anyone may take it.',
    "Somebody cooks real Cuban rum out in the woods, and no map shows where. The sailor in Salieri's billiard room drinks enough of it to know.",
    "A rum still's gauge climbs on its own: tap G to vent it and keep it in the green. Past the top, the still blows.",
    'Hold TAB for the city map: job stations, your objectives and your crewmates are marked on it.',
    'Lucas Bertone pays for particular traffic cars. /export shows his list.',
    'Go on police duty with /duty and take down wanted players for pay.',
    'Thirty cash stashes hide at the roadside. They show only up close.',
    'Crews can take over street corners with turf wars. /turf shows who holds what.',
    'Speed traps sit on the longest roads. /speedtraps shows the records.',
    'Feeling lucky? /dice <player> <bet>. Feeling bold? /rob a shop counter with a gun drawn.',
    'Open a street race with /race and race anyone who joins. Routes follow the real city roads.',
    'Three kills in a row put a price on your head. Anyone who drops you collects it.',
    'Challenge someone with /duel <name>. Duels run in a private copy of the city.',
    'Start a crew with /crew create <TAG>. Crewmates share a colour and see each other on the city map (TAB).',
    'Supply drops and hot cars appear around the city, marked on the city map (TAB). /event follows one.',
    "Salieri's back window has courier work, Yellow Pete sells guns, the pump sells fuel.",
    'Use /goto <name> to ask for a ride to a friend; they decide with /accept.',
];

// The mission generation each player was last spawned in by spawnReadyPlayers.
const spawnedInGeneration = new PlayerMap<number>();
const welcomed = new PlayerSet();
let generation = 0;
let tipIndex = 0;

// Stock missions other than Free Ride have no selector; their native spawn
// hint places everyone as soon as the whole server has loaded.
function spawnReadyPlayers(): void {
    const next = World.getMissionGeneration();

    if (next !== generation) {
        generation = next;
        spawnedInGeneration.clear();
    }

    if (!World.isReady() || inFreeRide()) {
        return;
    }

    for (const player of World.getPlayers()) {
        if (player.spawned || spawnedInGeneration.get(player.id) === generation) {
            continue;
        }

        const position = player.getSuggestedSpawn();

        if (!position || !player.spawn(position)) {
            continue;
        }

        spawnedInGeneration.set(player.id, generation);
    }
}

function welcome(player: Player): void {
    const record = profiles.get(player);

    if (welcomed.has(player.id) || !record) {
        return;
    }

    welcomed.add(player.id);
    const { level, rank } = profiles.progress(record);
    const returning = record.playMs > 0 || record.xp > 0;

    toast(
        player,
        returning
            ? `Welcome back, ${player.nickname}. Level ${level} ${rank} · ${formatMoney(player.money)}.`
            : `Welcome to Lost Heaven, ${player.nickname}. You start with ${formatMoney(player.money)}.`,
        { title: 'Free Ride', tone: 'gold', seconds: 7 },
    );

    setTimeout(() => {
        if (player.spawned) {
            toast(
                player,
                'F2 opens the city menu · F1 shows who is in town · /help lists commands.',
                { seconds: 8 },
            );
        }
    }, 2500);
}

function showTip(): void {
    const players = World.getPlayers().filter((player) => player.spawned);

    if (!players.length) {
        return;
    }

    const tip = TIPS[tipIndex++ % TIPS.length];

    for (const player of players) {
        toast(player, tip, { title: 'Tip', seconds: 7 });
    }
}

every(SPAWN_CHECK_MS, spawnReadyPlayers);
every(TIP_EVERY_MS, showTip);

onStop(() => {
    // A restart keeps the players connected; take off what this run put on them.
    for (const player of World.getPlayers()) {
        for (const key of ['fr.bounty', 'fr.activity', 'fr.crew']) {
            profiles.setPublic(player, key, null);
        }

        nametag.reset(player);
    }

    // Nothing to drop from Node's module cache for the restart: the resource
    // is one bundle, and each start runs it in a fresh Node runtime of its
    // own (the framework's NodeEngine::CreateResourceRuntime), so every
    // module is loaded afresh.
});

// The record is attached once the player has logged in (selector/).
events.on('playerConnect', (player) => log.info(`${player.nickname} connected`));

onLogin((player, { returning, restored }) => {
    if (!restored) {
        feed('join', [
            name(player, COLORS.gold),
            text(returning ? ' is back in town' : ' arrived in Lost Heaven'),
        ]);
    }
});

events.on('playerDisconnect', (player) => {
    if (profiles.get(player)) {
        feed('leave', [name(player), text(' left the city')]);
    }
});

// After every other module's disconnect handler, so they can still write to
// the player's record (a parked car's condition, for one).
afterDisconnect(profiles.detach);

events.on('missionReady', spawnReadyPlayers);
events.on('playerMissionReady', spawnReadyPlayers);
events.on('playerSpawn', (player) => {
    if (!life.isTransit(player)) {
        welcome(player);
    }
});
