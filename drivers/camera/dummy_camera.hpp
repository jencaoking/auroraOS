#ifndef AURORA_DUMMY_CAMERA_HPP
#define AURORA_DUMMY_CAMERA_HPP

#include "camera_device.hpp"

namespace auroraos {
namespace camera {

class DummyCamera : public CameraDevice {
public:
    DummyCamera();
    ~DummyCamera() override;

    bool init(uint16_t width, uint16_t height, PixelFormat fmt) override;
    void set_frame_callback(FrameCallback callback, void* user_data) override;
    void start_capture() override;
    void stop_capture() override;

    // Simulate an interrupt or timer tick arriving
    void simulate_frame_arrival();

private:
    uint16_t width_;
    uint16_t height_;
    PixelFormat format_;
    bool capturing_;
    uint32_t frame_count_;
    
    gpu::Surface* frame_surface_;
    
    FrameCallback callback_;
    void* user_data_;
};

} // namespace camera
} // namespace auroraos

#endif // AURORA_DUMMY_CAMERA_HPP
