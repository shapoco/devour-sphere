#ifndef DS_ATTITUDE_HPP
#define DS_ATTITUDE_HPP

// The stick is the stick: this front end has no pad and no direction keys,
// only two buttons and a BMI270. The player tips the whole device and the
// game reads the tip as the analog direction: left / right (turn) and
// forward / back (dash / brake), as hard as it is tipped.
//
// Everything below works in SCREEN axes, not the sensor's:
//
//     x = screen right,  y = screen down,  z = INTO the screen
//
// which is right handed (x cross y = z). The sensor's own axes are mapped
// onto these in attitude.cpp, which is a property of how the BMI270 is
// mounted in this case and had to be found on the hardware -- M5Unified
// hands over the raw sensor axes for this board.
//
// **The vector this carries points AWAY from the floor, not at it.** An
// accelerometer at rest measures the force holding the device up, so it
// reads +1 G along whichever axis points at the sky; that is why the name
// here is accel and not gravity, and why the boot screen's needle is
// labelled UP. Nothing downstream cares -- the controls come from a cross
// product of two of these, and negating both leaves it unchanged -- but a
// reader who assumes it points at the floor will "fix" a sign that is not
// broken. It was checked as a whole on hardware; see attitude.cpp.
//
// --- The two halves ---------------------------------------------------------
//
// At start up the screen is still portrait and the player is asked to tip
// the stick onto its side. Which side is read from the reading: it comes to
// lie along the screen's x axis, and its sign says which of M5GFX's two
// landscape rotations the picture wants. Lying flat (the reading along z)
// says nothing, so that is when the prompt stays up. Once the stick has
// been still for a moment, that attitude is frozen as the NEUTRAL one and
// the game starts.
//
// In play, the axes come from how far the current reading has moved away
// from the neutral one. Taking the cross product of the two gives a vector
// along the axis the device was turned about, whose length is the sine of
// the angle -- so one cross product yields both axes at once:
//
//   turning about the view axis (z)      tips the picture left or right
//   turning about the screen's x axis    tips the far edge away or near
//
// A turn about the screen's own y axis is invisible to an accelerometer
// (it is a yaw around gravity), which is why those are the two axes the
// controls use. No angle is ever taken: the sine is mapped linearly between
// the sines of the dead zone and of the full tip (at these few degrees the
// sine is the angle to within a percent).
//
// A shake is a pause. While the device is being shaken -- and for a moment
// after, so that the way it comes to rest is not read as a turn -- the
// direction is held at the center.

#include <cstdint>

namespace ds {

struct Vec3f {
  float x, y, z;
};

class Attitude {
 public:
  // Which way round the stick was tipped, from the reading along the
  // screen's x axis. UNKNOWN while it is too near flat (or still upright)
  // to tell. The two names are labels for the two landscape rotations, not
  // a claim about which way the player's hand went: see attitude.cpp.
  enum class Side : uint8_t { UNKNOWN, TOP_LEFT, TOP_RIGHT };

  // After M5.begin(). False if there is no IMU, in which case nothing else
  // here means anything and the caller has to fall back to the buttons.
  bool begin();

  // Read the sensor and advance the filters. Call once a frame: dtSec is
  // the time since the previous call, and every time constant here is
  // expressed in seconds so that the frame rate cannot change the feel.
  void sample(float dtSec);

  // The smoothed accelerometer reading in screen axes. At rest it points
  // away from the floor (see the note above).
  Vec3f accel() const { return a_; }
  Side side() const;
  // |a| - 1 G, low-passed: 0 when the device is merely being held.
  float motion() const { return motion_; }
  // Still enough to take as a neutral attitude (and long enough).
  bool settled() const { return settledSec_ >= STILL_SEC; }

  // Freeze the current attitude as the neutral one. Returns the M5GFX
  // rotation the landscape picture wants (ds::ROTATION_TOP_LEFT / _RIGHT),
  // or 0 if the side is not yet known.
  int confirm();

