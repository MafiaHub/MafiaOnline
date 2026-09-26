# From Mafia 1 script commands to Mafia1Online scripts

The stock game executes mission commands through `C_program`. In a
server-managed mission, resource scripts use the APIs below instead. The
server owns shared world state and inventory; camera and HUD calls act on the
client running them. Use the [event bridge](server_scripting_api.md#events) to
ask one client to show a local effect.

| Stock command or behavior | Resource API | Where it runs |
| --- | --- | --- |
| `HUMAN_ADDWEAPON`, `HUMAN_DELWEAPON`, `HUMAN_HOLSTER` | `Player.giveWeapon`, `Player.removeWeapon`, `Player.setCurrentWeapon(0)` | Server |
| Native weapon drop, `FRM` weapon pickup | `Player.dropWeapon`, `Pickup.create`, `Pickup.getAll` | Server; replicated to clients |
| `CITYMUSIC_ON`, `CITYMUSIC_OFF` | `World.setCityMusic` | Server; replicated to clients |
| Native night mode | `World.setNightMode`, `World.getNightModeOverride` | Server; replicated to clients |
| `WEATHER_SETPARAM`, `WEATHER_RESET` | `World.setWeather` | Server; replicated to clients |
| Mission object visibility | `World.setFrameVisible`, `World.resetFrames` | Server; replicated to clients |
| `FRM_SETALPHA` | `World.setFrameOpacity`, `World.resetFrameOpacity` | Server; replicated to clients |
| `MODEL_CREATE`, `MODEL_DESTROY`, `FRM_SETPOS`, `FRM_SETROT`, `FRM_SETSCALE` | `Scene.createModelFrame`, `Frame.destroy`, `Frame.setWorldPosition`, `Frame.setRotation`, `Frame.setScale` | Client-only visuals |
| Named mission frame lookup | `World.findWorldFrame`, `Frame.getWorldPosition`, `Frame.getWorldDirection` | Client; read-only native scene frame |
| Native HUD text and scene projection | `Draw.text`, `Draw.worldText`, `Scene.projectWorld` | Client-only overlays |
| `EXPLOSION`, fire effect | `World.createExplosion`, `World.createFire` | Server; broadcast with server damage |
| `SOUND` and positional sound effects | `Sound.create`, `Sound.play` | Server; replicated or broadcast |
| Native car damage state | `Vehicle.setMechanicalDamage`, `Vehicle.setDamage` | Server; controller applies native state |
| `CAR_REPAIR` damage and deformation reset | `Vehicle.repair` | Server; replicated to clients |
| `C_car::SetTransparency` | `Vehicle.setOpacity` | Server; replicated to clients |
| Car lights, engine and fuel | `Vehicle.setLights`, `Vehicle.setEngine`, `Vehicle.setFuel` | Server; controller applies native state |
| `TIMERON`, `TIMEROFF`, freeride score | `Hud.startCountdown`, `Hud.hideWatch`, `Hud.setScore`, `Hud.addScore` | Client |
| `SETCOMPASS`, console text, race flash text | `Hud.setCompassTarget`, `Hud.showMessage`, `Hud.announce` | Client |
| `ZATMYSE` fade | `Fade.out`, `Fade.in` | Client |
| `CAMERA_SETSWING` | `Camera.setSwing` | Client |
| Camera field of view and range | `Camera.getFov`, `Camera.setFov`, `Camera.setRange` | Client |
| `CAMERA_LOCK`, `CAMERA_UNLOCK` | `Camera.lock`, `Camera.unlock` | Client |

Stock `SETCITYTRAFFICVISIBLE` remains disabled in multiplayer: the retail
traffic simulation would create independent NPC actors on each client. Native
`CAR_INVISIBLE` changes collision and entry protection, while
`Vehicle.setOpacity` controls the model's transparency. Native `CAR_REPAIR`
also rights and restarts a car; `Vehicle.repair` restores damage and shape
while preserving its seat and motion state.

The [server guide](server_scripting_api.md), [client guide](client_scripting_api.md)
and [generated reference](../scripting-api/README.md) contain signatures, accepted
values and event behavior.
