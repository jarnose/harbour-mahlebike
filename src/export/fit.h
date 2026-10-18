#pragma once

#include "ride/ride.h"

#include <cstdint>
#include <string>
#include <vector>

// A recorded ride as a FIT activity file, the way harbour-suuntosync writes one (src/fit/fitencoder.cpp, the
// same author): the container is the one that app's decoder reads and that Garmin Connect, Strava and
// others accept, and the field numbers of `record`, `lap` and `session` are the ones in
// muktihari/fit's generated profile.
//
// What a ride of a Mahle bike adds:
//  - sport 2 (cycling), sub-sport 28 (e_bike_fitness);
//  - record fields 118 `ebike_battery_level` (the charge, %) and 119 `ebike_assist_mode` (the assist level),
//    which are the profile's own e-bike fields;
//  - `power` only where the bike reports the rider's own power; the motor's electrical power is not what
//    a head unit means by power, so it is not written there;
//  - manufacturer 255 (development) and a serial number made from the bike's VIN.
//
// A reading the ride does not have is written as its base type's invalid value, as in the original, so that
// every message of a kind shares one definition. The altitude is the cleaned one (Ride::cleanAltitude).
namespace Fit {

// Empty if the ride has fewer than two samples.
std::vector<uint8_t> encodeRide(const std::vector<Ride::Sample> &samples, const std::string &vin);

} // namespace Fit
