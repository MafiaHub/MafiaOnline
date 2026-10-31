// One command table for chat and the F2 city menu. A menu button sends the
// same text a player could type, so both paths share every server check.
import * as log from './log';
import * as profiles from './profiles';
import { toast } from './notify';
import { inFreeRide } from './util';
import { events } from './lifecycle';

const CATEGORY_ORDER = [
    'City',
    'Cars',
    'Races',
    'Combat',
    'Crew',
    'Social',
    'Showcase',
    'Dev',
] as const;

export type Category = (typeof CATEGORY_ORDER)[number];

export interface CommandSpec {
    name: string;
    aliases?: readonly string[];
    /** How to type it; "/name" when omitted. */
    usage?: string;
    help?: string;
    /** The city menu's section; City when omitted. */
    category?: Category;
    /** Left out of /help and the menu. */
    hidden?: boolean;
}

export type CommandHandler = (player: Player, args: string[]) => void;

/** A command as /help and the city menu list it. */
export interface CommandInfo {
    name: string;
    usage: string;
    help: string;
    category: Category;
}

interface Command extends CommandInfo {
    hidden: boolean;
    handler: CommandHandler;
}

const commands = new Map<string, Command>();
const aliases = new Map<string, string>();

export function register(spec: CommandSpec, handler: CommandHandler): void {
    commands.set(spec.name, {
        name: spec.name,
        usage: spec.usage ?? `/${spec.name}`,
        help: spec.help ?? '',
        category: spec.category ?? 'City',
        hidden: Boolean(spec.hidden),
        handler,
    });

    for (const alias of spec.aliases ?? []) {
        aliases.set(alias, spec.name);
    }
}

function resolve(name: string): Command | undefined {
    const key = name.toLowerCase();
    const alias = aliases.get(key);

    return commands.get(key) ?? (alias === undefined ? undefined : commands.get(alias));
}

export function run(player: Player, command: string, args: readonly unknown[]): void {
    // On a Free Ride map a player's record is attached at login; nothing
    // runs before it.
    if (inFreeRide() && !profiles.get(player)) {
        player.sendMessage('Log in first.', 0xd0574a);

        return;
    }

    const entry = resolve(command);

    if (!entry) {
        toast(player, `Unknown command /${command}. Press F2 for the city menu or type /help.`, {
            tone: 'bad',
        });

        return;
    }

    try {
        entry.handler(
            player,
            args.map((arg) => String(arg)),
        );
    } catch (caught) {
        log.error(`/${entry.name} failed for ${player.nickname}: ${log.describe(caught, true)}`);
        toast(player, 'That did not work. The city is looking into it.', { tone: 'bad' });
    }
}

/** Splits a menu command line the way chat does. */
export function runText(player: Player, text: unknown): void {
    if (typeof text !== 'string' || !text.startsWith('/') || text.length > 160) {
        return;
    }

    const [command, ...args] = text.slice(1).trim().split(/\s+/);

    if (command) {
        run(player, command, args);
    }
}

export function list({ includeHidden = false } = {}): CommandInfo[] {
    return [...commands.values()]
        .filter((entry) => includeHidden || !entry.hidden)
        .sort(
            (a, b) =>
                CATEGORY_ORDER.indexOf(a.category) - CATEGORY_ORDER.indexOf(b.category) ||
                a.name.localeCompare(b.name),
        )
        .map(({ name, usage, help, category }) => ({ name, usage, help, category }));
}

events.on('playerCommand', (player, command, args) => run(player, command, args));
