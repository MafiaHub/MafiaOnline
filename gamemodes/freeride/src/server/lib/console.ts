// Free Ride's server console commands ("fr", "account", "traffic"). The
// console hands every line no built-in command handles to every resource;
// each command here is a word followed by an action, and the modules that
// own an action register it. A line with no action, an unknown one or one its
// action turns down (by returning false) runs the command's fallback, which
// usually prints the usage.
import { events } from './lifecycle';

/** Runs an action with the words after it; false runs the command's fallback instead. */
export type ConsoleAction = (args: string[]) => boolean | void | Promise<void>;

/** Runs with every word after the command. */
export type ConsoleFallback = (args: string[]) => void;

interface ConsoleCommand {
    actions: Map<string, ConsoleAction>;
    fallback: ConsoleFallback;
}

const commands = new Map<string, ConsoleCommand>();

function entry(name: string): ConsoleCommand {
    let command = commands.get(name);

    if (!command) {
        command = { actions: new Map(), fallback: () => {} };
        commands.set(name, command);
    }

    return command;
}

/** Declares a console command and what it does without a known action. */
export function command(name: string, fallback: ConsoleFallback): void {
    entry(name).fallback = fallback;
}

/** Adds an action to a console command, declared here or in another module. */
export function action(command: string, name: string, handler: ConsoleAction): void {
    entry(command).actions.set(name, handler);
}

events.on('consoleCommand', (name, args) => {
    const command = commands.get(name);

    if (!command) {
        return undefined;
    }

    const [first, ...rest] = args;
    const handler = first === undefined ? undefined : command.actions.get(first);
    const result = handler ? handler(rest) : false;

    if (result === false) {
        command.fallback(args);
    }

    // An asynchronous action's failure reaches the framework, as before.
    return result instanceof Promise ? result : undefined;
});
