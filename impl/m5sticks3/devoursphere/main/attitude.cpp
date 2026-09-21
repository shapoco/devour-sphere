// Tilt as a gamepad (see attitude.hpp for the axes and the idea).

#include "attitude.hpp"

#include <M5Unified.h>

#include <cmath>

#include "devoursphere/sim/entities.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"

namespace ds {

namespace {

namespace sim = devoursphere::sim;

// The BMI270 as it is mounted in this case, mapped onto the screen axes of
// attitude.hpp (x = screen right, y = screen down, z = into the screen)
// while the panel is in its native portrait orientation.
//
// M5Unified applies a per-board axis fix for a handful of boards and none
// for this one, so what getAccel() returns is the sensor's own frame and
// this mapping is a fact about the hardware. It cannot be derived from
// gravity alone -- gravity says which way is down, not which way is right.
//
// **Check this on the device.** The boot screen draws an arrow along the
// gravity this produces: it must point at the floor whichever way the stick
// is held. If the arrow is mirrored or turned, that is the only thing to
// fix here, and the controls follow: a left turn has to give LEFT and
// tipping the far edge away has to give UP (dash).
//
//                         which sensor axis    sign
constexpr int SRC_X = 0, SIGN_X = +1;  // screen right
constexpr int SRC_Y = 1, SIGN_Y = +1;  // screen down
constexpr int SRC_Z = 2, SIGN_Z = +1;  // into the screen

// One pole of exponential smoothing, as a fraction per second rather than
// per sample: the frame rate varies here and the feel of the controls must
// not vary with it.
float lerpRate(float tauSec, float dtSec) {
  if (dtSec <= 0) return 0.0f;
  if (dtSec >= tauSec) return 1.0f;
  return 1.0f - std::exp(-dtSec / tauSec);
}

Vec3f normalize(const Vec3f &v) {
  const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (n < 1e-4f) return {0, 1, 0};
  return {v.x / n, v.y / n, v.z / n};
}

Vec3f cross(const Vec3f &a, const Vec3f &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

// A threshold with hysteresis: on beyond TILT_ON, off below TILT_OFF.
bool held(float v, float on, float off, bool was) {
  return was ? v > off : v > on;
}

}  // namespace

bool Attitude::begin() {
  if (!M5.Imu.isEnabled()) {
    trace("no IMU found", 0);
    return false;
  }
  float a[3] = {0, 0, 0};
  M5.Imu.getAccel(&a[0], &a[1], &a[2]);
  g_ = normalize({SIGN_X * a[SRC_X], SIGN_Y * a[SRC_Y], SIGN_Z * a[SRC_Z]});
  ref_ = g_;
  return true;
}

void Attitude::sample(float dtSec) {
  float a[3] = {0, 0, 0};
  M5.Imu.getAccel(&a[0], &a[1], &a[2]);
  const Vec3f raw = {SIGN_X * a[SRC_X], SIGN_Y * a[SRC_Y], SIGN_Z * a[SRC_Z]};

  // Direction first, magnitude separately: the direction is what the
  // controls read and wants smoothing, the magnitude is what says the
  // device is being shaken and wants the same treatment for the same
  // reason (a single sample crosses any threshold you like).
  const float mag = std::sqrt(raw.x * raw.x + raw.y * raw.y + raw.z * raw.z);
  const float k = lerpRate(FILTER_SEC, dtSec);
  const Vec3f unit = normalize(raw);
  g_ = normalize({g_.x + (unit.x - g_.x) * k, g_.y + (unit.y - g_.y) * k,
                  g_.z + (unit.z - g_.z) * k});
  const float dev = std::fabs(mag - 1.0f);
  motion_ += (dev - motion_) * k;

  // Still, for the boot screen
  if (dev < STILL_TOL && motion_ < STILL_TOL) {
    settledSec_ += dtSec;
  } else {
    settledSec_ = 0;
  }

  // Shaking. It ends only after the device has been quiet for a while, so
  // that the gap between two swings does not count as the end of one shake
  // and the start of another.
  if (motion_ > SHAKE_ON) {
    if (!shaking_) {
      shaking_ = true;
      shakePending_ = true;  // one pause per shake, taken by the frame loop
    }
    quietSec_ = 0;
  } else if (motion_ < SHAKE_OFF) {
    quietSec_ += dtSec;
    if (shaking_ && quietSec_ >= SHAKE_QUIET_SEC) shaking_ = false;
  } else {
    quietSec_ = 0;
  }
  if (shaking_) muteSec_ = SHAKE_MUTE_SEC;
  else if (muteSec_ > 0) muteSec_ -= dtSec;

  updateKeys(dtSec);
}

Attitude::Side Attitude::side() const {
  // Tipping the stick so its top goes to the player's RIGHT turns the
  // device clockwise about the view axis, which swings its own +x (the
  // portrait screen's right edge) round to point at the floor -- so
  // gravity comes to lie along +x. The other way round gives -x.
  if (g_.x > SIDE_MIN) return Side::TOP_RIGHT;
  if (g_.x < -SIDE_MIN) return Side::TOP_LEFT;
  return Side::UNKNOWN;
}

int Attitude::confirm() {
  const Side s = side();
  if (s == Side::UNKNOWN) return 0;
  ref_ = g_;
  haveRef_ = true;
  keys_ = 0;
  // The landscape picture's own left-right axis, in portrait axes. With the
  // stick's top to the player's right, world-right is where the portrait
  // screen's UP used to point, which is -y; the other way round it is +y.
  // Its down axis is gravity either way, which is what makes the picture
  // upright. This sign is the only thing that differs between the two.
  rightSign_ = (s == Side::TOP_RIGHT) ? -1 : +1;
  return s == Side::TOP_RIGHT ? ROTATION_TOP_RIGHT : ROTATION_TOP_LEFT;
}

bool Attitude::takeShake() {
  const bool s = shakePending_;
  shakePending_ = false;
  return s;
}

void Attitude::updateKeys(float dtSec) {
  if (!haveRef_) {
    keys_ = 0;
    return;
  }
  if (muteSec_ > 0) {
    // Shaken: no direction keys, and no drift either -- the neutral
    // attitude must not follow the device through the shake.
    keys_ = 0;
    return;
  }

  // c points along the axis the device was turned about since the neutral
  // attitude was taken, and |c| is the sine of how far. See attitude.hpp.
  const Vec3f c = cross(ref_, g_);

  // Turning about the view axis (z, into the screen) tips the picture: a
  // turn that carries the picture's top to the LEFT is anti-clockwise seen
  // by the player, which is negative about z, which leaves +c.z. This one
  // does not depend on which way the stick was tipped -- the view axis is
  // the view axis either way.
  const bool wasLeft = (keys_ & sim::Button::LEFT) != 0;
  const bool wasRight = (keys_ & sim::Button::RIGHT) != 0;
  const bool left = held(c.z, TILT_ON, TILT_OFF, wasLeft);
  const bool right = held(-c.z, TILT_ON, TILT_OFF, wasRight);

  // Turning about the picture's own left-right axis tips its far edge away
  // (dash) or near (brake). That axis is the portrait screen's y axis, up
  // to the sign confirm() worked out, and tipping the far edge away is a
  // positive turn about it -- which leaves a NEGATIVE component along it.
  const float fwd = -(float)rightSign_ * c.y;
  const bool wasUp = (keys_ & sim::Button::UP) != 0;
  const bool wasDown = (keys_ & sim::Button::DOWN) != 0;
  const bool up = held(fwd, TILT_ON, TILT_OFF, wasUp);
  const bool down = held(-fwd, TILT_ON, TILT_OFF, wasDown);

  keys_ = (uint8_t)((left ? sim::Button::LEFT : 0) |
                    (right ? sim::Button::RIGHT : 0) |
                    (up ? sim::Button::UP : 0) | (down ? sim::Button::DOWN : 0));

  // With nothing held, let the neutral attitude follow the player's
  // posture, slowly. A deliberate tip holds a key and stops this, so it can
  // only ever creep towards where the stick rests between turns.
  if (keys_ == 0) {
    const float step = DRIFT_RAD_PER_SEC * dtSec;
    ref_ = normalize({ref_.x + (g_.x - ref_.x) * step,
                      ref_.y + (g_.y - ref_.y) * step,
                      ref_.z + (g_.z - ref_.z) * step});
  }
}

}  // namespace ds
