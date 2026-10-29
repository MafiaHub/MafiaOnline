const {onStart, onStop} = require("./lib/util.js");

// Weather fronts over the city. Which map plays, always by day, is
// map_vote.js's business.
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
