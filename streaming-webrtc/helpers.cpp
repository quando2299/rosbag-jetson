/**
 * libdatachannel streamer example
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "helpers.hpp"
#include <sys/time.h>
#include <cstddef>

ClientTrackData::ClientTrackData(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sender) : track(track), sender(sender) {}

void Client::setState(State state) {
    std::unique_lock<std::mutex> lock(_mutex);
    this->state = state;
}

Client::State Client::getState() {
    std::unique_lock<std::mutex> lock(_mutex);
    return state;
}

ClientTrack::ClientTrack(std::string id, std::shared_ptr<ClientTrackData> trackData) : id(id), trackData(trackData) {}

uint64_t currentTimeInMicroSeconds() {
	struct timeval time;
	gettimeofday(&time, NULL);
	return uint64_t(time.tv_sec) * 1000 * 1000 + time.tv_usec;
}