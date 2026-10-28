const {onStart, onStop} = require("./lib/util.js");
const {toastAll} = require("./lib/notify.js");

// Free Ride always plays the stock day map. The night map is loaded only if
// a server is configured with it; this module then moves to day, so the
// sky, lighting and the gamemode's recorded positions stay together.
const DAY_MAP = "freeride";
const NIGHT_MAP = "freeridenoc";
const UPDATE_MS = 60_000;
const WEATHER_FRONT_HOURS = 2;

let timer = null;
let lastWeather = "";

function weatherSeed(date, front) {
    let value = date.getFullYear() * 10000 + (date.getMonth() + 1) * 100 + date.getDate();
    value = Math.imul(value ^ front, 0x45d9f3b);
    value = Math.imul(value ^ (value >>> 16), 0x45d9f3b);
    return (value ^ (value >>> 16)) >>> 0;
}

function weatherAt(date) {
    const hour = date.getHours() + date.getMinutes() / 60;
    const front = Math.floor(hour / WEATHER_FRONT_HOURS);
    const progress = (hour % WEATHER_FRONT_HOURS) / WEATHER_FRONT_HOURS;
    const seed = weatherSeed(date, front);
    const month = date.getMonth();
    const winter = month === 11 || month <= 1;
    const wet = seed % 100 < (winter ? 46 : 34);
    if (!wet) return {kind: "clear", intensity: 0};
    // Each front arrives and passes gradually; the native weather particle
    // count follows the same arc on every connected client.
    const envelope = Math.sin(Math.PI * progress);
    const peak = 35 + ((seed >>> 8) % 41);
    const intensity = Math.max(0, Math.min(100, Math.round(peak * envelope / 5) * 5));
    const kind = winter && (seed >>> 16) % 4 === 0 ? "snow" : "rain";
    return {kind, intensity};
}

function updateAmbience() {
    const now = new Date();
    if (World.getMission() === NIGHT_MAP) {
        toastAll("Lost Heaven moves to daylight.", {title: "Free Ride", tone: "gold", seconds: 6});
        if (!World.changeMission(DAY_MAP)) console.error(`[freeride] Could not load ${DAY_MAP}`);
    }

    const weather = weatherAt(now);
    const signature = `${weather.kind}:${weather.intensity}`;
    if (signature === lastWeather) return;
    if (weather.kind === "clear") World.setWeather("clear");
    else World.setWeather(weather.kind, weather.intensity);
    lastWeather = signature;
}

onStart(() => {
    World.setCityMusic(false); // The car radio owns the music in this gamemode.
    updateAmbience();
    timer = setInterval(updateAmbience, UPDATE_MS);
});

onStop(() => {
    if (timer !== null) clearInterval(timer);
    timer = null;
});