  // The direction of this sample, -127..127 (sim::Input's axes: x right,
  // y back = brake, forward = dash). Centered while the stick is being
  // shaken and for a moment afterwards.
  int8_t x() const { return x_; }
  int8_t y() const { return y_; }

  // True once per shake, for the pause toggle. Reading it clears it.
  bool takeShake();

 private:
  // --- Thresholds -----------------------------------------------------------
  // Each axis is 0 within the dead zone and full at the full tip, linear in
  // between. Compared as sines, which is what the cross product gives
  // directly.
  //
  // As digital keys both started at 15 degrees, which on the device asked
  // for a deliberate lean of the whole forearm and had turned the screen
  // away from the player by the time a key went down; they ended at 5
  // (turn) and 7 (dash / brake) degrees. The two axes want different
  // answers:
  //
  //   turning   full at 7 degrees. It is the control that is held for
  //             seconds at a time and worked constantly, and the wrist
  //             rolls far more easily than it pitches.
  //   dash /    full at 10 degrees. Tipping the far edge away also tips
  //   brake     the screen out of view, so this one wants to cost more.
  //
  // The dead zones swallow the hand's tremor and the slight tip a stick is
  // held at. With them, the game's menus (sim::directionBits, half
  // strength) see a key at about 4.3 and 6.3 degrees, near where the
  // digital keys went down.
  static constexpr float TURN_DEAD = 0.0262f;   // sin 1.5 deg
  static constexpr float TURN_FULL = 0.1219f;   // sin 7 deg
  static constexpr float PITCH_DEAD = 0.0436f;  // sin 2.5 deg
  static constexpr float PITCH_FULL = 0.1736f;  // sin 10 deg
  // The reading has to lie this far along the screen's x axis before the
  // boot screen believes the stick has been tipped onto a side.
  static constexpr float SIDE_MIN = 0.342f;  // sin 20 deg
  // The accelerometer, smoothed. Long enough to swallow the hand's tremor
  // and the knocks of play, short enough not to lag a deliberate tip.
  static constexpr float FILTER_SEC = 0.06f;
  // What counts as a shake, and how long it has to be quiet again to end.
  // The direction stays centered until then plus SHAKE_MUTE_SEC, so that
  // the attitude the stick happens to come to rest in is not read as a turn.
  static constexpr float SHAKE_ON = 0.70f;   // G away from 1 G
  static constexpr float SHAKE_OFF = 0.30f;  // G
  static constexpr float SHAKE_QUIET_SEC = 0.25f;
  static constexpr float SHAKE_MUTE_SEC = 0.30f;
  // Still: the magnitude within this of 1 G, for this long.
  static constexpr float STILL_TOL = 0.12f;  // G
  static constexpr float STILL_SEC = 0.5f;
  // The neutral attitude follows the player's posture at this rate while
  // both axes are in their dead zones -- slow enough that it cannot fight a
  // deliberate tip (which leaves the dead zone and stops it), fast enough
  // to forgive a hand that has settled somewhere else over a game.
  static constexpr float DRIFT_RAD_PER_SEC = 0.0175f;  // 1 deg/s

  Vec3f a_ = {0, 1, 0};    // the accelerometer, screen axes, filtered
  Vec3f ref_ = {0, 1, 0};  // the neutral attitude
  float motion_ = 0;       // |a| - 1 G, filtered
  float settledSec_ = 0;   // how long it has been still
  float quietSec_ = 0;     // how long since the last sign of a shake
  float muteSec_ = 0;      // the direction held centered until this runs out
  bool shaking_ = false;
  bool shakePending_ = false;
  bool haveRef_ = false;
  int8_t rightSign_ = 0;  // screen right in portrait axes: -y or +y
  int8_t x_ = 0, y_ = 0;

  void updateAxes(float dtSec);
};

}  // namespace ds

#endif
