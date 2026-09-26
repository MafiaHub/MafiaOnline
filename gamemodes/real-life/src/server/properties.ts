import type { Accounts } from './accounts';
import type { Session } from './session';
import { PropertyDefinitions } from './property-definitions';
import { can } from '../shared/permissions';
import { isRecord, type SavedPlace } from '../shared/protocol';
import { adminText } from './messages';
import { propertyText, type PropertyMessage } from './property-messages';
import {
    distance,
    heading,
    HOUSE_WORLD,
    INTERIOR_WORLD,
    INTERACTION_RADIUS,
    PROPERTY_EVENT,
    type House,
    type Interior,
    type Pose,
    type PropertyMarker,
} from '../shared/properties';

interface Visit {
    world: number;
    outside: SavedPlace;
}
interface GarageCar {
    houseId: number;
    slot: number;
    model: string;
    snapshot: string;
    vehicle?: Vehicle;
    retryAt: number;
}
const editorCommands = new Set([
    'house_entry',
    'house_garage',
    'house_set',
    'house_select',
    'interior_exit',
    'interior_entry',
    'interior_remove',
    'interior_go',
]);
const commands = new Set([
    ...editorCommands,
    'house_buy',
    'house_lock',
    'house_unlock',
    'garage_save',
    'houses',
    'interiors',
    'leave',
]);

export class Properties {
    private readonly selected = new Map<number, number>();
    private readonly visits = new Map<number, Visit>();
    private readonly cooldown = new Map<number, number>();
    private readonly sent = new Map<number, string>();
    private readonly cars = new Map<string, GarageCar>();
    readonly definitions: PropertyDefinitions;
    constructor(
        directory: string,
        private readonly accounts: Accounts,
        private readonly sessions: Map<number, Session>,
        private readonly claimCar: (vehicle: Vehicle) => void,
    ) {
        this.definitions = new PropertyDefinitions(directory);
        for (const save of accounts.garages()) {
            this.cars.set(`${save.houseId}:${save.slot}`, { ...save, retryAt: 0 });
        }
    }

    private reply(
        session: Session,
        key: PropertyMessage,
        values: Record<string, string | number> = {},
    ): void {
        session.player.sendMessage(propertyText(session.locale, key, values), 0xc8b382);
    }

    private ready(
        session: Session | undefined,
    ): session is Session & { account: NonNullable<Session['account']> } {
        return Boolean(
            session?.account &&
            session.entered &&
            session.player.spawned &&
            session.player.alive &&
            World.isReady() &&
            !this.accounts.getBan(session.account.id),
        );
    }

    private pose(player: Player): Pose {
        const p = player.position;

        return { position: { x: p.x, y: p.y, z: p.z }, heading: heading(player.rotation) };
    }

    private interior(house: House): Interior | undefined {
        return this.definitions.interiors.find(
            (value) => value.name === house.interior && value.mission === house.mission,
        );
    }

    private house(session: Session, argument?: string): House | undefined {
        const id = argument === undefined ? this.selected.get(session.player.id) : Number(argument);

        return this.definitions.houses.find(
            (value) => value.id === id && value.mission === World.getMission(),
        );
    }

    private nearbyHouse(player: Player, id?: string): House | undefined {
        return this.definitions.houses.find(
            (house) =>
                house.mission === World.getMission() &&
                (id === undefined || house.id === Number(id)) &&
                (player.virtualWorld === HOUSE_WORLD + house.id ||
                    (player.virtualWorld === 0 &&
                        house.entries.some(
                            (entry) =>
                                distance(entry.position, player.position) <= INTERACTION_RADIUS,
                        ))),
        );
    }

    handle(player: Player, name: string, args: string[]): boolean {
        const command = name.toLowerCase();

        if (!commands.has(command)) {
            return false;
        }

        const session = this.sessions.get(player.id);

        if (!this.ready(session)) {
            player.sendMessage(adminText(session?.locale ?? 'en', 'loginRequired'));

            return true;
        }

        const role = this.accounts.find(session.account.username)?.role ?? 'user';

        if (editorCommands.has(command) && !can(role, 'property.edit')) {
            player.sendMessage(adminText(session.locale, 'denied'));

            return true;
        }

        try {
            this.command(session, command, args);
        } catch (error) {
            process.stderr.write(`[lhrp] Property command failed: ${error}\n`);
            this.reply(session, 'failed');
        }

        return true;
    }

