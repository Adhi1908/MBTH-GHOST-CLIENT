#pragma once

// =============================================================================
// rotationUtils.h
//   C++ port of LiquidBounce legacy's RotationUtils.kt humanisation pipeline.
//   Reference: research/LiquidBounce-RotationUtils.kt (lines 344-459).
//
//   Given (current yaw/pitch, desired target yaw/pitch, settings) this
//   produces a *humanised* rotation for this tick that incorporates:
//     - Per-axis speed limiting (yawCap, pitchCap)
//     - Imperfect correlation (±10% per-axis speed noise)
//     - Per-tick mouse jitter (±3% yaw, ±2% pitch, scaled to delta)
//     - Slow-down lerp on approach (random subset of remaining distance)
//     - GCD / aim-modulus quantisation relative to the last server rotation
//     - Optional short-stop (occasional 1-2 tick freeze every >180° accumulated)
//
//   v0.1 NOTE:
//     This module computes a humanised rotation for a single tick. Whether the
//     result is published as the visible camera rotation (currently) or
//     sneaked into the C03 packet via a JVMTI sendPacket hook (future) is the
//     responsibility of the caller. The math here is identical either way.
// =============================================================================

#include "util/math/geometry.h"

namespace RotationUtils
{
	struct Settings
	{
		// All speeds are degrees-per-tick caps. LiquidBounce default is 180.
		float yawCap          = 180.0f;
		float pitchCap        = 180.0f;

		// If true, applies the ±10% imperfect-correlation noise plus the
		// per-tick jitter. (LB calls this "legitimize" -- on by default for
		// every aura preset.)
		bool  legitimize      = true;

		// If true, refuses to publish micro-rotations smaller than this many
		// degrees from the current. LB default 2.0°.
		float minRotationDiff = 2.0f;

		// If true, mounts an occasional 1-2 tick stop every time the
		// cumulative travelled-arc passes ~180°.
		bool  simulateShortStop = false;

		// GCD fix: snap result to a multiple of this delta (degrees) relative
		// to the previous server rotation. Set ≤ 0 to disable. LB derives
		// this from mouseSensitivity; for now we expose the cooked value.
		// 0.15° corresponds to ~99% sensitivity, which is the typical Hypixel
		// PvP setting.
		float gcdAngleDelta   = 0.15f;
	};

	// Stateful per-instance smoother. Owns "last server rotation" used for
	// the GCD step and a small short-stop scheduler.
	class Smoother
	{
	public:
		Smoother();

		// Resets internal accumulators (call when target changes / module
		// disabled / player teleports).
		void Reset(Vector2 currentRotation);

		// Compute the humanised rotation for this tick.
		//   currentRot  : where the player is currently aiming (visible)
		//   targetRot   : the desired aim (typically GetAngles to entity head)
		//   settings    : the user-configured behaviour
		//   outRotation : the per-tick rotation to publish.
		// Returns true if a rotation should be published, false if the diff is
		// below `minRotationDiff` (caller should leave the rotation alone).
		bool Apply(Vector2 currentRot, Vector2 targetRot,
			const Settings& settings, Vector2& outRotation);

	private:
		Vector2 m_serverRot;        // last published rotation (for GCD anchor)
		float   m_arcAccum;         // accumulated rotation distance (for short-stop)
		int     m_shortStopTicks;   // > 0 = currently in a short-stop, decrementing
	};

	// Wrap an angle to [-180, 180].
	float WrapAngleTo180(float angle);

	// Quantise `angle` to an integer multiple of `gcd`, anchored to `serverAngle`.
	// LB's Rotation.kt:73-84 fixedSensitivity equivalent.
	float QuantiseToGCD(float angle, float serverAngle, float gcd);
}
