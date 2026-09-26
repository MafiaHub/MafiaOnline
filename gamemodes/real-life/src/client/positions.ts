import { EVENT, isRecord } from '../shared/protocol';

(Events as EventBus).on(EVENT.positionCapture, (payload) => {
    if (
        !isRecord(payload) ||
        typeof payload.request !== 'string' ||
        payload.generation !== World.getMissionGeneration() ||
        !LocalPlayer?.spawned ||
        !LocalPlayer.alive ||
        payload.world !== LocalPlayer.virtualWorld
    ) {
        return;
    }

    const vehicle = LocalPlayer.getVehicle();
    const subject = vehicle ?? LocalPlayer;
    const transform = subject.getWorldTransform();

    Events.emitServer(EVENT.positionCaptured, {
        request: payload.request,
        generation: payload.generation,
        world: payload.world,
        vehicleId: vehicle?.id ?? null,
        transform,
    });
});
