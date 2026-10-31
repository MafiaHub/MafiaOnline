// Server admins. An admin is a player record (the one an account owns, see
// profiles.ts and accounts.ts) granted from the server console:
// "fr admin add <player>", "fr admin remove <player>", "fr admin list".
import * as store from './store';
import * as profiles from './profiles';

interface AdminsDocument {
    version: number;
    admins?: string[];
}

let admins = new Set<string>();

function load(): void {
    admins = new Set(store.load<AdminsDocument>('admins')?.admins ?? []);
}

function save(): void {
    store.save('admins', { version: 1, admins: [...admins] } satisfies AdminsDocument);
}

export function isAdmin(player: Player): boolean {
    const key = profiles.keyOf(player);

    return Boolean(key && admins.has(key));
}

/** Grants or takes away admin from a record key. */
export function set(key: string, admin: boolean): void {
    if (admin) {
        admins.add(key);
    } else {
        admins.delete(key);
    }

    save();
}

/** The record keys of every admin. */
export function list(): string[] {
    return [...admins];
}

load();
