// /rum and the distillery's section of the F2 city menu (the Jobs tab).
import * as commands from '../lib/commands';
import * as menu from '../lib/menu';
import * as profiles from '../lib/profiles';
import { toast } from '../lib/notify';
import { OUTLETS } from './cargo';
import { bribe, heatWord } from './heat';
import { available, opKeyOf, opOf, QUEST, QUESTS, stageOf } from './operations';
import { acceptWedding, weddingOf } from './orders';
import { guide, hide, isHidden, nextStep } from './presentation';
import { revenueCall } from './raids';

commands.register(
    {
        name: 'rum',
        aliases: ['distillery', 'still'],
        usage: '/rum [bribe|wedding|hide]',
        help: "The hidden rum distillery: show the way, bribe the revenue men at Salieri's, take a wedding order or put it aside.",
        category: 'City',
    },
    (player, args) => {
        if (!available()) {
            toast(player, 'The rum distillery is on Lost Heaven Extended (/map).', { tone: 'bad' });

            return;
        }

        const sub = args[0]?.toLowerCase();

        if (sub === 'hide') {
            hide(player);
        } else if (sub === 'bribe') {
            bribe(player);
        } else if (sub === 'wedding' || sub === 'accept') {
            acceptWedding(player);
        } else {
            guide(player);
        }
    },
);

menu.provide('rum', (player) => {
    if (!available() || !profiles.keyOf(player)) {
        return { available: false };
    }

    const stage = stageOf(player);
    const op = opOf(player);
    const opKey = opKeyOf(player);

    return {
        available: true,
        stage,
        quests: QUESTS.length,
        quest: QUESTS[Math.min(stage, QUESTS.length - 1)].title,
        step: nextStep(player, stage, op),
        unlocked: Boolean(op?.unlocked),
        molasses: op?.molasses ?? 0,
        casks: op?.casks.length ?? 0,
        ready: op ? op.casks.filter((cask) => cask.left <= 0).length : 0,
        cases: op?.cases.n ?? 0,
        heat: Math.round(op?.heat ?? 0),
        heatWord: heatWord(op?.heat ?? 0),
        wedding: weddingOf(opKey)?.state ?? null,
        docks: stage >= QUEST.DONE,
        hidden: isHidden(player),
        raid: revenueCall(opKey) !== null,
        prices: Object.fromEntries(OUTLETS.map((outlet) => [outlet.id, outlet.price])),
    };
});
