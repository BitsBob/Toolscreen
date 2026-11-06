#include "ninjabrain_data.h"

#include "common/atomic_shared_ptr.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace {

AtomicSharedPtr<const NinjabrainData> g_ninjabrainDataSnapshot{
	std::make_shared<const NinjabrainData>()
};
std::mutex g_ninjabrainDataWriteMutex;

} // namespace

std::shared_ptr<const NinjabrainData> GetNinjabrainDataSnapshot() {
	return g_ninjabrainDataSnapshot.load(std::memory_order_acquire);
}

void PublishNinjabrainData(NinjabrainData data) {
	std::lock_guard<std::mutex> lock(g_ninjabrainDataWriteMutex);
	if (data.lastUpdateTime == std::chrono::steady_clock::time_point{}) {
		data.lastUpdateTime = std::chrono::steady_clock::now();
	}
	g_ninjabrainDataSnapshot.store(std::make_shared<const NinjabrainData>(std::move(data)), std::memory_order_release);
}

void ModifyNinjabrainData(const std::function<void(NinjabrainData&)>& modifier) {
	std::lock_guard<std::mutex> lock(g_ninjabrainDataWriteMutex);

	const auto current = g_ninjabrainDataSnapshot.load(std::memory_order_acquire);
	auto next = std::make_shared<NinjabrainData>(current ? *current : NinjabrainData{});
	modifier(*next);
	next->lastUpdateTime = std::chrono::steady_clock::now();

	g_ninjabrainDataSnapshot.store(std::shared_ptr<const NinjabrainData>(std::move(next)), std::memory_order_release);
}