    private command(
        session: Session & { account: NonNullable<Session['account']> },
        command: string,
        args: string[],
    ): void {
        const player = session.player;
        const mission = World.getMission();

        if (command === 'houses') {
            this.reply(session, 'help');
            for (const house of this.definitions.houses.filter(
                (h) =>
                    h.mission === mission &&
                    this.accounts.houseOwner(h.id)?.accountId === session.account.id,
            )) {
                this.reply(session, 'houseInfo', {
                    id: house.id,
                    price: house.price,
                    interior: house.interior ?? '-',
                    garages: house.garages.length,
                });
            }

            return;
        }

        if (command === 'interiors') {
            for (const interior of this.definitions.interiors.filter(
                (i) => i.mission === mission,
            )) {
                this.reply(session, 'interiorInfo', { name: interior.name });
            }

            if (
                can(this.accounts.find(session.account.username)?.role ?? 'user', 'property.edit')
            ) {
                this.reply(session, 'editorHelp');
                this.reply(session, 'interiorHelp');
            }

            return;
        }

        if (command === 'leave') {
            this.leave(session);

            return;
        }

        if (command === 'house_buy') {
            const house = args.length <= 1 ? this.nearbyHouse(player, args[0]) : undefined;

            if (!house || player.getVehicle() || player.virtualWorld !== 0) {
                this.reply(session, 'nearHouse');

                return;
            }

            if (!this.interior(house)) {
                this.reply(session, 'unfinished');

                return;
            }

            const result = this.accounts.buyHouse(session.account.id, house.id, house.price);

            if (result === 'bought') {
                player.setMoney(this.accounts.balance(session.account.id));
            }

            this.reply(session, result, { id: house.id, price: house.price });
            this.sent.clear();

            return;
        }

        if (command === 'house_lock' || command === 'house_unlock') {
            const house = args.length <= 1 ? this.nearbyHouse(player, args[0]) : undefined;

            if (
                !house ||
                !this.accounts.lockHouse(house.id, session.account.id, command === 'house_unlock')
            ) {
                this.reply(session, 'ownerRequired');

                return;
            }

            this.reply(session, command === 'house_lock' ? 'locked' : 'unlocked', { id: house.id });
            this.sent.clear();

            return;
        }

        if (command === 'garage_save') {
            if (args.length > 2) {
                this.reply(session, 'garageHelp');

                return;
            }

            this.saveCar(session, args);

            return;
        }

        if (command === 'house_select') {
            const house = args.length === 1 ? this.house(session, args[0]) : undefined;

            if (!house) {
                this.reply(session, 'missing');

                return;
            }

            this.selected.set(player.id, house.id);
            this.reply(session, 'houseInfo', {
                id: house.id,
                price: house.price,
                interior: house.interior ?? '-',
                garages: house.garages.length,
            });

            return;
        }

        if (command === 'house_set') {
            const house = this.house(session, args[0]);
            const price = Number(args[1]);
            const interior = this.definitions.interiors.find(
                (i) => i.name === args[2] && i.mission === mission,
            );

            if (
                args.length !== 3 ||
                !house ||
                !interior ||
                !Number.isSafeInteger(price) ||
                price < 1 ||
                price > 1_000_000_000
            ) {
                this.reply(session, 'setHelp');

                return;
            }

            if (
                [...this.sessions.values()].some(
                    (s) => s.entered && s.player.virtualWorld === HOUSE_WORLD + house.id,
                )
            ) {
                this.reply(session, 'occupied');

                return;
            }

            this.definitions.updateHouse({ ...house, price, interior: interior.name });
            this.reply(session, 'houseInfo', {
                id: house.id,
                price,
                interior: interior.name,
                garages: house.garages.length,
            });

            this.sent.clear();

            return;
        }

        if (command === 'house_garage') {
            const house = args.length <= 1 ? this.house(session, args[0]) : undefined;
            const car = player.getVehicle();

            if (
                !house ||
                !car ||
                car.getDriver()?.id !== player.id ||
                player.virtualWorld !== 0 ||
                car.virtualWorld !== 0 ||
                Math.hypot(car.velocity.x, car.velocity.y, car.velocity.z) > 0.5
            ) {
                this.reply(session, 'garageRecordHelp');

                return;
            }

            const p = car.position,
                q = car.rotation;
            const garages = [
                ...house.garages,
                {
                    position: { x: p.x, y: p.y, z: p.z },
                    rotation: { w: q.w, x: q.x, y: q.y, z: q.z },
                },
            ];

            this.definitions.updateHouse({ ...house, garages });
            this.reply(session, 'garageRecorded', { id: house.id, slot: garages.length });

            return;
        }

        if (player.getVehicle()) {
            this.reply(session, 'onFoot');

            return;
        }

        if (command === 'house_entry') {
            if (args.length > 1 || player.virtualWorld !== 0) {
                this.reply(session, 'entryHelp');

                return;
            }

            const existing = args.length ? this.house(session, args[0]) : undefined;

            if (args.length && !existing) {
                this.reply(session, 'missing');

                return;
            }

            const house = existing
                ? { ...existing, entries: [...existing.entries, this.pose(player)] }
                : this.definitions.addHouse(mission, this.pose(player));

            if (existing) {
                this.definitions.updateHouse(house);
            }

            this.selected.set(player.id, house.id);
            this.reply(session, 'entryRecorded', { id: house.id });
        } else if (command === 'interior_exit') {
            if (args.length !== 1 || !/^[a-z0-9_-]{1,32}$/.test(args[0])) {
                this.reply(session, 'interiorHelp');

                return;
            }

            const current = this.definitions.interiors.find((i) => i.name === args[0]);

            if (
                current &&
                [...this.sessions.values()].some(
                    (s) =>
                        s.player.id !== player.id &&
                        s.entered &&
                        (s.player.virtualWorld === INTERIOR_WORLD + current.id ||
                            this.definitions.houses.some(
                                (h) =>
                                    h.interior === current.name &&
                                    s.player.virtualWorld === HOUSE_WORLD + h.id,
                            )),
                )
            ) {
                this.reply(session, 'occupied');

                return;
            }

            this.definitions.setInterior(args[0], mission, this.pose(player));
            this.reply(session, 'interiorRecorded', { name: args[0] });
        } else if (command === 'interior_entry') {
            const interior = this.definitions.interiors.find(
                (i) => i.name === args[0] && i.mission === mission,
            );

            if (args.length !== 1 || !interior || player.virtualWorld !== 0) {
                this.reply(session, 'interiorHelp');

                return;
            }

            const entrance = this.definitions.addEntrance(interior, this.pose(player));

            this.reply(session, 'publicRecorded', { id: entrance.id, name: interior.name });
        } else if (command === 'interior_remove') {
            this.reply(
                session,
                args.length === 1 && this.definitions.removeEntrance(Number(args[0]))
                    ? 'removed'
                    : 'missing',
            );
        } else if (command === 'interior_go') {
            const interior = this.definitions.interiors.find(
                (i) => i.name === args[0] && i.mission === mission,
            );

            if (args.length !== 1 || !interior) {
                this.reply(session, 'missing');

                return;
            }

            this.teleport(session, interior.exit, INTERIOR_WORLD + interior.id);
        }

        this.sent.clear();
    }

