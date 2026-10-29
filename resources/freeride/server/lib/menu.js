// Sections of the F2 city menu. Each module provides its own section and the
// menu collects them when a player opens it, and every few seconds while it
// stays open. A costly section names the tabs that show it and is built only
// for them; the client keeps the last copy for the others.
const providers = new Map();

function provide(section, handler, tabs = null) {
    providers.set(section, {handler, tabs});
}

function collect(player, tab = null) {
    const sections = {};
    for (const [section, {handler, tabs}] of providers) {
        if (tabs && tab && !tabs.includes(tab)) continue;
        try {
            sections[section] = handler(player);
        } catch (error) {
            console.error(`[freeride] Menu section ${section} failed: ${error?.stack ?? error}`);
        }
    }
    return sections;
}

module.exports = {provide, collect};
