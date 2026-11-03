// Generated from the stock scenes: every house Free Ride sells, with its door and kerb.
// Rows: [id, tier, model, door x, y, z, heading out of the door, parking x, y, z, heading]
// (parking only for flats, villas and houses). The id is the building's model and its placement
// (x, z rounded), so a row names the building it belongs to.
//
// How the rows were found, all from a1.dta/a2.dta and the Freeride Extended package:
//  * Buildings are the model placements of MISSIONS\FREERIDE\cache.bin (and the mod's
//    cache.bin, FREEKRAJINA's scene2.bin for the village), composed into world space as
//    tools/export_skyline.py does (I3D_frame::UpdateWMatrixProc, S_matrix::SetRot3).
//  * Doors of the tenements and apartment houses are the quads of each model textured
//    with a door bitmap (%KRDVR, %NR6DVR, %NYDVR, %REDVERE, %ZZDVR2, 3DVERE*, 5DVERE04,
//    5RADO4DV, @DVER04, %KIDVR*, %PCDVR, %KLDVR, 0TDVERE6, D3DVERE2): their bottom edge,
//    turned to face the nearest road of data/roads_<map>.js. The villas, the brick houses
//    (#rad042), 4build76 and 4build163 paint their door into the facade; those doors were
//    read off textured elevations of the model (local door centre per model).
//  * The marker stands 1 m out of the door, at the height of the highest upward-facing
//    stock surface there (pavement, stoop or porch) within 0.7 m of the door sill (1.3 m
//    for the painted doors, whose sill height was read off the elevation), with nothing
//    overhead below 2.3 m and, for the textured doors, pavement continuing 1.5 m out.
//  * Parking: within 16 m of the door and 1.5 m of its height (3 m below the stoops of the
//    stone townhouses), level within 0.35 m over a car's length and width, nothing
//    overhead below 2.3 m, at least 6 m from any other home's spot: on the house's side
//    of a road line at the door's own level (not an overpass), up to 3.2 m toward the
//    kerb, or else on the forecourt or drive in front of the door (villas and houses).
//  * City rows were checked on both city scenes (stock FREERIDE and freeride_extended:
//    same placement, marker within 0.3 m and 0.15 m in height); the village rows belong
//    to one scene each. City rooms and flats stand at least 50 m apart, villas and
//    houses 10 m, the village rows 15 m or more (side by side on a street); all keep
//    clear of the gamemode's own doors (Salieri's, the jail, the hospital, Morello's,
//    Sarah's).
module.exports = {
    // Lost Heaven, on both city maps.
    city: [
        ["#rad042@-2434,221", "rich", "#rad042", -2431.84, -4.28, 215.2, -3.142, -2431.84, -5.13, 209.7, -1.571],
        ["0viil@929,-131", "rich", "0viil", 930.37, 120.76, -125.29, 0, 930.37, 118.35, -109.9, 1.571],
        ["0vila@786,-259", "rich", "0vila", 791.24, 112.98, -254.44, 1.571, 796.74, 112.73, -254.44, -3.141],
        ["9vila2@1116,-5", "rich", "9vila2", 1109.51, 119.17, -11.3, -2.325, 1105.5, 119.25, -15.07, -0.754],
        ["vilap@845,-121", "rich", "vilap", 848.62, 116.51, -120.85, 1.571, 854.12, 117.01, -120.85, -3.141],
        ["#rad042@472,-238", "rich", "#rad042", 478.06, 47.72, -235.17, 1.571, 483.56, 47.14, -235.17, -3.141],
        ["0viil@929,-143", "rich", "0viil", 930.37, 120.76, -136.69, 0, 930.37, 118.72, -131.19, 1.571],
        ["#rad042@-2466,221", "rich", "#rad042", -2463.55, -4.07, 215.05, -3.142, -2463.55, -4.88, 209.55, -1.571],
        ["#rad042@341,-314", "rich", "#rad042", 346.81, 38.24, -311.37, 1.571, 352.31, 37.92, -311.37, -3.141],
        ["#rad042@338,-198", "rich", "#rad042", 344.19, 43.75, -195.3, 1.558, 349.69, 43.03, -195.23, 3.129],
        ["#rad042@-2588,165", "rich", "#rad042", -2590.29, 1.75, 170.92, -0.013, -2594.87, 0.25, 177.36, 1.558],
        ["#rad042@280,-299", "rich", "#rad042", 277.75, 35.26, -292.84, 0, 277.75, 34.74, -287.34, 1.571],
        ["#rad042@269,-325", "rich", "#rad042", 263.26, 33.61, -327.27, -1.571, 257.76, 32.57, -327.27, -0.0],
        ["#rad042@931,-180", "rich", "#rad042", 924.64, 119.13, -182.46, -1.571, 919.14, 118.21, -182.46, -0.0],
        ["%cinzak2@-2415,-499", "middle", "%cinzak2", -2408.62, -7.06, -492.08, 1.571, -2403.2, -7.13, -492.08, 3.142],
        ["%cinzak3@327,133", "middle", "%cinzak3", 326.17, -1.48, 128.21, 3.142, 336.52, -1.61, 128.2, 0.001],
        ["%obldum1@-556,-402", "middle", "%obldum1", -552.03, 3.29, -395.62, -1.596, -558.5, 3.17, -395.62, 3.142],
        ["%radovk10@-847,-752", "middle", "%radovk10", -841.27, -7.4, -755.92, 2.13, -836.15, -7.45, -758.8, -2.63],
        ["%radovk30@100,-436", "middle", "%radovk30", 95.24, 25.64, -432.59, 0.038, 95.24, 25.5, -418.3, 1.571],
        ["%radovk5@-331,-412", "middle", "%radovk5", -339.19, 13.26, -411.75, -1.571, -342.19, 13.34, -411.75, 3.14],
        ["3rad06@10,-381", "middle", "3rad06", 9.76, 22.63, -377.46, -1.571, 1.07, 22.51, -377.47, -0.001],
        ["3rad10@-156,-436", "middle", "3rad10", -156.35, 19.49, -429.63, 0, -156.37, 19.44, -418.55, 1.569],
        ["4build38z@-1197,236", "middle", "4build38z", -1187.43, -5.95, 228.47, 2.47, -1187.42, -6.06, 221.5, -1.572],
        ["4build40@-295,-457", "middle", "4build40", -285.09, 16.5, -462.4, 3.142, -281, 16.46, -462.4, -3.141],
        ["4build46@-884,457", "middle", "4build46", -884.2, 1.1, 447.91, -2.301, -894.7, 1.05, 440.24, 2.511],
        ["4build56@-493,-488", "middle", "4build56", -489.88, 5.98, -483.36, 0, -489.87, 5.93, -476.58, 1.572],
        ["4build76@-1779,-330", "middle", "4build76", -1774.05, -5.29, -330.26, 2.975, -1772.04, -5.37, -334.66, 1.143],
        ["%cinzak2@547,309", "middle", "%cinzak2", 540.36, -5.47, 301.83, -1.571, 534.7, -5.52, 301.84, 0.001],
        ["%cinzak3@-924,59", "middle", "%cinzak3", -919.25, -7.37, 60.48, 1.279, -914.17, -7.41, 62.26, 2.809],
        ["%obldum1@-1760,258", "middle", "%obldum1", -1752.73, -6.41, 253.72, 0.014, -1752.71, -6.5, 263.66, 1.573],
        ["%radovk10@-232,-675", "middle", "%radovk10", -231.13, 17.36, -668.59, 1.57, -231.13, 17.31, -656.9, 1.571],
        ["%radovk30@-1714,150", "middle", "%radovk30", -1712.3, -5.92, 144.01, -1.591, -1712.29, -5.93, 129.04, 1.57],
        ["%radovk5@253,-3", "middle", "%radovk5", 251.08, -0.84, 4.61, -0.214, 248.64, -0.89, 14.87, 1.338],
        ["3rad06@61,-241", "middle", "3rad06", 61.47, 24.67, -237.88, 1.571, 70.6, 24.65, -237.88, 3.142],
        ["3rad10@-532,125", "middle", "3rad10", -532.28, 6.84, 131.36, 0.023, -532.28, 6.72, 137.5, 1.571],
        ["4build38z@-1387,-244", "middle", "4build38z", -1377.68, -7.04, -234.86, 0.785, -1373.79, -6.92, -230.97, 2.356],
        ["4build40@-468,168", "middle", "4build40", -473.41, 8.33, 156.71, -1.574, -484.5, 8.02, 156.71, 0],
        ["4build46@-578,-284", "middle", "4build46", -583.87, 2.61, -278.93, 0, -583.87, 2.47, -273.43, 1.571],
        ["4build56@126,37", "middle", "4build56", 130.34, 0.74, 33.68, 1.546, 137.44, 0.66, 33.77, -0.013],
        ["4build76@-2024,142", "middle", "4build76", -2018.82, -5.5, 140.32, -3.139, -2018.81, -6.02, 129.19, 1.57],
        ["%cinzak2@494,295", "middle", "%cinzak2", 487.33, -4.89, 301.82, 0, 487.33, -4.94, 304.95, 1.572],
        ["%radovk10@-835,-200", "middle", "%radovk10", -827.64, -7.34, -199.82, 1.571, -822.03, -7.39, -199.83, 0.001],
        ["%radovk5@-491,-678", "middle", "%radovk5", -490.66, 6.17, -669.79, 0.016, -490.55, 6.14, -656.72, 1.579],
        ["3rad06@-649,610", "middle", "3rad06", -652.41, 10.35, 608.94, 3.139, -652.39, 10.31, 600.38, -1.573],
        ["3rad10@-62,-436", "middle", "3rad10", -62.59, 21.17, -430, 0, -62.63, 21.15, -418.57, 1.568],
        ["4build38z@-446,-418", "middle", "4build38z", -437.43, 9.41, -426.88, 2.358, -437.43, 9.41, -435.2, 1.571],
        ["%radovk21@45,-470", "poor", "%radovk21", 40.31, 22.74, -469.57, 0],
        ["%zzdum2@138,-385", "poor", "%zzdum2", 141.55, 27.98, -396.39, -1.571],
        ["%zzdum3@213,-455", "poor", "%zzdum3", 213.24, 30.65, -452.23, 0.568],
        ["3rad02@-302,344", "poor", "3rad02", -297.67, 15.14, 341.11, 1.547],
        ["3rad03@-780,460", "poor", "3rad03", -780.38, 3.74, 467.42, -0.005],
        ["3rad04@-1799,-75", "poor", "3rad04", -1794.62, -5.98, -78.92, 2.836],
        ["3rad07@-172,-303", "poor", "3rad07", -178.28, 18.67, -298.61, -1.571],
        ["4build142@-106,217", "poor", "4build142", -112.27, 16.42, 219.79, -1.565],
        ["4build163@-1817,407", "poor", "4build163", -1819.19, 15.8, 412.34, 0],
        ["4build29@-616,573", "poor", "4build29", -613.46, 11.06, 568.59, -3.136],
        ["9dum1@-38,-186", "poor", "9dum1", -41.77, 20.27, -188.56, -2.908],
        ["9dum2@207,-342", "poor", "9dum2", 210.63, 31.69, -341.51, 1.572],
        ["9dum8r@-1546,539", "poor", "9dum8r", -1541.54, 16.03, 533.66, -3.142],
        ["9dumch1@-2422,-167", "poor", "9dumch1", -2420.55, -3.58, -170.31, -1.097],
        ["9dumch3@-2251,-109", "poor", "9dumch3", -2248.42, -5.29, -111.22, -1.571],
        ["3rad02@-366,-671", "poor", "3rad02", -367.42, 12.48, -667.25, -0.006],
        ["3rad03@-106,-204", "poor", "3rad03", -100.17, 17.38, -199.74, 0.996],
        ["3rad07@-578,-621", "poor", "3rad07", -571.84, 4.64, -625.11, 1.58],
        ["4build142@-180,456", "poor", "4build142", -182.09, 22.06, 449.92, 3.142],
        ["4build163@31,226", "poor", "4build163", 36.7, 4.75, 228.96, 1.561],
        ["4build29@-4,-433", "poor", "4build29", -6.74, 22.58, -429.05, 0],
        ["9dum1@-2296,178", "poor", "9dum1", -2293.12, -6, 181.61, 0],
        ["9dum2@-837,-677", "poor", "9dum2", -837.85, -6.89, -673.27, -0.008],
        ["9dumch1@-2132,-150", "poor", "9dumch1", -2134.3, -6.05, -153.13, 0.012],
        ["9dumch3@-2121,-95", "poor", "9dumch3", -2119.27, -5.96, -90.55, 0],
        ["3rad02@-174,-621", "poor", "3rad02", -177.81, 18.42, -617.87, -1.571],
        ["3rad03@-213,-418", "poor", "3rad03", -205.44, 18.99, -418.26, 1.571],
        ["3rad07@182,-18", "poor", "3rad07", 184.53, -0.09, -10.55, -0.211],
        ["4build142@227,331", "poor", "4build142", 233.92, -1.28, 328.79, 1.549],
        ["4build163@-1984,200", "poor", "4build163", -1989, -6.08, 197.43, -1.571],
        ["9dum1@-1678,407", "poor", "9dum1", -1674.4, 14.82, 410.69, -0.007],
        ["9dum2@-1846,298", "poor", "9dum2", -1845.24, -5.46, 293.26, -3.141],
        ["3rad02@-209,-511", "poor", "3rad02", -204.74, 19.04, -514.23, 1.571],
        ["3rad03@-171,-213", "poor", "3rad03", -178.39, 18.01, -213.09, -1.571],
        ["4build142@-269,614", "poor", "4build142", -264.6, 21.4, 611.86, -1.571],
        ["4build163@609,339", "poor", "4build163", 614.79, -7.1, 341.11, 1.571],
    ],
    // Lost Heaven Extended only: the village of its countryside.
    extended: [
        ["4build163@-1453,600", "poor", "4build163", -1447.04, 14.12, 600.7, 1.982],
        ["4build163@-1423,654", "poor", "4build163", -1420.91, 14.25, 648.07, -3.042],
    ],
    // The stock countryside only: its village and two log cabins (models/chajda03.4ds).
    krajina: [
        ["4build76@-1508,714", "middle", "4build76", -1512.6, 14.7, 715.72, 0, -1512.6, 14.47, 721.9, -1.571],
        ["4build76@-1488,714", "middle", "4build76", -1493.41, 14.7, 715.84, 0, -1493.41, 14.53, 721.9, -1.571],
        ["4build163@-1453,711", "poor", "4build163", -1455.55, 14.77, 715.96, 0.006],
        ["4build163@-1468,711", "poor", "4build163", -1470.35, 14.64, 716.05, 0.006],
        ["chajda03@-1173,1638", "poor", "chajda03", -1175.6, 141.56, 1632.38, -2.065],
        ["chajda03@181,2899", "poor", "chajda03", 175.48, 91.01, 2897.18, -1.184],
    ],
};
