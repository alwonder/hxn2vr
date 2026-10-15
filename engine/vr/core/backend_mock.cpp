// backend_mock.cpp -- fake headset: fixed eye height, optional yaw sway, paced to mockHz.
#include "backend.hpp"

#include <chrono>
#include <cmath>
#include <thread>

namespace hxvr {
namespace {

constexpr int   kBaseEyeW = 1440;
constexpr int   kBaseEyeH = 1584;
constexpr float kIpd = 0.064f;
constexpr float kHalfFov = 0.785f;
constexpr double kSwayDeg = 20.0;
constexpr double kSwayPeriodS = 4.0;
constexpr double kPi = 3.14159265358979323846;

class MockBackend final : public Backend {
public:
	explicit MockBackend(const GlExt* gl) : gl_(gl) {}

	bool start(const StartParams& p, std::string& err) override {
		if (!(p.renderScale > 0.0f) || !(p.eyeHeightM > 0.0f)) {
			err = "mock backend: render scale and eye height must be positive";
			return false;
		}
		params_ = p;
		size_ = glm::ivec2(scaled(kBaseEyeW, p.renderScale), scaled(kBaseEyeH, p.renderScale));
		paced_ = false;
		updatePoses(0.0);
		return true;
	}

	void stop() override {}

	FrameInfo beginFrame(double now) override {
		pace();
		updatePoses(now);
		return FrameInfo{true, true, false};
	}

	Pose headPose() const override { return head_; }
	Pose eyePose(int eye) const override { return eyes_[eye == 0 ? 0 : 1]; }
	Fov eyeFov(int) const override { return Fov{-kHalfFov, kHalfFov, kHalfFov, -kHalfFov}; }
	glm::ivec2 eyeSize() const override { return size_; }

	// Eye textures (GL_RGBA8) are created only when a GlExt is supplied; GlExt has no entry
	// points yet, so texture creation is deferred to the GL task and this returns 0 for now.
	unsigned acquireEye(int) override { return 0; }
	void releaseEye(int) override {}
	void endFrame() override {}

private:
	static int scaled(int base, float scale) { return static_cast<int>(std::lround(base * scale)); }

	void updatePoses(double t) {
		head_.pos = glm::vec3(0.0f, params_.eyeHeightM, 0.0f);
		head_.rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		if (params_.mockMotion) {
			const double yaw = kSwayDeg * std::sin(2.0 * kPi * t / kSwayPeriodS);
			head_.rot = glm::angleAxis(glm::radians(static_cast<float>(yaw)), glm::vec3(0.0f, 1.0f, 0.0f));
		}
		for (int i = 0; i < 2; ++i) {
			const float x = (i == 0 ? -0.5f : 0.5f) * kIpd;
			eyes_[i] = Pose{head_.pos + head_.rot * glm::vec3(x, 0.0f, 0.0f), head_.rot};
		}
	}

	// Sleeps until the next frame slot of mockHz. The first frame after start() is immediate.
	void pace() {
		if (!(params_.mockHz > 0.0f)) return;
		using clock = std::chrono::steady_clock;
		const auto period = std::chrono::duration_cast<clock::duration>(
			std::chrono::duration<double>(1.0 / params_.mockHz));
		const auto tnow = clock::now();
		if (!paced_ || next_ + period < tnow) next_ = tnow;   // first frame, or fell behind: no catch-up burst
		paced_ = true;
		std::this_thread::sleep_until(next_);
		next_ += period;
	}

	const GlExt* gl_;
	StartParams params_{1.0f, 1.65f, false, 0.0f};
	glm::ivec2 size_{kBaseEyeW, kBaseEyeH};
	Pose head_{glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
	Pose eyes_[2] = {head_, head_};
	bool paced_ = false;
	std::chrono::steady_clock::time_point next_{};
};

}  // namespace

std::unique_ptr<Backend> makeMockBackend(const GlExt* gl) { return std::make_unique<MockBackend>(gl); }

}  // namespace hxvr