    private teleport(session: Session, destination: Pose, world: number): boolean {
        const player = session.player;
        const inventory = player.getInventory();

        if (!inventory || player.getVehicle()) {
            this.reply(session, 'onFoot');

            return false;
        }

        const health = player.health,
            money = player.money;
        const outside = this.savedPlace(session) ?? {
            mission: World.getMission(),
            ...this.pose(player),
        };

        if (!player.spawn(destination.position, destination.heading)) {
            this.reply(session, 'failed');

            return false;
        }

        player.setVirtualWorld(world);
        player.setHealth(health);
        player.setInventory(inventory.weapons, inventory.selected);
        player.setMoney(money);
        player.setCameraTarget(null);

        if (world === 0) {
            this.visits.delete(player.id);
        } else {
            this.visits.set(player.id, { world, outside });
        }

        this.cooldown.set(player.id, Date.now() + 1000);
        this.sent.delete(player.id);

        return true;
    }

    savedPlace(session: Session): SavedPlace | undefined {
        const visit = this.visits.get(session.player.id);

        if (visit && session.player.virtualWorld === visit.world) {
            return visit.outside;
        }

        // Admin teleports may arrive without a doorway visit. Never persist a
        // private interior coordinate for the next public-world login.
        const house = this.definitions.houses.find(
            (h) =>
                HOUSE_WORLD + h.id === session.player.virtualWorld &&
                h.mission === World.getMission(),
        );

        if (house) {
            return { mission: house.mission, ...house.entries[0] };
        }

        const interior = this.definitions.interiors.find(
            (i) =>
                INTERIOR_WORLD + i.id === session.player.virtualWorld &&
                i.mission === World.getMission(),
        );
        const entrance =
            interior &&
            this.definitions.entrances.find(
                (e) => e.interior === interior.name && e.mission === interior.mission,
            );

        if (entrance) {
            return { mission: entrance.mission, ...entrance.entry };
        }

        if (session.player.virtualWorld !== 0) {
            const position = session.player.getSuggestedSpawn();

            if (position) {
                return { mission: World.getMission(), position, heading: 0 };
            }
        }

        return undefined;
    }

