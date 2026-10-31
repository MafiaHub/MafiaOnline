// Built from gamemodes/freeride by scripts/build.mjs. Do not edit: change the source and rebuild.
"use strict";
(() => {
  // src/client/lib/runtime.ts
  var events = Events;
  var RESOURCE = "freeride";
  var sink = console;
  var log = {
    info: (message2) => sink.log(`[freeride] ${message2}`),
    warn: (message2) => sink.warn(`[freeride] ${message2}`),
    error: (message2) => sink.error(`[freeride] ${message2}`)
  };
  function onStart(handler) {
    events.on("resourceStart", (name) => {
      if (name === RESOURCE) {
        handler();
      }
    });
  }
  function onStop(handler) {
    events.on("resourceStop", (name) => {
      if (name === RESOURCE) {
        handler();
      }
    });
  }
  function onServer(name, handler) {
    events.on(name, (data) => {
      if (typeof data === "object" && data !== null) {
        handler(data);
      }
    });
  }

  // src/client/lib/page.ts
  var Page = class {
    /**
     * @param name What the log calls it when it cannot open.
     * @param file The page under the resource's client folder.
     * @param onReady Runs each time a newly opened page's document is ready.
     */
    constructor(name, file, options, onReady = () => {
    }) {
      this.name = name;
      this.file = file;
      this.options = options;
      events.on("browserDocumentReady", ({ viewId }) => {
        if (this.view === null || viewId !== this.view) {
          return;
        }
        this.ready = true;
        onReady();
      });
    }
    name;
    file;
    options;
    view = null;
    ready = false;
    get isOpen() {
      return this.view !== null;
    }
    get isReady() {
      return this.view !== null && this.ready;
    }
    /** Opens the view unless it is open; false (and logged) when it could not. */
    open() {
      if (this.view !== null) {
        return true;
      }
      try {
        this.view = Web.createView(
          `fw://resources/${RESOURCE}/client/${this.file}`,
          this.options
        );
      } catch (error) {
        log.warn(`${this.name} could not open: ${String(error)}`);
        return false;
      }
      this.ready = false;
      return true;
    }
    /** Handles a page event of the view open now. */
    on(name, handler) {
      const view = this.view;
      if (view === null) {
        return;
      }
      Web.on(view, name, (payload) => {
        if (view === this.view) {
          handler(payload);
        }
      });
    }
    /** Sends to the page once it is ready; false (and dropped) before. */
    emit(name, payload) {
      if (this.view === null || !this.ready) {
        return false;
      }
      return payload === void 0 ? Web.emit(this.view, name) : Web.emit(this.view, name, payload);
    }
    focus(focused) {
      if (this.view !== null) {
        Web.focusView(this.view, focused);
      }
    }
    close() {
      if (this.view !== null) {
        Web.destroyView(this.view);
      }
      this.view = null;
      this.ready = false;
    }
  };

  // src/client/lib/color.ts
  var PARCHMENT = 15985110;
  function rgb(hex) {
    return typeof hex === "string" && /^#[0-9a-f]{6}$/i.test(hex) ? parseInt(hex.slice(1), 16) : PARCHMENT;
  }
  function withAlpha(color, alpha = 1) {
    return Math.round(alpha * 255) * 16777216 + color;
  }
  function argb(hex, alpha = 1) {
    return withAlpha(rgb(hex), alpha);
  }
  var COLORS = {
    /** Labels and key prompts. */
    gold: 4293379197,
    text: 4294305483,
    muted: 4290161297,
    /** The rules across a counter card. */
    rule: 4288444227,
    panel: 3859944729,
    /** Hud.showMessage takes no alpha. */
    message: 15254909
  };

  // src/client/lib/panels.ts
  function keyHint(key) {
    return `[${Settings.getKeyLabel(key).toUpperCase()}]`;
  }
  var NEXT_KEY = "n";
  var BUY_KEY = "b";
  function bindCounterKeys(buy, next) {
    Settings.describeKey(BUY_KEY, "Buy or accept at a shop or bar counter");
    Settings.describeKey(NEXT_KEY, "Next offer at a shop or bar counter");
    Key.bind(BUY_KEY, buy);
    Key.bind(NEXT_KEY, next);
  }
  var counters = /* @__PURE__ */ new Map();
  var sink2 = null;
  var shown = "";
  function setCounterSink(next) {
    sink2 = next;
    shown = "";
  }
  function showCounter(source, card) {
    counters.set(source, card && { ...card, nextKey: keyHint(NEXT_KEY), buyKey: keyHint(BUY_KEY) });
    const current = [...counters.values()].find((entry) => entry !== null) ?? null;
    const key = current ? JSON.stringify(current) : "";
    if (key !== shown) {
      shown = key;
      sink2?.(current);
    }
  }
  var PROMPT_FILL = 3490845977;
  var PROMPT_EDGE = 1894237309;
  function drawPrompt(text2, { rise, width, height = 36, accent = false }) {
    const screen = Web.getScreenSize();
    const center = screen.width / 2;
    const left = center - width / 2;
    const top = screen.height - rise;
    Draw.rect(left, top, width, height, PROMPT_FILL);
    if (accent) {
      Draw.rect(left, top, 4, height, COLORS.gold);
      Draw.rect(left + 4, top, width - 4, 1, PROMPT_EDGE);
    }
    Draw.text(text2, accent ? center + 2 : center, top + 24, 15, COLORS.gold, 3);
    return { center, top };
  }

  // src/client/lib/payload.ts
  function fields(payload) {
    return typeof payload === "object" && payload !== null ? payload : {};
  }
  function isFiniteNumber(value) {
    return typeof value === "number" && Number.isFinite(value);
  }

  // src/client/lib/picker.ts
  var PICKER_EVENT = "fr:car:picker";
  function announcePicker(open) {
    void events.emitLocal(PICKER_EVENT, { open });
  }
  function onPicker(handler) {
    events.onLocal(PICKER_EVENT, (data) => handler(Boolean(fields(data).open)));
  }

  // src/client/lib/timers.ts
  var Timers = class {
    timeouts = [];
    intervals = [];
    after(ms, run) {
      this.timeouts.push(setTimeout(run, ms));
    }
    every(ms, run) {
      this.intervals.push(setInterval(run, ms));
    }
    clear() {
      for (const handle of this.timeouts) {
        clearTimeout(handle);
      }
      for (const handle of this.intervals) {
        clearInterval(handle);
      }
      this.timeouts.length = 0;
      this.intervals.length = 0;
    }
  };

  // src/client/hud.ts
  var MENU_REFRESH_MS = 3e3;
  var COMPASS_CHECK_MS = 500;
  var QUEUE_LIMIT = 60;
  var HELP_KEYS = ["f2", "f9", "b", "n", "g", "u"];
  var page = new Page("HUD view", "hud.html", { visible: true, focus: false, zIndex: 70 }, () => {
    events.emitServer("fr:hud:ready", {});
    const queued = queue;
    queue = [];
    for (const [name, payload] of queued) {
      page.emit(name, payload ?? {});
    }
    page.emit("counter", counter ?? { hidden: true });
    lastPlayerState = "";
  });
  var queue = [];
  var counter = null;
  var menuOpen = false;
  var menuTab = "city";
  var menuRefresh = new Timers();
  var catalogNames = null;
  var menuData = {};
  var menuPosted = "";
  var pickerOpen = false;
  var lastPlayerState = "";
  var compass = /* @__PURE__ */ new Map();
  var compassKey = null;
  var compassCheckedAt = 0;
  function post(name, payload) {
    if (!page.isReady) {
      queue.push([name, payload]);
      if (queue.length > QUEUE_LIMIT) {
        queue.shift();
      }
      return;
    }
    page.emit(name, payload ?? {});
  }
  setCounterSink((card) => {
    counter = card;
    if (page.isReady) {
      page.emit("counter", card ?? { hidden: true });
    }
  });
  function openView() {
    if (page.isOpen || !page.open()) {
      return;
    }
    page.on("hud:command", (request) => {
      const text2 = fields(request).text;
      if (typeof text2 !== "string") {
        return;
      }
      events.emitServer("fr:menu:command", { text: text2 });
      setTimeout(requestMenu, 350);
    });
    page.on("hud:tab", (request) => {
      const tab = fields(request).tab;
      menuTab = typeof tab === "string" ? tab : "city";
      requestMenu();
    });
    page.on("hud:close", closeMenu);
    page.on("hud:settings", () => {
      closeMenu();
      Settings.open();
    });
    page.on("hud:car", () => {
      closeMenu();
      events.emitServer("fr:menu:command", { text: "/garage" });
    });
  }
  function closeView() {
    closeMenu();
    page.close();
    queue = [];
  }
  function requestMenu() {
    if (menuOpen) {
      events.emitServer("fr:menu:request", { tab: menuTab });
    }
  }
  function openMenu(tab) {
    if (!LocalPlayer?.spawned || pickerOpen) {
      return;
    }
    openView();
    if (tab) {
      menuTab = tab;
    }
    menuOpen = true;
    menuPosted = "";
    const keys = {};
    for (const key of HELP_KEYS) {
      keys[key] = Settings.getKeyLabel(key);
    }
    post("menu:open", { tab: menuTab, keys });
    page.focus(true);
    requestMenu();
    menuRefresh.clear();
    menuRefresh.every(MENU_REFRESH_MS, requestMenu);
    Sound.play("00_kompas.wav", { volume: 0.35 });
  }
  function closeMenu() {
    if (!menuOpen) {
      return;
    }
    menuOpen = false;
    menuRefresh.clear();
    post("menu:close");
    page.focus(false);
  }
  function resolveTarget(target) {
    if (target.position) {
      return target.position;
    }
    if (typeof target.playerId === "number") {
      return World.getPlayers().find((player) => player.id === target.playerId) ?? null;
    }
    if (typeof target.vehicleId === "number") {
      return World.getVehicles().find((vehicle) => vehicle.id === target.vehicleId) ?? null;
    }
    return null;
  }
  function updateCompass(force) {
    const now = Date.now();
    if (!force && now - compassCheckedAt < COMPASS_CHECK_MS) {
      return;
    }
    compassCheckedAt = now;
    let best = null;
    for (const [source, entry] of compass) {
      const resolved = resolveTarget(entry.target);
      if (resolved && (!best || entry.priority > best.priority)) {
        best = {
          priority: entry.priority,
          resolved,
          key: `${source}:${JSON.stringify(entry.target)}`
        };
      }
    }
    const key = best?.key ?? null;
    if (key === compassKey) {
      return;
    }
    compassKey = key;
    if (best) {
      Hud.setCompassTarget(best.resolved);
    } else {
      Hud.clearCompassTarget();
    }
  }
  function carNames() {
    if (!catalogNames?.size) {
      catalogNames = new Map(
        Scene.getCarCatalog().map((entry) => [entry.model.toLowerCase(), entry.name])
      );
    }
    return catalogNames;
  }
  function relay(serverEvent, pageEvent = serverEvent.slice(3)) {
    events.on(serverEvent, (data) => post(pageEvent, data));
  }
  relay("fr:toast");
  relay("fr:feed");
  relay("fr:card");
  relay("fr:xp");
  relay("fr:voice");
  relay("fr:race:state", "race");
  relay("fr:profile");
  onServer("fr:levelup", (data) => {
    post("levelup", data);
    Sound.play("beep50.wav", { volume: 0.6 });
    Hud.announce(data.promoted ? data.rank.toUpperCase() : `LEVEL ${data.level}`, 3);
  });
  onServer("fr:menu:open", (request) => openMenu(request.tab));
  onServer("fr:menu:data", (data) => {
    const names = carNames();
    for (const entry of data.export?.models ?? []) {
      entry.name = names.get(entry.model) ?? entry.model.replace(/00\.i3d$/i, "");
    }
    menuData = { ...menuData, ...data };
    const posted = JSON.stringify(menuData);
    if (posted === menuPosted) {
      return;
    }
    menuPosted = posted;
    post("menu:data", menuData);
  });
  onServer("fr:compass", (request) => {
    if (typeof request.source !== "string") {
      return;
    }
    if (request.target) {
      compass.set(request.source, { priority: request.priority ?? 0, target: request.target });
    } else {
      compass.delete(request.source);
    }
    updateCompass(true);
  });
  function postVoice(talking) {
    if (LocalPlayer) {
      post("voice", { id: LocalPlayer.id, name: LocalPlayer.nickname, talking });
    }
  }
  events.on("voiceStart", () => postVoice(true));
  events.on("voiceStop", () => postVoice(false));
  onPicker((open) => {
    pickerOpen = open;
    if (pickerOpen) {
      closeMenu();
    }
    post("visibility", { hidden: pickerOpen });
  });
  Settings.describeKey("f2", "City menu");
  Key.bind("f2", () => {
    if (menuOpen) {
      closeMenu();
    } else {
      openMenu();
    }
  });
  events.on("missionReady", () => {
    openView();
    compassKey = null;
    updateCompass(true);
  });
  events.on("missionUnload", () => {
    closeMenu();
    menuData = {};
    compass.clear();
    compassKey = null;
    post("reset");
  });
  onStart(() => {
    if (World.isReady()) {
      openView();
    }
  });
  onStop(closeView);
  events.on("render", () => {
    if (!page.isOpen) {
      return;
    }
    const player = LocalPlayer;
    const state = `${Boolean(player?.spawned)}:${Boolean(player?.alive)}`;
    if (state !== lastPlayerState && page.isReady) {
      lastPlayerState = state;
      post("player", {
        spawned: Boolean(player?.spawned),
        alive: Boolean(player?.alive),
        id: player?.id ?? null
      });
      if (!player?.alive) {
        closeMenu();
      }
    }
    updateCompass(false);
  });

  // src/client/lib/format.ts
  function formatMoney(amount) {
    return `$${Math.round(amount).toLocaleString("en-US")}`;
  }

  // src/client/lib/geometry.ts
  function distance(a, b) {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
  }
  function flatDistance(a, b) {
    return Math.hypot(a.x - b.x, a.z - b.z);
  }
  function plain(point) {
    return { x: point.x, y: point.y, z: point.z };
  }
  function toward(from, to) {
    return { x: to.x - from.x, y: to.y - from.y, z: to.z - from.z };
  }
  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }

  // src/client/markers.ts
  var BEACON_HEIGHT = 40;
  var BEACON_RANGE = 3e3;
  var LABEL_NEAR = 60;
  var PRIMARY_LABEL_NEAR = 250;
  var MAP_FROM_RANGE = 150;
  var BOUNTY_RANGE = 350;
  var MODEL_RETRY_MS = 2e3;
  var BLIP_SYNC_MS = 500;
  var RING_SEGMENTS = 32;
  var FAR_RING_SEGMENTS = 12;
  var FAR_RING_DISTANCE = 50;
  var LABEL_SIGHT_MS = 150;
  var BOUNTY_REFRESH_MS = 250;
  var HINT_NEAR = 15;
  var ARROW_MODEL = "sipka.i3d";
  var FALLBACK_MODEL = "9money.i3d";
  var groups = /* @__PURE__ */ new Map();
  var models = /* @__PURE__ */ new Map();
  var blips = /* @__PURE__ */ new Map();
  var blipsSyncedAt = 0;
  var frameVehicles = null;
  var labelSight = /* @__PURE__ */ new Map();
  var bounties = [];
  var bountiesAt = 0;
  function itemKey(group, item) {
    return `${group}/${item.id}`;
  }
  function streamedVehicle(item) {
    if (typeof item.vehicleId !== "number") {
      return null;
    }
    frameVehicles ??= new Map(World.getVehicles().map((vehicle) => [vehicle.id, vehicle]));
    return frameVehicles.get(item.vehicleId) ?? null;
  }
  function itemPosition(item) {
    return streamedVehicle(item)?.getWorldTransform()?.position ?? item.position ?? null;
  }
  function screenPoint(position) {
    const point = Scene.projectWorld(position, false);
    return point && point.x !== null && point.y !== null ? { x: point.x, y: point.y } : null;
  }
  function releaseModels(group) {
    for (const [key, record] of models) {
      if (group && !key.startsWith(`${group}/`)) {
        continue;
      }
      record.frame?.destroy();
      models.delete(key);
    }
  }
  function drawBeacon(base, color, distance2) {
    const bottom = screenPoint(base);
    const top = screenPoint({ x: base.x, y: base.y + BEACON_HEIGHT, z: base.z });
    if (!bottom || !top) {
      return;
    }
    const width = Math.max(2, Math.min(10, 10 - distance2 / 120));
    Draw.line(bottom.x, bottom.y, top.x, top.y, width + 6, argb(color, 0.12));
    Draw.line(bottom.x, bottom.y, top.x, top.y, width, argb(color, 0.55));
  }
  function hideModel(group, item) {
    models.get(itemKey(group, item))?.frame?.setVisible(false);
  }
  function circle(center, radius, height, thickness, color, segments) {
    let previous = null;
    for (let step = 0; step <= segments; step++) {
      const angle = step / segments * Math.PI * 2;
      const point = screenPoint({
        x: center.x + Math.cos(angle) * radius,
        y: center.y + height,
        z: center.z + Math.sin(angle) * radius
      });
      if (point && previous) {
        Draw.line(previous.x, previous.y, point.x, point.y, thickness, color);
      }
      previous = point;
    }
  }
  function drawRing(center, radius, color, distance2) {
    circle(
      center,
      radius,
      0.4,
      3,
      argb(color, 0.6),
      distance2 - radius > FAR_RING_DISTANCE ? FAR_RING_SEGMENTS : RING_SEGMENTS
    );
  }
  function drawPlate(x, y, title, subtitle, color, alpha, size) {
    const width = Math.max(title.length * size * 0.56, subtitle ? subtitle.length * 12 * 0.56 : 0) + 28;
    const height = size + 14 + (subtitle ? 16 : 0);
    const left = x - width / 2;
    const top = y - height / 2;
    Draw.rect(left, top, width, height, argb("#101418", 0.72 * alpha));
    Draw.rect(left, top, 4, height, argb(color, alpha));
    Draw.rect(left + 4, top, width - 4, 1, argb(color, 0.45 * alpha));
    Draw.text(title, x + 2, top + 6 + size, size, argb(color, alpha), 3);
    if (subtitle) {
      Draw.text(subtitle, x + 2, top + size + 22, 12, argb("#f3e9d6", 0.85 * alpha), 3);
    }
  }
  function drawModel(key, model, position, now) {
    let record = models.get(key);
    if (!record) {
      record = { frame: null, nextAttempt: 0, model };
      models.set(key, record);
    }
    if (record.frame === null) {
      if (now < record.nextAttempt) {
        return;
      }
      record.nextAttempt = now + MODEL_RETRY_MS;
      record.frame = Scene.createModelFrame(model) ?? Scene.createModelFrame(FALLBACK_MODEL);
      if (record.frame === null) {
        return;
      }
    }
    record.frame.setVisible(true);
    const spin = now * 1e-3;
    const bob = 0.12 * Math.sin(now * 3e-3);
    if (record.model === ARROW_MODEL) {
      const c = Math.cos(spin / 2) * Math.SQRT1_2;
      const s = Math.sin(spin / 2) * Math.SQRT1_2;
      record.frame.setWorldPosition({ x: position.x, y: position.y + 1.45 + bob, z: position.z });
      record.frame.setRotation({ w: c, x: s, y: -s, z: c });
      return;
    }
    record.frame.setWorldPosition({ x: position.x, y: position.y + 0.25 + bob, z: position.z });
    record.frame.setRotation({ w: Math.cos(spin / 2), x: 0, y: Math.sin(spin / 2), z: 0 });
  }
  function drawItems(eye, now) {
    for (const [group, items] of groups) {
      for (const item of items) {
        const position = itemPosition(item);
        if (!position) {
          continue;
        }
        const distance2 = flatDistance(eye, position);
        if (item.range && distance2 > item.range) {
          if (item.model) {
            hideModel(group, item);
          }
          continue;
        }
        const key = itemKey(group, item);
        if (item.model) {
          drawModel(key, item.model, position, now);
        }
        if (item.ring && distance2 < item.ring * 6) {
          drawRing(position, item.ring, item.color, distance2);
        }
        if (item.beacon && distance2 < BEACON_RANGE) {
          drawBeacon(position, item.color, distance2);
        }
        const near = item.near ?? (item.primary ? PRIMARY_LABEL_NEAR : LABEL_NEAR);
        if (!item.label || distance2 > near) {
          continue;
        }
        const lift = Math.min(
          item.vehicleId ? 2.6 : item.model ? 3.4 : 3,
          1.6 + distance2 * 0.25
        );
        const anchor = { x: position.x, y: position.y + lift, z: position.z };
        let sight = labelSight.get(key);
        if (!sight || now - sight.at >= LABEL_SIGHT_MS) {
          sight = { at: now, visible: Boolean(Scene.projectWorld(anchor, true)?.visible) };
          labelSight.set(key, sight);
        }
        if (!sight.visible) {
          continue;
        }
        const label = Scene.projectWorld(anchor, false);
        if (!label?.visible || label.x === null || label.y === null) {
          continue;
        }
        const alpha = Math.min(1, (near - distance2) / (near * 0.2));
        const subtitle = item.hint && distance2 < HINT_NEAR ? item.hint : item.primary ? `${Math.round(distance2)} m` : null;
        drawPlate(
          label.x,
          label.y,
          item.label,
          subtitle,
          item.color,
          alpha,
          item.primary ? 17 : 14
        );
      }
    }
  }
  function drawPeople(eye, now) {
    if (now - bountiesAt >= BOUNTY_REFRESH_MS) {
      bountiesAt = now;
      bounties = [];
      for (const player of World.getPlayers()) {
        if (player.isLocal || !player.alive) {
          continue;
        }
        const bounty = player.state.get("fr.bounty");
        if (typeof bounty === "number" && bounty) {
          bounties.push({ player, text: `${formatMoney(bounty)} BOUNTY` });
        }
      }
    }
    for (const { player, text: text2 } of bounties) {
      let position = null;
      try {
        position = player.getWorldPosition();
      } catch {
        continue;
      }
      if (!position || flatDistance(eye, position) >= BOUNTY_RANGE) {
        continue;
      }
      Draw.worldText(
        text2,
        { x: position.x, y: position.y + 2.7, z: position.z },
        15,
        argb("#d0574a"),
        3,
        true
      );
    }
  }
  function titleCase(label) {
    return (label ?? "").split(" · ")[0].toLowerCase().replace(/(^|\s)\S/g, (letter) => letter.toUpperCase());
  }
  function blipOf(item) {
    if (item.map === false) {
      return null;
    }
    const spec = typeof item.map === "object" && item.map ? item.map : null;
    if (!spec && !item.primary && !item.beacon && item.range && item.range < MAP_FROM_RANGE) {
      return null;
    }
    const options = {
      color: rgb(item.color),
      icon: spec?.icon ?? (item.primary ? "diamond" : "dot"),
      size: spec?.size ?? (item.primary ? 6 : 4),
      edge: spec?.edge ?? Boolean(item.primary),
      flash: Boolean(spec?.flash)
    };
    const label = spec?.label ?? (item.primary ? titleCase(item.label) : null);
    if (label) {
      options.label = label;
    }
    return options;
  }
  function crewOf(player) {
    const crew = player.state.get("fr.crew");
    return typeof crew === "object" && crew !== null ? crew : null;
  }
  function wantedBlips() {
    const wanted = /* @__PURE__ */ new Map();
    for (const [group, items] of groups) {
      for (const item of items) {
        const options = blipOf(item);
        if (!options) {
          continue;
        }
        const target = streamedVehicle(item) ?? item.position ?? null;
        if (target) {
          wanted.set(itemKey(group, item), { target, options });
        }
      }
    }
    const ownCrew = LocalPlayer ? crewOf(LocalPlayer)?.tag ?? null : null;
    if (ownCrew) {
      for (const player of World.getPlayers()) {
        const crew = crewOf(player);
        if (player.isLocal || crew?.tag !== ownCrew) {
          continue;
        }
        wanted.set(`crew/${player.id}`, {
          target: player,
          options: { color: rgb(crew.color), icon: "dot", size: 4, label: player.nickname }
        });
      }
    }
    return wanted;
  }
  function entityId(target) {
    const id = target.id;
    return typeof id === "number" ? id : null;
  }
  function syncBlips() {
    const wanted = wantedBlips();
    for (const id of [...blips.keys()]) {
      if (wanted.has(id)) {
        continue;
      }
      CityMap.removeBlip(id);
      blips.delete(id);
    }
    for (const [id, { target, options }] of wanted) {
      const entity = entityId(target);
      const key = JSON.stringify([options, entity !== null ? `#${entity}` : target]);
      if (blips.get(id) === key) {
        continue;
      }
      if (CityMap.setBlip(id, target, options)) {
        blips.set(id, key);
      }
    }
  }
  onServer("fr:markers", (request) => {
    if (typeof request.group !== "string" || !Array.isArray(request.items)) {
      return;
    }
    releaseModels(request.group);
    for (const key of [...labelSight.keys()]) {
      if (key.startsWith(`${request.group}/`)) {
        labelSight.delete(key);
      }
    }
    if (request.items.length) {
      groups.set(request.group, request.items);
    } else {
      groups.delete(request.group);
    }
    blipsSyncedAt = 0;
  });
  events.on("missionUnload", () => {
    groups.clear();
    models.clear();
    labelSight.clear();
    bounties = [];
    blips.clear();
  });
  onStop(() => {
    releaseModels(null);
    groups.clear();
  });
  events.on("render", () => {
    if (!World.isReady() || !LocalPlayer?.spawned) {
      return;
    }
    const now = Date.now();
    frameVehicles = null;
    if (now - blipsSyncedAt >= BLIP_SYNC_MS) {
      blipsSyncedAt = now;
      syncBlips();
    }
    if (CityMap.isOpen()) {
      for (const [group, items] of groups) {
        for (const item of items) {
          if (item.model) {
            hideModel(group, item);
          }
        }
      }
      return;
    }
    const eye = LocalPlayer.getWorldPosition();
    if (!eye) {
      return;
    }
    drawItems(eye, now);
    drawPeople(eye, now);
  });

  // src/client/lib/player.ts
  function livePlayer() {
    const player = LocalPlayer;
    return player?.spawned && player.alive ? player : null;
  }
  function onLocalSpawn(handler) {
    events.on("playerSpawn", (player) => {
      if (player.isLocal) {
        handler(player);
      }
    });
  }
  function onLocalDeath(handler) {
    events.on("playerDeath", (player) => {
      if (player.isLocal) {
        handler(player);
      }
    });
  }

  // src/client/main.ts
  var COUNTDOWN_BEEP = "beep50.wav";
  var CHECKPOINT_SOUND = "00_kompas.wav";
  var countdown = new Timers();
  events.on("missionReady", () => Fade.in(1.5));
  onLocalSpawn(() => Fade.in(1));
  onLocalDeath(() => {
    countdown.clear();
    Sound.play("01b_srdce.wav");
    Fade.out(1.2);
  });
  events.on("missionUnload", () => countdown.clear());
  onServer("fr:race:countdown", ({ seconds }) => {
    countdown.clear();
    const count = clamp(seconds ?? 3, 1, 5);
    Hud.announce("GET READY", 1);
    for (let step = 1; step <= count; step++) {
      countdown.after(step * 1e3, () => {
        Hud.announce(String(count - step + 1), 1);
        Sound.play(COUNTDOWN_BEEP, { volume: 0.7 });
      });
    }
    countdown.after((count + 1) * 1e3, () => {
      Hud.announce("GO!", 1.5);
      Sound.play(COUNTDOWN_BEEP, { volume: 1 });
      Sound.play("18bc_start.wav", { volume: 0.8 });
    });
  });
  events.on("fr:race:checkpoint", () => Sound.play(CHECKPOINT_SOUND, { volume: 0.8 }));
  onServer("fr:fade", ({ out, seconds }) => {
    if (out) {
      Fade.out(seconds ?? 0.4);
    } else {
      Fade.in(seconds ?? 0.6);
    }
  });
  onServer(
    "fr:announce",
    ({ text: text2, seconds, sound }) => {
      if (typeof text2 === "string") {
        Hud.announce(text2, seconds ?? 2);
      }
      if (typeof sound === "string") {
        Sound.play(sound, { volume: 0.8 });
      }
    }
  );
  onServer("fr:dev:hud", ({ action, value }) => {
    const number = Number(value);
    switch (action) {
      case "countdown":
        Hud.startCountdown(Number.isFinite(number) ? number : 30);
        break;
      case "watch":
        Hud.showWatch(12, 0);
        break;
      case "swing":
        Camera.setSwing(Number.isFinite(number) ? number : 30);
        break;
      case "fade":
        Fade.out(1);
        setTimeout(() => Fade.in(1), 2e3);
        break;
      case "clear":
        Hud.hideWatch();
        Camera.setSwing(0);
        break;
      default:
        if (Sound.play(typeof value === "string" ? value : "00_dog.wav") === null) {
          Hud.showMessage("Could not play that sound");
        }
    }
  });

  // src/client/selector.ts
  var FREE_RIDE_FRAMES = ["emeth_1", "emeth_2", "emeth_3", "emeth_4"];
  var MISSIONS = /* @__PURE__ */ new Set(["freeride", "freeride_extended", "freekrajina"]);
  var NAVIGATION_SOUND = "00_kompas.wav";
  var HELLO_RETRY_MS = 500;
  var CAMERA_RECHECK_MS = 200;
  var BEGIN_RETRY_MS = 1e3;
  var SPAWN_TIMEOUT_MS = 12e3;
  var FLYOVER_FOV = 62;
  var LOOK_FOV = 54;
  var SAMPLE_SIZE = 7;
  var page2 = new Page(
    "Login card",
    "selector.html",
    { visible: true, focus: false, zIndex: 90 },
    () => {
      page2.focus(true);
      sendToPage();
    }
  );
  var picker = {
    active: false,
    open: false,
    phase: "connecting",
    generation: 0,
    anchors: [],
    nextHelloAt: 0,
    nextBeginAt: 0,
    missingStartsReported: false,
    serverId: null,
    nickname: "",
    name: null,
    authenticated: null,
    message: "Finding your way into Lost Heaven…",
    error: null,
    pending: false,
    pendingSince: 0,
    flight: null,
    flightStart: 0,
    characters: [],
    fallbackModel: null,
    modelIndex: 0,
    lookAnchor: null,
    preview: null,
    cameraSide: 0,
    cameraChangedAt: 0
  };
  function playTick() {
    Sound.play(NAVIGATION_SOUND, { volume: 0.65 });
  }
  function wait(message2) {
    picker.pending = true;
    picker.pendingSince = Date.now();
    picker.message = message2;
  }
  function flightFrom(path) {
    if (!path || !Array.isArray(path.samples) || path.samples.length < SAMPLE_SIZE * 8 || path.samples.length % SAMPLE_SIZE !== 0) {
      return null;
    }
    if (!path.samples.every(Number.isFinite) || !(path.step > 0) || !(path.speed > 0)) {
      return null;
    }
    return {
      samples: path.samples,
      count: path.samples.length / SAMPLE_SIZE,
      step: path.step,
      speed: path.speed
    };
  }
  function uniform(p0, p1, p2, p3, t) {
    return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
  }
  function flightAt(flight, distance2) {
    const { samples, count, step } = flight;
    const length = count * step;
    const along = (distance2 % length + length) % length / step;
    const i = Math.floor(along);
    const t = along - i;
    const value = (field) => uniform(
      samples[(i - 1 + count) % count * SAMPLE_SIZE + field],
      samples[i * SAMPLE_SIZE + field],
      samples[(i + 1) % count * SAMPLE_SIZE + field],
      samples[(i + 2) % count * SAMPLE_SIZE + field],
      t
    );
    const position = { x: value(0), y: value(1), z: value(2) };
    const target = { x: value(3), y: value(4), z: value(5) };
    return { position, direction: toward(position, target), roll: value(6) };
  }
  function flyFrame() {
    if (!picker.flight) {
      return;
    }
    const pose = flightAt(
      picker.flight,
      (Date.now() - picker.flightStart) / 1e3 * picker.flight.speed
    );
    Camera.lock(pose.position, pose.direction, pose.roll);
  }
  function startFlight(path) {
    picker.flight = flightFrom(path);
    if (!picker.flight) {
      return;
    }
    const { count, step, speed } = picker.flight;
    picker.flightStart = Date.now() - Math.random() * count * step / speed * 1e3;
    Camera.setFov(FLYOVER_FOV);
  }
  function sendToPage() {
    const model = picker.characters[picker.modelIndex];
    page2.emit("login:state", {
      phase: picker.phase,
      open: picker.open,
      serverId: picker.serverId,
      nickname: picker.nickname,
      name: picker.name,
      authenticated: picker.authenticated,
      pending: picker.pending,
      message: picker.message,
      error: picker.error,
      model: model ? { name: model.name, role: model.role } : null,
      modelIndex: picker.modelIndex,
      modelCount: picker.characters.length
    });
  }
  function bindPage() {
    page2.on("login:submit", (payload) => {
      if (picker.phase !== "auth" || picker.pending) {
        return;
      }
      const request = fields(payload);
      const mode2 = request.mode === "register" ? "register" : "login";
      if (typeof request.name !== "string" || typeof request.password !== "string") {
        return;
      }
      picker.error = null;
      wait(mode2 === "register" ? "Writing your name in the book…" : "Checking the book…");
      sendToPage();
      events.emitServer("fr:login:submit", {
        generation: picker.generation,
        mode: mode2,
        name: request.name,
        password: request.password,
        remember: Boolean(request.remember),
        logoutOthers: request.logoutOthers === true
      });
    });
    page2.on("login:token", (payload) => {
      const token = fields(payload).token;
      if (picker.phase !== "auth" || picker.pending || typeof token !== "string") {
        return;
      }
      picker.error = null;
      wait("Welcome back. The city is opening…");
      sendToPage();
      events.emitServer("fr:login:token", { generation: picker.generation, token });
    });
    page2.on("login:forget", (payload) => {
      const token = fields(payload).token;
      if (typeof token === "string") {
        events.emitServer("fr:login:forget", { token });
      }
    });
    page2.on("login:look:move", (payload) => moveModel(fields(payload).step === -1 ? -1 : 1));
    page2.on("login:look:camera", rotateCamera);
    page2.on("login:look:confirm", confirmLook);
  }
  function freePreview() {
    picker.preview?.destroy();
    picker.preview = null;
  }
  function cameraFor(anchor, side) {
    const { position, direction } = anchor;
    const length = Math.hypot(direction.x, direction.z) || 1;
    const forward = { x: direction.x / length, z: direction.z / length };
    const directions = [
      forward,
      { x: -forward.z, z: forward.x },
      { x: -forward.x, z: -forward.z },
      { x: forward.z, z: -forward.x }
    ];
    const away = directions[side];
    const eye = { x: position.x + away.x * 4, y: position.y + 2.1, z: position.z + away.z * 4 };
    const subject = { x: position.x, y: position.y + 1.25, z: position.z };
    Camera.lock(eye, toward(eye, subject));
    Camera.setFov(LOOK_FOV);
    picker.cameraChangedAt = Date.now();
  }
  function showLook() {
    const anchor = picker.lookAnchor;
    if (!anchor) {
      return;
    }
    freePreview();
    let attempts = picker.characters.length;
    while (attempts-- > 0) {
      const model = picker.characters[picker.modelIndex];
      picker.preview = Scene.createHumanFrame(model.file, anchor.position, anchor.direction);
      if (picker.preview !== null) {
        break;
      }
      picker.modelIndex = (picker.modelIndex + 1) % picker.characters.length;
    }
    if (picker.preview === null) {
      skipLook();
      return;
    }
    picker.cameraSide = 0;
    cameraFor(anchor, picker.cameraSide);
    picker.message = "How will Lost Heaven first see you? What you become is up to you.";
    sendToPage();
  }
  function moveModel(step) {
    if (picker.phase !== "look" || picker.pending || picker.characters.length < 2) {
      return;
    }
    picker.modelIndex = (picker.modelIndex + step + picker.characters.length) % picker.characters.length;
    playTick();
    showLook();
  }
  function rotateCamera() {
    if (picker.phase !== "look" || picker.pending || !picker.lookAnchor) {
      return;
    }
    picker.cameraSide = (picker.cameraSide + 1) % 4;
    cameraFor(picker.lookAnchor, picker.cameraSide);
    playTick();
    sendToPage();
  }
  function confirmLook() {
    if (picker.phase !== "look" || picker.pending || picker.preview === null) {
      return;
    }
    wait("Your story begins…");
    playTick();
    sendToPage();
    events.emitServer("fr:login:look", {
      generation: picker.generation,
      model: picker.characters[picker.modelIndex].file
    });
  }
  function skipLook() {
    freePreview();
    picker.phase = "entering";
    wait("Your story begins…");
    sendToPage();
    if (picker.fallbackModel) {
      events.emitServer("fr:login:look", {
        generation: picker.generation,
        model: picker.fallbackModel
      });
    }
  }
  function isFinitePoint(point) {
    return Boolean(point) && [point?.x, point?.y, point?.z].every(Number.isFinite);
  }
  function beginLook(result) {
    const anchor = result.anchor;
    const valid = anchor && isFinitePoint(anchor.position) && isFinitePoint(anchor.direction);
    const offered = Array.isArray(result.characters) ? result.characters.filter((model) => typeof model?.file === "string") : [];
    picker.fallbackModel = offered[0]?.file ?? picker.fallbackModel;
    picker.characters = offered.filter((model) => Scene.canUseHumanModel(model.file));
    if (!valid || !picker.characters.length) {
      skipLook();
      return;
    }
    picker.phase = "look";
    picker.flight = null;
    picker.lookAnchor = anchor;
    picker.modelIndex = 0;
    showLook();
  }
  function startFrames() {
    const anchors = [];
    for (const frame of FREE_RIDE_FRAMES) {
      const worldFrame = World.findWorldFrame(frame);
      const position = worldFrame?.getWorldPosition();
      const direction = worldFrame?.getWorldDirection();
      if (position && direction) {
        anchors.push({ frame, position: plain(position), direction: plain(direction) });
      }
    }
    return anchors;
  }
  function begin() {
    if (picker.active || !MISSIONS.has(World.getMission() ?? "") || LocalPlayer?.spawned) {
      return;
    }
    const anchors = startFrames();
    if (!anchors.some((anchor) => anchor.frame === FREE_RIDE_FRAMES[0]) || anchors.length < 2) {
      picker.nextBeginAt = Date.now() + BEGIN_RETRY_MS;
      if (!picker.missingStartsReported) {
        picker.missingStartsReported = true;
        log.warn("Waiting for Free Ride start frames");
      }
      return;
    }
    Object.assign(picker, {
      active: true,
      open: false,
      phase: "connecting",
      generation: World.getMissionGeneration(),
      anchors,
      nextHelloAt: 0,
      missingStartsReported: false,
      authenticated: null,
      error: null,
      pending: false,
      flight: null,
      characters: [],
      lookAnchor: null,
      message: "Finding your way into Lost Heaven…"
    });
    if (!page2.open()) {
      return;
    }
    bindPage();
    Hud.hideWatch();
    Hud.hideScore();
    cameraFor(anchors.find((anchor) => anchor.frame === "emeth_3") ?? anchors[0], 0);
  }
  function end() {
    if (!picker.active) {
      return;
    }
    freePreview();
    page2.close();
    picker.active = false;
    picker.open = false;
    picker.flight = null;
    picker.characters = [];
    picker.lookAnchor = null;
    Camera.unlock();
  }
  events.on("missionReady", begin);
  onStart(() => {
    if (World.isReady()) {
      begin();
    }
  });
  events.on("missionUnload", end);
  onStop(end);
  onLocalSpawn(end);
  onServer("fr:login:open", (state) => {
    if (!picker.active || picker.open || state.generation !== picker.generation || state.mission !== World.getMission()) {
      return;
    }
    picker.open = true;
    picker.serverId = typeof state.serverId === "string" ? state.serverId : null;
    picker.nickname = typeof state.nickname === "string" ? state.nickname : "";
    picker.authenticated = typeof state.authenticated === "string" ? state.authenticated : null;
    picker.fallbackModel = typeof state.fallbackModel === "string" ? state.fallbackModel : null;
    startFlight(state.flight);
    if (picker.authenticated) {
      picker.phase = "entering";
      picker.name = picker.authenticated;
      picker.message = `Welcome back, ${picker.authenticated}. The city is opening…`;
    } else {
      picker.phase = "auth";
      picker.message = "";
    }
    sendToPage();
  });
  onServer("fr:login:result", (result) => {
    if (!picker.active) {
      return;
    }
    picker.pending = false;
    if (!result.ok) {
      picker.error = {
        field: typeof result.field === "string" ? result.field : "form",
        message: typeof result.message === "string" ? result.message : "That did not work. Try again.",
        code: typeof result.code === "string" ? result.code : null
      };
      picker.message = "";
      sendToPage();
      return;
    }
    picker.error = null;
    picker.name = typeof result.name === "string" ? result.name : picker.name;
    picker.authenticated = picker.name;
    page2.emit(
      "login:remember",
      typeof result.token === "string" && picker.serverId ? {
        serverId: picker.serverId,
        name: picker.name,
        token: result.token,
        expires: result.expires
      } : { serverId: picker.serverId, name: picker.name, token: null }
    );
    if (result.needsLook) {
      beginLook(result);
    } else {
      picker.phase = "entering";
      wait(`Welcome back, ${picker.name}. The city is opening…`);
      sendToPage();
    }
  });
  events.on("fr:login:accepted", () => {
    if (!picker.active) {
      return;
    }
    picker.phase = "entering";
    wait("The city is opening. One moment…");
    sendToPage();
  });
  onServer("fr:login:error", (error) => {
    if (!picker.active) {
      return;
    }
    picker.pending = false;
    picker.message = typeof error.message === "string" ? error.message : "That entrance is unavailable. Try again.";
    sendToPage();
  });
  function sayHello(now) {
    if (picker.open && picker.phase !== "entering" || now < picker.nextHelloAt) {
      return;
    }
    events.emitServer("fr:login:hello", {
      generation: picker.generation,
      anchors: picker.anchors,
      opened: picker.open
    });
    picker.nextHelloAt = now + (picker.open ? HELLO_RETRY_MS * 4 : HELLO_RETRY_MS);
  }
  function findLookSide(now) {
    const anchor = picker.lookAnchor;
    if (!anchor || picker.cameraSide >= 3 || now - picker.cameraChangedAt < CAMERA_RECHECK_MS) {
      return;
    }
    const head = { x: anchor.position.x, y: anchor.position.y + 1.25, z: anchor.position.z };
    if (!Scene.projectWorld(head, true)?.visible) {
      picker.cameraSide += 1;
      cameraFor(anchor, picker.cameraSide);
    } else {
      picker.cameraChangedAt = Number.POSITIVE_INFINITY;
    }
  }
  events.on("render", () => {
    const now = Date.now();
    if (!picker.active) {
      if (World.isReady() && MISSIONS.has(World.getMission() ?? "") && !LocalPlayer?.spawned && now >= picker.nextBeginAt) {
        begin();
      }
      return;
    }
    if (LocalPlayer?.spawned) {
      end();
      return;
    }
    sayHello(now);
    if (picker.pending && now - picker.pendingSince > SPAWN_TIMEOUT_MS) {
      picker.pending = false;
      picker.pendingSince = Number.POSITIVE_INFINITY;
      picker.message = picker.phase === "entering" ? "Your place is ready. Waiting for your player…" : "The city did not answer. Try again.";
      sendToPage();
    }
    if (picker.phase === "look") {
      findLookSide(now);
      return;
    }
    flyFrame();
  });

  // src/client/shops.ts
  var INTERACT_DISTANCE = 3.2;
  var LABEL_DISTANCE = 24;
  var DELIVERY_RETRY_MS = 1200;
  var MODEL_RETRY_MS2 = 2e3;
  var generation = 0;
  var shops = [];
  var courier = false;
  var lastMoney = null;
  var lastDelivery = 0;
  var lastModelAttempt = 0;
  var actors = /* @__PURE__ */ new Map();
  var selected = /* @__PURE__ */ new Map();
  function nearestShop(player) {
    if (!player) {
      return null;
    }
    let nearest = null;
    for (const shop of shops) {
      const gap = distance(player.position, shop.position);
      if (gap <= INTERACT_DISTANCE && (!nearest || gap < nearest.distance)) {
        nearest = { shop, distance: gap };
      }
    }
    return nearest?.shop ?? null;
  }
  function clearActors() {
    for (const handle of actors.values()) {
      Scene.destroy(handle);
    }
    actors.clear();
  }
  function clearMarket() {
    clearActors();
    generation = 0;
    shops = [];
    selected.clear();
    courier = false;
    lastMoney = null;
    Hud.hideScore();
  }
  function shopFacing(shop) {
    if (shop.facing) {
      return shop.facing;
    }
    const doorHinge = shop.entranceFrame ? World.findWorldFrame(shop.entranceFrame)?.getWorldPosition() ?? shop.entrance : null;
    if (!doorHinge) {
      return { x: 0, y: 0, z: 1 };
    }
    const x = shop.position.x - doorHinge.x;
    const z = shop.position.z - doorHinge.z;
    const length = Math.hypot(x, z);
    return length > 1e-3 ? { x: x / length, y: 0, z: z / length } : { x: 0, y: 0, z: 1 };
  }
  function placeActors() {
    if (!World.isReady() || generation !== World.getMissionGeneration()) {
      return;
    }
    for (const shop of shops) {
      if (!shop.model || actors.has(shop.id) || !Scene.canUseHumanModel(shop.model)) {
        continue;
      }
      const handle = Scene.createHuman(shop.model, shop.position, shopFacing(shop), true);
      if (handle === null) {
        continue;
      }
      actors.set(shop.id, handle);
      if (shop.animation && !Scene.playHumanAnimation(handle, shop.animation, true)) {
        log.warn(`Could not play ${shop.animation} for ${shop.name}`);
      }
    }
  }
  function selectedOffer(shop) {
    return shop.offers[(selected.get(shop.id) ?? 0) % shop.offers.length];
  }
  function buyNearest() {
    const shop = nearestShop(livePlayer());
    if (!shop) {
      return;
    }
    events.emitServer("fr:shop:buy", {
      generation,
      shopId: shop.id,
      offerId: selectedOffer(shop).id
    });
  }
  function nextOffer() {
    const shop = nearestShop(livePlayer());
    if (!shop || shop.offers.length < 2) {
      return;
    }
    selected.set(shop.id, ((selected.get(shop.id) ?? 0) + 1) % shop.offers.length);
    Sound.play("00_kompas.wav", { volume: 0.45 });
  }
  function priceOf(offer) {
    if (offer.price === null) {
      return "PRICE AT PUMP";
    }
    return offer.price === 0 ? "NO CHARGE" : `$${offer.price}`;
  }
  function showShopCard(shop, player) {
    if (!shop) {
      showCounter("shop", null);
      return;
    }
    const offer = selectedOffer(shop);
    showCounter("shop", {
      title: shop.name,
      tagline: shop.tagline,
      lines: shop.offers.map((entry) => ({
        name: entry.name,
        price: priceOf(entry),
        note: entry.note,
        affordable: entry.price === null || entry.price <= player.money
      })),
      index: (selected.get(shop.id) ?? 0) % shop.offers.length,
      action: offer.price === 0 ? "ACCEPT" : "BUY",
      money: player.money
    });
  }
  function showMoney(player) {
    if (Hud.getCountdown() !== null) {
      if (Hud.isScoreVisible()) {
        Hud.hideScore();
      }
      lastMoney = null;
    } else if ((lastMoney !== player.money || Hud.getScore() !== player.money || Hud.isScoreVisible() !== true) && Hud.setScore(player.money)) {
      lastMoney = player.money;
    }
  }
  bindCounterKeys(buyNearest, nextOffer);
  onServer("fr:shops:state", (state) => {
    if (state.generation !== World.getMissionGeneration() || !Array.isArray(state.shops)) {
      return;
    }
    if (generation !== state.generation) {
      clearActors();
    }
    generation = state.generation;
    shops = state.shops;
    courier = Boolean(state.courier);
    placeActors();
  });
  events.on("missionUnload", clearMarket);
  onStop(clearMarket);
  events.on("render", () => {
    const player = livePlayer();
    if (!player || generation !== World.getMissionGeneration()) {
      showCounter("shop", null);
      return;
    }
    showMoney(player);
    const now = Date.now();
    if (actors.size < shops.filter((shop2) => shop2.model).length && now - lastModelAttempt > MODEL_RETRY_MS2) {
      lastModelAttempt = now;
      placeActors();
    }
    for (const shop2 of shops) {
      if (distance(player.position, shop2.position) > LABEL_DISTANCE) {
        continue;
      }
      const top = { x: shop2.position.x, y: shop2.position.y + 2.15, z: shop2.position.z };
      Draw.worldText(shop2.name.toUpperCase(), top, 16, COLORS.gold, 3, true);
    }
    const shop = nearestShop(player);
    showShopCard(shop, player);
    if (shop?.id === "pete" && courier && now - lastDelivery > DELIVERY_RETRY_MS) {
      lastDelivery = now;
      events.emitServer("fr:shop:deliver", {});
    }
  });

  // src/client/bars.ts
  var LABEL_DISTANCE2 = 18;
  var SHOP_REACH = 3.2;
  var generation2 = 0;
  var bars = [];
  var shopCounters = [];
  var selected2 = /* @__PURE__ */ new Map();
  function counterHere() {
    const player = livePlayer();
    if (!player || player.getVehicle() || generation2 !== World.getMissionGeneration()) {
      return null;
    }
    const position = player.getWorldPosition() ?? player.position;
    if (shopCounters.some((shop) => distance(position, shop) <= SHOP_REACH)) {
      return null;
    }
    return bars.find(
      (bar) => player.virtualWorld === bar.world && flatDistance(position, bar.counter.position) <= bar.counter.reach && Math.abs(position.y - bar.counter.position.y) <= 1.5
    ) ?? null;
  }
  function selectedIndex(bar) {
    return (selected2.get(bar.id) ?? 0) % bar.drinks.length;
  }
  function orderDrink() {
    const bar = counterHere();
    if (!bar) {
      return;
    }
    events.emitServer("fr:bar:buy", {
      generation: generation2,
      barId: bar.id,
      drinkId: bar.drinks[selectedIndex(bar)].id
    });
  }
  function nextDrink() {
    const bar = counterHere();
    if (!bar) {
      return;
    }
    selected2.set(bar.id, ((selected2.get(bar.id) ?? 0) + 1) % bar.drinks.length);
    Sound.play("00_kompas.wav", { volume: 0.45 });
  }
  function showCard(bar, player) {
    if (!bar) {
      showCounter("bar", null);
      return;
    }
    showCounter("bar", {
      title: bar.name,
      tagline: bar.tagline,
      lines: bar.drinks.map((drink) => ({
        name: drink.name,
        price: drink.price === null ? "A ROUND" : `$${drink.price}`,
        note: drink.note,
        affordable: drink.price === null || drink.price <= player.money
      })),
      index: selectedIndex(bar),
      action: "ORDER",
      money: player.money
    });
  }
  bindCounterKeys(orderDrink, nextDrink);
  onServer("fr:bars:state", (state) => {
    if (state.generation !== World.getMissionGeneration() || !Array.isArray(state.bars)) {
      return;
    }
    generation2 = state.generation;
    bars = state.bars;
  });
  onServer("fr:shops:state", (state) => {
    if (Array.isArray(state.shops)) {
      shopCounters = state.shops.map((shop) => shop.position).filter(Boolean);
    }
  });
  events.on("missionUnload", () => {
    shopCounters = [];
    generation2 = 0;
    bars = [];
    selected2.clear();
  });
  events.on("render", () => {
    const player = livePlayer();
    if (!player || generation2 !== World.getMissionGeneration()) {
      showCounter("bar", null);
      return;
    }
    const position = player.getWorldPosition() ?? player.position;
    for (const bar of bars) {
      if (player.virtualWorld !== bar.world || flatDistance(position, bar.counter.position) > LABEL_DISTANCE2) {
        continue;
      }
      const { x, y, z } = bar.counter.position;
      Draw.worldText("BAR · N / B", { x, y: y + 1.9, z }, 15, COLORS.gold, 3, true);
    }
    showCard(counterHere(), player);
  });

  // src/client/drunk.ts
  var DRUNK_KEY = "fr.drunk";
  var SWING_FROM = 20;
  var SWING_MAX = 70;
  var WOBBLE_FROM = 25;
  var WOBBLE_DEGREES = 6;
  var WOBBLE_PERIOD_MS = 4500;
  var HAZE_FROM = 10;
  var HAZE_COLOR = 5912594;
  var LURCH_FROM = 70;
  var LURCH_SWING = 100;
  var LURCH_MS = 1200;
  var BLACKOUT_GRACE_MS = 3e3;
  var swing = 0;
  var baseFov = null;
  var lurchUntil = 0;
  var nextLurch = 0;
  var blackout = false;
  var wake = new Timers();
  function drunkLevel(player) {
    const value = Number(player.state.get(DRUNK_KEY) ?? 0);
    return Number.isFinite(value) ? clamp(value, 0, 100) : 0;
  }
  function setSwing(value) {
    const rounded = Math.round(value);
    if (rounded !== swing && Camera.setSwing(rounded)) {
      swing = rounded;
    }
  }
  function restoreFov() {
    if (baseFov === null) {
      return;
    }
    Camera.setFov(baseFov);
    baseFov = null;
  }
  function sober() {
    setSwing(0);
    restoreFov();
    lurchUntil = 0;
    nextLurch = 0;
  }
  function endBlackout(seconds) {
    if (!blackout) {
      return;
    }
    blackout = false;
    wake.clear();
    Fade.in(seconds);
  }
  function drawHaze(level, now) {
    const { width, height } = Web.getScreenSize();
    const pulse = 0.85 + 0.15 * Math.sin(now / 900);
    Draw.rect(
      0,
      0,
      width,
      height,
      withAlpha(HAZE_COLOR, Math.min(0.22, level / 100 * 0.22) * pulse)
    );
    const edge = Math.round(Math.min(width, height) * (0.06 + level / 100 * 0.1));
    const shade = withAlpha(0, Math.min(0.5, level / 100 * 0.5));
    Draw.rect(0, 0, width, edge, shade);
    Draw.rect(0, height - edge, width, edge, shade);
    Draw.rect(0, edge, edge, height - 2 * edge, shade);
    Draw.rect(width - edge, edge, edge, height - 2 * edge, shade);
  }
  events.on("render", () => {
    if (blackout) {
      return;
    }
    const player = livePlayer();
    const level = player ? drunkLevel(player) : 0;
    const now = Date.now();
    if (!player || level <= 0) {
      if (swing !== 0 || baseFov !== null) {
        sober();
      }
      return;
    }
    if (level >= LURCH_FROM && !player.getVehicle()) {
      if (!nextLurch) {
        nextLurch = now + 3e3 + Math.random() * 5e3;
      }
      if (now >= nextLurch) {
        lurchUntil = now + LURCH_MS;
        nextLurch = now + 6e3 + Math.random() * 6e3;
      }
    } else {
      nextLurch = 0;
    }
    if (now < lurchUntil) {
      setSwing(LURCH_SWING);
    } else {
      setSwing(level < SWING_FROM ? 0 : Math.min(SWING_MAX, (level - SWING_FROM + 10) * 0.9));
    }
    if (level >= WOBBLE_FROM) {
      baseFov ??= Camera.getFov();
      if (baseFov !== null) {
        const amplitude = (level - WOBBLE_FROM + 5) / (100 - WOBBLE_FROM + 5) * WOBBLE_DEGREES;
        Camera.setFov(baseFov + Math.sin(now / WOBBLE_PERIOD_MS * 2 * Math.PI) * amplitude);
      }
    } else {
      restoreFov();
    }
    if (level >= HAZE_FROM) {
      drawHaze(level, now);
    }
  });
  onServer("fr:drunk:blackout", ({ seconds }) => {
    blackout = true;
    sober();
    Fade.out(1.6);
    Sound.play("01b_srdce.wav", { volume: 0.8 });
    wake.clear();
    wake.after((Number(seconds) || 6) * 1e3 + BLACKOUT_GRACE_MS, () => endBlackout(1.5));
  });
  onLocalSpawn(() => {
    if (blackout) {
      Fade.out(0);
    }
  });
  events.on("fr:drunk:wake", () => endBlackout(2.5));
  onLocalDeath(sober);
  events.on("missionUnload", () => {
    sober();
    blackout = false;
    wake.clear();
    baseFov = null;
    swing = 0;
  });
  onStop(() => {
    sober();
    endBlackout(0.5);
  });

  // src/client/radio.ts
  var STREAM_TIMEOUT_MS = 12e3;
  var STREAM_VOLUME = 0.72;
  var RADIO_KEY = "f9";
  var TOGGLE = "radio";
  var page3 = new Page(
    "Web radio view",
    "radio.html",
    { width: 1, height: 1, x: 0, y: 0, zIndex: -100, visible: true, focus: false },
    // A page still loading plays once it is ready.
    () => {
      if (vehicleId !== null) {
        page3.emit("radio:play", { volume: streamVolume() });
      }
    }
  );
  var vehicleId = null;
  var online = false;
  var startedAt = 0;
  var reportedFailure = false;
  var enabled = true;
  Settings.describeKey(RADIO_KEY, "Car radio on or off");
  Settings.defineToggle(TOGGLE, "Car radio", enabled);
  function streamVolume() {
    return STREAM_VOLUME * Settings.getMediaVolume();
  }
  function say(message2) {
    Hud.showMessage(`RADIO · ${message2}`, COLORS.message);
  }
  function reportFailure() {
    if (vehicleId === null || reportedFailure) {
      return;
    }
    online = false;
    reportedFailure = true;
    say("Station unavailable");
  }
  function stopRadio() {
    if (vehicleId !== null) {
      page3.emit("radio:stop");
    }
    vehicleId = null;
    online = false;
    startedAt = 0;
    reportedFailure = false;
  }
  function destroyRadio() {
    stopRadio();
    page3.close();
  }
  function openPage() {
    if (page3.isOpen) {
      return true;
    }
    if (!page3.open()) {
      return false;
    }
    page3.on("radio:playing", () => {
      if (vehicleId === null) {
        return;
      }
      online = true;
      reportedFailure = false;
      say("Jazz Club Bandstand");
    });
    page3.on("radio:failed", reportFailure);
    return true;
  }
  function startRadio(vehicle) {
    stopRadio();
    vehicleId = vehicle.id;
    startedAt = Date.now();
    if (!openPage()) {
      reportFailure();
      return;
    }
    page3.emit("radio:play", { volume: streamVolume() });
  }
  function setRadio(on, announce) {
    enabled = on;
    Settings.setToggle(TOGGLE, on);
    if (!on) {
      stopRadio();
      if (announce) {
        say(`Off (${Settings.getKeyLabel(RADIO_KEY)} or /radio to tune in)`);
      }
      return;
    }
    const vehicle = livePlayer()?.getVehicle() ?? null;
    if (vehicle && vehicleId === null) {
      startRadio(vehicle);
    }
    if (announce) {
      say(vehicle ? "Tuning in…" : "On for your next car");
    }
  }
  Key.bind(RADIO_KEY, () => {
    setRadio(!enabled, true);
    events.emitServer("fr:radio:set", { enabled });
  });
  onServer(
    "fr:radio:setting",
    (data) => setRadio(data.enabled !== false, data.announce === true)
  );
  events.on("settingToggle", (id, value, resource) => {
    if (resource !== RESOURCE || id !== TOGGLE || value === enabled) {
      return;
    }
    setRadio(value, false);
    events.emitServer("fr:radio:set", { enabled: value });
  });
  events.on("mediaVolumeChange", () => {
    page3.emit("radio:volume", { volume: streamVolume() });
  });
  events.on("missionUnload", stopRadio);
  onLocalDeath(stopRadio);
  onStop(destroyRadio);
  events.on("render", () => {
    const vehicle = livePlayer()?.getVehicle() ?? null;
    if (!enabled) {
      return;
    }
    if ((vehicle?.id ?? null) !== vehicleId) {
      if (vehicle) {
        startRadio(vehicle);
      } else if (vehicleId !== null) {
        stopRadio();
      }
    }
    if (vehicleId !== null && !online && Date.now() - startedAt > STREAM_TIMEOUT_MS) {
      reportFailure();
    }
  });

  // src/client/car_picker.ts
  var TIMEOUT_MS = 1e4;
  var START_YAW = 0.55;
  var DEFAULT_MODEL = "thunderbird00.i3d";
  var TITLES = {
    admin: { edition: "Lost Heaven · Motor Pool", title: "Choose any car" },
    dealer: { edition: "Lost Heaven · Car Lot", title: "Buy a car" },
    garage: { edition: "Lost Heaven · Your Garage", title: "Bring a car round" }
  };
  var page4 = new Page(
    "Car picker",
    "car_picker.html",
    { visible: true, focus: true, zIndex: 110 },
    () => sendState()
  );
  var generation3 = 0;
  var mode = "admin";
  var models2 = [];
  var selectedId = null;
  var preview = null;
  var viewport = null;
  var yaw = START_YAW;
  var zoom = 1;
  var pending = false;
  var pendingSince = 0;
  var message = "";
  function isMode(value) {
    return value === "admin" || value === "dealer" || value === "garage";
  }
  function offerChoice(offer, name, request) {
    if (mode === "dealer") {
      const price = formatMoney(offer.price ?? 0);
      const full = request.room !== void 0 && request.room <= 0;
      return {
        id: offer.id,
        name,
        model: offer.model,
        note: offer.owned ? `${price} · already yours` : price,
        action: offer.owned ? "Already yours" : full ? "Garage full" : `Buy for ${price}`,
        disabled: offer.owned || full
      };
    }
    const recover = formatMoney(offer.recover ?? 0);
    return {
      id: offer.id,
      name,
      model: offer.model,
      note: offer.totaled ? `Wrecked · rebuild ${recover}` : "In your garage",
      action: offer.totaled ? `Rebuild for ${recover}` : `Bring it round (${formatMoney(request.fee ?? 0)} away from the lot)`
    };
  }
  function pickerModels(request) {
    const catalog = Scene.getCarCatalog().filter(
      (entry) => Number.isInteger(entry.id) && entry.id > 0 && typeof entry.name === "string" && typeof entry.model === "string"
    );
    if (mode === "admin") {
      return catalog.map((entry) => ({ ...entry, note: entry.model, action: "Drive this car" }));
    }
    const names = new Map(catalog.map((entry) => [entry.model.toLowerCase(), entry.name]));
    return (Array.isArray(request.offers) ? request.offers : []).filter((offer) => Number.isInteger(offer.id) && typeof offer.model === "string").map(
      (offer) => offerChoice(offer, names.get(offer.model.toLowerCase()) ?? offer.model, request)
    );
  }
  function sendState() {
    page4.emit("car:state", {
      ...TITLES[mode],
      models: models2,
      selectedId,
      previewReady: preview !== null,
      pending,
      message
    });
  }
  function closePicker() {
    const wasOpen = page4.isOpen;
    if (preview !== null) {
      Scene.destroyPreview(preview);
    }
    page4.close();
    preview = null;
    viewport = null;
    models2 = [];
    selectedId = null;
    pending = false;
    message = "";
    if (wasOpen) {
      announcePicker(false);
    }
  }
  function selectModel(id) {
    if (!page4.isOpen || pending || typeof id !== "number" || !Number.isInteger(id)) {
      return;
    }
    const model = models2.find((entry) => entry.id === id);
    if (!model || selectedId === id && preview !== null) {
      return;
    }
    if (preview !== null) {
      Scene.destroyPreview(preview);
    }
    selectedId = id;
    preview = Scene.createPreview(model.model);
    yaw = START_YAW;
    zoom = 1;
    message = preview === null ? "This model could not be previewed." : "Drag to turn · Scroll to zoom";
    sendState();
  }
  function chooseModel() {
    if (!page4.isOpen || pending || preview === null || selectedId === null) {
      return;
    }
    if (!livePlayer()) {
      message = "Enter Free Ride before choosing a car.";
      sendState();
      return;
    }
    const choice = models2.find((entry) => entry.id === selectedId);
    if (choice?.disabled) {
      return;
    }
    pending = true;
    pendingSince = Date.now();
    message = mode === "dealer" ? "Signing the papers…" : "Taking the wheel…";
    sendState();
    events.emitServer("fr:car:choose", { generation: generation3, id: selectedId, mode });
  }
  function viewportOf(payload) {
    const rect = fields(payload);
    const { x, y, width, height, pageWidth, pageHeight } = rect;
    if (!isFiniteNumber(x) || !isFiniteNumber(y) || !isFiniteNumber(width) || !isFiniteNumber(height) || !isFiniteNumber(pageWidth) || !isFiniteNumber(pageHeight)) {
      return null;
    }
    if (width <= 0 || height <= 0 || pageWidth <= 0 || pageHeight <= 0) {
      return null;
    }
    return { x, y, width, height, pageWidth, pageHeight };
  }
  function bindPage2() {
    page4.on("car:select", (choice) => selectModel(fields(choice).id));
    page4.on("car:choose", chooseModel);
    page4.on("car:close", closePicker);
    page4.on("car:viewport", (rect) => {
      viewport = viewportOf(rect) ?? viewport;
    });
    page4.on("car:rotate", (drag) => {
      const dx = fields(drag).dx;
      if (preview === null || !isFiniteNumber(dx)) {
        return;
      }
      yaw += clamp(dx, -120, 120) * 0.012;
      if (yaw > Math.PI * 2 || yaw < -Math.PI * 2) {
        yaw %= Math.PI * 2;
      }
    });
    page4.on("car:zoom", (wheel) => {
      const delta = fields(wheel).delta;
      if (preview === null || !isFiniteNumber(delta)) {
        return;
      }
      zoom = clamp(zoom - Math.sign(delta) * 0.12, 0.5, 2.5);
    });
  }
  function openPicker(request) {
    if (!World.isReady() || request.generation !== World.getMissionGeneration() || !livePlayer()) {
      return;
    }
    closePicker();
    generation3 = request.generation;
    mode = isMode(request.mode) ? request.mode : "admin";
    models2 = pickerModels(request);
    if (models2.length === 0) {
      Hud.showMessage("The stock car catalog is unavailable.", COLORS.message);
      return;
    }
    if (!page4.open()) {
      Hud.showMessage("The car picker could not open.", COLORS.message);
      closePicker();
      return;
    }
    announcePicker(true);
    bindPage2();
    selectModel(
      models2.find((entry) => entry.model.toLowerCase() === DEFAULT_MODEL)?.id ?? models2[0].id
    );
  }
  onServer("fr:car:open", openPicker);
  onServer("fr:car:result", (result) => {
    if (!page4.isOpen || !pending || result.id !== selectedId) {
      return;
    }
    if (result.ok) {
      closePicker();
      return;
    }
    pending = false;
    message = typeof result.message === "string" ? result.message : "The car could not be called. Try again.";
    sendState();
  });
  events.on("missionUnload", closePicker);
  onStop(closePicker);
  onLocalDeath(closePicker);
  events.on("render", () => {
    if (!page4.isOpen) {
      return;
    }
    if (!livePlayer() || generation3 !== World.getMissionGeneration()) {
      closePicker();
      return;
    }
    if (pending && Date.now() - pendingSince > TIMEOUT_MS) {
      pending = false;
      message = "The city did not answer. Choose again.";
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
      zoom
    );
  });

  // src/client/police.ts
  var CALL_KEY = "u";
  var reach = 0;
  var booths = [];
  function atBooth() {
    const player = livePlayer();
    if (!booths.length || !player || player.getVehicle()) {
      return false;
    }
    const position = player.position;
    return booths.some(
      (booth) => flatDistance(booth, position) <= reach && Math.abs(booth.y - position.y) <= 2.5
    );
  }
  onServer("fr:police", (data) => {
    switch (data.action) {
      case "text":
        Hud.showGameText(data.textId);
        break;
      case "wanted":
        Hud.setWanted(data.level ?? 0);
        break;
      case "fine":
        Sound.play("01b_srdce.wav", { volume: 0.5 });
        break;
      case "arrest":
        Hud.setWanted(0);
        Fade.out(1);
        setTimeout(() => Fade.in(1), 3500);
        break;
      case "booths":
        reach = Number(data.reach) || 0;
        booths = Array.isArray(data.booths) ? data.booths : [];
        break;
      case "sound":
        if (typeof data.wave === "string") {
          Sound.play(data.wave, { volume: Number(data.volume) || 0.8 });
        }
        break;
    }
  });
  Settings.describeKey(CALL_KEY, "Police: call for backup at a phone booth");
  Key.bind(CALL_KEY, () => {
    if (atBooth()) {
      events.emitServer("fr:backup:call", {});
    }
  });
  events.on("render", () => {
    if (atBooth()) {
      drawPrompt(`${keyHint(CALL_KEY)} CALL THE PRECINCT FOR BACKUP`, { rise: 96, width: 380 });
    }
  });
  events.on("missionUnload", () => {
    booths = [];
  });

  // src/client/rescue.ts
  var JOB_KEY = "g";
  var PING_MS = 250;
  var BAR_FILL = 4287611514;
  var BAR_TRACK = 2150644288;
  var text = null;
  var progress = null;
  var holding = false;
  var pingAt = 0;
  Settings.describeKey(JOB_KEY, "Job key: sign on, use, treat, plant, pick up");
  Key.bind(JOB_KEY, "both", (_key, state) => {
    holding = state === "down";
    pingAt = Date.now();
    events.emitServer("fr:rescue:use", { down: holding });
  });
  onServer("fr:rescue", (data) => {
    text = typeof data.text === "string" ? data.text : null;
    progress = typeof data.progress === "number" ? clamp(data.progress, 0, 1) : null;
  });
  events.on("render", () => {
    if (holding && Date.now() - pingAt >= PING_MS) {
      pingAt = Date.now();
      if (Key.isDown(JOB_KEY)) {
        events.emitServer("fr:rescue:use", { down: true });
      } else {
        holding = false;
        events.emitServer("fr:rescue:use", { down: false });
      }
    }
    if (!text) {
      return;
    }
    const { center, top } = drawPrompt(text.replace("[G]", keyHint(JOB_KEY)), {
      rise: 140,
      width: 460,
      height: progress === null ? 36 : 50,
      accent: true
    });
    if (progress !== null) {
      Draw.rect(center - 200, top + 36, 400, 6, BAR_TRACK);
      Draw.rect(center - 200, top + 36, Math.round(400 * progress), 6, BAR_FILL);
    }
  });
  events.on("missionUnload", () => {
    text = null;
    progress = null;
    holding = false;
  });

  // src/client/jail.ts
  var counting = false;
  onServer("fr:jail", (state) => {
    if (state.active) {
      if (state.seconds !== void 0 && Number.isInteger(state.seconds) && state.seconds > 0) {
        Hud.startCountdown(state.seconds);
        counting = true;
      }
      return;
    }
    counting = false;
    Hud.hideWatch();
    Hud.announce("CELL OPEN", 2);
  });
  events.on("countdownEnd", () => {
    if (!counting) {
      return;
    }
    counting = false;
    Hud.hideWatch();
  });
  onStop(() => Hud.hideWatch());

  // src/client/distillery.ts
  var REVEAL_HOLD_MS = 4200;
  var WHITE = 16777215;
  var revealing = false;
  var shot = new Timers();
  function endReveal(seconds = 0.6) {
    if (!revealing) {
      return;
    }
    shot.clear();
    revealing = false;
    Camera.unlock();
    Fade.in(seconds);
  }
  onServer("fr:rum:reveal", (data) => {
    const { position, target, title } = data;
    if (!position || !target) {
      return;
    }
    endReveal(0);
    revealing = true;
    Fade.out(0.5);
    shot.after(600, () => {
      if (!revealing) {
        return;
      }
      if (!Camera.lock(position, toward(position, target))) {
        endReveal();
        return;
      }
      Fade.in(0.8);
      if (typeof title === "string") {
        Hud.announce(title, 3);
      }
    });
    shot.after(600 + REVEAL_HOLD_MS, () => {
      if (!revealing) {
        return;
      }
      Fade.out(0.4);
      shot.after(450, () => endReveal());
    });
  });
  onServer("fr:rum:flash", (data) => {
    const strength = typeof data.strength === "number" ? clamp(data.strength, 0, 1) : 1;
    Fade.out(0.05, WHITE);
    setTimeout(() => Fade.in(0.4 + strength, WHITE), 80);
  });
  onLocalDeath(() => endReveal(0));
  events.on("missionUnload", () => endReveal(0));
  onStop(() => endReveal(0));

  // src/client/housing.ts
  var DECOR_KEYS = [
    ["end", { turn: 1 }, "Decorating: turn right"],
    ["home", { turn: -1 }, "Decorating: turn left"],
    ["numpad6", { turn: 1 }, "Decorating: turn right (2nd)"],
    ["numpad4", { turn: -1 }, "Decorating: turn left (2nd)"],
    ["pageup", { lift: 1 }, "Decorating: raise"],
    ["pagedown", { lift: -1 }, "Decorating: lower"],
    ["numpad8", { lift: 1 }, "Decorating: raise (2nd)"],
    ["numpad2", { lift: -1 }, "Decorating: lower (2nd)"],
    ["delete", { sell: true }, "Decorating: sell the piece back"],
    ["numpad0", { cancel: true }, "Decorating: put back or finish"]
  ];
  var decorating = false;
  onServer("fr:home:edit", (data) => {
    decorating = Boolean(data.on);
  });
  for (const [key, request, label] of DECOR_KEYS) {
    Settings.describeKey(key, label);
    Key.bind(key, () => {
      if (decorating) {
        events.emitServer("fr:home:edit", request);
      }
    });
  }
  events.on("missionUnload", () => {
    decorating = false;
  });
})();
