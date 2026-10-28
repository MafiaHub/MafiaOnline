// The stock car catalog and what each car costs in Free Ride. IDs and
// default-color models are from patched retail tables/carindex.def in a8.dta;
// the client reads that table for display names and sends only an ID, and the
// server chooses the model. Prices are this gamemode's, by class.
const MODELS_BY_ID = [
    null,
    "fordttud00.i3d", "fordtto00.i3d", "fordtru00.i3d", "fordtpi00.i3d", "fordtfor00.i3d", "fordtco00.i3d",
    "foratu00.i3d", "foraro00.i3d", "forapic00.i3d", "forafo00.i3d", "forade00.i3d", "foracou00.i3d", "foraca00.i3d",
    "chev00.i3d", "forvco00.i3d", "forvfor00.i3d", "forvro00.i3d", "forvto00.i3d", "forvtud00.i3d",
    "chemafor00.i3d", "chematud00.i3d", "blackha00.i3d", "taxi00.i3d", "pontfor00.i3d", "ponttud00.i3d",
    "hudcou00.i3d", "hudfor00.i3d", "hudtu00.i3d", "cordca00.i3d", "cordph00.i3d", "cordse00.i3d",
    "buicou00.i3d", "buikfor00.i3d", "speedster00.i3d", "merced500k00.i3d", "cad_ford00.i3d", "cad_phaeton00.i3d",
    "cad_road00.i3d", "arrow00.i3d", "hartmann00.i3d", "phantom00.i3d", "deusejco00.i3d", "bugatti00.i3d",
    "miller00.i3d", "duesenberg00.i3d", "alfa8c00.i3d", "ambulance00.i3d", "fire00.i3d", "hearsea00.i3d",
    "hearseca00.i3d", "airflfor00.i3d", "airfltud00.i3d", "polcad00.i3d", "poli00.i3d", "polimfor00.i3d",
    "polimtud00.i3d", "trucka00.i3d", "truckb00.i3d", "alfa00.i3d", "thunderbird00.i3d", "TruckBx00.i3d",
    "fordhot00.i3d", "buigang00.i3d", "black00.i3d", "lowgas00.i3d", "blackdragon00.i3d", "cord_sedanh00.i3d",
    "flamer00.i3d", "fordapick00.i3d", "fordapicktaxi00.i3d", "fordth00.i3d", "fthot00.i3d", "hotrodp200.i3d",
    "hotrodp300.i3d", "hotrodp400.i3d", "hotrodp500.i3d", "chevroletm6h00.i3d", "tbirdold00.i3d",
    "fordadelh00.i3d", "hotrodp600.i3d", "phantomtaxi00.i3d",
];

// First match wins; police cars are not for sale.
const PRICE_CLASSES = [
    [/^(polcad|poli|polim)/, null],
    [/^fordt(tud|to|ru|pi|for|co)/, 450],
    [/^fora(tu|ro|pic|fo|de|cou|ca)/, 750],
    [/^fordapick00/, 900],
    [/^(chev00|chema)/, 1100],
    [/^(forv|pont)/, 1400],
    [/^(taxi|fordapicktaxi|phantomtaxi)/, 1300],
    [/^(hud|bui[ck]|cord(ca|ph|se)|blackha|airfl)/, 2200],
    [/^(truck|ambulance|fire|hearse)/i, 2600],
    [/^(arrow|hartmann|cad_|merced500k|phantom00)/, 4200],
    [/^(speedster|bugatti|miller|duesenberg|deusejco|alfa8c|alfa00)/, 9000],
];
const SPECIAL_PRICE = 6500;

const CATALOG = MODELS_BY_ID.map((model, id) => {
    if (!model) return null;
    const match = PRICE_CLASSES.find(([pattern]) => pattern.test(model.toLowerCase()));
    return {id, model, price: match ? match[1] : SPECIAL_PRICE};
}).filter(Boolean);
const BY_MODEL = new Map(CATALOG.map((entry) => [entry.model.toLowerCase(), entry]));

function byId(id) {
    return CATALOG.find((entry) => entry.id === id) ?? null;
}

function byModel(model) {
    return BY_MODEL.get(String(model).toLowerCase()) ?? null;
}

function forSale() {
    return CATALOG.filter((entry) => entry.price !== null);
}

module.exports = {CATALOG, byId, byModel, forSale};