    private leave(session: Session): void {
        const outside = this.savedPlace(session);

        if (!outside || outside.mission !== World.getMission()) {
            this.reply(session, 'missing');

            return;
        }

        this.teleport(session, outside, 0);
    }

    private markers(session: Session): PropertyMarker[] {
        const player = session.player;
        const mission = World.getMission();
        const origin = player.getVehicle()?.position ?? player.position;
        const all: PropertyMarker[] = [];

        if (player.virtualWorld === 0) {
            for (const house of this.definitions.houses.filter((h) => h.mission === mission)) {
                // Skip distant properties before querying ownership or formatting labels.
                if (
                    !house.entries.some((e) => distance(e.position, origin) < 65) &&
                    !house.garages.some((g) => distance(g.position, origin) < 65)
                ) {
                    continue;
                }

                const owner = this.accounts.houseOwner(house.id);

                house.entries.forEach((entry, index) =>
                    all.push({
                        key: `house:${house.id}:${index}`,
                        position: entry.position,
                        kind: owner ? 'entry' : 'sale',
                        label: propertyText(
                            session.locale,
                            owner ? (owner.unlocked ? 'entryLabel' : 'lockedLabel') : 'saleLabel',
                            { id: house.id, price: house.price },
                        ),
                    }),
                );

                if (owner?.accountId === session.account?.id) {
                    house.garages.forEach((slot, index) =>
                        all.push({
                            key: `garage:${house.id}:${index}`,
                            position: slot.position,
                            kind: 'garage',
                            label: propertyText(session.locale, 'garageLabel', {
                                id: house.id,
                                slot: index + 1,
                            }),
                        }),
                    );
                }
            }

            for (const entry of this.definitions.entrances.filter((e) => e.mission === mission)) {
                if (
                    distance(entry.entry.position, origin) < 65 &&
                    this.definitions.interiors.some(
                        (i) => i.name === entry.interior && i.mission === mission,
                    )
                ) {
                    all.push({
                        key: `public:${entry.id}`,
                        position: entry.entry.position,
                        kind: 'entry',
                        label: propertyText(session.locale, 'publicLabel', {
                            name: entry.interior,
                        }),
                    });
                }
            }
        } else {
            const house = this.definitions.houses.find(
                (h) => HOUSE_WORLD + h.id === player.virtualWorld && h.mission === mission,
            );
            const interior = house
                ? this.interior(house)
                : this.definitions.interiors.find(
                      (i) => INTERIOR_WORLD + i.id === player.virtualWorld && i.mission === mission,
                  );

            if (interior) {
                all.push({
                    key: 'exit',
                    position: interior.exit.position,
                    kind: 'exit',
                    label: propertyText(session.locale, 'exitLabel'),
                });
            }
        }

        return all
            .filter((marker) => distance(marker.position, origin) < 65)
            .sort((a, b) => distance(a.position, origin) - distance(b.position, origin))
            .slice(0, 20);
    }

