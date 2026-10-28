// Shows what server/police.js decides: the retail police lines, the wanted
// badge and the arrest fade. Fines and bail come out of the cash balance,
// which the score counter mirrors.
Events.on("fr:police", (data) => {
    switch (data?.action) {
    case "text":
        Hud.showGameText(data.textId);
        break;
    case "wanted":
        Hud.setWanted(data.level ?? 0);
        break;
    case "fine":
        Sound.play("01b_srdce.wav", {volume: 0.5});
        break;
    case "arrest":
        Hud.setWanted(0);
        Fade.out(1);
        setTimeout(() => Fade.in(1), 3500);
        break;
    }
});
