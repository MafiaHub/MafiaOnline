// Free Ride changes with the host's local clock. A map transition only
// happens at dawn/dusk; it keeps the correct stock sky and lighting together.
const DAY_MAP = "freeride";
const NIGHT_MAP = "freeridenoc";
const DAWN_HOUR = 6;
const DUSK_HOUR = 19;
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
    const hour = now.getHours();
    const expectedMap = hour >= DAWN_HOUR && hour < DUSK_HOUR ? DAY_MAP : NIGHT_MAP;
    const mission = World.getMission();
    if ([DAY_MAP, NIGHT_MAP].includes(mission) && mission !== expectedMap) {
        for (const player of World.getPlayers()) player.sendMessage("The light is changing. Lost Heaven is turning to its other hour…");
        if (!World.changeMission(expectedMap)) console.error(`[mafia1online-sample] Could not load ${expectedMap}`);
    }

    const weather = weatherAt(now);
    const signature = `${weather.kind}:${weather.intensity}`;
    if (signature === lastWeather) return;
    if (weather.kind === "clear") World.setWeather("clear");
    else World.setWeather(weather.kind, weather.intensity);
    lastWeather = signature;
}

Events.on("resourceStart", (name) => {
    if (name !== "mafia1online-sample") return;
    World.setCityMusic(false); // The car radio owns the music in this gamemode.
    updateAmbience();
    timer = setInterval(updateAmbience, UPDATE_MS);
});

Events.on("resourceStop", (name) => {
    if (name !== "mafia1online-sample" || timer === null) return;
    clearInterval(timer);
    timer = null;
});
