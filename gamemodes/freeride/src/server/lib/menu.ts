// Sections of the F2 city menu. Each module provides its own section and the
// menu collects them when a player opens it, and every few seconds while it
// stays open. A costly section names the tabs that show it and is built only
// for them; the client keeps the last copy for the others.
import * as log from './log';

/** Builds a section for the player: anything JSON can carry. */
export type SectionBuilder = (player: Player) => unknown;

interface Provider {
    build: SectionBuilder;
    tabs: readonly string[] | null;
}

const providers = new Map<string, Provider>();

export function provide(
    section: string,
    build: SectionBuilder,
    tabs: readonly string[] | null = null,
): void {
    providers.set(section, { build, tabs });
}

/** Every section for the player; with `tab`, only the ones that tab shows. */
export function collect(player: Player, tab: string | null = null): Record<string, unknown> {
    const sections: Record<string, unknown> = {};

    for (const [section, { build, tabs }] of providers) {
        if (tabs && tab && !tabs.includes(tab)) {
            continue;
        }

        try {
            sections[section] = build(player);
        } catch (caught) {
            log.error(`Menu section ${section} failed: ${log.describe(caught, true)}`);
        }
    }

    return sections;
}
