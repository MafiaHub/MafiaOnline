// A focused CEF catalog surrounds a native LS3D model preview. The page
// supplies its actual CSS rectangle; the preview uses backbuffer pixels. The
// server opens it as the car lot (prices), the player's garage, or the admin
// motor pool; it decides every purchase and delivery.
import { COLORS } from './lib/color';
import { formatMoney } from './lib/format';
import { clamp } from './lib/geometry';
import { Page } from './lib/page';
import { fields, isFiniteNumber } from './lib/payload';
import { announcePicker } from './lib/picker';
import { livePlayer, onLocalDeath } from './lib/player';
import { events, onServer, onStop } from './lib/runtime';

const TIMEOUT_MS = 10_000;
const START_YAW = 0.55;
const DEFAULT_MODEL = 'thunderbird00.i3d';

type Mode = 'admin' | 'dealer' | 'garage';

const TITLES: Record<Mode, { edition: string; title: string }> = {
    admin: { edition: 'Lost Heaven · Motor Pool', title: 'Choose any car' },
    dealer: { edition: 'Lost Heaven · Car Lot', title: 'Buy a car' },
    garage: { edition: 'Lost Heaven · Your Garage', title: 'Bring a car round' },
};

/** A car on the lot (dealer: price, owned) or in the garage (totaled, recover). */
interface CarOffer {
    id: number;
    model: string;
    price?: number;
    owned?: boolean;
    totaled?: boolean;
    recover?: number;
}

interface OpenRequest {
    generation: number;
    mode?: string;
    offers?: CarOffer[];
    /** What bringing a garage car round costs away from the lot. */
    fee?: number;
    /** Free garage places. */
    room?: number;
}

interface ChooseResult {
    id: number;
    ok: boolean;
    message?: string;
}

/** A row of the page's list. */
interface CarChoice {
    id: number;
    name: string;
    model: string;
    note: string;
    action: string;
    disabled?: boolean;
}

/** The preview's rectangle in page pixels, and the page's size. */
interface Viewport {
    x: number;
    y: number;
    width: number;
    height: number;
    pageWidth: number;
    pageHeight: number;
}

const page = new Page(
    'Car picker',
    'car_picker.html',
    { visible: true, focus: true, zIndex: 110 },
    () => sendState(),
);

let generation = 0;
let mode: Mode = 'admin';
let models: CarChoice[] = [];
let selectedId: number | null = null;
let preview: number | null = null;
let viewport: Viewport | null = null;
let yaw = START_YAW;
let zoom = 1;
let pending = false;
let pendingSince = 0;
let message = '';

function isMode(value: unknown): value is Mode {
    return value === 'admin' || value === 'dealer' || value === 'garage';
}

function offerChoice(offer: CarOffer, name: string, request: OpenRequest): CarChoice {
    if (mode === 'dealer') {
        const price = formatMoney(offer.price ?? 0);
        const full = request.room !== undefined && request.room <= 0;

        return {
            id: offer.id,
            name,
            model: offer.model,
            note: offer.owned ? `${price} · already yours` : price,
            action: offer.owned ? 'Already yours' : full ? 'Garage full' : `Buy for ${price}`,
            disabled: offer.owned || full,
        };
    }

    const recover = formatMoney(offer.recover ?? 0);

    return {
        id: offer.id,
        name,
        model: offer.model,
        note: offer.totaled ? `Wrecked · rebuild ${recover}` : 'In your garage',
        action: offer.totaled
            ? `Rebuild for ${recover}`
            : `Bring it round (${formatMoney(request.fee ?? 0)} away from the lot)`,
    };
}

// What the page lists for each mode: the catalog names with the server's offers.
function pickerModels(request: OpenRequest): CarChoice[] {
    const catalog = Scene.getCarCatalog().filter(
        (entry) =>
            Number.isInteger(entry.id) &&
            entry.id > 0 &&
            typeof entry.name === 'string' &&
            typeof entry.model === 'string',
    );

    if (mode === 'admin') {
        return catalog.map((entry) => ({ ...entry, note: entry.model, action: 'Drive this car' }));
    }

    const names = new Map(catalog.map((entry) => [entry.model.toLowerCase(), entry.name]));

    return (Array.isArray(request.offers) ? request.offers : [])
        .filter((offer) => Number.isInteger(offer.id) && typeof offer.model === 'string')
        .map((offer) =>
            offerChoice(offer, names.get(offer.model.toLowerCase()) ?? offer.model, request),
        );
}

function sendState(): void {
    page.emit('car:state', {
        ...TITLES[mode],
        models,
        selectedId,
        previewReady: preview !== null,
        pending,
        message,
    });
}

function closePicker(): void {
    const wasOpen = page.isOpen;

    if (preview !== null) {
        Scene.destroyPreview(preview);
    }

    page.close();
    preview = null;
    viewport = null;
    models = [];
    selectedId = null;
    pending = false;
    message = '';

    if (wasOpen) {
        announcePicker(false);
    }
}

