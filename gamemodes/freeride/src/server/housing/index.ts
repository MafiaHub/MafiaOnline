// Homes. Every map's real residential buildings (data/houses.js) can be
// bought: tenement rooms, apartments and, on the hills, villas. A small
// arrow at the door says what is for sale and for how much; G buys it (press
// twice), G at your own door goes in. Inside, each home is its own virtual
// world over a shared room (lib/house_rooms.ts), so a hundred homes need one
// set of rooms. The owner locks it, opens it to their crew, invites friends,
// furnishes it from a catalogue (lib/house_decor.ts) with a small editor,
// and, at a flat or a house, parks a personal car at the kerb outside: it
// keeps its condition and is there after a restart (server/dealership.ts).
// Selling a home back returns 60% of its price; there is no running cost.
//
// The modules, each building on the ones before it:
//  catalogue   every home of every map, its style, price, door and kerb
//  homes       who owns which (saved), who may come in
//  occupancy   who is in which home and room
//  furniture   a home's decorations standing in its world
//  decorating  the editor: buying, carrying and placing pieces
//  entering    the moves in, between the rooms and out
//  markers     the arrows and map marks on the street
//  market      buying and selling homes
//  keys        locking, the crew, guests, showing people out
//  parking     cars at the kerb
//  doorstep    the G key and its prompt, and the watch on the homes
//  menu        the F2 Homes tab
//  commands    /home and /homes
import * as life from '../lib/life';
import * as rooms from '../lib/house_rooms';
import { events, onStart, onStop } from '../lib/lifecycle';
import { MAIN_WORLD } from '../lib/util';
import { ALL } from './catalogue';
import { forgetAll as forgetCarried, stopCarrying } from './decorating';
import { release } from './entering';
import { despawnHome, forgetAll as forgetFurniture } from './furniture';
import { load, ownedBy, save } from './homes';
import { publish } from './markers';
import { inside, insideHouse, moves } from './occupancy';
import './market';
import './keys';
import './parking';
import './doorstep';
import './menu';
import './commands';

onStart(() => {
    load();
    rooms.build();
});

onStop(() => {
    for (const player of World.getPlayers()) {
        const house = insideHouse(player);

        stopCarrying(player);

        if (!house) {
            continue;
        }

        if (player.virtualWorld === house.world) {
            player.setVirtualWorld(MAIN_WORLD);
        }

        if (player.alive) {
            life.teleport(player, house.out.position, house.out.heading);
        }
    }

    for (const house of ALL.values()) {
        despawnHome(house);
    }

    rooms.teardown();
    save();
});

events.on('playerSpawn', (player) => {
    if (!life.isTransit(player)) {
        publish(player);
    }
});

events.on('playerDeath', (player) => {
    stopCarrying(player);

    if (inside.has(player.id)) {
        release(player);
    }
});

events.on('playerDisconnect', (player) => {
    stopCarrying(player);
    release(player);
});

// The mission change took every map object and world with it.
events.on('missionChange', () => {
    for (const player of World.getPlayers()) {
        const house = insideHouse(player);

        if (house && player.virtualWorld === house.world) {
            player.setVirtualWorld(MAIN_WORLD);
        }
    }

    inside.clear();
    moves.clear();
    forgetCarried();
    forgetFurniture();
    rooms.forget();
    rooms.build();
});

export { insideHouse, release, ownedBy as homesOf, ALL };