    use(player: Player, payload: unknown): void {
        const session = this.sessions.get(player.id);

        if (
            !this.ready(session) ||
            !isRecord(payload) ||
            typeof payload.key !== 'string' ||
            payload.generation !== World.getMissionGeneration() ||
            payload.world !== player.virtualWorld ||
            Date.now() < (this.cooldown.get(player.id) ?? 0)
        ) {
            return;
        }

        this.cooldown.set(player.id, Date.now() + 500);
        const marker = this.markers(session).find((m) => m.key === payload.key);

        if (
            !marker ||
            distance(marker.position, player.getVehicle()?.position ?? player.position) >
                (marker.kind === 'garage' ? 5 : INTERACTION_RADIUS)
        ) {
            return;
        }

        try {
            if (marker.kind === 'garage') {
                const [, id, slot] = marker.key.split(':');

                this.saveCar(session, [id, String(Number(slot) + 1)]);

                return;
            }

            if (player.getVehicle()) {
                this.reply(session, 'onFoot');

                return;
            }

            if (marker.key === 'exit') {
                this.leave(session);

                return;
            }

            const [kind, id] = marker.key.split(':');

            if (kind === 'house') {
                const house = this.definitions.houses.find((h) => h.id === Number(id))!;
                const owner = this.accounts.houseOwner(house.id);

                if (!owner) {
                    this.reply(session, 'buyHelp', { id: house.id, price: house.price });

                    return;
                }

                if (owner.accountId !== session.account.id && !owner.unlocked) {
                    this.reply(session, 'locked', { id: house.id });

                    return;
                }

                const interior = this.interior(house);

                if (!interior) {
                    this.reply(session, 'unfinished');

                    return;
                }

                this.teleport(session, interior.exit, HOUSE_WORLD + house.id);
            } else {
                const entrance = this.definitions.entrances.find((e) => e.id === Number(id))!;
                const interior = this.definitions.interiors.find(
                    (i) => i.name === entrance.interior && i.mission === World.getMission(),
                );

                if (interior) {
                    this.teleport(session, interior.exit, INTERIOR_WORLD + interior.id);
                }
            }
        } catch (error) {
            process.stderr.write(`[lhrp] Property interaction failed: ${error}\n`);
            this.reply(session, 'failed');
        }
    }

    private saveCar(
        session: Session & { account: NonNullable<Session['account']> },
        args: string[],
    ): void {
        const player = session.player,
            car = player.getVehicle();

        if (
            !car ||
            car.getDriver()?.id !== player.id ||
            player.virtualWorld !== 0 ||
            car.virtualWorld !== 0 ||
            car.terminalState !== 0 ||
            Math.hypot(car.velocity.x, car.velocity.y, car.velocity.z) > 0.5 ||
            car.getOccupants().some((p) => p && p.id !== player.id)
        ) {
            this.reply(session, 'garageHelp');

            return;
        }

        const available = this.definitions.houses
            .filter(
                (h) =>
                    h.mission === World.getMission() &&
                    this.accounts.houseOwner(h.id)?.accountId === session.account.id &&
                    (args[0] === undefined || h.id === Number(args[0])),
            )
            .flatMap((h) => h.garages.map((slot, i) => ({ house: h, slot, index: i })))
            .filter(
                (s) =>
                    (args[1] === undefined || s.index + 1 === Number(args[1])) &&
                    distance(s.slot.position, car.position) <= 5,
            )
            .sort(
                (a, b) =>
                    distance(a.slot.position, car.position) -
                    distance(b.slot.position, car.position),
            );
        const chosen = available[0];

        if (!chosen) {
            this.reply(session, 'ownerRequired');

            return;
        }

        const key = `${chosen.house.id}:${chosen.index}`;

        if (
            [...this.cars.entries()].some(
                ([otherKey, value]) => otherKey !== key && value.vehicle?.id === car.id,
            )
        ) {
            this.reply(session, 'alreadySaved');

            return;
        }

        const old = this.cars.get(key)?.vehicle;

        if (old && old.id !== car.id && old.getOccupants().some(Boolean)) {
            this.reply(session, 'occupied');

            return;
        }

        const snapshot = car.saveState();

        if (!snapshot) {
            this.reply(session, 'waitForCar');

            return;
        }

        this.accounts.saveGarage(chosen.house.id, chosen.index, car.model, snapshot);
        this.claimCar(car);
        this.cars.set(key, {
            houseId: chosen.house.id,
            slot: chosen.index,
            model: car.model,
            snapshot,
            vehicle: car,
            retryAt: 0,
        });

        if (old && old.id !== car.id) {
            old.destroy();
        }

        this.reply(session, 'carSaved', { id: chosen.house.id, slot: chosen.index + 1 });
    }

