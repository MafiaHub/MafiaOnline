// Free Ride, the default Mafia1Online gamemode. This entry owns the player
// lifecycle, the admin motor pool, the everyday commands, the menu plumbing
// and the "fr" console command; each activity lives in its own module next
// to it. Shared rules are in lib/.
//
// The car lot loads first, as it always has: its handlers (a parked car
// written to the record on disconnect, for one) run before main's.
import '../dealership';
import './players';
import './cars';
import './menu';
import './commands';
import './console';
