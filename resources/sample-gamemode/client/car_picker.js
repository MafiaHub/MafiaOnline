// A focused CEF catalog surrounds a native LS3D model preview. The page
// supplies its actual CSS rectangle; the preview uses backbuffer pixels.
const CAR_PICKER_PAGE = "fw://resources/mafia1online-sample/client/car_picker.html";
const CAR_PICKER_RESOURCE = "mafia1online-sample";
const CAR_PICKER_TIMEOUT_MS = 10_000;

const carPicker = {
    view: null,
    pageReady: false,
    generation: 0,
    models: [],
    selectedId: null,
    preview: null,
    viewport: null,
    yaw: 0.55,
    zoom: 1,
    pending: false,
    pendingSince: 0,
    message: "",
};

function carPickerState() {
    return {
        models: carPicker.models,
        selectedId: carPicker.selectedId,
        previewReady: carPicker.preview !== null,
        pending: carPicker.pending,
        message: carPicker.message,
    };
}

function sendCarPickerState() {
    if (carPicker.view !== null && carPicker.pageReady) Web.emit(carPicker.view, "car:state", carPickerState());
}

function closeCarPicker() {
    const wasOpen = carPicker.view !== null;
    if (carPicker.preview !== null) Scene.destroyPreview(carPicker.preview);
    if (carPicker.view !== null) Web.destroyView(carPicker.view);
    carPicker.view = null;
    carPicker.preview = null;
    carPicker.viewport = null;
    carPicker.pageReady = false;
    carPicker.models = [];
    carPicker.selectedId = null;
    carPicker.pending = false;
    carPicker.message = "";
    if (wasOpen) Events.emit("sample:car:pickerClosed");
}

function selectCarModel(id) {
    if (carPicker.view === null || carPicker.pending || !Number.isInteger(id)) return;
    const model = carPicker.models.find((entry) => entry.id === id);
    if (!model || (carPicker.selectedId === id && carPicker.preview !== null)) return;
    if (carPicker.preview !== null) Scene.destroyPreview(carPicker.preview);
    carPicker.selectedId = id;
    carPicker.preview = Scene.createPreview(model.model);
    carPicker.yaw = 0.55;
    carPicker.zoom = 1;
    carPicker.message = carPicker.preview === null ? "This model could not be previewed." : "Drag to turn · Scroll to zoom";
    sendCarPickerState();
}

function chooseCarModel() {
    if (carPicker.view === null || carPicker.pending || carPicker.preview === null || carPicker.selectedId === null) return;
    if (!LocalPlayer?.spawned || !LocalPlayer.alive) {
        carPicker.message = "Enter Free Ride before choosing a car.";
        sendCarPickerState();
        return;
    }
    carPicker.pending = true;
    carPicker.pendingSince = Date.now();
    carPicker.message = "Taking the wheel…";
    sendCarPickerState();
    Events.emitServer("sample:car:choose", {generation: carPicker.generation, id: carPicker.selectedId});
}

function openCarPicker(request) {
    if (!World.isReady() || request?.generation !== World.getMissionGeneration() || !LocalPlayer?.spawned || !LocalPlayer.alive) return;
    closeCarPicker();
    carPicker.generation = request.generation;
    carPicker.models = Scene.getCarCatalog().filter((entry) => Number.isInteger(entry.id) && entry.id > 0 && typeof entry.name === "string" && typeof entry.model === "string");
    if (carPicker.models.length === 0) {
        Hud.showMessage("The stock car catalog is unavailable.", 0xe8c57d);
        return;
    }
    try {
        carPicker.view = Web.createView(CAR_PICKER_PAGE, {visible: true, focus: true, zIndex: 110});
    } catch (error) {
        console.warn(`[mafia1online-sample] Car picker could not open: ${error}`);
        Hud.showMessage("The car picker could not open.", 0xe8c57d);
        closeCarPicker();
        return;
    }
    const view = carPicker.view;
    Events.emit("sample:car:pickerOpened");
    Web.on(view, "car:select", (choice) => { if (view === carPicker.view) selectCarModel(choice?.id); });
    Web.on(view, "car:choose", () => { if (view === carPicker.view) chooseCarModel(); });
    Web.on(view, "car:close", () => { if (view === carPicker.view) closeCarPicker(); });
    Web.on(view, "car:viewport", (rect) => {
        if (view !== carPicker.view || !rect || ![rect.x, rect.y, rect.width, rect.height, rect.pageWidth, rect.pageHeight].every(Number.isFinite)) return;
        if (rect.width <= 0 || rect.height <= 0 || rect.pageWidth <= 0 || rect.pageHeight <= 0) return;
        carPicker.viewport = rect;
    });
    Web.on(view, "car:rotate", (drag) => {
        if (view !== carPicker.view || carPicker.preview === null || !Number.isFinite(drag?.dx)) return;
        carPicker.yaw += Math.max(-120, Math.min(120, drag.dx)) * 0.012;
        if (carPicker.yaw > Math.PI * 2 || carPicker.yaw < -Math.PI * 2) carPicker.yaw %= Math.PI * 2;
    });
    Web.on(view, "car:zoom", (wheel) => {
        if (view !== carPicker.view || carPicker.preview === null || !Number.isFinite(wheel?.delta)) return;
        carPicker.zoom = Math.max(0.5, Math.min(2.5, carPicker.zoom - Math.sign(wheel.delta) * 0.12));
    });
    selectCarModel(carPicker.models.find((entry) => entry.model.toLowerCase() === "thunderbird00.i3d")?.id ?? carPicker.models[0].id);
}

Events.on("sample:car:open", openCarPicker);
Events.on("sample:car:result", (result) => {
    if (carPicker.view === null || !carPicker.pending || result?.id !== carPicker.selectedId) return;
    if (result.ok) {
        closeCarPicker();
        return;
    }
    carPicker.pending = false;
    carPicker.message = typeof result.message === "string" ? result.message : "The car could not be called. Try again.";
    sendCarPickerState();
});
Events.on("browserDocumentReady", ({viewId}) => {
    if (viewId !== carPicker.view) return;
    carPicker.pageReady = true;
    sendCarPickerState();
});
Events.on("missionUnload", closeCarPicker);
Events.on("resourceStop", (name) => { if (name === CAR_PICKER_RESOURCE) closeCarPicker(); });
Events.on("playerDeath", (player) => { if (player.isLocal) closeCarPicker(); });
Events.on("render", () => {
    if (carPicker.view === null) return;
    if (!LocalPlayer?.spawned || !LocalPlayer.alive || carPicker.generation !== World.getMissionGeneration()) {
        closeCarPicker();
        return;
    }
    if (carPicker.pending && Date.now() - carPicker.pendingSince > CAR_PICKER_TIMEOUT_MS) {
        carPicker.pending = false;
        carPicker.message = "The city did not answer. Choose again.";
        sendCarPickerState();
    }
    const rect = carPicker.viewport;
    if (carPicker.preview === null || !rect) return;
    const screen = Web.getScreenSize();
    const sx = screen.width / rect.pageWidth;
    const sy = screen.height / rect.pageHeight;
    Draw.preview(carPicker.preview, Math.round(rect.x * sx), Math.round(rect.y * sy),
        Math.round(rect.width * sx), Math.round(rect.height * sy), carPicker.yaw, carPicker.zoom);
});
