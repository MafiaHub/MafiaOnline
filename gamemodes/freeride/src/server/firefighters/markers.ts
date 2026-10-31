// The fires on everyone's HUD: the flames, who is trapped, the arsonist.
import { COLORS, arrow, crewMarkers, type Marker } from '../lib/notify';
import { events } from '../lib/lifecycle';
import { SentCache } from '../lib/players';
import { crews } from './duty';
import { active, burning, incidents, intensityOf } from './model';

// The fire markers last sent to each player (lib/notify.ts crewMarkers).
const sent = new SentCache({ clearOnMissionChange: true });

export function presentIncidents(): void {
    const items: Marker[] = [];

    for (const incident of incidents.values()) {
        const alight = burning(incident);

        items.push({
            id: `fire${incident.id}`,
            position: incident.position,
            label: alight
                ? `${incident.label} · ${Math.round(intensityOf(incident) * 10) * 10}%`
                : 'FIRE OUT',
            color: alight ? COLORS.red : COLORS.muted,
            beacon: alight,
            range: 1200,
            near: 120,
            map: alight
                ? { icon: 'diamond', label: incident.label.split(' · ')[0], flash: true }
                : false,
        });

        for (const civilian of incident.civilians) {
            if (civilian.state === 'trapped' && civilian.npc) {
                items.push({
                    id: `trapped${incident.id}_${civilian.index}`,
                    position: civilian.npc.position,
                    label: 'TRAPPED',
                    color: COLORS.gold,
                    range: incident.bar || incident.building ? 40 : 150,
                    map: false,
                });
            }
        }

        // The way in for the crew: G at the arrow over the street door.
        if (incident.building && active(incident)) {
            items.push(
                arrow(
                    `door${incident.id}`,
                    incident.building.door,
                    'FIRE · GO IN WITH G',
                    COLORS.red,
                    60,
                    {
                        near: 20,
                        map: false,
                    },
                ),
            );
        }

        const arsonist = incident.arsonist;

        if (arsonist?.state === 'fleeing' && arsonist.npc) {
            const { x, y, z } = arsonist.npc.position;

            items.push({
                id: `arson${incident.id}`,
                position: { x: Math.round(x), y: Math.round(y), z: Math.round(z) },
                label: 'ARSONIST',
                color: COLORS.red,
                range: 400,
                near: 60,
                map: { icon: 'triangle', label: 'Arsonist' },
            });
        }
    }

    // Everyone sees the flames; only the crew on duty gets them on the map.
    crewMarkers('fires', sent, items, (player) => crews.has(player));
}

// The fires burning before this player came go out on the next tick.
events.on('playerSpawn', (player) => sent.delete(player.id));
