import { can, isRole, type Permission } from '../shared/permissions';
import { adminText, type AdminMessage } from './messages';
import type { Accounts } from './accounts';
import type { Session } from './session';

const permissions: Record<string, Permission> = {
    car: 'car.spawn',
    kick: 'player.kick',
    ban: 'player.ban',
    unban: 'player.ban',
    follow: 'player.follow',
    tp: 'player.teleport',
    goto: 'player.teleport',
    weapon: 'weapon.give',
    giveweapon: 'weapon.give',
    weapons: 'weapon.give',
};

export class AdminCommands {
    private readonly cars = new Map<number, { owner: number; retired: boolean }>();
    private readonly lastCar = new Map<number, number>();

    constructor(
        private readonly accounts: Accounts,
        private readonly sessions: Map<number, Session>,
        private readonly save: (session: Session) => void,
    ) {}

    private reply(
        session: Session,
        key: AdminMessage,
        values: Record<string, string | number> = {},
    ): void {
        session.player.sendMessage(adminText(session.locale, key, values), 0xc8b382);
    }

    private log(key: AdminMessage, values: Record<string, string | number> = {}): void {
        process.stdout.write(`[lhrp] ${adminText('en', key, values)}\n`);
    }

    private player(value: string): Player | undefined {
        const players = World.getPlayers();

        return value.startsWith('#')
            ? players.find((player) => String(player.id) === value.slice(1))
            : players.find((player) => player.nickname.toLowerCase() === value.toLowerCase());
    }

    private target(session: Session, value: string): Player | undefined {
        const player = this.player(value);

        if (!player) {
            this.reply(session, 'playerMissing', { player: value });
        }

        return player;
    }

    handle(player: Player, name: string, args: string[]): boolean {
        const command = name.toLowerCase();
        const permission = Object.hasOwn(permissions, command) ? permissions[command] : undefined;

        if (!permission && command !== 'help' && command !== 'players') {
            return false;
        }

        const session = this.sessions.get(player.id);

        if (!session?.account || !session.entered) {
            player.sendMessage(adminText(session?.locale ?? 'en', 'loginRequired'));

            return true;
        }

        // Read the current rank so a console change takes effect immediately.
        session.account.role = this.accounts.find(session.account.username)?.role ?? 'user';

        if (
            this.accounts.getBan(session.account.id) ||
            (permission && !can(session.account.role, permission))
        ) {
            this.reply(session, 'denied');

            return true;
        }

        switch (command) {
            case 'help':
                this.reply(session, 'helpUser');

                if (can(session.account.role, 'player.kick')) {
                    this.reply(session, 'helpAdmin');
                    this.reply(session, 'helpAdminTools');
                }

                break;
            case 'players':
                for (const other of World.getPlayers()) {
                    this.reply(session, 'playerEntry', { id: other.id, name: other.nickname });
                }

                break;
            case 'car':
                this.car(session, args);
                break;
            case 'kick':
                this.kick(session, args);
                break;
            case 'ban':
                this.ban(session, args);
                break;
            case 'unban':
                this.unban(session, args);
                break;
            case 'follow':
                this.follow(session, args);
                break;
            case 'tp':
            case 'goto':
                this.teleport(session, args);
                break;
            case 'weapon':
            case 'giveweapon':
                this.weapon(session, args);
                break;
            case 'weapons':
                for (const weapon of Weapon.list()) {
                    this.reply(session, 'weaponList', {
                        weapons: `${weapon.weaponId}: ${weapon.name}`,
                    });
                }

                break;
        }

        return true;
    }

    private car(session: Session, args: string[]): void {
        const player = session.player;
        const model = args[0] ?? 'taxi00.i3d';

        if (args.length > 1 || !/^[a-z0-9_-]{1,59}\.i3d$/i.test(model)) {
            this.reply(session, 'carUsage');

            return;
        }

        if (!World.isReady() || !player.spawned || !player.alive) {
            this.reply(session, 'spawnRequired');

            return;
        }

        const now = Date.now();

        if (now - (this.lastCar.get(player.id) ?? 0) < 1000) {
            this.reply(session, 'cooldown');

            return;
        }

        this.lastCar.set(player.id, now);
        const origin = player.getVehicle() ?? player;
        const { x, y, z } = origin.position;
        const vehicle = Vehicle.spawn(model, { x: x + 4, y, z }, origin.rotation, player);

        if (!vehicle) {
            this.reply(session, 'failed');

            return;
        }

        vehicle.setVirtualWorld(player.virtualWorld);

        if (!player.putInVehicle(vehicle, 0)) {
            vehicle.destroy();
            this.reply(session, 'failed');

            return;
        }

        this.retireCars(player.id);
        this.cars.set(vehicle.id, { owner: player.id, retired: false });
        this.reply(session, 'carSpawned', { model });
    }

