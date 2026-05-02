#include "rotationUtils.h"

// Reach.cpp pattern: Windows.h leaks min/max as macros and breaks std::min/std::max.
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
	// Single shared RNG. We don't bother with thread-safety — Update() is
	// always called on the render/main thread.
	static std::mt19937& Rng()
	{
		static std::mt19937 g{ std::random_device{}() };
		return g;
	}

	static float UniformFloat(float lo, float hi)
	{
		std::uniform_real_distribution<float> d(lo, hi);
		return d(Rng());
	}

	static int UniformInt(int lo, int hi)
	{
		if (hi < lo) std::swap(lo, hi);
		std::uniform_int_distribution<int> d(lo, hi);
		return d(Rng());
	}

	static float Clamp(float v, float lo, float hi)
	{
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}
}

namespace RotationUtils
{
	float WrapAngleTo180(float angle)
	{
		// Same convention as Math::WrapAngleTo180 already in this codebase.
		angle = std::fmod(angle + 180.0f, 360.0f);
		if (angle < 0.0f) angle += 360.0f;
		return angle - 180.0f;
	}

	float QuantiseToGCD(float angle, float serverAngle, float gcd)
	{
		// LB Rotation.kt:73-84 — round (angle - serverAngle) to a multiple of gcd
		// and add it back. Defeats Watchdog's aim-modulus / GCD heuristic.
		if (gcd <= 0.0f) return angle;
		float diff = WrapAngleTo180(angle - serverAngle);
		float quantised = std::round(diff / gcd) * gcd;
		return serverAngle + quantised;
	}

	Smoother::Smoother()
		: m_serverRot(0.0f, 0.0f), m_arcAccum(0.0f), m_shortStopTicks(0)
	{
	}

	void Smoother::Reset(Vector2 currentRotation)
	{
		m_serverRot       = currentRotation;
		m_arcAccum        = 0.0f;
		m_shortStopTicks  = 0;
	}

	bool Smoother::Apply(Vector2 currentRot, Vector2 targetRot,
		const Settings& settings, Vector2& outRotation)
	{
		// In a short-stop window: keep the rotation frozen for this tick.
		// LB RotationUtils.kt:357-375.
		if (m_shortStopTicks > 0)
		{
			m_shortStopTicks--;
			outRotation = currentRot;
			return true;
		}

		// Per-axis difference, wrapped to [-180, 180].
		float yawDiff   = WrapAngleTo180(targetRot.x - currentRot.x);
		float pitchDiff = targetRot.y - currentRot.y;
		// Pitch is naturally in [-90, 90] so don't wrap it; just clamp later.

		float rotDiff = std::sqrt(yawDiff * yawDiff + pitchDiff * pitchDiff);

		// Below the minimum-rotation threshold? Don't publish a micro-rotation
		// — this is the LB minRotationDifference check (RotationSettings.kt:51-53).
		if (rotDiff < settings.minRotationDiff)
		{
			outRotation = currentRot;
			return false;
		}

		// Per-axis speed limiting.
		// LB RotationUtils.kt:359-375 — compute arc-proportional caps.
		float yawCap   = (rotDiff > 0.0f) ? std::fabs(yawDiff   / rotDiff) * settings.yawCap   : settings.yawCap;
		float pitchCap = (rotDiff > 0.0f) ? std::fabs(pitchDiff / rotDiff) * settings.pitchCap : settings.pitchCap;

		// (2) Imperfect correlation — ±10% per-axis speed noise.
		// LB RotationUtils.kt:381-388.
		if (settings.legitimize)
		{
			yawCap   *= UniformFloat(0.9f, 1.1f);
			pitchCap *= UniformFloat(0.9f, 1.1f);
		}

		// (1) Apply the caps and (5) slow-down lerp on approach.
		// When the per-axis remaining distance fits inside the cap, scale by a
		// random subset (LB RotationUtils.kt:417-459: applySlowDown).
		float yawStep, pitchStep;

		if (std::fabs(yawDiff) > yawCap)
		{
			yawStep = (yawDiff > 0.0f ? yawCap : -yawCap);
		}
		else
		{
			float frac = settings.legitimize ? UniformFloat(0.3f, 0.7f) : 1.0f;
			yawStep = yawDiff * frac;
		}

		if (std::fabs(pitchDiff) > pitchCap)
		{
			pitchStep = (pitchDiff > 0.0f ? pitchCap : -pitchCap);
		}
		else
		{
			float frac = settings.legitimize ? UniformFloat(0.3f, 0.7f) : 1.0f;
			pitchStep = pitchDiff * frac;
		}

		// (3) Per-tick mouse jitter — ±3% yaw / ±2% pitch, scaled to the step.
		// LB RotationUtils.kt:393-401.
		if (settings.legitimize)
		{
			yawStep   += yawStep   * UniformFloat(-0.03f, 0.03f);
			pitchStep += pitchStep * UniformFloat(-0.02f, 0.02f);
		}

		// Build the new rotation.
		Vector2 newRot;
		newRot.x = currentRot.x + yawStep;
		newRot.y = Clamp(currentRot.y + pitchStep, -90.0f, 90.0f);

		// (4) GCD / aim-modulus fix — snap to multiples of mouse-sensitivity
		// step relative to the last server rotation.
		// LB Rotation.kt:73-84 fixedSensitivity equivalent.
		if (settings.gcdAngleDelta > 0.0f)
		{
			newRot.x = QuantiseToGCD(newRot.x, m_serverRot.x, settings.gcdAngleDelta);
			newRot.y = QuantiseToGCD(newRot.y, m_serverRot.y, settings.gcdAngleDelta);
		}

		// Update accumulators.
		float stepArc = std::sqrt(yawStep * yawStep + pitchStep * pitchStep);
		m_arcAccum += stepArc;

		// (6) Short-stop simulation. When cumulative travelled-arc passes
		// 180°, with a small probability, freeze for 1-2 ticks.
		// LB RotationUtils.kt:357-375 + RotationSettings.kt:27-32.
		if (settings.simulateShortStop && m_arcAccum > 180.0f)
		{
			m_arcAccum = 0.0f;
			if (UniformFloat(0.0f, 1.0f) < 0.35f)
			{
				m_shortStopTicks = UniformInt(1, 2);
			}
		}

		m_serverRot = newRot;
		outRotation = newRot;
		return true;
	}
}
