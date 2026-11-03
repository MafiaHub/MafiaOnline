// The stock characters a new player picks their look from, once, after
// registering (selector/look.ts). Later looks come from play and are kept in
// the record's `model` field.
//
// Stock models from a2.dta; each client also drops any that fail the full
// native skeleton check before offering them. Left out on purpose: the
// undressed TommyNAHAC and the shirtless boxers (Boxer01-04, TommyBOXER);
// uniforms (police, firemen, guards, lifeguards, railwaymen), which jobs
// hand out; cutscene variants (HIGH, BLOOD, corpse); and joke models.
export interface Character {
    name: string;
    file: string;
    role: string;
}

export const CHARACTERS: readonly Character[] = [
    { name: 'Tommy Angelo', file: 'Tommy.i3d', role: 'The Driver' },
    { name: 'Tommy, Hat On', file: 'TommyHAT.i3d', role: 'The Driver' },
    { name: 'Tommy, Coat and Hat', file: 'TommyCOATHAT.i3d', role: 'The Made Man' },
    { name: 'Tommy the Cabbie', file: 'TommyTAXIDRIVER.i3d', role: 'The Cabbie' },
    { name: 'Tommy the Sailor', file: 'TommySAILOR.i3d', role: 'The Sailor' },
    { name: 'Tommy the Racer', file: 'TommyRACER.i3d', role: 'The Racer' },
    { name: 'Paulie', file: 'Paulie.i3d', role: 'The Hothead' },
    { name: 'Paulie, Coat and Hat', file: 'PaulieCOATHAT.i3d', role: 'The Hothead' },
    { name: 'Sam', file: 'Sam.i3d', role: 'The Muscle' },
    { name: 'Sam, Coat and Hat', file: 'SamCOATHAT.i3d', role: 'The Muscle' },
    { name: 'Frank Colletti', file: 'frank.i3d', role: 'The Consigliere' },
    { name: 'Vincenzo', file: 'VincenzoLOW.i3d', role: 'The Armourer' },
    { name: 'Don Salieri', file: 'Salieri2.i3d', role: 'The Don' },
    { name: 'Don Morello', file: 'MorelloLOW.i3d', role: 'The Rival Don' },
    { name: 'Sergio Morello', file: 'Sergio.i3d', role: 'The Enforcer' },
    { name: 'Luigi', file: 'Luigi.i3d', role: 'The Barkeep' },
    { name: 'Lucas Bertone', file: 'Lucas.i3d', role: 'The Car Man' },
    { name: 'Ralph', file: 'Ralph.i3d', role: 'The Mechanic' },
    { name: 'Pepe', file: 'Pepe.i3d', role: 'The Fixer' },
    { name: 'Carlo', file: 'Carlo.i3d', role: 'The Hired Gun' },
    { name: 'Joe', file: 'Joe.i3d', role: 'The Regular' },
    { name: 'Tony', file: 'Tony.i3d', role: 'The Kid' },
    { name: 'Biff', file: 'Biff.i3d', role: 'The Loudmouth' },
    { name: 'Miller', file: 'Miller.i3d', role: 'The Businessman' },
    { name: 'Michelle', file: 'Michelle.i3d', role: 'The Informant' },
    { name: 'Sarah', file: 'Sarah1.i3d', role: "The Bartender's Daughter" },
    { name: 'Sarah, Evening Dress', file: 'Sarah2.i3d', role: "The Bartender's Daughter" },
    { name: 'Berta', file: 'Berta.i3d', role: 'The Landlady' },
    { name: 'Alice', file: 'Alice1.i3d', role: 'The Socialite' },
    { name: 'Morello Soldier', file: 'Gangster01.i3d', role: 'The Rival' },
    { name: 'Morello Gunman', file: 'Gangster02.i3d', role: 'The Rival' },
    { name: 'Morello Heavy', file: 'Gangster03.i3d', role: 'The Rival' },
    { name: 'Morello Driver', file: 'Gangster04.i3d', role: 'The Rival' },
    { name: 'Morello Capo', file: 'Gangster05.i3d', role: 'The Rival' },
    { name: "Salieri's Man", file: 'SalMan03.i3d', role: 'The Family' },
    { name: "Salieri's Lookout", file: 'SalMan04.i3d', role: 'The Family' },
    { name: "Salieri's Driver", file: 'SalMan05.i3d', role: 'The Family' },
    { name: 'Street Tough', file: 'Hoolig02.i3d', role: 'The Outsider' },
    { name: 'Street Punk', file: 'Hoolig01.i3d', role: 'The Outsider' },
    { name: 'Dock Rat', file: 'Hoolig03.i3d', role: 'The Outsider' },
    { name: 'Alley Cat', file: 'Hoolig04.i3d', role: 'The Outsider' },
    { name: 'Corner Boy', file: 'Hoolig05.i3d', role: 'The Outsider' },
    { name: 'Brawler', file: 'Hoolig06.i3d', role: 'The Outsider' },
    { name: 'Free Rider', file: 'FREEgang01.i3d', role: 'The Drifter' },
    { name: 'Road Gang', file: 'FREEgang02.i3d', role: 'The Drifter' },
    { name: 'Private Eye', file: 'Detektiv01.i3d', role: 'The Detective' },
    { name: 'Gumshoe', file: 'Detektiv02.i3d', role: 'The Detective' },
    { name: 'Bookmaker', file: 'Bookmaker01.i3d', role: 'The Bookie' },
    { name: 'Barman', file: 'Barman01.i3d', role: 'The Barkeep' },
    { name: 'Politician', file: 'Politik.i3d', role: 'The Councillor' },
    { name: 'Prosecutor', file: 'Prokur.i3d', role: 'The Prosecutor' },
    { name: 'Gentleman', file: 'HighCivil.i3d', role: 'The Upper Crust' },
    { name: 'Dock Worker', file: 'Delnik01.i3d', role: 'The Worker' },
    { name: 'Labourer', file: 'Delnik02.i3d', role: 'The Worker' },
    { name: 'Foreman', file: 'Delnik03.i3d', role: 'The Worker' },
    { name: 'Lady About Town', file: 'Czena01.i3d', role: 'The Citizen' },
    { name: 'Housewife', file: 'Czena02.i3d', role: 'The Citizen' },
    { name: 'Shopgirl', file: 'Czena06.i3d', role: 'The Citizen' },
    { name: 'Widow', file: 'Czena07.i3d', role: 'The Citizen' },
    { name: 'Seamstress', file: 'Czena08.i3d', role: 'The Citizen' },
    { name: 'Drifter', file: 'Homeless01.i3d', role: 'The Down-and-Out' },
];
