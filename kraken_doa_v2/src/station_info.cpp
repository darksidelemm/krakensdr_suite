#include "station_info.hpp"
#include "globals.hpp"
#include "networking/gpsd_client.hpp"

void StationInfo::setId(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    id_ = id;
}

std::string StationInfo::getId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return id_;
}

void StationInfo::setSource(LocationSource s) {
    std::lock_guard<std::mutex> lock(mutex_);
    source_ = s;
}

LocationSource StationInfo::getSource() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return source_;
}

void StationInfo::setStatic(double lat, double lon, double heading) {
    std::lock_guard<std::mutex> lock(mutex_);
    static_lat_ = lat;
    static_lon_ = lon;
    static_heading_ = heading;
}

void StationInfo::getStatic(double& lat, double& lon, double& heading) const {
    std::lock_guard<std::mutex> lock(mutex_);
    lat = static_lat_;
    lon = static_lon_;
    heading = static_heading_;
}

StationLocation StationInfo::resolve() const {
    StationLocation out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out.id = id_;
        out.source = source_;
        if (source_ == LocationSource::STATIC) {
            out.lat = static_lat_;
            out.lon = static_lon_;
            out.heading = static_heading_;
            out.heading_valid = true;   // user-specified
            out.from_compass = false;
        }
        // MOBILE: leave everything at 0 (phone-app path not implemented).
    }

    if (out.source == LocationSource::GPS) {
        GpsFix f = gps_client.get();
        if (f.has_fix()) {
            out.lat = f.lat;
            out.lon = f.lon;
            out.speed = f.speed;
            out.timestamp_ms = f.timestamp_ms;
        }
        // A compass heading can be valid even without a position fix; otherwise
        // fall back to the movement-derived track. Mirrors GpsFix::heading().
        if (f.has_compass) {
            out.heading = f.compass_heading;
            out.heading_valid = true;
            out.from_compass = true;
        } else if (f.track_valid) {
            out.heading = f.track;
            out.heading_valid = true;
            out.from_compass = false;
        }
    }

    return out;
}