    private kick(session: Session, args: string[]): void {
        if (!args.length) {
            this.reply(session, 'kickUsage');

            return;
        }

        const target = this.target(session, args[0]);
        const reason = this.reason(session, args);

        if (!target || reason === null) {
            return;
        }

        const other = this.sessions.get(target.id);

        if (other) {
            this.save(other);
        }

        this.reply(session, 'kickDone', { player: target.nickname, reason });
        target.kick(adminText(other?.locale ?? 'en', 'kicked', { reason }));
    }

    private reason(session: Session, args: string[]): string | null {
        const reason = args.slice(1).join(' ').trim() || adminText(session.locale, 'noReason');

        if (reason.length > 160 || Buffer.byteLength(reason) > 300 || /[\r\n\0]/.test(reason)) {
            this.reply(session, 'reasonLong');

            return null;
        }

        return reason;
    }

    private ban(session: Session, args: string[]): void {
        if (!args.length) {
            this.reply(session, 'banUsage');

            return;
        }

        const target = this.player(args[0]);
        const account =
            (target ? this.sessions.get(target.id)?.account : undefined) ??
            this.accounts.find(args[0]);
        const reason = this.reason(session, args);

        if (!account || reason === null) {
            if (!account) {
                this.reply(session, 'accountMissing', { player: args[0] });
            }

            return;
        }

        this.accounts.ban(account.id, reason, session.account!.id);
        this.reply(session, 'banDone', { player: account.username, reason });

        // Include authenticated sessions still waiting for the city to load.
        for (const other of this.sessions.values()) {
            if (other.account?.id === account.id) {
                this.save(other);
                other.player.kick(adminText(other.locale, 'banned', { reason }));
            }
        }
    }

    private unban(session: Session, args: string[]): void {
        if (args.length !== 1) {
            this.reply(session, 'unbanUsage');

            return;
        }

        const account = this.accounts.find(args[0]);

        this.reply(
            session,
            !account
                ? 'accountMissing'
                : this.accounts.unban(account.id)
                  ? 'unbanDone'
                  : 'notBanned',
            { player: args[0] },
        );
    }

    private follow(session: Session, args: string[]): void {
        if (args.length !== 1) {
            this.reply(session, 'followUsage');

            return;
        }

        if (args[0].toLowerCase() === 'off') {
            this.reply(session, session.player.setCameraTarget(null) ? 'followOff' : 'failed');

            return;
        }

        const target = this.target(session, args[0]);

        if (target) {
            if (target.id === session.player.id) {
                this.reply(session, 'selfTarget');

                return;
            }

            this.reply(session, session.player.setCameraTarget(target) ? 'following' : 'failed', {
                player: target.nickname,
            });
        }
    }

    private teleport(session: Session, args: string[]): void {
        if (args.length !== 1) {
            this.reply(session, 'tpUsage');

            return;
        }

        const player = session.player;
        const target = this.target(session, args[0]);

        if (!target) {
            return;
        }

        if (target.id === player.id) {
            this.reply(session, 'selfTarget');

            return;
        }

        if (!player.alive || !player.spawned || !target.alive || !target.spawned) {
            this.reply(session, 'spawnRequired');

            return;
        }

        const health = player.health;
        const inventory = player.getInventory()!;
        const origin = target.getVehicle() ?? target;
        const { x, y, z } = origin.position;
        const q = player.rotation;
        const heading = Math.atan2(2 * (q.w * q.y + q.x * q.z), 1 - 2 * (q.y * q.y + q.x * q.x));

        // The public spawn path invalidates old movement packets; preserve the current loadout and health.
        if (!player.spawn({ x: x + 2, y: y + 0.2, z }, heading)) {
            this.reply(session, 'failed');

            return;
        }

        player.setVirtualWorld(target.virtualWorld);
        player.setHealth(health);
        player.setInventory(inventory.weapons, inventory.selected);
        player.setCameraTarget(null);
        this.save(session);
        this.reply(session, 'teleported', { player: target.nickname });
    }

