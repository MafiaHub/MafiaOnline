// Sections of the F1 city menu. Each module provides its own section and the
// menu collects them when a player opens it.
const providers = new Map();

function provide(section, handler) {
    providers.set(section, handler);
}

function collect(player) {
    const sections = {};
    for (const [section, handler] of providers) {
        try {
            sections[section] = handler(player);
        } catch (error) {
            console.error(`[freeride] Menu section ${section} failed: ${error?.stack ?? error}`);
        }
    }
    return sections;
}

module.exports = {provide, collect};
