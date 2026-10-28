// Shows what server/police.js decides: the retail police lines, the wanted
// badge, the fine and the arrest fade.

Events.on("sample:police", (data) => {
    switch (data?.action) {
    case "text":
        Hud.showGameText(data.textId);
        break;
    case "wanted":
        Hud.setWanted(data.level ?? 0);
        break;
    case "fine":
        Hud.addScore(-(data.points ?? 0));
        break;
    case "arrest":
        Hud.setWanted(0);
        Fade.out(1);
        setTimeout(() => Fade.in(1), 3500);
        break;
    }
});
