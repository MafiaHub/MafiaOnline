import { isPoint, isRecord, RESOURCE } from '../shared/protocol';
import {
    arrowRotation,
    housePose,
    HOUSE_PICKUP_MODEL,
    HOUSE_PICKUP_SCALE,
} from './property-visuals';
import { distance, PROPERTY_EVENT, type PropertyMarker } from '../shared/properties';

const events = Events as EventBus;
let markers: PropertyMarker[] = [];
let markerWorld = -1;
const models = new Map<string, { frame: Frame; kind: string }>();
const failed = new Map<string, number>();

function clear(): void {
    for (const { frame } of models.values()) {
        frame.destroy();
    }

    models.clear();
    failed.clear();
    markers = [];
    markerWorld = -1;
}

events.on(PROPERTY_EVENT.markers, (payload) => {
    if (
        !isRecord(payload) ||
        payload.generation !== World.getMissionGeneration() ||
        !Number.isInteger(payload.world) ||
        !Array.isArray(payload.markers) ||
        payload.markers.length > 20
    ) {
        return;
    }

    if (
        !payload.markers.every(
            (m) =>
                isRecord(m) &&
                typeof m.key === 'string' &&
                m.key.length <= 64 &&
                isPoint(m.position) &&
                ['sale', 'entry', 'exit', 'garage'].includes(String(m.kind)) &&
                typeof m.label === 'string' &&
                m.label.length < 200,
        )
    ) {
        return;
    }

    markers = payload.markers as PropertyMarker[];
    markerWorld = payload.world as number;
});

events.on('missionUnload', clear);
events.on('resourceStop', (name) => {
    if (name === RESOURCE) {
        clear();
    }
});

events.on('render', () => {
    const player = LocalPlayer;

    if (!player?.spawned || !player.alive || player.virtualWorld !== markerWorld) {
        for (const { frame } of models.values()) {
            frame.setVisible(false);
        }

        return;
    }

    const now = Date.now();
    const origin = player.getWorldPosition() ?? player.position;
    const active = new Set(markers.map((m) => m.key));

    for (const [key, model] of models) {
        if (!active.has(key)) {
            model.frame.destroy();
            models.delete(key);
            failed.delete(key);
        }
    }

    for (const marker of markers) {
        let model = models.get(marker.key);

        if (model && model.kind !== marker.kind) {
            model.frame.destroy();
            models.delete(marker.key);
            model = undefined;
        }

        if (!model && now >= (failed.get(marker.key) ?? 0)) {
            const frame = Scene.createModelFrame(
                marker.kind === 'sale' ? HOUSE_PICKUP_MODEL : 'sipka.i3d',
            );

            if (frame) {
                frame.setScale(marker.kind === 'sale' ? HOUSE_PICKUP_SCALE : 0.7);

                if (marker.kind !== 'sale' && frame.id !== null) {
                    Scene.playModelAnimation(frame.id, null, true);
                }

                model = { frame, kind: marker.kind };
                models.set(marker.key, model);
            } else {
                failed.set(marker.key, now + 5000);
            }
        }

        if (model) {
            model.frame.setVisible(true);

            if (marker.kind === 'sale') {
                const pose = housePose(marker.position, now);

                model.frame.setWorldPosition(pose.position);
                model.frame.setRotation(pose.rotation);
            } else {
                model.frame.setWorldPosition({ ...marker.position, y: marker.position.y + 1.5 });
                model.frame.setRotation(arrowRotation(marker.position, origin));
            }
        }

        if (distance(marker.position, origin) < 18) {
            Draw.worldText(
                marker.label,
                { ...marker.position, y: marker.position.y + 1.8 },
                17,
                marker.kind === 'sale' ? 0xffffd070 : 0xff80e89b,
            );
        }
    }
});

Key.bind('enter', 'down', () => {
    const player = LocalPlayer;

    if (!player?.spawned || !player.alive || player.virtualWorld !== markerWorld || Chat.isOpen()) {
        return;
    }

    const origin = player.getWorldPosition() ?? player.position;
    const nearest = markers
        .filter((m) => distance(m.position, origin) <= (m.kind === 'garage' ? 5 : 2))
        .sort((a, b) => distance(a.position, origin) - distance(b.position, origin))[0];

    if (nearest) {
        Events.emitServer(PROPERTY_EVENT.use, {
            key: nearest.key,
            world: markerWorld,
            generation: World.getMissionGeneration(),
        });
    }
});