    private weapon(session: Session, args: string[]): void {
        const id = Number(args[0]);
        const weapon = Number.isInteger(id) ? Weapon.get(id) : null;
        const ammo =
            args[1] === undefined
                ? weapon?.kind === 'firearm'
                    ? weapon.magazine * 4
                    : 1
                : Number(args[1]);

        if (
            !weapon ||
            args.length > 3 ||
            !Number.isInteger(ammo) ||
            !ammo ||
            ammo < 1 ||
            ammo > 65535
        ) {
            this.reply(session, 'weaponUsage');

            return;
        }

        const target = args[2] ? this.target(session, args[2]) : session.player;

        if (!target) {
            return;
        }

        const loaded =
            weapon.kind === 'firearm'
                ? Math.min(ammo, weapon.magazine)
                : weapon.kind === 'melee'
                  ? 1
                  : ammo;
        const reserve = weapon.kind === 'firearm' ? ammo - loaded : 0;

        if (!target.giveWeapon(id, loaded, reserve, true)) {
            this.reply(session, 'failed');

            return;
        }

        this.reply(session, 'weaponGiven', { weapon: id, player: target.nickname });

        if (target.id !== session.player.id) {
            target.sendMessage(
                adminText(this.sessions.get(target.id)?.locale ?? 'en', 'weaponReceived', {
                    weapon: id,
                }),
            );
        }
    }

    handleConsole(command: string, args: string[]): void {
        if (command.toLowerCase() === 'setrole') {
            if (args.length !== 2 || !isRole(args[1])) {
                this.log('roleUsage');

                return;
            }

            const account = this.accounts.find(args[0]);

            if (!account) {
                this.log('accountMissing', { player: args[0] });

                return;
            }

            this.accounts.setRole(account.id, args[1]);

            for (const session of this.sessions.values()) {
                if (session.account?.id === account.id) {
                    session.account.role = args[1];

                    if (!can(args[1], 'player.follow')) {
                        session.player.setCameraTarget(null);
                    }

                    this.reply(session, 'rankChanged', {
                        role: adminText(
                            session.locale,
                            args[1] === 'admin' ? 'rankAdmin' : 'rankUser',
                        ),
                    });
                }
            }

            this.log('roleChanged', { player: account.username, role: args[1] });
        } else if (command.toLowerCase() === 'unban') {
            const account = args.length === 1 ? this.accounts.find(args[0]) : undefined;

            this.log(
                args.length !== 1
                    ? 'unbanUsage'
                    : !account
                      ? 'accountMissing'
                      : this.accounts.unban(account.id)
                        ? 'unbanDone'
                        : 'notBanned',
                { player: args[0] ?? '' },
            );
        }
    }

    release(player: Player): void {
        player.setCameraTarget(null);
        this.retireCars(player.id);
        this.lastCar.delete(player.id);
    }

    private retireCars(owner: number): void {
        for (const car of this.cars.values()) {
            if (car.owner === owner) {
                car.retired = true;
            }
        }
    }

    update(): void {
        if (!this.cars.size) {
            return;
        }

        const vehicles = new Map(World.getVehicles().map((vehicle) => [vehicle.id, vehicle]));

        for (const [id, car] of this.cars) {
            const vehicle = vehicles.get(id);

            if (
                !vehicle ||
                (car.retired && !vehicle.getOccupants().some(Boolean) && vehicle.destroy())
            ) {
                this.cars.delete(id);
            }
        }
    }

    reset(): void {
        for (const session of this.sessions.values()) {
            session.player.setCameraTarget(null);
        }

        for (const vehicle of World.getVehicles()) {
            if (this.cars.has(vehicle.id)) {
                vehicle.destroy();
            }
        }

        this.cars.clear();
        this.lastCar.clear();
    }
}