    update(): void {
        if (!World.isReady()) {
            return;
        }

        for (const session of this.sessions.values()) {
            if (!this.ready(session)) {
                continue;
            }

            const message = JSON.stringify({
                generation: World.getMissionGeneration(),
                world: session.player.virtualWorld,
                markers: this.markers(session),
            });

            if (this.sent.get(session.player.id) !== message) {
                session.player.emit(PROPERTY_EVENT.markers, message);
                this.sent.set(session.player.id, message);
            }
        }

        const vehicles = World.getVehicles();
        const now = Date.now();

        for (const saved of this.cars.values()) {
            const house = this.definitions.houses.find(
                (h) => h.id === saved.houseId && h.mission === World.getMission(),
            );
            const slot = house?.garages[saved.slot];

            if (!slot) {
                continue;
            }

            if (saved.vehicle) {
                const car = vehicles.find((v) => v.id === saved.vehicle!.id);

                if (
                    car &&
                    car.terminalState === 0 &&
                    car.position.y > -100 &&
                    Math.abs(car.position.x) < 16384 &&
                    Math.abs(car.position.z) < 16384
                ) {
                    continue;
                }

                if (car?.getOccupants().some(Boolean)) {
                    continue;
                }

                car?.destroy();
                saved.vehicle = undefined;
                saved.retryAt = now + 10000;
            }

            if (
                now < saved.retryAt ||
                vehicles.some(
                    (car) => car.virtualWorld === 0 && distance(car.position, slot.position) < 4,
                ) ||
                [...this.sessions.values()].some(
                    (s) =>
                        s.entered &&
                        s.player.virtualWorld === 0 &&
                        distance(s.player.position, slot.position) < 2,
                )
            ) {
                continue;
            }

            saved.retryAt = now + 10000;
            const controller = [...this.sessions.values()].find(
                (s) => this.ready(s) && s.player.virtualWorld === 0,
            )?.player;

            if (!controller) {
                continue;
            }

            const vehicle = Vehicle.spawn(
                saved.model,
                slot.position,
                new Quaternion(slot.rotation.w, slot.rotation.x, slot.rotation.y, slot.rotation.z),
                controller,
            );

            if (!vehicle) {
                continue;
            }

            if (!vehicle.restoreState(saved.snapshot)) {
                vehicle.destroy();
                process.stderr.write(
                    `[lhrp] Could not restore garage ${saved.houseId}:${saved.slot + 1}\n`,
                );

                continue;
            }

            vehicle.setVirtualWorld(0);
            saved.vehicle = vehicle;
            vehicles.push(vehicle);
        }
    }

    release(player: Player): void {
        this.visits.delete(player.id);
        this.selected.delete(player.id);
        this.sent.delete(player.id);
        this.cooldown.delete(player.id);
    }

    reset(): void {
        for (const saved of this.cars.values()) {
            saved.vehicle?.destroy();
            saved.vehicle = undefined;
            saved.retryAt = 0;
        }

        this.visits.clear();
        this.selected.clear();
        this.sent.clear();
        this.cooldown.clear();
    }
}