function selectModel(id: unknown): void {
    if (!page.isOpen || pending || typeof id !== 'number' || !Number.isInteger(id)) {
        return;
    }

    const model = models.find((entry) => entry.id === id);

    if (!model || (selectedId === id && preview !== null)) {
        return;
    }

    if (preview !== null) {
        Scene.destroyPreview(preview);
    }

    selectedId = id;
    preview = Scene.createPreview(model.model);
    yaw = START_YAW;
    zoom = 1;
    message =
        preview === null ? 'This model could not be previewed.' : 'Drag to turn · Scroll to zoom';

    sendState();
}

function chooseModel(): void {
    if (!page.isOpen || pending || preview === null || selectedId === null) {
        return;
    }

    if (!livePlayer()) {
        message = 'Enter Free Ride before choosing a car.';
        sendState();

        return;
    }

    const choice = models.find((entry) => entry.id === selectedId);

    if (choice?.disabled) {
        return;
    }

    pending = true;
    pendingSince = Date.now();
    message = mode === 'dealer' ? 'Signing the papers…' : 'Taking the wheel…';
    sendState();
    events.emitServer('fr:car:choose', { generation, id: selectedId, mode });
}

function viewportOf(payload: unknown): Viewport | null {
    const rect = fields(payload);
    const { x, y, width, height, pageWidth, pageHeight } = rect;

    if (
        !isFiniteNumber(x) ||
        !isFiniteNumber(y) ||
        !isFiniteNumber(width) ||
        !isFiniteNumber(height) ||
        !isFiniteNumber(pageWidth) ||
        !isFiniteNumber(pageHeight)
    ) {
        return null;
    }

    if (width <= 0 || height <= 0 || pageWidth <= 0 || pageHeight <= 0) {
        return null;
    }

    return { x, y, width, height, pageWidth, pageHeight };
}

function bindPage(): void {
    page.on('car:select', (choice) => selectModel(fields(choice).id));
    page.on('car:choose', chooseModel);
    page.on('car:close', closePicker);
    page.on('car:viewport', (rect) => {
        viewport = viewportOf(rect) ?? viewport;
    });

    page.on('car:rotate', (drag) => {
        const dx = fields(drag).dx;

        if (preview === null || !isFiniteNumber(dx)) {
            return;
        }

        yaw += clamp(dx, -120, 120) * 0.012;

        if (yaw > Math.PI * 2 || yaw < -Math.PI * 2) {
            yaw %= Math.PI * 2;
        }
    });

    page.on('car:zoom', (wheel) => {
        const delta = fields(wheel).delta;

        if (preview === null || !isFiniteNumber(delta)) {
            return;
        }

        zoom = clamp(zoom - Math.sign(delta) * 0.12, 0.5, 2.5);
    });
}

function openPicker(request: OpenRequest): void {
    if (!World.isReady() || request.generation !== World.getMissionGeneration() || !livePlayer()) {
        return;
    }

    closePicker();
    generation = request.generation;
    mode = isMode(request.mode) ? request.mode : 'admin';
    models = pickerModels(request);

    if (models.length === 0) {
        Hud.showMessage('The stock car catalog is unavailable.', COLORS.message);

        return;
    }

    if (!page.open()) {
        Hud.showMessage('The car picker could not open.', COLORS.message);
        closePicker();

        return;
    }

    announcePicker(true);
    bindPage();
    selectModel(
        models.find((entry) => entry.model.toLowerCase() === DEFAULT_MODEL)?.id ?? models[0].id,
    );
}

onServer<OpenRequest>('fr:car:open', openPicker);

onServer<ChooseResult>('fr:car:result', (result) => {
    if (!page.isOpen || !pending || result.id !== selectedId) {
        return;
    }

    if (result.ok) {
        closePicker();

        return;
    }

    pending = false;
    message =
        typeof result.message === 'string'
            ? result.message
            : 'The car could not be called. Try again.';

    sendState();
});

events.on('missionUnload', closePicker);
onStop(closePicker);
onLocalDeath(closePicker);

events.on('render', () => {
    if (!page.isOpen) {
        return;
    }

    if (!livePlayer() || generation !== World.getMissionGeneration()) {
        closePicker();

        return;
    }

    if (pending && Date.now() - pendingSince > TIMEOUT_MS) {
        pending = false;
        message = 'The city did not answer. Choose again.';
        sendState();
    }

    if (preview === null || !viewport) {
        return;
    }

    const screen = Web.getScreenSize();
    const sx = screen.width / viewport.pageWidth;
    const sy = screen.height / viewport.pageHeight;

    Draw.preview(
        preview,
        Math.round(viewport.x * sx),
        Math.round(viewport.y * sy),
        Math.round(viewport.width * sx),
        Math.round(viewport.height * sy),
        yaw,
        zoom,
    );
});
