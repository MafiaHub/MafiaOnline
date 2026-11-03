// What the city pays a medic.
import * as profiles from '../lib/profiles';
import type { Medic } from './duty';

// A ride to the hospital pays its base, PER_METRE of the straight distance
// and RIDE_TIP inside the time: a serious call about $300-460, a woman in
// labour $510-700 with the smooth-ride tip, a heart attack $520-690. A
// pile-up's three patients (critical rides in, the serious and the minor
// are patched up where they sit, no quick tip) with the triage pay and the
// bonus come to about $530-700 for one medic.
export const PAY = {
    minor: 140,
    serious: 220,
    revive: 200,
    labour: 330,
    cardiac: 400,
    critical: 160,
    hurt: 100,
    scraped: 70,
} as const;
// A pile-up: treating the critical patient first, and once all three are
// seen to with nobody lost, split between the medics who treated them.
export const TRIAGE_PAY = 40;
export const PILEUP_PAY = 100;
export const PER_METRE = 0.1;
// Treated where they lie, quickly: 40% more. Brought in inside the time: 25%.
export const TIP = 0.4;
export const RIDE_TIP = 0.25;
// A woman in labour: the tip window is shorter, a smooth ride tips more.
export const SMOOTH_TIP = 0.2;
const XP = 50;

/** Pays for a patient seen to: the money, the count and the experience. */
export function payMedic(player: Player, medic: Medic, amount: number, reason: string): void {
    player.giveMoney(amount);
    medic.earned += amount;
    medic.patients++;
    profiles.bump(player, 'patients');
    profiles.addXp(player, XP, reason);
}

/** Pays on top of a patient: triage, a pile-up's share. */
export function payExtra(player: Player, medic: Medic, amount: number): void {
    player.giveMoney(amount);
    medic.earned += amount;
}
