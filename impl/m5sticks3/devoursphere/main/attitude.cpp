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
// this mapping is a fact about the hardware.
//
// **These signs were settled on hardware as a set, and belong to a set.**
// Four things have to come out right together -- the needle below, the
// landscape picture's orientation, left/right, and dash/brake -- and the
// pieces that decide them are shared: side() reads only the x sign, the
// left/right test carries the product of the x and y signs, and the
// dash/brake test carries z and the sign side() hands to rightSign_. Change
// one of these constants and at least two of the four move. The way to
// settle it is to play the game and read all four, which is how the
// starting guess (all +1) was corrected to this on 2026-09-21.
//
// **It carries a reading, not a gravity vector.** At rest an accelerometer
// measures the force holding the device up, so the vector points away from
// the floor -- hence the needle on the boot screen is labelled UP, and the
// names TOP_LEFT / TOP_RIGHT in side() are labels for the two landscape
// rotations rather than a claim about the player's hand. The controls do
// not care (they come from a cross product of two readings, which is
// unchanged if both are negated), but a reader who assumes otherwise will
// "fix" a sign that is not broken.
//
// The boot screen is the standing check: the needle's UP end has to point
// away from the floor **in every attitude, upright included**. Not just
// tipped: with only the y sign wrong it still pointed the right way once
// the stick was on its side, and gave itself away only while upright. And
// it swings the OPPOSITE way to the stick, which is right -- the screen
// turns with the device and the world does not -- so watch the label, not
// the direction of travel.
//
//                         which sensor axis    sign
constexpr int SRC_X = 0, SIGN_X = +1;  // screen right
constexpr int SRC_Y = 1, SIGN_Y = -1;  // screen down
constexpr int SRC_Z = 2, SIGN_Z = -1;  // into the screen

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

// A threshold with hysteresis: on beyond `on`, off below `off`.
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
  a_ = normalize({SIGN_X * a[SRC_X], SIGN_Y * a[SRC_Y], SIGN_Z * a[SRC_Z]});
  ref_ = a_;
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
  a_ = normalize({a_.x + (unit.x - a_.x) * k, a_.y + (unit.y - a_.y) * k,
                  a_.z + (unit.z - a_.z) * k});
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
  // Tipping the stick onto either side swings the reading onto the screen's
  // x axis, one way or the other; that is the whole of the test. Which
  // physical way round each of the two answers is remains a label -- see
  // the note on the axis signs above -- and all that matters is that each
  // reaches the landscape rotation that comes out the right way up.
  if (a_.x > SIDE_MIN) return Side::TOP_RIGHT;
  if (a_.x < -SIDE_MIN) return Side::TOP_LEFT;
  return Side::UNKNOWN;
}

int Attitude::confirm() {
  const Side s = side();
  if (s == Side::UNKNOWN) return 0;
  ref_ = a_;
  haveRef_ = true;
  keys_ = 0;
  // The landscape picture's own left-right axis, in portrait axes: the
  // portrait screen's -y for one of the two, +y for the other. It is what
  // the dash/brake test is measured against, and it is the only thing that
  // differs between the two ways round (left/right is a turn about the view
  // axis, which is the same axis either way).
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
  const Vec3f c = cross(ref_, a_);

  // Turning about the view axis (z, into the screen) tips the picture: a
  // turn that carries the picture's top to the LEFT is anti-clockwise seen
  // by the player, which is negative about z, which leaves +c.z. This one
  // does not depend on which way the stick was tipped -- the view axis is
  // the view axis either way.
  const bool wasLeft = (keys_ & sim::Button::LEFT) != 0;
  const bool wasRight = (keys_ & sim::Button::RIGHT) != 0;
  const bool left = held(c.z, TURN_ON, TURN_OFF, wasLeft);
  const bool right = held(-c.z, TURN_ON, TURN_OFF, wasRight);

  // Turning about the picture's own left-right axis tips its far edge away
  // (dash) or near (brake). That axis is the portrait screen's y axis, up
  // to the sign confirm() worked out, and tipping the far edge away is a
  // positive turn about it -- which leaves a NEGATIVE component along it.
  const float fwd = -(float)rightSign_ * c.y;
  const bool wasUp = (keys_ & sim::Button::UP) != 0;
  const bool wasDown = (keys_ & sim::Button::DOWN) != 0;
  const bool up = held(fwd, PITCH_ON, PITCH_OFF, wasUp);
  const bool down = held(-fwd, PITCH_ON, PITCH_OFF, wasDown);

  keys_ = (uint8_t)((left ? sim::Button::LEFT : 0) |
                    (right ? sim::Button::RIGHT : 0) |
                    (up ? sim::Button::UP : 0) | (down ? sim::Button::DOWN : 0));

  // With nothing held, let the neutral attitude follow the player's
  // posture, slowly. A deliberate tip holds a key and stops this, so it can
  // only ever creep towards where the stick rests between turns.
  if (keys_ == 0) {
    const float step = DRIFT_RAD_PER_SEC * dtSec;
    ref_ = normalize({ref_.x + (a_.x - ref_.x) * step,
                      ref_.y + (a_.y - ref_.y) * step,
                      ref_.z + (a_.z - ref_.z) * step});
  }
}

}  // namespace ds
